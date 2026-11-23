"""Drive pins vs ribbon-cable SIG inputs, and the Hall mismatch option.

Schematic.pdf (Altium, rev A, 2026-10-06):
- p.2 STM32.SchDoc: PB3 N2_Valve_EN, PB4 N20_Valve_EN, PB7 Igniter_EN are
  output ports; PB12 Igniter_Sig, PB13 N2_Sig, PB14 N20_Sig are input ports.
- p.3 Solenoid_Drive.SchDoc: N2/N20_Valve_EN -> 100R -> 2N7002 gate with a 10k
  pull-down (Q7/R25, Q4/R18) -> LTV-827 opto -> IRFR9024 high side.
- p.7 Igniter_Circuit.SchDoc: Igniter_EN -> 100R -> 2N7002 (Q26, 10k R72) ->
  LTV-817 -> IRFR5305.
- p.8 Connectors.SchDoc: *_Sig only reach the J6 ribbon cable.
So the firmware drives the EN pins and must never treat the SIG pins as
outputs. The source checks below keep it that way.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVES = ('NITROGEN_PIN', 'NITROUS_PIN', 'IGNITER_PIN')
SIGS = ('N2_SIG', 'N20_SIG', 'IGNITER_SIG')


def sources():
    for folder in ('Core/Src', 'Core/Inc', 'Drivers/sol_stuff', 'Drivers/spark_stuff'):
        for path in sorted((ROOT / folder).glob('*.[ch]')):
            yield path, path.read_text(errors='replace')


class DrivePins(unittest.TestCase):
    def test_ioc_and_header_match_the_schematic(self):
        ioc = next(ROOT.glob('*.ioc')).read_text()
        for pin, label, signal in (('PB3', 'NITROGEN_PIN', 'GPIO_Output'), ('PB4', 'NITROUS_PIN', 'GPIO_Output'),
                                   ('PB7', 'IGNITER_PIN', 'GPIO_Output'), ('PB12', 'IGNITER_SIG', 'GPIO_Input'),
                                   ('PB13', 'N2_SIG', 'GPIO_Input'), ('PB14', 'N20_SIG', 'GPIO_Input')):
            self.assertIn(f'{pin}.GPIO_Label={label}\n', ioc)
            self.assertIn(f'{pin}.Signal={signal}\n', ioc)
        header = (ROOT / 'Core/Inc/main.h').read_text()
        for name, number in (('NITROGEN_PIN', 3), ('NITROUS_PIN', 4), ('IGNITER_PIN', 7),
                             ('IGNITER_SIG', 12), ('N2_SIG', 13), ('N20_SIG', 14)):
            self.assertIn(f'#define {name}_Pin GPIO_PIN_{number}\n', header)
            self.assertIn(f'#define {name}_GPIO_Port GPIOB\n', header)

    def test_drives_are_written_off_before_they_become_outputs(self):
        main = (ROOT / 'Core/Src/main.c').read_text()
        init = main[main.index('static void MX_GPIO_Init(void)\n{'):]
        init = init[:init.index('\n}\n')]
        init = re.sub(r'/\*.*?\*/', '', init, flags=re.S)
        statements = [s.strip() for s in init.split(';')]
        for drive in DRIVES:
            low = next(i for i, s in enumerate(statements)
                       if s.startswith('HAL_GPIO_WritePin(GPIOB') and f'{drive}_Pin' in s and 'GPIO_PIN_RESET' in s)
            configured = next(i for i, s in enumerate(statements)
                              if s.startswith('GPIO_InitStruct.Pin') and f'{drive}_Pin' in s)
            self.assertLess(low, configured, drive)
            self.assertEqual(statements[configured + 1], 'GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP')
        for sig in SIGS:
            configured = next(i for i, s in enumerate(statements)
                              if s.startswith('GPIO_InitStruct.Pin') and f'{sig}_Pin' in s)
            self.assertEqual(statements[configured + 1], 'GPIO_InitStruct.Mode = GPIO_MODE_INPUT')

    def test_sig_inputs_are_never_written_or_reported_as_outputs(self):
        for path, text in sources():
            if path.name in ('main.h', 'main.c'):
                continue
            for sig in SIGS:
                self.assertNotRegex(text, rf'HAL_GPIO_WritePin\(\s*{sig}_GPIO_Port', f'{path}: {sig}')
                self.assertNotRegex(text, rf'{sig}_GPIO_Port->ODR', f'{path}: {sig}')
                self.assertNotIn(f'{sig}_Pin', text, f'{path}: {sig}')
        driver = (ROOT / 'Drivers/sol_stuff/solenoid_driver.h').read_text()
        self.assertNotIn('sig_port', driver)


SAFETY_HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include "safety_diagnostics.h"
#include "current_sense_trust.h"
typedef struct { uint32_t value; } safety_adc_sample_t;
static safety_adc_sample_t safety_adc_latest_sample;
static uint32_t safety_adc_mismatch_count, safety_adc_last_mismatch_mask, safety_adc_latched_mismatch_mask;
#define SAFETY_ADC_MISMATCH_LIMIT 10U
static unsigned aborts, allowed = 1;
static uint32_t thread_comm_abort_allowed(void) { return allowed; }
static uint32_t safety_sample_mismatch_mask(const safety_adc_sample_t *s) { return s->value; }
static uint64_t safety_now_ms(void) { return 1; }
static uint64_t thread_comm_get_groundstation_heartbeat_ms(void) { return 1; }
static uint32_t thread_comm_get_flight_state(void) { return 2; }
static uint64_t telemetry_event_clock_snapshot(void) { return 0; }
static uint8_t thread_comm_request_local_abort(uint32_t reason, uint64_t a, uint64_t b, uint32_t c,
    uint32_t detail, uint64_t d, uint8_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    assert(reason == SAFETY_ABORT_CURRENT_MISMATCH && detail == 3U); aborts++; return 1;
}
FUNCTIONS
int main(void) {
    const safety_adc_sample_t bad = {3U};
    for (unsigned i = 0; i < 1000; i++) safety_process_adc_sample(&bad);
    const uint32_t status = safety_current_sense_status();
    assert(((status >> CURRENT_SENSE_STATUS_MISMATCH_SHIFT) & 7U) == 3U);
#if AB_HALL_MISMATCH_ABORT
    assert(aborts > 0U && (status & CURRENT_SENSE_STATUS_ABORT_ENABLED));
#else
    assert(aborts == 0U && !(status & CURRENT_SENSE_STATUS_ABORT_ENABLED));
    assert((status & CURRENT_SENSE_ALL_UNTRUSTED) == CURRENT_SENSE_ALL_UNTRUSTED);
#endif
    return 0;
}
'''


class HallMismatchOption(unittest.TestCase):
    def build(self, *flags):
        safety = (ROOT / 'Core/Src/safety_task.c').read_text()
        functions = '\n'.join(re.search(pattern, safety, re.S).group() for pattern in (
            r'static void safety_process_adc_sample\([^)]*\)\s*\{.*?\n\}',
            r'uint32_t safety_current_sense_status\(void\)\s*\{.*?\n\}'))
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'mismatch'
            result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', *flags,
                                     '-I', str(ROOT / 'Core/Inc'), '-x', 'c', '-', '-o', str(binary)],
                                    input=SAFETY_HARNESS.replace('FUNCTIONS', functions),
                                    text=True, capture_output=True)
            if result.returncode:
                return result
            return subprocess.run([str(binary)], capture_output=True, text=True)

    def test_default_build_never_aborts_and_reports_untrusted(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_enabling_the_abort_on_untrusted_hardware_does_not_build(self):
        result = self.build('-DAB_HALL_MISMATCH_ABORT=1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('AB_HALL_MISMATCH_ABORT acts on Hall channels marked untrusted', result.stderr)

    def test_explicitly_enabled_abort_on_trusted_hardware_acts(self):
        result = self.build('-DAB_HALL_MISMATCH_ABORT=1', '-DAB_CURRENT_SENSE_UNTRUSTED=0')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_no_commented_out_abort_and_cmake_default_off(self):
        safety = (ROOT / 'Core/Src/safety_task.c').read_text()
        self.assertNotRegex(safety, r'//\s*\(void\)\s*thread_comm_set_abort')
        self.assertIn('#if AB_HALL_MISMATCH_ABORT', safety)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('option(AB_HALL_MISMATCH_ABORT "Abort on commanded-vs-Hall current mismatch" OFF)', cmake)


if __name__ == '__main__':
    unittest.main()
