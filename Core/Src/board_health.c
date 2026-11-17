#include "board_health.h"
#include "board_refusal.h"
#include "board_temperature.h"
#include "board_uptime.h"
#include "board_watchdog.h"
#include "telemetry.h"
#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Board diagnostic words require little-endian encoding"
#endif
#ifdef TELEMETRY_USE_TLSF
extern volatile uint32_t g_telemetry_tlsf_live_bytes, g_telemetry_tlsf_peak_bytes, g_telemetry_tlsf_region_bytes, g_telemetry_tlsf_failures;
#endif
extern uint32_t board_health_can_rx_drops(void);
extern int telemetry_router_enter(void);
extern int telemetry_router_leave(void);
extern volatile uint32_t g_can_tx_rejected, g_can_tx_high_water;

void board_health_poll(SedsRouter *router)
{
#ifdef TELEMETRY_ENABLED
  static uint32_t last_report_ms;
  static uint32_t failures;
  /* Last refusal sequence reported and when this poll first saw it. */
  static uint32_t refusal_seen, refusal_seen_ms;
  if (!router) return;
  const uint64_t uptime = board_uptime_ticks();
  const uint32_t now = (uint32_t)uptime;
  const uint32_t refusal = board_refusal_current();
  /* A new refusal goes out at once (at most every 200 ms), not in up to 5 s. */
  const bool fresh_refusal = (refusal >> 16) != refusal_seen;
  if ((uint32_t)(now - last_report_ms) < (fresh_refusal ? 200U : 5000U)) return;
  last_report_ms = now;
  if (fresh_refusal) { refusal_seen = refusal >> 16; refusal_seen_ms = now; }

  uint32_t words[24] = {1U, 2U, (uint32_t)uptime, (uint32_t)(uptime >> 32U), BOARD_UPTIME_FREQUENCY_HZ};
  const uint32_t saved = __get_PRIMASK(); __disable_irq();
  words[5] = g_watchdog_reset_flags; words[6] = g_watchdog_missing_mask;
#ifdef TELEMETRY_USE_TLSF
  /* Exact existing occupied-byte counters. Avoid interrupt-disabled heap
   * walks, and do not mislabel occupied headroom as contiguous free space. */
  words[1] |= 1U;
  words[7] = g_telemetry_tlsf_live_bytes; words[8] = g_telemetry_tlsf_peak_bytes;
  words[9] = g_telemetry_tlsf_region_bytes; words[10] = g_telemetry_tlsf_failures;
#endif
  words[12] = board_health_can_rx_drops();
  words[1] |= 4U | 64U; words[13] = g_can_tx_rejected; words[15] = g_can_tx_high_water;
  __set_PRIMASK(saved);
  words[11] = (uint32_t)seds_memory_admission_rejected();
  words[23] = failures;
  words[16] = BOARD_REFUSAL_TAG; words[17] = refusal;
  words[18] = refusal_seen ? now - refusal_seen_ms : 0U;
  words[1] |= board_temperature_health_flags();
  if (!telemetry_router_enter()) { ++failures; return; }
  /* Static UInt32 schema + exact little-endian bytes: use the existing
   * bounded byte path without pulling every generic numeric converter in. */
  const SedsResult result = seds_router_log_bytes_ex(router, SEDS_DT_BOARD_HEALTH,
      (const uint8_t *)words, sizeof(words), NULL, true);
  const int released = telemetry_router_leave();
  if (result != SEDS_OK || !released) ++failures;
#else
  (void)router;
#endif
}
