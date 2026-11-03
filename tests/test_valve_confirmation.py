"""Execute production valve switch cases with inert driver/transport mocks."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ValveConfirmationTests(unittest.TestCase):
    def test_success_rejection_close_and_abort_report_actual_state(self):
        source = (ROOT / 'Core/Src/main_task.c').read_text()
        from router_lifecycle_fixture import function
        from test_safety_owner_race import block
        handle=function(source,'handle_command')
        publication=block(handle,'if(cmd.cmd==CMD_IGNITER_ON')
        raw=function(source,'apply_automatic_valve')+'\n'+function(source,'apply_manual_valve_locked')
        code = r'''
#include <assert.h>
#include <stdint.h>
enum { CMD_NITROGEN_OPEN=9, CMD_NITROUS_OPEN=10,
       CMD_NITROGEN_CLOSE=12, CMD_NITROUS_CLOSE=13, CMD_IGNITER_ON=6, CMD_IGNITER_OFF=7 };
static int n2_solenoid, n20_solenoid, reject_open, aborted;
static uint8_t g_nitrogen_open, g_nitrous_open,g_igniter_on;
static int count, reported_command, reported_state;
static int solenoidOn(int *p) { (void)p; return reject_open ? -1 : 0; }
static void solenoidOff(int *p) { (void)p; }
static uint8_t g_launch_sequence_active,g_launch_sequence_igniter_started,g_launch_sequence_holdoff_active;
static int igniter_on(void){return 0;}static void igniter_off(void){}
static int thread_comm_manual_effect_owned(uint64_t t,uint32_t epoch,void(*apply)(void*),void*p){(void)t;assert(epoch==7);if(aborted)return 0;apply(p);return 1;}
static void main_task_force_outputs_safe_off(void) {
    g_nitrogen_open=0; g_nitrous_open=0;
}
static void publish_expected_outputs(void) {}
static int publish_umbilical_status(int cmd, int on) {
    count++; reported_command=cmd; reported_state=on; return 0;
}
''' + raw + r'''
static void command(int value) { struct {uint8_t cmd;uint64_t timestamp_ms;} cmd={(uint8_t)value,501};uint32_t command_epoch=7;
''' + publication + r'''
}
int main(void) {
    const int opens[]={CMD_NITROGEN_OPEN,CMD_NITROUS_OPEN};
    const int closes[]={CMD_NITROGEN_CLOSE,CMD_NITROUS_CLOSE};
    for (int i=0;i<2;i++) {
        main_task_force_outputs_safe_off();
        reject_open=1; aborted=0; count=0;
        command(opens[i]);
        assert(count==1 && reported_command==opens[i] && reported_state==0);
        reject_open=0; count=0;
        command(opens[i]);
        assert(count==1 && reported_command==opens[i] && reported_state==1);
        count=0; command(closes[i]);
        assert(count==1 && reported_command==opens[i] && reported_state==0);
        count=0; command(closes[i]);
        assert(count==1 && reported_state==0);
        aborted=1; count=0; command(opens[i]);
        assert(count==0 && !g_nitrogen_open && !g_nitrous_open);
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(pathlib.Path(tmp) / 'valve-confirmation')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-x', 'c', '-', '-o', binary], input=code, text=True, check=True)
            subprocess.run([binary], check=True)
