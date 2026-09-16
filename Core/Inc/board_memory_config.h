#ifndef BOARD_MEMORY_CONFIG_H
#define BOARD_MEMORY_CONFIG_H
/* Release omits the USB debug pool. Reuse those same reserved bytes for the
 * application tasks and SEDSNet instead of leaving physical RAM idle. Include
 * after the CubeMX pool configuration; debug allocations remain unchanged. */
#if defined(FIRMWARE_USB_DEBUG_ENABLED) && !FIRMWARE_USB_DEBUG_ENABLED
enum { BOARD_RELEASE_APP_POOL_SIZE = TX_APP_MEM_POOL_SIZE + UX_DEVICE_APP_MEM_POOL_SIZE };
#undef TX_APP_MEM_POOL_SIZE
#define TX_APP_MEM_POOL_SIZE BOARD_RELEASE_APP_POOL_SIZE
#endif
#endif
