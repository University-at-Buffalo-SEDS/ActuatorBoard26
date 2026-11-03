"""Real stepper driver plus main/ThreadX-owner recovery, with fake HAL only.

Covers transient rearm, unsafe readbacks, stale wake cleanup, and a real host
mutex contender. Register levels do not prove motor position or hardware time.
"""
from pathlib import Path
import ast
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", source)
    assert match, name
    start = source.rfind("\n", 0, match.start()) + 1
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RealDriverRecoveryTests(unittest.TestCase):
    def run_checked(self, args):
        result = subprocess.run(args, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_driver_owner_rearm_and_concurrent_stop(self):
        constants = [node.value for node in ast.walk(ast.parse(
            (ROOT / "tests/test_board_recovery_owner.py").read_text()
        )) if isinstance(node, ast.Constant) and isinstance(node.value, str)]
        tx = next(text for text in constants if "typedef unsigned UINT;" in text)
        tx = tx.replace("#include <stdint.h>",
                        "#include <stdint.h>\n#include <pthread.h>\n#include <stdatomic.h>")
        tx = tx.replace("typedef struct{UINT locked;}TX_MUTEX;",
                        "typedef struct{_Atomic UINT locked;pthread_mutex_t native;}TX_MUTEX;")
        self.assertIn("pthread_mutex_t native", tx)
        code = next(text for text in constants if "PRODUCTION" in text and "int main(void)" in text)
        code = code.split("static unsigned prepared")[0]
        code = code.replace("PRODUCTION", (ROOT / "Core/Src/thread_comm.c").read_text())
        replacements = {
            "UINT tx_mutex_create(TX_MUTEX*m,const char*n,UINT i){(void)n;(void)i;m->locked=0;return 0;}": """
#ifdef TEST_REAL_MUTEX
static _Thread_local bool stop_worker;
static atomic_bool stop_waiting;
#endif
UINT tx_mutex_create(TX_MUTEX*m,const char*n,UINT i){
 (void)n;(void)i;m->locked=0;
#ifdef TEST_REAL_MUTEX
 return pthread_mutex_init(&m->native,0)==0?0:TX_MUTEX_ERROR;
#else
 return 0;
#endif
}
""",
            "UINT tx_mutex_get(TX_MUTEX*m,ULONG w){(void)w;if(m->locked)return TX_MUTEX_ERROR;m->locked=1;return 0;}": """
UINT tx_mutex_get(TX_MUTEX*m,ULONG w){
#ifdef TEST_REAL_MUTEX
 if(stop_worker && m==&g_thread_comm_state_mutex)atomic_store(&stop_waiting,true);
 int rc=w==TX_NO_WAIT?pthread_mutex_trylock(&m->native):pthread_mutex_lock(&m->native);
 if(rc)return TX_MUTEX_ERROR;
#else
 (void)w;if(m->locked)return TX_MUTEX_ERROR;
#endif
 m->locked=1;return 0;
}
""",
            "UINT tx_mutex_put(TX_MUTEX*m){assert(m->locked);m->locked=0;return 0;}": """
UINT tx_mutex_put(TX_MUTEX*m){assert(m->locked);m->locked=0;
#ifdef TEST_REAL_MUTEX
 return pthread_mutex_unlock(&m->native)==0?0:TX_MUTEX_ERROR;
#else
 return 0;
#endif
}
""",
        }
        for before, after in replacements.items():
            self.assertIn(before, code)
            code = code.replace(before, after)
        code += (ROOT / "tests/real_driver_recovery/owner_body.c").read_text()
        owner = (ROOT / "Core/Src/main_task.c").read_text()
        names = [
            "all_fill_outputs_off", "fill_outputs_are_off", "fill_stop_locked",
            "fill_monotonic_ms", "fill_minimum_start_ms", "next_fill_serial",
            "prepare_legacy_start", "wake_legacy_start", "finish_legacy_start", "start_plumbing_retract",
            "start_aux_locked", "start_selected_plumbing_retract", "service_aux_locked",
            "service_auxiliary_retract", "service_legacy_locked", "service_plumbing_retract",
            "invalidate_motion_provenance_locked", "fill_snapshot_v2_locked",
            "main_task_fill_snapshot_v2", "recovery_read_outputs", "recovery_prepare_base",
            "recovery_finish_base", "recovery_fill_rearm_ready", "recovery_prepare_outputs",
            "recovery_finish_owner", "bootstrap_stop_owner",
        ]
        actual = "typedef struct {uint64_t source,now;uint32_t epoch,serial;uint8_t result;bool ticket,woke;} FillStart;\n"
        actual += "\n".join(function(owner, name) for name in names)
        code = code.replace("ACTUAL_OWNER", actual)
        cases = [
            "stop_wake", "newer_wake", "stop_recover_newer", "config_failure",
            "timer_failure", "postwake_busy", "minimum_and_complete", "failure_recovery",
            "config_recovery", "failure_unsafe", "step_stuck", "timer_stuck", "stop_failure",
            "unknown_selector", "unready_selector", "exhausted_serial", "bootstrap_direct",
        ]
        with tempfile.TemporaryDirectory(prefix="real-driver-recovery-") as directory:
            work = Path(directory)
            (work / "tx_api.h").write_text(tx)
            (work / "test.c").write_text(code)
            common = ["cc", "-std=c11", "-pthread", "-Wall", "-Wextra", "-Werror",
                      "-DTELEMETRY_ENABLED", "-fsanitize=address,undefined",
                      "-I" + str(work), "-I" + str(ROOT / "tests/real_driver_recovery"),
                      "-I" + str(ROOT / "Core/Inc"),
                      "-I" + str(ROOT / "Drivers/stepper_stuff"), str(work / "test.c"),
                      str(ROOT / "Drivers/stepper_stuff/stepper_driver.c"),
                      str(ROOT / "Core/Src/fill_motion.c"),
                      str(ROOT / "Core/Src/fill_motion_status.c")]
            for real_mutex, selected in [(False, cases), (True, ["concurrent_stop"])]:
                executable = work / ("real_mutex" if real_mutex else "owner_controls")
                args = common + (["-DTEST_REAL_MUTEX"] if real_mutex else [])
                self.run_checked(args + ["-o", str(executable)])
                for case in selected:
                    with self.subTest(case=case):
                        self.run_checked([str(executable), case])


if __name__ == "__main__":
    unittest.main()
