#include "telemetry_event_clock.h"
#include "board_watchdog.h"
#include "AB-Threads.h"
#include "main.h"
#include "board_temperature.h"
#include "igniter_sense.h"
#include "igniter_sense_math.h"
#include "thread_comm.h"
#include "tx_api.h"
#include "safety_diagnostics.h"



#include <stdbool.h>
#include <stdint.h>

TX_THREAD safety_task_thread;

#define SAFETY_TASK_STACK_SIZE (4U * 1024U)
#define SAFETY_ADC_CHANNEL_COUNT 3U
#define SAFETY_ADC_DMA_FRAME_COUNT 1U
#define SAFETY_ADC_DMA_BUFFER_COUNT (SAFETY_ADC_CHANNEL_COUNT * SAFETY_ADC_DMA_FRAME_COUNT)
#define SAFETY_HEARTBEAT_TIMEOUT_MS 5000ULL
#define SAFETY_CHECK_PERIOD_TICKS ((TX_TIMER_TICKS_PER_SECOND + 19U) / 20U)
#define SAFETY_ADC_MISMATCH_LIMIT 10U
#define SAFETY_ADC_COUNTS_MAX 4095U
#define SAFETY_ADC_MISMATCH_N20 (1UL << 0)
#define SAFETY_ADC_MISMATCH_N2 (1UL << 1)
#define SAFETY_ADC_MISMATCH_IGNITER (1UL << 2)

#ifndef SAFETY_N20_ADC_CLOSED_MIN_COUNTS
#define SAFETY_N20_ADC_CLOSED_MIN_COUNTS 0U
#endif
#ifndef SAFETY_N20_ADC_CLOSED_MAX_COUNTS
#define SAFETY_N20_ADC_CLOSED_MAX_COUNTS 128U
#endif
#ifndef SAFETY_N20_ADC_OPEN_MIN_COUNTS
#define SAFETY_N20_ADC_OPEN_MIN_COUNTS 256U
#endif
#ifndef SAFETY_N20_ADC_OPEN_MAX_COUNTS
#define SAFETY_N20_ADC_OPEN_MAX_COUNTS SAFETY_ADC_COUNTS_MAX
#endif

#ifndef SAFETY_N2_ADC_CLOSED_MIN_COUNTS
#define SAFETY_N2_ADC_CLOSED_MIN_COUNTS 0U
#endif
#ifndef SAFETY_N2_ADC_CLOSED_MAX_COUNTS
#define SAFETY_N2_ADC_CLOSED_MAX_COUNTS 128U
#endif
#ifndef SAFETY_N2_ADC_OPEN_MIN_COUNTS
#define SAFETY_N2_ADC_OPEN_MIN_COUNTS 256U
#endif
#ifndef SAFETY_N2_ADC_OPEN_MAX_COUNTS
#define SAFETY_N2_ADC_OPEN_MAX_COUNTS SAFETY_ADC_COUNTS_MAX
#endif

#ifndef SAFETY_IGNITER_ADC_CLOSED_MIN_COUNTS
#define SAFETY_IGNITER_ADC_CLOSED_MIN_COUNTS 0U
#endif
#ifndef SAFETY_IGNITER_ADC_CLOSED_MAX_COUNTS
#define SAFETY_IGNITER_ADC_CLOSED_MAX_COUNTS 128U
#endif
#ifndef SAFETY_IGNITER_ADC_OPEN_MIN_COUNTS
#define SAFETY_IGNITER_ADC_OPEN_MIN_COUNTS 256U
#endif
#ifndef SAFETY_IGNITER_ADC_OPEN_MAX_COUNTS
#define SAFETY_IGNITER_ADC_OPEN_MAX_COUNTS SAFETY_ADC_COUNTS_MAX
#endif

typedef struct
{
    uint16_t n20_adc;
    uint16_t n2_adc;
    uint16_t igniter_adc;
    uint32_t sequence;
} safety_adc_sample_t;

typedef struct
{
    uint16_t closed_min;
    uint16_t closed_max;
    uint16_t open_min;
    uint16_t open_max;
} safety_adc_limits_t;

extern ADC_HandleTypeDef hadc1;

static const safety_adc_limits_t safety_n20_adc_limits = {
    SAFETY_N20_ADC_CLOSED_MIN_COUNTS,
    SAFETY_N20_ADC_CLOSED_MAX_COUNTS,
    SAFETY_N20_ADC_OPEN_MIN_COUNTS,
    SAFETY_N20_ADC_OPEN_MAX_COUNTS,
};
static const safety_adc_limits_t safety_n2_adc_limits = {
    SAFETY_N2_ADC_CLOSED_MIN_COUNTS,
    SAFETY_N2_ADC_CLOSED_MAX_COUNTS,
    SAFETY_N2_ADC_OPEN_MIN_COUNTS,
    SAFETY_N2_ADC_OPEN_MAX_COUNTS,
};
static const safety_adc_limits_t safety_igniter_adc_limits = {
    SAFETY_IGNITER_ADC_CLOSED_MIN_COUNTS,
    SAFETY_IGNITER_ADC_CLOSED_MAX_COUNTS,
    SAFETY_IGNITER_ADC_OPEN_MIN_COUNTS,
    SAFETY_IGNITER_ADC_OPEN_MAX_COUNTS,
};

static TX_SEMAPHORE safety_adc_dma_sem;
static uint16_t safety_adc_dma_buffer[SAFETY_ADC_DMA_BUFFER_COUNT];
static volatile safety_adc_sample_t safety_adc_isr_sample;
static safety_adc_sample_t safety_adc_latest_sample;
static volatile uint32_t safety_adc_processed_count;
static volatile uint32_t safety_adc_mismatch_count;
static volatile uint32_t safety_adc_last_mismatch_mask;
static volatile uint32_t safety_adc_latched_mismatch_mask;
static volatile uint32_t safety_heartbeat_timeout_count;
static volatile uint32_t safety_adc_start_error_count;
static volatile HAL_StatusTypeDef safety_adc_start_status = HAL_OK;
static volatile uint32_t safety_adc_start_step;
volatile uint32_t g_safety_adc_start_failures;
volatile uint8_t g_safety_adc_warning_pending;
static volatile uint32_t safety_adc_start_hal_state;
static volatile uint32_t safety_adc_start_hal_error;
static volatile HAL_DMA_StateTypeDef safety_adc_start_dma_state;
static volatile uint32_t safety_adc_start_dma_error;
static volatile bool safety_adc_started;
static volatile bool safety_adc_needs_quiesce;
static uint32_t safety_adc_started_ms;
static uint64_t safety_heartbeat_missing_since_ms;

static uint64_t safety_now_ms(void)
{
    return ((uint64_t)tx_time_get() * 1000ULL) / (uint64_t)TX_TIMER_TICKS_PER_SECOND;
}

/* Task owner only. No global IRQ mask surrounds HAL stop or semaphore work.
 * No ADC12 reset is allowed: ADC2 remains independently owned. */
static bool safety_adc_quiesce(void)
{
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    safety_adc_started = false;
    safety_adc_needs_quiesce = true;
    igniter_sense_fault();
    const uint32_t errors = safety_adc_start_error_count;
    __set_PRIMASK(saved);
    if (hadc1.DMA_Handle == NULL || hadc1.DMA_Handle->Instance != DMA1_Channel1) return false;
    HAL_NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    __HAL_DMA_DISABLE(hadc1.DMA_Handle);
    __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
    __DSB();
    /* G4 HAL ADC_ConversionStop is bounded by ADC_STOP_CONVERSION_TIMEOUT=5ms.
     * The independent system tick remains enabled during that wait. */
    const HAL_StatusTypeDef stopped = HAL_ADC_Stop_DMA(&hadc1);
    __DSB();
    if (stopped != HAL_OK ||
        (hadc1.Instance->CR & (ADC_CR_ADSTART | ADC_CR_JADSTART)) != 0U ||
        (hadc1.DMA_Handle->Instance->CCR & DMA_CCR_EN) != 0U) return false;
    __HAL_DMA_CLEAR_FLAG(hadc1.DMA_Handle, __HAL_DMA_GET_GI_FLAG_INDEX(hadc1.DMA_Handle));
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    __DSB();
    bool drained = false;
    for (unsigned n = 0U; n < 8U; ++n) {
        const UINT status = tx_semaphore_get(&safety_adc_dma_sem, TX_NO_WAIT);
        if (status == TX_NO_INSTANCE) { drained = true; break; }
        if (status != TX_SUCCESS) return false;
    }
    if (!drained) return false;
    const uint32_t final_irq = __get_PRIMASK();
    __disable_irq();
    if (safety_adc_start_error_count != errors) {
        __set_PRIMASK(final_irq);
        return false;
    }
    safety_adc_needs_quiesce = false;
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    __set_PRIMASK(final_irq);
    return true;
}

static UINT safety_adc_start(void)
{
    safety_adc_start_step = 1U;
    if (hadc1.DMA_Handle == NULL || hadc1.DMA_Handle->Instance != DMA1_Channel1) {
        safety_adc_start_step = 2U;
        safety_adc_started = false;
        safety_adc_needs_quiesce = true;
        igniter_sense_fault();
        safety_adc_start_error_count++;
        return TX_NOT_DONE;
    }


    if (safety_adc_started &&
        (uint32_t)(HAL_GetTick() - safety_adc_started_ms) > IGNITER_SENSE_SAMPLE_MAX_AGE_MS) {
        safety_adc_needs_quiesce = true;
        igniter_sense_fault();
    }
    if (safety_adc_needs_quiesce && !safety_adc_quiesce()) return TX_NOT_DONE;
    if (safety_adc_started) return TX_SUCCESS;

    safety_adc_start_hal_state = HAL_ADC_GetState(&hadc1);
    safety_adc_start_hal_error = HAL_ADC_GetError(&hadc1);
    safety_adc_start_dma_state = hadc1.DMA_Handle->State;
    safety_adc_start_dma_error = hadc1.DMA_Handle->ErrorCode;

    if (((safety_adc_start_hal_state & HAL_ADC_STATE_REG_BUSY) != 0U) ||
        (hadc1.DMA_Handle->State == HAL_DMA_STATE_BUSY))
    {
        safety_adc_start_step = 3U;
        /* An unexplained busy DMA is not this request's completed sample. */
        safety_adc_needs_quiesce = true;
        if (!safety_adc_quiesce()) return TX_NOT_DONE;
    }

    safety_adc_start_step = 4U;
    safety_adc_started_ms = HAL_GetTick();
    safety_adc_started = true;
    safety_adc_start_status = HAL_ADC_Start_DMA(&hadc1,
                                                (uint32_t *)safety_adc_dma_buffer,
                                                SAFETY_ADC_DMA_BUFFER_COUNT);
    safety_adc_start_step = 5U;
    safety_adc_start_hal_state = HAL_ADC_GetState(&hadc1);
    safety_adc_start_hal_error = HAL_ADC_GetError(&hadc1);
    safety_adc_start_dma_state = hadc1.DMA_Handle->State;
    safety_adc_start_dma_error = hadc1.DMA_Handle->ErrorCode;

    if (safety_adc_start_status != HAL_OK)
    {
        safety_adc_started = false;
        safety_adc_needs_quiesce = true;
        igniter_sense_fault();
        safety_adc_start_error_count++;
        return TX_NOT_DONE;
    }

    safety_adc_start_step = 6U;
    return TX_SUCCESS;
}

static void safety_adc_publish_frame(const uint16_t *frame)
{
    safety_adc_isr_sample.n20_adc = frame[0];
    safety_adc_isr_sample.n2_adc = frame[1];
    safety_adc_isr_sample.igniter_adc = frame[2];
    safety_adc_isr_sample.sequence++;
    __DMB();

    (void)tx_semaphore_put(&safety_adc_dma_sem);
}

static bool safety_adc_take_latest(safety_adc_sample_t *sample)
{
    const uint32_t old_threshold = __get_BASEPRI();

    __set_BASEPRI(6U << (8U - __NVIC_PRIO_BITS));
    __DSB();
    __ISB();

    *sample = safety_adc_isr_sample;

    __set_BASEPRI(old_threshold);

    return sample->sequence != 0U;
}

static bool safety_channel_matches(uint8_t expected_on,
                                   uint16_t adc_counts,
                                   const safety_adc_limits_t *limits)
{
    if (expected_on != 0U)
    {
        return (adc_counts >= limits->open_min) && (adc_counts <= limits->open_max);
    }

    return (adc_counts >= limits->closed_min) && (adc_counts <= limits->closed_max);
}

static uint32_t safety_sample_mismatch_mask(const safety_adc_sample_t *sample)
{
    const thread_comm_expected_outputs_t expected = thread_comm_get_expected_outputs();
    uint32_t mask = 0U;

    if (!safety_channel_matches(expected.n20_on, sample->n20_adc, &safety_n20_adc_limits))
    {
        mask |= SAFETY_ADC_MISMATCH_N20;
    }

    if (!safety_channel_matches(expected.n2_on, sample->n2_adc, &safety_n2_adc_limits))
    {
        mask |= SAFETY_ADC_MISMATCH_N2;
    }

    if (!safety_channel_matches(expected.igniter_on, sample->igniter_adc, &safety_igniter_adc_limits))
    {
        mask |= SAFETY_ADC_MISMATCH_IGNITER;
    }

    return mask;
}

static void safety_check_heartbeat(void)
{
    const uint64_t last_heartbeat_ms = thread_comm_get_groundstation_heartbeat_ms();
    /* Snapshot reception before local uptime: a concurrently received
     * heartbeat must not underflow the elapsed-time calculation. */
    const uint64_t now_ms = safety_now_ms();
    const uint8_t flight_state = thread_comm_get_flight_state();

    /* Idle has no active filling operation to abandon on heartbeat loss.
     * Keep the shared ADC/active-state policy and retained abort latch intact. */
    if (flight_state <= ACTUATOR_FLIGHT_STATE_IDLE ||
        flight_state >= ACTUATOR_FLIGHT_STATE_LAUNCH)
    {
        safety_heartbeat_missing_since_ms = 0ULL;
        return;
    }

    if (last_heartbeat_ms != 0ULL)
    {
        safety_heartbeat_missing_since_ms = 0ULL;
        if ((now_ms >= last_heartbeat_ms) &&
            ((now_ms - last_heartbeat_ms) >= SAFETY_HEARTBEAT_TIMEOUT_MS))
        {
            safety_heartbeat_timeout_count++;
            const uint64_t origin_utc = telemetry_event_clock_snapshot();
            if (thread_comm_request_local_abort(SAFETY_ABORT_HEARTBEAT, now_ms, last_heartbeat_ms, flight_state, 0U, origin_utc, 1U)) {
                /* Retained Abort is consumed by the control owner; do not
                 * mutate a later recovered sequence from this producer. */
            }
        }
        return;
    }

    if (safety_heartbeat_missing_since_ms == 0ULL)
    {
        safety_heartbeat_missing_since_ms = now_ms;
        return;
    }

    if ((now_ms - safety_heartbeat_missing_since_ms) >= SAFETY_HEARTBEAT_TIMEOUT_MS)
    {
        safety_heartbeat_timeout_count++;
        const uint64_t origin_utc = telemetry_event_clock_snapshot();
        if (thread_comm_request_local_abort(SAFETY_ABORT_INITIAL_HEARTBEAT, now_ms, last_heartbeat_ms, flight_state, 0U, origin_utc, 1U)) {
            /* Control owner observes the retained Abort and owns safe outputs. */
        }
    }
}

static void safety_process_adc_sample(const safety_adc_sample_t *sample)
{
    safety_adc_latest_sample = *sample;

    if (thread_comm_abort_allowed() == 0U)
    {
        safety_adc_mismatch_count = 0U;
        return;
    }

    const uint32_t mismatch_mask = safety_sample_mismatch_mask(sample);
    safety_adc_last_mismatch_mask = mismatch_mask;

    if (mismatch_mask == 0U)
    {
        safety_adc_mismatch_count = 0U;
        return;
    }

    safety_adc_mismatch_count++;
    safety_adc_latched_mismatch_mask |= mismatch_mask;
    if (safety_adc_mismatch_count >= SAFETY_ADC_MISMATCH_LIMIT)
    {
        // (void)thread_comm_set_abort(1U);
    }
}

void safety_task_entry(ULONG initial_input)
{
    (void)initial_input;

    while (1)
    {
        board_watchdog_progress(BOARD_WATCHDOG_SAFETY);
        safety_check_heartbeat();
        /* Do not inject a temperature/reference conversion while recovery
         * or an outstanding regular scan still owns the ADC. */
        if (!safety_adc_needs_quiesce && !safety_adc_started)
            board_temperature_poll();

        /* Read-only acquisition continues after Launch/Abort. All existing
         * safety evaluation and failure Abort paths retain their own gate. */
        if (safety_adc_start() != TX_SUCCESS)
        {
            /* An ADC start failure is a diagnostic, not a safety condition:
             * report a warning and retry. Aborts are for safety only. */
            g_safety_adc_start_failures++;
            g_safety_adc_warning_pending = 1U;
            tx_thread_sleep(SAFETY_CHECK_PERIOD_TICKS);
            continue;
        }

        if (tx_semaphore_get(&safety_adc_dma_sem, SAFETY_CHECK_PERIOD_TICKS) == TX_SUCCESS)
        {
            safety_adc_sample_t sample;
            if (safety_adc_take_latest(&sample))
            {
                if (thread_comm_abort_allowed() != 0U) {
                    safety_process_adc_sample(&sample);
                    safety_adc_processed_count++;
                }
            }
        }
        tx_thread_sleep(SAFETY_CHECK_PERIOD_TICKS);
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1 && hadc->Instance == ADC1)
    {
        /* Only a completed transfer into this owner's existing DMA buffer
         * can publish a measurement. Late/unowned callbacks change no clock. */
        if (safety_adc_started && !safety_adc_needs_quiesce && hadc->DMA_Handle != NULL &&
            hadc->DMA_Handle->Instance == DMA1_Channel1 &&
            __HAL_DMA_GET_COUNTER(hadc->DMA_Handle) == 0U &&
            (uintptr_t)hadc->DMA_Handle->Instance->CMAR == (uintptr_t)safety_adc_dma_buffer) {
            igniter_sense_capture(safety_adc_dma_buffer[2], HAL_GetTick());
            safety_adc_publish_frame(&safety_adc_dma_buffer[0]);
        } else {
            igniter_sense_fault();
            safety_adc_needs_quiesce = true;
        }
        safety_adc_started = false;
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        safety_adc_started = false;
        safety_adc_needs_quiesce = true;
        safety_adc_start_error_count++;
        igniter_sense_fault();
    }
}

UINT create_safety_task(TX_BYTE_POOL *byte_pool)
{
    CHAR *pointer;

    if (tx_semaphore_create(&safety_adc_dma_sem, "Safety ADC DMA", 0) != TX_SUCCESS)
    {
        return TX_SEMAPHORE_ERROR;
    }

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, SAFETY_TASK_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
    {
        return TX_POOL_ERROR;
    }

    return tx_thread_create(&safety_task_thread,
                            "Safety Task",
                            safety_task_entry,
                            1,
                            pointer,
                            SAFETY_TASK_STACK_SIZE,
                            6,
                            6,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START);
}
