#ifndef BOARD_RECOVERY_FLOOR_H
#define BOARD_RECOVERY_FLOOR_H
#include <stdint.h>
/* Every source/cutoff time here is a packed (epoch, ms) stamp (board_stamp.h)
 * and the persisted 16-byte floor record (issuer, stamp) keeps its layout: a
 * floor written before epochs holds plain ms and reads as epoch 0, so a Reset
 * stamped after any later network time step (a newer epoch) orders above it.
 * At boot the router's epoch is raised to the floor's (telemetry.c). */
/* No implicit clear. Corrupt/storage-failed floor rejects commands; QUERY lives.
 * Caller must separately validate canonical current network time and source. */
uint8_t board_recovery_floor_active(void);
/* May perform storage access: never call under control-owner mutex. */
uint8_t board_recovery_floor_read_cutoff(uint64_t *cutoff_ms);
uint8_t board_recovery_floor_allows_recovery_time(uint64_t issuer_session, uint64_t source_time_ms);
uint8_t board_recovery_floor_is_healthy(void);
uint8_t board_recovery_command_time_allowed(uint64_t canonical_time_ms);
/* Explicit authorized RECOVER only, after boot/epoch/nonce/fresh-time checks.
 * Repairs require a strictly later (epoch, ms) stamp than every verified or
 * attempted one, across all issuers. A network time step back starts a newer
 * epoch, so it never blocks a fresh Reset; a replay stamped before the floor
 * stays below it. Changing issuer never clears or lowers the floor.
 * Returns 1 only after persistent write + exact readback; must precede latches. */
uint8_t board_recovery_floor_commit(uint64_t issuer_session, uint64_t source_time_ms);
uint8_t board_recovery_floor_accept_bootstrap(uint64_t original_source_time_ms);
uint8_t board_recovery_floor_note_bootstrap_attempt(uint64_t original_source_time_ms);
uint8_t board_recovery_floor_prime_boot(void);
uint8_t board_recovery_floor_cached_cutoff(uint64_t*cutoff_ms);
/* RAM-only/nonblocking; caller holds actual Stop state/IRQ owner. */
uint8_t board_recovery_manual_time_retired(uint64_t source);
#endif
