#ifndef BOARD_REFUSAL_H
#define BOARD_REFUSAL_H
/* Why the board last refused an operator command, so the station can tell the
 * operator instead of the press doing nothing. Diagnostics only: no admission
 * decision reads it.
 *
 * BOARD_HEALTH carries it (board_health.c): word 16 BOARD_REFUSAL_TAG, word 17
 * sequence<<16 | command<<8 | reason, word 18 ms since the board first
 * reported that refusal (0 while none). The sequence counts refusals (1..65535, wraps to 1);
 * 0 means none since boot. command is the ACTUATOR_COMMAND byte, or
 * BOARD_REFUSAL_DRIVE_SET | requested mode for a fill-drive change.
 * Automatic (GS sequence) commands are not recorded.
 *
 * Header-only on purpose: every module shares one weak state word, so a host
 * harness compiling a single module needs no extra source. */
#include <stdatomic.h>
#include <stdint.h>

enum {
 BOARD_REFUSAL_NONE=0,
 BOARD_REFUSAL_STALE=1,               /* stamp outside the freshness window */
 BOARD_REFUSAL_BELOW_FLOOR=2,         /* stamp at/before the recovery floor */
 BOARD_REFUSAL_WRONG_DRIVE=3,         /* selected fill drive cannot do it */
 BOARD_REFUSAL_INTERLOCK=4,           /* flight-state/launch interlock */
 BOARD_REFUSAL_BUSY=5,                /* motion or drive change in progress */
 BOARD_REFUSAL_NOT_COMMISSIONED=6,    /* critical commissioning pending */
 BOARD_REFUSAL_RECOVERY_PENDING=7,    /* board state recovery in progress */
 BOARD_REFUSAL_ABORT=8,               /* Abort latched */
 BOARD_REFUSAL_STORE_NOT_READY=9,     /* persistent store not ready */
 BOARD_REFUSAL_DRIVE_NOT_LOADED=10,   /* fill-drive setting not loaded */
 BOARD_REFUSAL_DRIVE_FAULT=11,        /* fill drive faulted */
 BOARD_REFUSAL_OLD_EPOCH=12,          /* names an older boot/Stop epoch */
 BOARD_REFUSAL_BEFORE_DRIVE_CHANGE=13,/* stamped before the last drive change */
 BOARD_REFUSAL_OWNER_FAULT=14,        /* command owner fault latched */
};
#define BOARD_REFUSAL_TAG UINT32_C(0x31524241) /* "ABR1" little-endian */
#define BOARD_REFUSAL_DRIVE_SET UINT8_C(0x40)
#define BOARD_REFUSAL_AUTOMATIC UINT8_C(0x80)  /* THREAD_COMM_AUTOMATIC_TAG */
/* Never reported: a caller placeholder for "ask the command owner why". */
#define BOARD_REFUSAL_ASK_OWNER UINT8_C(0xFF)

extern _Atomic uint32_t board_refusal_word;
__attribute__((weak)) _Atomic uint32_t board_refusal_word;

static inline void board_refusal_note(uint8_t command,uint8_t reason)
{
 if(reason==BOARD_REFUSAL_NONE||(command&BOARD_REFUSAL_AUTOMATIC)!=0U)return;
 uint32_t old=atomic_load_explicit(&board_refusal_word,memory_order_relaxed),next;
 do{
  uint32_t seq=((old>>16)+1U)&0xFFFFU;if(!seq)seq=1U;
  next=(seq<<16)|((uint32_t)command<<8)|reason;
 }while(!atomic_compare_exchange_weak_explicit(&board_refusal_word,&old,next,memory_order_release,memory_order_relaxed));
}
/* A refused one-byte command packet (SedsPacketView *p); anything else is
 * passed as automatic and not recorded. */
#define BOARD_REFUSAL_NOTE_PACKET(p,reason) board_refusal_note( \
 ((p)&&(p)->payload&&(p)->payload_len==1U)?(p)->payload[0]:BOARD_REFUSAL_AUTOMATIC,(reason))
static inline uint32_t board_refusal_current(void)
{
 return atomic_load_explicit(&board_refusal_word,memory_order_acquire);
}
#endif
