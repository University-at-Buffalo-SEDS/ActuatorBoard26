# Hall current-sense schematic audit (2026-10-06)

Source: Schematic PDF_[No Variations] (7).pdf, sheets 3, 4 and 7.
This is a schematic/source audit, not a measurement of the populated board.

ADC1 channels IN1/PA0, IN2/PA1 and IN3/PA2 match N2O, N2 and
igniter respectively. ADC2 IN3/PA6 matches H_Bridge_ADC, but the firmware
currently never starts/samples ADC2. No current-in-amperes telemetry is
implemented; safety_task.c compares raw ADC counts instead.

## Analog circuit limitations

- N2O and N2 use ACS711ELCTR-25AB-T at 3.3 V. Their zero-current
  output is nominally 1.65 V and sensitivity is 55 mV/A. U2/U4 are
  non-inverting amplifiers with 27k feedback and 1k to ground: gain 28.
  With no subtraction of the Hall sensor's midpoint, even zero current
  demands 46.2 V at the output of a 3.3 V amplifier, so it saturates.
- H-bridge U6 similarly has gain 1 + 2.5k/1k = 3.5. Zero current demands
  5.775 V, again beyond its 3.3 V supply. The annotation's 3.636 gain
  also differs from the fitted resistor values.
- H-bridge U7's IP+ and IP- nets are H_BRIDGE_IN and H_BRIDGE_OUT,
  the same two nodes as motor connector P5. As drawn the Hall sensor's
  low-resistance primary conductor is parallel to the motor, not in series.
  Confirm the populated wiring before energizing this bridge; current
  sensing requires a series conductor in a motor leg or the supply path.
- Igniter U9 is a valid unity-gain buffer. However U10 is specified as
  ACS71240LLCBTR-045B5, a 5 V supply variant, and is powered from 3.3 V
  on this schematic. Its specified transfer function is not valid at 3.3 V.

The ACS711 midpoint means the current off-range of 0..128 ADC counts
cannot represent zero current on an unshifted, functioning Hall path.
Simply adjusting those thresholds cannot recover a saturated analog signal.
Do not silently weaken the interlock to make these inputs appear functional.
Correct/confirm the analog hardware, measure unloaded and known-current
points, then add offset/sensitivity conversion and per-channel safety limits.

References:
- https://www.allegromicro.com/-/media/files/datasheets/acs711-datasheet.pdf
- https://www.allegromicro.com/-/media/files/datasheets/acs71240-datasheet.pdf
- https://www.ti.com/lit/ds/symlink/tlv8811.pdf
