#pragma once

#include <stdint.h>

typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef void VOID;
typedef struct { UINT locked; } TX_MUTEX;
typedef struct { ULONG count; } TX_SEMAPHORE;
typedef struct { int unused; } TX_BYTE_POOL;

#define TX_NULL ((void *)0)
#define TX_SUCCESS 0U
#define TX_NO_INSTANCE 1U
#define TX_MUTEX_ERROR 2U
#define TX_QUEUE_ERROR 3U
#define TX_POOL_ERROR 4U
#define TX_NO_WAIT 0U
#define TX_WAIT_FOREVER 0xFFFFFFFFUL
#define TX_INHERIT 1U

UINT tx_mutex_create(TX_MUTEX *mutex, const char *name, UINT inherit);
UINT tx_mutex_get(TX_MUTEX *mutex, ULONG wait_option);
UINT tx_mutex_put(TX_MUTEX *mutex);
UINT tx_semaphore_create(TX_SEMAPHORE *semaphore, const char *name, ULONG count);
UINT tx_semaphore_get(TX_SEMAPHORE *semaphore, ULONG wait_option);
UINT tx_semaphore_put(TX_SEMAPHORE *semaphore);
