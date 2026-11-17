#ifndef BOARD_STAMP_H
#define BOARD_STAMP_H
#include <stdint.h>
/* A SEDSnet network stamp (time-base epoch, Unix ms) packed into one ordered
 * mark: the epoch in the top 16 bits, the milliseconds in the low 48 bits.
 * Epochs only grow and no node steps back inside one, so one sender's stamps
 * order by (epoch, ms) across network time steps in either direction; the
 * numeric order of packed marks is exactly seds_stamp_cmp() order.
 *
 * Every stored ordering mark (replay floor and command cutoff, fill cutoff,
 * fill provenance, QD last source, Launch original) is a packed mark. A record
 * written before epochs existed holds plain milliseconds and reads as epoch 0.
 * 0 and UINT64_MAX stay invalid. Freshness is never judged from a mark and a
 * local clock: use board_recovery_stamp_fresh() (SEDSnet's stamp window). */
#define BOARD_STAMP_MS_BITS 48U
#define BOARD_STAMP_MS_MASK ((UINT64_C(1) << BOARD_STAMP_MS_BITS) - 1U)
static inline uint64_t board_stamp_pack(uint16_t epoch, uint64_t ms)
{
    if (ms == 0U) return 0U;
    if (ms > BOARD_STAMP_MS_MASK) return UINT64_MAX;
    return ((uint64_t)epoch << BOARD_STAMP_MS_BITS) | ms;
}
static inline uint16_t board_stamp_epoch(uint64_t stamp) { return (uint16_t)(stamp >> BOARD_STAMP_MS_BITS); }
static inline uint64_t board_stamp_ms(uint64_t stamp) { return stamp & BOARD_STAMP_MS_MASK; }
/* The packed mark of a received packet view. */
#define board_stamp_of(view) board_stamp_pack((view)->time_epoch, (view)->timestamp)
#endif
