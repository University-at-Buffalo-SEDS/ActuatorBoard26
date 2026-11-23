#ifndef CURRENT_SENSE_TRUST_H
#define CURRENT_SENSE_TRUST_H
#include <stdint.h>

/* Hall current-sense trust (docs/hall-current-sense-audit.md).
 *
 * The fitted analog chains cannot measure the drive currents:
 *  - N2O (U3) and N2 (U5) ACS711 outputs sit at a 1.65 V midpoint and feed
 *    x28 non-inverting amplifiers (U2/U4) with no offset removal, so they
 *    saturate at any current, including zero.
 *  - The H-bridge Hall sensor (U7) primary is wired in parallel with the
 *    motor, and its amplifier (U6, x3.5) saturates the same way.
 *  - The igniter sensor (U10) is the 5 V ACS71240...-045B5 powered from 3V3;
 *    its buffer (U9) is fine, but amperes have no specified conversion. Raw
 *    counts are still published in AB_IGNITER_SENSE.
 *
 * So nothing may act on these readings. The commanded-vs-measured mismatch
 * abort is a compile-time option, default OFF (CMake AB_HALL_MISMATCH_ABORT).
 * Turning it on also requires clearing AB_CURRENT_SENSE_UNTRUSTED for the
 * channels once the hardware is corrected and characterised; the build
 * refuses an abort that would act on channels still marked untrusted. */
#ifndef AB_HALL_MISMATCH_ABORT
#define AB_HALL_MISMATCH_ABORT 0
#endif

#define CURRENT_SENSE_N2O_UNTRUSTED       (1UL << 0)
#define CURRENT_SENSE_N2_UNTRUSTED        (1UL << 1)
#define CURRENT_SENSE_IGNITER_UNTRUSTED   (1UL << 2)
#define CURRENT_SENSE_HBRIDGE_UNTRUSTED   (1UL << 3)
#define CURRENT_SENSE_ALL_UNTRUSTED       (0xFUL)

#ifndef AB_CURRENT_SENSE_UNTRUSTED
#define AB_CURRENT_SENSE_UNTRUSTED CURRENT_SENSE_ALL_UNTRUSTED
#endif

#if AB_HALL_MISMATCH_ABORT && (AB_CURRENT_SENSE_UNTRUSTED & \
    (CURRENT_SENSE_N2O_UNTRUSTED | CURRENT_SENSE_N2_UNTRUSTED | CURRENT_SENSE_IGNITER_UNTRUSTED))
#error "AB_HALL_MISMATCH_ABORT acts on Hall channels marked untrusted; fix the analog chain (docs/hall-current-sense-audit.md) and clear AB_CURRENT_SENSE_UNTRUSTED first"
#endif

/* BOARD_HEALTH words 19..20: tag "CSN1", then the status word below. */
#define CURRENT_SENSE_STATUS_TAG 0x314E5343UL
/* Status word: bits 0..3 untrusted channels (as above), bit 8 the mismatch
 * abort is compiled in, bits 16..18 the mismatch mask latched since boot
 * (N2O, N2, igniter; diagnostic only, never acted on while untrusted). */
#define CURRENT_SENSE_STATUS_ABORT_ENABLED (1UL << 8)
#define CURRENT_SENSE_STATUS_MISMATCH_SHIFT 16U

static inline uint32_t current_sense_status_word(uint32_t latched_mismatch_mask)
{
    return (uint32_t)(AB_CURRENT_SENSE_UNTRUSTED & CURRENT_SENSE_ALL_UNTRUSTED) |
           (AB_HALL_MISMATCH_ABORT ? (uint32_t)CURRENT_SENSE_STATUS_ABORT_ENABLED : 0U) |
           ((latched_mismatch_mask & 7U) << CURRENT_SENSE_STATUS_MISMATCH_SHIFT);
}

/* safety_task.c: the status word for the current boot. */
uint32_t safety_current_sense_status(void);

#endif
