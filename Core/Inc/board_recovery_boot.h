#ifndef BOARD_RECOVERY_BOOT_H
#define BOARD_RECOVERY_BOOT_H
#include <stdint.h>
/* A verified monotonic persisted boot generation; zero disables recovery.
 * It is not an authentication secret or a physical board identity. */
uint64_t board_recovery_boot_generation(void);
/* QUERY reads RAM only, including failed/unpublished boot priming.
 * Identity writes require boot exclusivity or a separate explicit control. */
uint64_t board_recovery_boot_query_generation(void);
uint8_t board_recovery_boot_retry_is_safe(void);
/* RECOVER cannot initialize/retry identity; it requires prior verified priming. */
uint64_t board_recovery_boot_current(void);
uint8_t board_recovery_boot_publish_bootstrap(uint64_t generation);
#endif
