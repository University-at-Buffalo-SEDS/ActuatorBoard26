#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef struct { uint32_t output; uint32_t input; } GPIO_TypeDef;

typedef struct { int unused; } FDCAN_HandleTypeDef;
typedef struct { uint32_t BusOff; } FDCAN_ProtocolStatusTypeDef;
typedef struct {
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t RxFrameType;
    uint32_t DataLength;
} FDCAN_RxHeaderTypeDef;
typedef struct {
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t TxFrameType;
    uint32_t DataLength;
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t TxEventFifoControl;
    uint32_t MessageMarker;
} FDCAN_TxHeaderTypeDef;

#define FDCAN_DLC_BYTES_0 0U
#define FDCAN_DLC_BYTES_1 1U
#define FDCAN_DLC_BYTES_2 2U
#define FDCAN_DLC_BYTES_3 3U
#define FDCAN_DLC_BYTES_4 4U
#define FDCAN_DLC_BYTES_5 5U
#define FDCAN_DLC_BYTES_6 6U
#define FDCAN_DLC_BYTES_7 7U
#define FDCAN_DLC_BYTES_8 8U
#define FDCAN_DLC_BYTES_12 9U
#define FDCAN_DLC_BYTES_16 10U
#define FDCAN_DLC_BYTES_20 11U
#define FDCAN_DLC_BYTES_24 12U
#define FDCAN_DLC_BYTES_32 13U
#define FDCAN_DLC_BYTES_48 14U
#define FDCAN_DLC_BYTES_64 15U
#define FDCAN_STANDARD_ID 0U
#define FDCAN_DATA_FRAME 0U
#define FDCAN_ESI_ACTIVE 0U
#define FDCAN_BRS_OFF 0U
#define FDCAN_FD_CAN 1U
#define FDCAN_CLASSIC_CAN 0U
#define FDCAN_NO_TX_EVENTS 0U
#define FDCAN_TX_BUFFER0 0x1U
#define FDCAN_TX_BUFFER1 0x2U
#define FDCAN_TX_BUFFER2 0x4U
#define FDCAN_ACCEPT_IN_RX_FIFO1 1U
#define FDCAN_ACCEPT_IN_RX_FIFO0 0U
#define FDCAN_REJECT_REMOTE 0U
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE 1U
#define FDCAN_IT_RX_FIFO1_NEW_MESSAGE 2U
#define FDCAN_RX_FIFO0 0U
#define FDCAN_RX_FIFO1 1U

HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h, uint32_t a,
                                                uint32_t b, uint32_t c, uint32_t d);
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *h, uint32_t flags,
                                                 uint32_t buffers);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef *h,
                                              FDCAN_ProtocolStatusTypeDef *status);
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h,
                                           uint32_t buffers);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h,
                                                FDCAN_TxHeaderTypeDef *header,
                                                uint8_t *data);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *h, uint32_t fifo);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t fifo,
                                         FDCAN_RxHeaderTypeDef *header, uint8_t *data);
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
