"""Valve-state stall self-recovery, compiled from the actual telemetry.c block.

If every valve-state submission fails for 5 s, the network thread drops queued
TX, requests a CAN controller restart and re-arms the schema announcement.
It never runs while submissions succeed and repeats at most once per 5 s.
"""
from pathlib import Path
import os, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]


class StatusStallRecovery(unittest.TestCase):
    def test_recovers_only_after_sustained_failure(self):
        source = Path(os.environ.get('AB_TELEMETRY_SOURCE', str(ROOT / 'Core/Src/telemetry.c'))).read_text()
        start = source.index('/* status-stall-service-begin */')
        end = source.index('/* status-stall-service-end */', start)
        sut = source[start:end]
        self.assertNotIn('HAL_GPIO', sut)
        self.assertNotIn('thread_comm', sut)
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int SedsResult; enum { SEDS_OK = 0 };
typedef struct SedsRouter SedsRouter;
#define TELEMETRY_STATUS_STALL_MS 5000U
static volatile int32_t g_umbilical_status_last_result;
static volatile uint32_t g_umbilical_status_ok_ms;
static volatile uint32_t g_network_self_recovery_count;
static uint8_t g_discovery_schema_announced = 1;
static uint64_t g_discovery_schema_retry_ms = 77;
static unsigned clears, restarts;
static SedsRouter *seen;
static SedsResult seds_router_clear_tx_queue(SedsRouter *r) { seen = r; ++clears; return SEDS_OK; }
static void can_bus_request_restart(void) { ++restarts; }
''' + sut + r'''
int main(void) {
  SedsRouter *router = (SedsRouter *)0x1234;
  /* Healthy: 1 Hz successes keep it idle forever. */
  for (uint32_t t = 0; t < 600000; t += 10) {
    if (t % 1000 == 0) { g_umbilical_status_last_result = SEDS_OK; g_umbilical_status_ok_ms = t; }
    telemetry_status_stall_service(router, t);
  }
  assert(!clears && !restarts && g_discovery_schema_announced == 1);
  /* A transient failure that heals within 5 s does nothing. */
  g_umbilical_status_ok_ms = 600000; g_umbilical_status_last_result = -5;
  for (uint32_t t = 600000; t < 604990; t += 10) telemetry_status_stall_service(router, t);
  assert(!clears);
  g_umbilical_status_last_result = SEDS_OK; g_umbilical_status_ok_ms = 604990;
  telemetry_status_stall_service(router, 610000); assert(!clears);
  /* Sustained failure (no route / full ordered window): recover at 5 s, then every 5 s. */
  g_umbilical_status_last_result = -5;
  for (uint32_t t = 605000; t < 609990; t += 10) telemetry_status_stall_service(router, t);
  assert(!clears);
  telemetry_status_stall_service(router, 609990);
  assert(clears == 1 && restarts == 1 && seen == router && g_network_self_recovery_count == 1);
  assert(g_discovery_schema_announced == 0 && g_discovery_schema_retry_ms == 0);
  for (uint32_t t = 609991; t < 614990; t += 10) telemetry_status_stall_service(router, t);
  assert(clears == 1);
  telemetry_status_stall_service(router, 614990); assert(clears == 2);
  /* Tick wrap. */
  g_umbilical_status_ok_ms = UINT32_MAX - 100; clears = 0;
  telemetry_status_stall_service(router, 4000); assert(!clears);
  telemetry_status_stall_service(router, 4899); assert(clears == 1);
  puts("status stall recovery: PASS");
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c = Path(directory) / 'stall.c'; exe = Path(directory) / 'stall'
            c.write_text(code)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
