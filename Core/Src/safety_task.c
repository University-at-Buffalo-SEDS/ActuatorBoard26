#include "AB-Threads.h"
#include "main.h"
#include "thread_comm.h"
#include "tx_api.h"

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
static volatile uint32_t safety_adc_start_hal_state;
static volatile uint32_t safety_adc_start_hal_error;
static volatile HAL_DMA_StateTypeDef safety_adc_start_dma_state;
static volatile uint32_t safety_adc_start_dma_error;
static volatile bool safety_adc_started;
static uint64_t safety_heartbeat_missing_since_ms;

static uint64_t safety_now_ms(void)
{
    return ((uint64_t)tx_time_get() * 1000ULL) / (uint64_t)TX_TIMER_TICKS_PER_SECOND;
}

static UINT safety_adc_start(void)
{
    safety_adc_start_step = 1U;

    if (safety_adc_started)
    {
        return TX_SUCCESS;
    }

    if (hadc1.DMA_Handle == NULL)
    {
        safety_adc_start_step = 2U;
        safety_adc_start_error_count++;
        return TX_NOT_DONE;
    }

    safety_adc_start_hal_state = HAL_ADC_GetState(&hadc1);
    safety_adc_start_hal_error = HAL_ADC_GetError(&hadc1);
    safety_adc_start_dma_state = hadc1.DMA_Handle->State;
    safety_adc_start_dma_error = hadc1.DMA_Handle->ErrorCode;

    if (((safety_adc_start_hal_state & HAL_ADC_STATE_REG_BUSY) != 0U) ||
        (hadc1.DMA_Handle->State == HAL_DMA_STATE_BUSY))
    {
        safety_adc_start_step = 3U;
        safety_adc_started = true;
        return TX_SUCCESS;
    }

    safety_adc_start_step = 4U;
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
    const uint64_t now_ms = safety_now_ms();
    const uint64_t last_heartbeat_ms = thread_comm_get_groundstation_heartbeat_ms();

    if (thread_comm_abort_allowed() == 0U)
    {
        safety_heartbeat_missing_since_ms = 0ULL;
        return;
    }

    if (last_heartbeat_ms != 0ULL)
    {
        safety_heartbeat_missing_since_ms = 0ULL;
        if ((now_ms - last_heartbeat_ms) >= SAFETY_HEARTBEAT_TIMEOUT_MS)
        {
            safety_heartbeat_timeout_count++;
            (void)thread_comm_set_abort(1U);
            main_task_force_outputs_safe_off();
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
        (void)thread_comm_set_abort(1U);
        main_task_force_outputs_safe_off();
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
        safety_check_heartbeat();

        if (thread_comm_abort_allowed() == 0U)
        {
            tx_thread_sleep(SAFETY_CHECK_PERIOD_TICKS);
            continue;
        }

        if (safety_adc_start() != TX_SUCCESS)
        {
            (void)thread_comm_set_abort(1U);
            main_task_force_outputs_safe_off();
            tx_thread_sleep(SAFETY_CHECK_PERIOD_TICKS);
            continue;
        }

        if (tx_semaphore_get(&safety_adc_dma_sem, SAFETY_CHECK_PERIOD_TICKS) == TX_SUCCESS)
        {
            safety_adc_sample_t sample;
            if (safety_adc_take_latest(&sample))
            {
                safety_process_adc_sample(&sample);
                safety_adc_processed_count++;
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
    if (hadc->Instance == ADC1)
    {
        safety_adc_publish_frame(&safety_adc_dma_buffer[0]);
        safety_adc_started = false;
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        safety_adc_started = false;
        safety_adc_start_error_count++;
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
