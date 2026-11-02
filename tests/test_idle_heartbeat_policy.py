"""Run the actual heartbeat and ADC guards over all wire state values."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, name):
    return re.search(r'(?:static void|uint8_t) ' + name + r'\([^)]*\)\s*\{.*?\n\}', source, re.S).group()


class IdleHeartbeatPolicyTests(unittest.TestCase):
    def test_heartbeat_state_table_preserves_adc_and_abort_latch(self):
        safety = (ROOT / 'Core/Src/safety_task.c').read_text()
        shared = (ROOT / 'Core/Src/thread_comm.c').read_text()
        header = (ROOT / 'Core/Inc/thread_comm.h').read_text()
        states = re.search(r'typedef enum \{.*?\} actuator_flight_state_t;', header, re.S).group()
        code = r'''
#include <assert.h>
#include <stdint.h>
#include "safety_diagnostics.h"
volatile safety_abort_diagnostic_t g_safety_first_abort;
static uint64_t now_ms, heartbeat, safety_heartbeat_missing_since_ms;
static uint32_t aborted, calls, state, safety_heartbeat_timeout_count;
static uint32_t state_reads, change_after_snapshot, state_after_snapshot;
static uint32_t safety_adc_mismatch_count, safety_adc_last_mismatch_mask;
static uint32_t safety_adc_latched_mismatch_mask;
typedef struct { uint32_t value; } safety_adc_sample_t;
static safety_adc_sample_t safety_adc_latest_sample;
#define SAFETY_HEARTBEAT_TIMEOUT_MS 5000ULL
#define SAFETY_ADC_MISMATCH_LIMIT 3U
static uint64_t safety_now_ms(void) { return now_ms; }
static uint64_t thread_comm_get_groundstation_heartbeat_ms(void) { return heartbeat; }
static uint32_t thread_comm_get_abort(void) { return aborted; }
static uint32_t thread_comm_get_flight_state(void) {
    state_reads++;
    return change_after_snapshot && state_reads > 1 ? state_after_snapshot : state;
}
static uint8_t retained_launch;
static uint8_t board_launch_latch_is_set(void) { return retained_launch; }
static void board_abort_latch_set(void) { aborted=1; }
typedef unsigned TX_MUTEX;
#define TX_SUCCESS 0U
#define TX_WAIT_FOREVER 0xffffffffU
static unsigned g_thread_comm_state_mutex,g_thread_comm_initialized=1;
static uint8_t g_recovery_pending;
static uint32_t g_thread_comm_recovery_epoch=1;
#define g_thread_comm_abort aborted
#define g_thread_comm_flight_state state
static unsigned tx_mutex_get(TX_MUTEX*m,unsigned wait) {(void)m;(void)wait;if(change_after_snapshot){state=state_after_snapshot;change_after_snapshot=0;}return TX_SUCCESS;}
static unsigned tx_mutex_put(TX_MUTEX*m){(void)m;return TX_SUCCESS;}
void main_task_force_outputs_safe_off(void) { calls++; }
static uint32_t safety_sample_mismatch_mask(const safety_adc_sample_t *sample) { return sample->value; }
'''
        code += states + '\n' + function(shared, 'thread_comm_latch_abort_locked') + '\n' + function(shared, 'thread_comm_abort_allowed') + '\n' + function(shared, 'thread_comm_request_automatic_abort') + '\n' + function(shared, 'thread_comm_request_heartbeat_abort') + '\n'
        code += function(safety, 'safety_check_heartbeat') + '\n'
        code += function(safety, 'safety_process_adc_sample') + r'''
static void reset(void) {
    now_ms=10000; heartbeat=1; safety_heartbeat_missing_since_ms=0;
    aborted=calls=safety_heartbeat_timeout_count=retained_launch=0;
    state_reads=change_after_snapshot=state_after_snapshot=0;
    safety_adc_mismatch_count=safety_adc_last_mismatch_mask=safety_adc_latched_mismatch_mask=0;
    g_safety_first_abort=(safety_abort_diagnostic_t){0};
}
int main(void) {
    for (state=0; state<=255; state++) {
        reset();
        unsigned shared_allowed=state>ACTUATOR_FLIGHT_STATE_STARTUP && state<ACTUATOR_FLIGHT_STATE_LAUNCH;
        unsigned heartbeat_allowed=state>ACTUATOR_FLIGHT_STATE_IDLE && state<ACTUATOR_FLIGHT_STATE_LAUNCH;
        assert(thread_comm_abort_allowed()==shared_allowed);
        safety_check_heartbeat();
        assert(aborted==heartbeat_allowed && calls==0);
        assert(safety_heartbeat_timeout_count==heartbeat_allowed);
        assert(g_safety_first_abort.reason==(heartbeat_allowed ? SAFETY_ABORT_HEARTBEAT : 0));
        reset(); heartbeat=0;
        safety_check_heartbeat(); assert(!aborted);
        now_ms+=5000; safety_check_heartbeat();
        assert(aborted==heartbeat_allowed);
        assert(g_safety_first_abort.reason==(heartbeat_allowed ? SAFETY_ABORT_INITIAL_HEARTBEAT : 0));
        /* Idle still reaches the original ADC mismatch accounting. */
        reset(); safety_adc_sample_t sample={7};
        safety_process_adc_sample(&sample);
        assert(safety_adc_mismatch_count==shared_allowed);
        assert(safety_adc_latched_mismatch_mask==(shared_allowed ? 7U : 0U));
        /* No state transition may clear an independently retained abort. */
        aborted=1; safety_check_heartbeat(); assert(aborted==1);
        assert(g_safety_first_abort.reason==0);
    }
    /* A scripted state change after the first read must not relabel an
     * abort selected in PreFill as a Launch-state abort. This is a snapshot
     * consistency regression, not evidence of a field race. */
    state=ACTUATOR_FLIGHT_STATE_PREFILL; reset(); change_after_snapshot=1;
    state_after_snapshot=ACTUATOR_FLIGHT_STATE_LAUNCH;
    safety_check_heartbeat();
    assert(!aborted && !calls && g_safety_first_abort.reason==0);
    /* Snapshot diagnostic is not permission to bypass the atomic launch guard. */
    state=ACTUATOR_FLIGHT_STATE_IDLE; reset();
    safety_heartbeat_missing_since_ms=123; safety_check_heartbeat();
    assert(safety_heartbeat_missing_since_ms==0);
    state=ACTUATOR_FLIGHT_STATE_PREFILL; heartbeat=now_ms;
    safety_check_heartbeat(); assert(!aborted);
    now_ms+=4999; safety_check_heartbeat(); assert(!aborted);
    now_ms++; safety_check_heartbeat(); assert(aborted && calls==0); /* Producer only latches; main-task owner applies outputs. */
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / 'idle-heartbeat-state-table'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'Core/Inc'), '-x', 'c', '-', '-o', str(binary)], input=code, text=True, check=True)
            subprocess.run([str(binary)], check=True)
