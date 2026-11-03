#include "igniter_sense.h"
#include "igniter_sense_math.h"
#include "board_temperature.h"
#include "board_uptime.h"
#include "board_recovery_boot.h"
#include "main.h"
#include <stdbool.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Igniter current-sense words require little-endian encoding"
#endif
/* No DMA storage or producer queue is added. A complete original sample,
 * including its source time, survives repeated publication unchanged. */
static volatile struct {
    uint32_t counts, sampled_ms, sequence;
    bool sampled, fault;
} signal_sample;
#ifdef TELEMETRY_ENABLED
static uint32_t last_report_ms;
static bool reported;
#endif

void igniter_sense_capture(uint16_t counts, uint32_t sampled_ms)
{
    signal_sample.counts = counts;
    signal_sample.sampled_ms = sampled_ms;
    if (++signal_sample.sequence == 0U) ++signal_sample.sequence;
    signal_sample.sampled = true;
    signal_sample.fault = false;
}
void igniter_sense_fault(void)
{
    /* An error or unowned/late completion must not refresh the source clock. */
    signal_sample.fault = true;
}
#ifdef TELEMETRY_ENABLED
void igniter_sense_poll(SedsRouter *router)
{
    if (!router) return;
    const uint64_t now = board_uptime_ticks();
    if (reported && (uint32_t)((uint32_t)now - last_report_ms) < 200U) return;
    reported = true;
    last_report_ms = (uint32_t)now;
    uint32_t counts, captured, sequence;
    bool sampled, fault;
    const uint32_t irq = __get_PRIMASK();
    __disable_irq();
    counts = signal_sample.counts;
    captured = signal_sample.sampled_ms;
    sequence = signal_sample.sequence;
    sampled = signal_sample.sampled;
    fault = signal_sample.fault;
    __set_PRIMASK(irq);
    uint32_t reference_mv = 0U, reference_age = UINT32_MAX;
    const bool reference_valid = board_temperature_reference_mv(&reference_mv, &reference_age);
    const uint32_t age = sampled ? (uint32_t)((uint32_t)now - captured) : UINT32_MAX;
    const uint64_t sample_uptime = sampled ? board_uptime_project(now, captured) : 0U;
    const uint64_t boot = board_recovery_boot_current();
    uint32_t words[12] = {1U,
        igniter_sense_flags(counts, age, sampled, reference_mv, reference_age, reference_valid, fault),
        (uint32_t)boot, (uint32_t)(boot >> 32U), sequence,
        (uint32_t)sample_uptime, (uint32_t)(sample_uptime >> 32U), counts,
        reference_valid ? reference_mv : 0U, age, reference_age,
        IGNITER_SENSE_PROFILE_SCHEMATIC_045B5_AT_3V3};
    (void)seds_router_log_bytes_ex(router, SEDS_DT_AB_IGNITER_SENSE,
        (const uint8_t *)words, sizeof(words), NULL, true);
}
#endif
