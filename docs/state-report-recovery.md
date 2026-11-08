# Output state submission and memory

Output actions publish `UMBILICAL_STATUS` immediately. This application state
is separate from the SEDSNet protocol acknowledgement. Failed submissions retain
only the newest value per output and retry at most every 100 ms from the output
task. A close or safety transition supersedes a failed open report. Successful
submission does not itself prove remote delivery.

This requires the SEDSNet v4.0.30 missing-route fix: a reliable application
send with no discovered remote route must return an error, not silently succeed.
The current board tracks SEDSNet `main`, including topology-baseline recovery from
v4.0.32 and bounded header-cache and
peer-restart recovery. The matching candidate passed the 16-second gate,
120-second restart regression and 600-second seven-board soak on Jupiter.
Hardware validation remains required.

Immediate publication executes the Rust router on the output task stack. The
old 4 KiB stack stalled in the seven-board simulation. The task now has 12 KiB,
with a simulator-visible remaining-stack probe, loop counter and hard-fault
probes. Safety behavior and output mappings are unchanged.

Release builds disable USB debugging. `board_memory_config.h`, included within
a CubeMX-preserved USER CODE block, reuses that unused USB pool allocation for
the application byte pool: 69,048 + 15,952 = 85,000 bytes. Debug retains the
original two pool sizes. The release image uses 105,264 of 114,688 SRAM bytes
with the current Jupiter compiler; inspect the build report after toolchain or
source updates rather than assuming this number stays fixed.

The FirmwareSimulator `scripts/test-route-recovery.py` test boots the real
seven-board images and GroundStation. It detaches Gateway from CAN for the first
12 simulated seconds while Valve and Actuator retain their local CAN peers.
The selected output board must report failed submission, retain state and retry
after discovery, without local CAN send errors. The old library fails these
checks; the fixed library passed the 40-second Valve regression on Jupiter.
The normal 16-second seven-board test also passed. Repeated post-reconnection
commands additionally require fresh state responses from both output boards.
Hardware behavior and long-duration stability still require separate validation.

The first 600-second run on v4.0.30 completed but failed command-response
assertions after GroundStation restarted: only 13 of 20 required responses were
observed. It is not a passing soak. The host command schedule incorrectly began
at round one after restarting; a regression now preserves global round numbers.
The additional `--restart-regression` scenario runs 120 simulated seconds with
the same restart schedule before repeating the full ten-minute qualification.
Actuator probes also check heartbeat safety timeouts, first local safety-abort
time, command queue drops and state-submission retries. Safety policy is unchanged.

A separate peer-restart compact-header bug was then reproduced in SEDSNet:
unchanged discovery identity did not cause a transmit-header refresh, so a
restarted GroundStation could not decode application reports immediately.
The dev fix refreshes TX headers on a topology/schema request without discarding
routes or RX dictionaries. It passes the full library suite and all seven
firmware builds. The subsequent v4.0.31 candidate also makes reliable frames
self-describing without retaining unused compact templates and bounds timestamp
retention when compression is disabled. It passed ten-minute qualification;
`bounded-header-ten-minute.log` retains the results on Jupiter.

The healthy-board simulator layout drives GPIOC0–GPIOC3 high: these are the
external active-low H-bridge, nitrous, nitrogen and igniter fault signals. Leaving
them at zero models a real driver fault and correctly rejects an open command.
The driver safety logic is not disabled for testing. A layout contract test keeps
these pin assignments aligned with the generated firmware header.

## Self-recovery of a stalled valve-state stream

Two network-thread checks restore valve-state delivery without a power cycle.
Neither touches outputs, commands, the abort latch or launch state.

- **CAN controller stall.** The controller is restarted through the bus-off
  path if it is not bus-off but its 3-slot transmit FIFO has not drained for
  2 s, or if hardware has left it in INIT while the driver believes it is
  running. Counter: `g_fdcan_tx_stall_count`. Bus-off restarts still count in
  `g_fdcan_bus_off_count`, `g_fdcan_recovery_count`,
  `g_fdcan_recovery_attempt_count` and `g_fdcan_recovery_fail_count`.
- **Valve-state stall.** If every `UMBILICAL_STATUS` submission has failed for
  5 s, the router's queued transmit items are dropped (retained reliable
  deliveries stay and retry), the CAN controller is restarted and discovery
  and schema are announced again. This clears a missing route, an ordered
  reliable entry pinned behind a queued copy, and a full reliable window. It
  repeats at most every 5 s while failures continue. Counter:
  `g_network_self_recovery_count`. Related probes:
  `g_umbilical_status_attempts`, `g_umbilical_status_enqueue_failures` and
  `g_umbilical_status_last_result`.

The checks cannot fix reports that GroundStation receives and then refuses,
for example because the board's clock is outside the station's acceptance
window. GroundStation reports those in the `valve_reports` field of
`/api/gse/status` and as a "valve state STALE" message.
