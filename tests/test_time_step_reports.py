"""Valve-state reports stay at 1 Hz and admissible across network clock steps.

Bench evidence (SEDSnet 4.3.14, Normal/Idle): both umbilical boards' reports
were refused once with source_minus_station about -607 s. A board clock that
disagreed with the GS by more than 60 s was restarted on every GS packet, and
while restarting SEDSnet stamped reports with board uptime, which the GS
refuses; a source that kept disagreeing with the GS left every report refused.

The actual 1 Hz reporter, retry, publisher, heartbeat, network-health and
recovery station-clock C runs tick by tick against a modeled SEDSnet consumer
clock (anchored by its upstream's announce every 2 s, invalid after a restart)
and a GS that sends a heartbeat stamped with its own network clock every
second. Each case steps one clock by 600 s in one direction.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from router_lifecycle_fixture import function

ROOT = Path(__file__).resolve().parents[1]


def block(source, first, last):
    return re.search(re.escape(first) + r".*?" + re.escape(last), source, re.S).group()


def harness():
    telemetry = (ROOT / "Core/Src/telemetry.c").read_text()
    transport = (ROOT / "Core/Src/board_recovery_transport.c").read_text()
    main = (ROOT / "Core/Src/main_task.c").read_text()
    retry = (ROOT / "Core/Inc/status_report_retry.h").read_text()
    retry = "\n".join(l for l in retry.splitlines() if not l.startswith("#include"))
    period = re.search(r"^#define UMBILICAL_STATUS_PERIOD_TICKS .*$", main, re.M).group(0)
    scaffold = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ground_heartbeat_owner.h"
#define TELEMETRY_ENABLED 1
#define TX_TIMER_TICKS_PER_SECOND 1000U
#define TX_SUCCESS 0U
typedef unsigned long ULONG;
typedef int SedsResult;typedef int SedsRouter;
enum {SEDS_OK=0,SEDS_ERR=-1,SEDS_BAD_ARG=-10,SEDS_IO=-14,SEDS_DT_UMBILICAL_STATUS=7,SEDS_DT_HEARTBEAT=9};
enum {CMD_IGNITER_ON=7,CMD_RETRACT_PLUMBING=8,CMD_NITROGEN_OPEN=9,CMD_NITROUS_OPEN=10};
typedef struct {int ty;const char*sender;size_t sender_len;uint64_t timestamp;} SedsPacketView;
#define BASE UINT64_C(1794183600000)
static SedsRouter router_cookie;
static struct {SedsRouter*r;uint8_t created;} g_router={&router_cookie,1};
static ULONG ticks;
static ULONG tx_time_get(void){return ticks;}
/* Modeled clocks, as offsets from true time BASE+ticks (1 tick = 1 ms). */
static int64_t gs_offset,upstream_offset,net_offset;static bool net_valid;
static uint64_t gs_now(void){return (uint64_t)((int64_t)(BASE+ticks)+gs_offset);}
static SedsResult seds_router_get_network_time_ms(SedsRouter*r,uint64_t*out){
 assert(r==&router_cookie);if(!net_valid)return SEDS_IO;*out=(uint64_t)((int64_t)(BASE+ticks)+net_offset);return SEDS_OK;}
static uint64_t telemetry_unix_ms_locked(void){uint64_t t=0;return seds_router_get_network_time_ms(g_router.r,&t)==SEDS_OK?t:0U;}
static unsigned restarts;
static SedsResult telemetry_configure_timesync_locked(SedsRouter*r){assert(r==&router_cookie);net_valid=false;restarts++;return SEDS_OK;}
static void telemetry_event_clock_observe(uint64_t utc,uint32_t tick){(void)utc;(void)tick;}
static unsigned depth;
static bool telemetry_router_enter(void){depth++;return true;}
static bool telemetry_router_leave(void){assert(depth);depth--;return true;}
static SedsResult init_telemetry_router(void){return SEDS_OK;}
static uint8_t authorized_primary(SedsRouter*r,const SedsPacketView*p){(void)r;return p&&p->sender_len==2&&!memcmp(p->sender,"GS",2);}
static int sim_probe_heartbeat_handler(const SedsPacketView*p,void*u){(void)p;(void)u;return 0;}
static unsigned thread_comm_get_abort(void){return 0U;}
static unsigned thread_comm_note_groundstation_heartbeat(uint64_t t){(void)t;return TX_SUCCESS;}
static uint64_t telemetry_now_ms(void){return ticks;}
static uint64_t tx_raw_now_ms_locked(void){return ticks;}
static volatile uint32_t g_heartbeat_handler_count,g_heartbeat_handler_error_count;
static volatile uint32_t g_telemetry_timesync_valid,g_telemetry_discovery_seen=1,g_telemetry_network_ready;
static volatile uint32_t g_umbilical_status_attempts,g_umbilical_status_last_payload,g_umbilical_status_enqueue_failures,g_umbilical_status_ok_ms;
static volatile int32_t g_umbilical_status_last_result;
/* GS admission: a report is refused more than 100 ms ahead or 2 s behind.
 * Without an explicit stamp SEDSnet uses its network clock, else uptime. */
static ULONG last_report[16];static ULONG max_gap,gap_from;static unsigned reports,refused;
static SedsResult seds_router_log_bytes_ex(SedsRouter*r,int type,const uint8_t*data,size_t len,const uint64_t*stamp,bool queue){
 assert(depth==1&&r==&router_cookie&&type==SEDS_DT_UMBILICAL_STATUS&&len==2&&!queue);
 const uint8_t id=data[0];assert(id<16);
 if(ticks>=gap_from&&last_report[id]&&ticks-last_report[id]>max_gap)max_gap=ticks-last_report[id];
 last_report[id]=ticks;reports++;
 const uint64_t sent=stamp?*stamp:(net_valid?telemetry_unix_ms_locked():ticks);
 const int64_t skew=(int64_t)sent-(int64_t)gs_now();
 if(ticks>=gap_from&&(skew>100||skew<-2000))refused++;
 return SEDS_OK;}
static uint8_t g_igniter_on,g_nitrogen_open,g_nitrous_open,g_plumbing_retracted;
volatile uint32_t g_network_clock_resyncs;
SedsResult telemetry_publish_umbilical_status(uint8_t cmd_id, uint8_t on);
"""
    pieces = [
        scaffold,
        block(transport, "static volatile uint64_t station_utc;", "static _Atomic uint32_t station_seq;"),
        *(function(transport, n) for n in ("station_estimate", "station_store",
                                             "board_recovery_observe_station_time",
                                             "board_recovery_network_clock_resync_needed")),
        block(telemetry, "#define NETWORK_CLOCK_RESYNC_HOLDOFF_TICKS", "static ULONG g_station_report_tick;"),
        *(function(telemetry, n) for n in ("Heartbeat_handler", "telemetry_update_network_health",
                                             "telemetry_report_utc_locked",
                                             "telemetry_log_umbilical_status",
                                             "telemetry_publish_umbilical_status")),
        retry, period,
        *(function(main, n) for n in ("publish_all_umbilical_statuses",
                                        "publish_umbilical_statuses_if_due")),
    ]
    return "\n".join(pieces) + MAIN


MAIN = r"""
/* One case: step the named clock by delta_ms at 60 s, then run 20 minutes.
 * upstream:   the board's time source steps away from the GS and stays there
 *             (a source change); the board clock steps with it.
 * gs:         the GS clock steps and the network follows it; a backward
 *             step reaches the board at once, a forward one only by slewing.
 * unanchored: the board clock steps once, and after its restart no announce
 *             anchors it again. */
static void run(const char*kind,int64_t delta){
 const ULONG step_at=60000U,end=step_at+20U*60000U;bool anchors=true;ULONG last_status=0;
 for(ticks=1;ticks<end;ticks++){
  /* Network thread: health first, then this tick's received packets. */
  telemetry_update_network_health(&router_cookie);
  if(ticks==step_at){
   if(!strcmp(kind,"upstream")){upstream_offset+=delta;net_offset=upstream_offset;}
   else if(!strcmp(kind,"unanchored")){net_offset+=delta;anchors=false;}
   else{gs_offset+=delta;upstream_offset=gs_offset;if(delta<0)net_offset=upstream_offset;}
   gap_from=step_at;
  }
  if(anchors&&ticks%2000U==37U&&!net_valid){net_valid=true;net_offset=upstream_offset;}
  if(ticks%1000U==500U){SedsPacketView hb={SEDS_DT_HEARTBEAT,"GS",2,gs_now()};(void)Heartbeat_handler(&hb,NULL);}
  /* Control owner: the actual 1 Hz reporter and 100 ms pending retry. */
  publish_umbilical_statuses_if_due(&last_status);
  retry_pending_status_reports();
 }
 printf("%s %+lld ms: reports=%u max_gap=%lu ms refused=%u restarts=%u\n",kind,(long long)delta,reports,max_gap,refused,restarts);
 assert(max_gap<=1000U+100U);
 assert(reports>=4U*(end/1000U-2U));
 /* Stamped near the GS clock, so the GS admits them, after at most one
  * heartbeat period of transition. */
 assert(refused<=8U);
 /* A source that disagrees with the GS is not restarted on every packet. */
 assert(restarts<=(end-step_at)/30000U+2U);
}
int main(int argc,char**argv){assert(argc==3);run(argv[1],atoll(argv[2]));return 0;}
"""


class TimeStepReports(unittest.TestCase):
    def test_reports_keep_cadence_and_admission_across_600_s_steps(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "time-step"
            build = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                    "-Wno-unused-function", "-fsanitize=address,undefined",
                                    "-I", str(ROOT / "Core/Inc"), "-x", "c", "-", "-o", str(exe)],
                                   input=harness(), text=True, capture_output=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            for kind in ("upstream", "gs", "unanchored"):
                for delta in (-600000, 600000):
                    with self.subTest(clock=kind, step_ms=delta):
                        run = subprocess.run([str(exe), kind, str(delta)], text=True,
                                             capture_output=True, timeout=120)
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
