#ifndef IGNITER_SENSE_MATH_H
#define IGNITER_SENSE_MATH_H
#include <stdint.h>
#include <stdbool.h>

/* Read-only signal interpretation. The schematic's 5 V sensor is powered
 * from 3V3; no specified amperes conversion exists for that hardware profile. */
#define IGNITER_SENSE_ADC_MAX 4095U
/* STM32G491 datasheet ADC VDDA operating range:1.62V to3.6V. */
#define IGNITER_SENSE_REFERENCE_MIN_MV 1620U
#define IGNITER_SENSE_REFERENCE_MAX_MV 3600U
#define IGNITER_SENSE_SAMPLE_MAX_AGE_MS 250U
#define IGNITER_SENSE_REFERENCE_MAX_AGE_MS 6000U
#define IGNITER_SENSE_PROFILE_SCHEMATIC_045B5_AT_3V3 1U
#define IGNITER_SENSE_FLAG_SAMPLE_VALID 1U
#define IGNITER_SENSE_FLAG_REFERENCE_VALID 2U
#define IGNITER_SENSE_FLAG_SATURATED 4U
#define IGNITER_SENSE_FLAG_CURRENT_UNSUPPORTED 8U
#define IGNITER_SENSE_FLAG_ADC_FAULT 16U

/* G491 factory VREFINT calibration is at 3.0 V and 12-bit resolution.
 * The caller supplies that device constant, rather than baking another
 * board's calibration voltage into a shared conversion. */
static inline bool igniter_sense_vdda_mv(uint32_t ref_counts,
    uint32_t factory_ref_counts, uint32_t calibration_mv, uint32_t *out)
{
    if (!out || !ref_counts || ref_counts >= IGNITER_SENSE_ADC_MAX ||
        !factory_ref_counts || factory_ref_counts >= IGNITER_SENSE_ADC_MAX ||
        calibration_mv < IGNITER_SENSE_REFERENCE_MIN_MV || calibration_mv > IGNITER_SENSE_REFERENCE_MAX_MV) return false;
    const uint32_t voltage = (calibration_mv * factory_ref_counts + ref_counts / 2U) / ref_counts;
    /* Reject impossible/reference-fault values, not extrapolated volts.
     * This 3V3 board's ADC rail must remain within the MCU's rated 3.6 V. */
    if (voltage < IGNITER_SENSE_REFERENCE_MIN_MV || voltage > IGNITER_SENSE_REFERENCE_MAX_MV) return false;
    *out = voltage;
    return true;
}

static inline uint32_t igniter_sense_flags(uint32_t counts, uint32_t sample_age,
    bool sampled, uint32_t vdda_mv, uint32_t reference_age, bool reference_valid,
    bool adc_fault)
{
    uint32_t flags = IGNITER_SENSE_FLAG_CURRENT_UNSUPPORTED;
    if (adc_fault) flags |= IGNITER_SENSE_FLAG_ADC_FAULT;
    if (sampled && !adc_fault && counts <= IGNITER_SENSE_ADC_MAX &&
        sample_age <= IGNITER_SENSE_SAMPLE_MAX_AGE_MS) {
        flags |= IGNITER_SENSE_FLAG_SAMPLE_VALID;
        if (!counts || counts == IGNITER_SENSE_ADC_MAX)
            flags |= IGNITER_SENSE_FLAG_SATURATED;
        if (reference_valid && vdda_mv >= IGNITER_SENSE_REFERENCE_MIN_MV && vdda_mv <= IGNITER_SENSE_REFERENCE_MAX_MV &&
            reference_age <= IGNITER_SENSE_REFERENCE_MAX_AGE_MS)
            flags |= IGNITER_SENSE_FLAG_REFERENCE_VALID;
    }
    return flags;
}
#endif
