#include "can_bus.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    FDCAN_TxHeaderTypeDef header;
    uint8_t data[64];
} captured_frame_t;

static captured_frame_t frames[64];
static size_t frame_count;
static uint32_t tick_ms;
static uint8_t received[2048];
static size_t received_len;
static size_t callback_count;
static uint32_t mock_bus_off;
static uint32_t stop_count;
static uint32_t start_count;
static uint32_t abort_count;
static uint32_t fifo_full_reads_remaining;
static uint32_t fifo_level_reads;

static size_t dlc_len(uint32_t dlc)
{
    static const uint8_t lengths[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                        8, 12, 16, 20, 24, 32, 48, 64};
    return lengths[dlc & 0xFU];
}

HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h)
{ (void)h; stop_count++; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h)
{ (void)h; start_count++; mock_bus_off = 0U; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef *h,
                                              FDCAN_ProtocolStatusTypeDef *status)
{ (void)h; status->BusOff = mock_bus_off; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h, uint32_t buffers)
{ (void)h; assert(buffers == 0x7U); abort_count++; return HAL_OK; }
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h)
{
    (void)h;
    fifo_level_reads++;
    tick_ms++;
    if (fifo_full_reads_remaining > 0U) {
        fifo_full_reads_remaining--;
        return 0U;
    }
    return 3U;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h, uint32_t a,
                                                uint32_t b, uint32_t c, uint32_t d)
{
    (void)h; (void)a; (void)b; (void)c; (void)d; return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *h, uint32_t flags,
                                                 uint32_t buffers)
{
    (void)h; (void)flags; (void)buffers; return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h,
                                                FDCAN_TxHeaderTypeDef *header,
                                                uint8_t *data)
{
    (void)h;
    assert(frame_count < sizeof(frames) / sizeof(frames[0]));
    frames[frame_count].header = *header;
    memcpy(frames[frame_count].data, data, dlc_len(header->DataLength));
    frame_count++;
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *h, uint32_t fifo)
{ (void)h; (void)fifo; return 0U; }
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t fifo,
                                         FDCAN_RxHeaderTypeDef *header, uint8_t *data)
{ (void)h; (void)fifo; (void)header; (void)data; return HAL_ERROR; }
uint32_t HAL_GetTick(void) { return tick_ms; }

static void receive(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    assert(len <= sizeof(received));
    memcpy(received, data, len);
    received_len = len;
    callback_count++;
}

int main(void)
{
    FDCAN_HandleTypeDef fdcan = {0};
    can_bus_init(&fdcan);
    assert(can_bus_subscribe_rx(receive, NULL) == HAL_OK);
    assert(can_bus_subscribe_rx(receive, NULL) == HAL_ERROR);

    const uint8_t raw[] = {1U, 2U, 3U};

    /* A board started on an absent bus must recover when a later send occurs. */
    mock_bus_off = 1U;
    const uint32_t starts_before_recovery = start_count;
    assert(can_bus_send_bytes(raw, sizeof(raw), 3U) == HAL_OK);
    assert(stop_count == 2U);
    assert(start_count == starts_before_recovery + 1U);
    assert(abort_count == 1U);

    /* A temporarily full three-slot FIFO must drain rather than dropping the
     * next fragment of a larger SEDSNet packet. */
    fifo_full_reads_remaining = 2U;
    const uint32_t reads_before_wait = fifo_level_reads;
    assert(can_bus_send_bytes(raw, sizeof(raw), 3U) == HAL_OK);
    assert(fifo_level_reads >= reads_before_wait + 3U);

    can_bus_test_inject(3U, raw, sizeof(raw));
    can_bus_process_rx();
    assert(callback_count == 1U);
    assert(received_len == sizeof(raw));
    assert(memcmp(received, raw, sizeof(raw)) == 0);

    uint8_t large[600];
    for (size_t i = 0; i < sizeof(large); ++i) large[i] = (uint8_t)i;
    frame_count = 0U;
    assert(can_bus_send_large(large, sizeof(large), 3U) == HAL_OK);
    assert(frame_count > 1U);
    for (size_t i = frame_count; i > 0U; --i)
    {
        const captured_frame_t *frame = &frames[i - 1U];
        can_bus_test_inject(frame->header.Identifier, frame->data,
                            dlc_len(frame->header.DataLength));
    }
    can_bus_process_rx();
    assert(callback_count == 2U);
    assert(received_len == sizeof(large));
    assert(memcmp(received, large, sizeof(large)) == 0);

    const size_t before_flood = callback_count;
    for (size_t i = 0; i < 200U; ++i) can_bus_test_inject(3U, raw, sizeof(raw));
    assert(can_bus_rx_dropped_frames() > 0U);
    can_bus_process_rx();
    assert(callback_count > before_flood);
    assert(callback_count - before_flood < 200U);

    tick_ms += 1000U;
    can_bus_process_rx();
    assert(can_bus_unsubscribe_rx(receive, NULL) == HAL_OK);
    assert(can_bus_unsubscribe_rx(receive, NULL) == HAL_ERROR);
    return 0;
}
