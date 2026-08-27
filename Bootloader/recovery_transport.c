#include "board_config.h"
#include "launchcore/delta.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/platform.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include "stm32g4xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define RECOVERY_MAGIC 0x5052434Cu /* "LCRP" on the wire */
#define RECOVERY_VERSION 1u
#define RECOVERY_MAX_CHUNK 256u
#define RECOVERY_BAUD 115200u
#define RECOVERY_CLOCK_HZ 16000000u

enum {
    RECOVERY_OP_INFO = 1u,
    RECOVERY_OP_BEGIN = 2u,
    RECOVERY_OP_DATA = 3u,
    RECOVERY_OP_FINISH = 4u,
    RECOVERY_OP_ABORT = 5u,
    RECOVERY_OP_RESET = 6u,
    RECOVERY_RESPONSE = 0x80u,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t opcode;
    uint16_t length;
    uint32_t offset;
    uint32_t crc32;
} recovery_frame_t;

static uint32_t expected_offset;
static uint32_t declared_size;
static bool install_active;

static void uart_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_UART4EN;
    (void)RCC->APB1ENR1;
    GPIOC->MODER = (GPIOC->MODER & ~((3u << (10u * 2u)) | (3u << (11u * 2u)))) |
                   (2u << (10u * 2u)) | (2u << (11u * 2u));
    GPIOC->AFR[1] = (GPIOC->AFR[1] & ~((0xFu << 8u) | (0xFu << 12u))) |
                    (5u << 8u) | (5u << 12u);
    UART4->CR1 = 0u;
    UART4->BRR = (RECOVERY_CLOCK_HZ + RECOVERY_BAUD / 2u) / RECOVERY_BAUD;
    UART4->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static uint8_t uart_read(void)
{
    while ((UART4->ISR & USART_ISR_RXNE_RXFNE) == 0u) platform_feed_watchdog();
    return (uint8_t)UART4->RDR;
}

static void uart_write(const void *data, uint32_t length)
{
    const uint8_t *bytes = data;
    while (length-- != 0u) {
        while ((UART4->ISR & USART_ISR_TXE_TXFNF) == 0u) platform_feed_watchdog();
        UART4->TDR = *bytes++;
    }
}

static void read_exact(void *data, uint32_t length)
{
    uint8_t *bytes = data;
    while (length-- != 0u) *bytes++ = uart_read();
}

static void send_response(uint8_t opcode, launchcore_recovery_status_t status)
{
    uint32_t payload[2] = {expected_offset, ACTUATOR_SLOT_A_SIZE};
    recovery_frame_t frame = {
        .magic = RECOVERY_MAGIC,
        .version = RECOVERY_VERSION,
        .opcode = (uint8_t)(opcode | RECOVERY_RESPONSE),
        .length = sizeof(payload),
        .offset = (uint32_t)status,
    };
    frame.crc32 = launchcore_crc32(&frame, offsetof(recovery_frame_t, crc32));
    frame.crc32 = launchcore_crc32_update(frame.crc32, payload, sizeof(payload));
    uart_write(&frame, sizeof(frame));
    uart_write(payload, sizeof(payload));
    while ((UART4->ISR & USART_ISR_TC) == 0u) platform_feed_watchdog();
}

static launchcore_recovery_status_t clear_recovery_request(void)
{
    launchcore_metadata_t metadata;
    (void)launchcore_metadata_read_latest(&metadata);
    metadata.flags &= ~LAUNCHCORE_METADATA_FLAG_RECOVERY_REQUESTED;
    return launchcore_metadata_commit(&metadata) == LAUNCHCORE_METADATA_OK ?
           LAUNCHCORE_RECOVERY_OK : LAUNCHCORE_RECOVERY_ERR_STORAGE;
}

launchcore_recovery_status_t launchcore_recovery_handle_command(
    const char *command, char *response, size_t response_len)
{
    static const char hello[] = "OK LAUNCHCORE UART";
    if (command == NULL || response == NULL || response_len == 0u)
        return LAUNCHCORE_RECOVERY_ERR_ARG;
    if (strncmp(command, "HELLO", 5u) != 0)
        return LAUNCHCORE_RECOVERY_ERR_COMMAND;
    size_t count = sizeof(hello);
    if (count > response_len) count = response_len;
    memcpy(response, hello, count);
    response[response_len - 1u] = '\0';
    return LAUNCHCORE_RECOVERY_OK;
}

void launchcore_recovery_run(void)
{
    uint8_t payload[RECOVERY_MAX_CHUNK];
    uart_init();
    for (;;) {
        uint32_t magic = 0u;
        do {
            magic = (magic >> 8u) | ((uint32_t)uart_read() << 24u);
        } while (magic != RECOVERY_MAGIC);

        recovery_frame_t frame = {.magic = magic};
        read_exact(&frame.version, sizeof(frame) - sizeof(frame.magic));
        if (frame.length > sizeof(payload)) continue;
        read_exact(payload, frame.length);
        uint32_t expected_crc = launchcore_crc32(&frame, offsetof(recovery_frame_t, crc32));
        expected_crc = launchcore_crc32_update(expected_crc, payload, frame.length);
        if (frame.version != RECOVERY_VERSION || frame.crc32 != expected_crc) continue;

        launchcore_recovery_status_t status = LAUNCHCORE_RECOVERY_ERR_COMMAND;
        bool reset = false;
        switch (frame.opcode) {
        case RECOVERY_OP_INFO:
            status = frame.length == 0u ? LAUNCHCORE_RECOVERY_OK :
                                         LAUNCHCORE_RECOVERY_ERR_ARG;
            break;
        case RECOVERY_OP_BEGIN:
            if (frame.length != 0u) {
                status = LAUNCHCORE_RECOVERY_ERR_ARG;
                break;
            }
            status = launchcore_recovery_install_begin(frame.offset);
            if (status == LAUNCHCORE_RECOVERY_OK) {
                expected_offset = 0u;
                declared_size = frame.offset;
                install_active = true;
            }
            break;
        case RECOVERY_OP_DATA:
            if (!install_active || frame.offset != expected_offset || frame.length == 0u ||
                ((frame.offset & 7u) != 0u) ||
                frame.offset > declared_size || frame.length > declared_size - frame.offset ||
                (frame.offset + frame.length < declared_size && (frame.length & 7u) != 0u)) {
                status = LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
                break;
            }
            status = launchcore_recovery_install_write(payload, frame.length);
            if (status == LAUNCHCORE_RECOVERY_OK) expected_offset += frame.length;
            break;
        case RECOVERY_OP_FINISH:
            status = frame.length == 0u ? launchcore_recovery_install_finish() :
                                         LAUNCHCORE_RECOVERY_ERR_ARG;
            if (status == LAUNCHCORE_RECOVERY_OK) {
                install_active = false;
                status = clear_recovery_request();
                reset = status == LAUNCHCORE_RECOVERY_OK;
            }
            break;
        case RECOVERY_OP_ABORT:
            status = frame.length == 0u && install_active ?
                     launchcore_recovery_install_abort() : LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
            if (status == LAUNCHCORE_RECOVERY_OK) install_active = false;
            break;
        case RECOVERY_OP_RESET:
            status = frame.length == 0u ? LAUNCHCORE_RECOVERY_OK :
                                         LAUNCHCORE_RECOVERY_ERR_ARG;
            reset = status == LAUNCHCORE_RECOVERY_OK;
            break;
        default:
            break;
        }
        send_response(frame.opcode, status);
        if (reset) platform_system_reset();
    }
}
