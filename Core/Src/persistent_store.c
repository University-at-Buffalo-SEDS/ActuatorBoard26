#include "persistent_store.h"

#include "launchcore/storage.h"
#include "tx_api.h"

#include <stdbool.h>

extern const launchcore_storage_driver_t launchcore_board_storage_driver;

static TX_MUTEX g_persistent_store_mutex;
static UINT g_persistent_store_mutex_ready;

static void ensure_runtime_mutex(void)
{
    if (g_persistent_store_mutex_ready == 0U && tx_thread_identify() != TX_NULL)
    {
        if (tx_mutex_create(&g_persistent_store_mutex, "persist", TX_INHERIT) == TX_SUCCESS)
            g_persistent_store_mutex_ready = 1U;
    }
}

static bool lock_store(void)
{
    ensure_runtime_mutex();
    if (g_persistent_store_mutex_ready == 0U || tx_thread_identify() == TX_NULL)
        return false;
    return tx_mutex_get(&g_persistent_store_mutex, TX_WAIT_FOREVER) == TX_SUCCESS;
}

static void unlock_store(bool locked)
{
    if (locked) (void)tx_mutex_put(&g_persistent_store_mutex);
}

launchcore_persist_status_t persistent_store_init(void)
{
    launchcore_storage_set_driver(&launchcore_board_storage_driver);
    const bool locked = lock_store();
    const launchcore_persist_status_t status = launchcore_persist_init();
    unlock_store(locked);
    return status;
}

launchcore_persist_status_t persistent_store_get(uint32_t key, void *value,
                                                  size_t *value_size)
{
    const bool locked = lock_store();
    const launchcore_persist_status_t status =
        launchcore_persist_get(key, value, value_size);
    unlock_store(locked);
    return status;
}

launchcore_persist_status_t persistent_store_set(uint32_t key,
                                                  const void *value,
                                                  size_t value_size)
{
    const bool locked = lock_store();
    const launchcore_persist_status_t status =
        launchcore_persist_set(key, value, value_size);
    unlock_store(locked);
    return status;
}
