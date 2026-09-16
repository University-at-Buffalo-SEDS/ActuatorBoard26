"""Exercise the production state publisher without any protocol ACK arriving."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ImmediateStateReportTests(unittest.TestCase):
    def test_reports_open_and_closed_before_return_without_protocol_ack(self):
        source = (ROOT / "Core/Src/telemetry.c").read_text()
        publisher = re.search(
            r"SedsResult telemetry_publish_umbilical_status\(.*?\n}",
            source, re.S).group()
        code = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define TELEMETRY_ENABLED 1
typedef int SedsResult;
enum { SEDS_OK=0, SEDS_ERR=-1, SEDS_DT_UMBILICAL_STATUS=7, SEDS_EK_UNSIGNED=0 };
uint32_t g_umbilical_status_attempts, g_umbilical_status_last_payload;
uint32_t g_umbilical_status_enqueue_failures;
int32_t g_umbilical_status_last_result;
typedef struct { uint32_t calls, accepted, failed, last_state, last_tick;
                 int32_t last_result; } ValveStatusDiagnostic;
volatile ValveStatusDiagnostic g_valve_status_publish[7];
static struct { void *r; } g_router = { (void *)1 };
static int calls, state, status_id, fail;
static unsigned tx_time_get(void) { return 42; }
static void tx_thread_sleep(unsigned ticks) { (void)ticks; }
static int init_telemetry_router(void) { return SEDS_OK; }
static int seds_router_log_typed(void *router, int type, const void *data,
                               size_t count, size_t size, int kind) {
    assert(router && type==SEDS_DT_UMBILICAL_STATUS);
    assert(count==2 && size==1 && kind==SEDS_EK_UNSIGNED);
    const uint8_t *p=data;
    calls++; status_id=p[0]; state=p[1];
    return fail ? SEDS_ERR : SEDS_OK;
}
static int log_telemetry_synchronous(int type, const void *data,
                                     size_t count, size_t size) {
    return seds_router_log_typed(g_router.r,type,data,count,size,SEDS_EK_UNSIGNED);
}
/* Deliberately no async queue or protocol ACK mock: neither may be required. */
""" + publisher + r"""
int main(void) {
    for (int id=0; id<7; ++id) {
        calls=0;
        assert(telemetry_publish_umbilical_status(id,1)==SEDS_OK);
        assert(calls==1 && status_id==id && state==1);
        assert(telemetry_publish_umbilical_status(id,0)==SEDS_OK);
        assert(calls==2 && status_id==id && state==0);
    }
    fail=1; calls=0;
    assert(telemetry_publish_umbilical_status(2,0)==SEDS_ERR);
    assert(calls>=1 && calls<=3);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(pathlib.Path(tmp) / "immediate-state")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-x", "c", "-", "-o", binary],
                           input=code, text=True, check=True)
            subprocess.run([binary], check=True)
