"""A delivered reliable packet that AB deliberately refuses is still acknowledged.

The SDK withholds the end-to-end receipt whenever an endpoint handler or the
managed-variable callback returns an error, and then lets every retransmit run
the handlers again. The station therefore retransmits a refused packet for its
whole retry budget. Deterministic refusals (stale, aborted, foreign sender,
malformed) must return success without applying anything and be counted;
transient failures (full queue, owner busy) must still withhold the receipt.

Real SDK router, discovery and receipts; actual AB endpoint handlers, managed
FLIGHT_STATE owner, fill settings and recovery transport. Owners are fakes.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from native_sdk import configured_dependency_source, prepare_native_sdk

ROOT = Path(__file__).resolve().parents[1]
HANDLERS = ('launch_monotonic_tick', 'admit_timed_launch', 'Valve_Command_handler',
            'Abort_handler', 'Flight_State_handler', 'flight_state_cache_apply_network_update',
            'Board_Recovery_handler', 'control_packet_handler', 'control_endpoint',
            'flight_state_endpoint')


def function(source, name):
    match = re.search(r'(?m)^[A-Za-z][^\n;]*\b' + name + r'\([^;{]*\)\s*\{', source)
    if not match:
        return None
    depth, end = 1, source.index('{', match.start()) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


class ReliableRefusalAckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sdk = prepare_native_sdk(ROOT)
        launchcore = configured_dependency_source(ROOT, 'sedslaunchcore', 'SDK_FREE_LAUNCHCORE_SOURCE')
        cls.tmp = tempfile.TemporaryDirectory(prefix='reliable-refusal-ack-')
        path = Path(cls.tmp.name)
        telemetry = (ROOT / 'Core/Src/telemetry.c').read_text()
        handlers = [function(telemetry, name) for name in HANDLERS]
        self_check = [name for name, body in zip(HANDLERS, handlers) if body is None]
        if self_check:
            raise AssertionError('Production handler missing: ' + ', '.join(self_check))
        fixtures = ROOT / 'tests/fixtures/reliable_refusal_ack'
        source = (fixtures / 'harness.c').read_text().replace(
            '@@HANDLERS@@', '\n'.join('static ' + body if not body.startswith(('static', 'SedsResult flight_state_cache')) else body
                                     for body in handlers if body))
        (path / 'harness.c').write_text(source)
        cls.exe = path / 'harness'
        modules = ['flight_state_cache.c', 'fill_settings.c', 'fill_motion_status.c',
                   'board_recovery_transport.c', 'board_recovery_wire.c']
        command = ['cc', '-std=c11', '-DTELEMETRY_ENABLED=1', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-g', '-fsanitize=address,undefined',
                   '-I' + str(fixtures), '-I' + str(ROOT / 'Core/Inc'),
                   '-I' + str(Path(cls.sdk['header']).parent),
                   '-I' + str(launchcore / 'bootloader/include'),
                   str(path / 'harness.c'), *[str(ROOT / 'Core/Src' / name) for name in modules],
                   cls.sdk['archive'], '-lpthread', '-lm']
        if sys.platform.startswith('linux'):
            command.append('-ldl')
        subprocess.run(command + ['-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def case(self, name):
        result = subprocess.run([str(self.exe), str(ROOT / 'config/sedsnet.json'), name],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stale_flight_state_is_acknowledged_not_applied(self): self.case('fstate_stale')
    def test_foreign_flight_state_write_is_acknowledged_not_applied(self): self.case('fstate_foreign')
    def test_flight_state_in_abort_is_acknowledged_not_applied(self): self.case('fstate_aborted')
    def test_flight_state_older_than_cutoff_is_acknowledged_not_applied(self): self.case('fstate_late')
    def test_transient_flight_state_failure_is_still_retried(self): self.case('fstate_transient')
    def test_accepted_flight_state_is_applied_and_persisted(self): self.case('fstate_applied')
    def test_in_flight_state_is_applied_and_persisted(self): self.case('fstate_applied_in_flight')
    def test_retired_flight_state_is_mapped_and_logged_once(self): self.case('fstate_applied_retired')
    def test_flight_state_above_in_flight_is_acknowledged_not_applied(self): self.case('fstate_out_of_set')
    def test_station_fill_query_is_acknowledged_and_answered(self): self.case('fill_query')
    def test_foreign_fill_request_is_acknowledged_not_answered(self): self.case('fill_foreign')
    def test_malformed_fill_request_is_acknowledged_not_answered(self): self.case('fill_malformed')
    def test_unknown_automatic_command_is_acknowledged_not_admitted(self): self.case('command_malformed')
    def test_full_command_queue_is_still_retried(self): self.case('command_queue_full')
    def test_foreign_recovery_request_is_acknowledged(self): self.case('recovery_foreign')
    def test_malformed_recovery_request_is_acknowledged(self): self.case('recovery_malformed')
    def test_station_abort_fan_out_is_acknowledged_once(self): self.case('abort')


if __name__ == '__main__':
    unittest.main()
