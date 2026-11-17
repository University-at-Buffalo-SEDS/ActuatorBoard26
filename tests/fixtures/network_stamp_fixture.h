/* SEDSnet 6 stamp-window fixture for host harnesses that stub the SDK clock.
 * Include it after the harness's network clock variable (STAMP_FIXTURE_NOW,
 * default `now`, stepped by stamp_fixture_step; STAMP_FIXTURE_READ, default
 * the same, is the clock as read, 0 meaning no network time). It models the public contract of
 * seds_router_network_stamp_age_ms/_fresh: this node's epoch, the last four
 * steps it remembers (an older epoch's age is translated across them), and a
 * newer epoch it has heard of but not adopted yet (age = how long it has known
 * of it; a newer epoch never heard of is new: 0). */
#ifndef NETWORK_STAMP_FIXTURE_H
#define NETWORK_STAMP_FIXTURE_H
#include <stdbool.h>
#include <stdint.h>
#ifndef STAMP_FIXTURE_NOW
#define STAMP_FIXTURE_NOW now
#endif
#ifndef STAMP_FIXTURE_READ
#define STAMP_FIXTURE_READ STAMP_FIXTURE_NOW
#endif
#ifndef STAMP_FIXTURE_ASSERT_UNLOCKED
#define STAMP_FIXTURE_ASSERT_UNLOCKED() ((void)0)
#endif
static uint16_t stamp_fixture_epoch;
static uint16_t stamp_fixture_newer;
static uint64_t stamp_fixture_newer_known_ms;
static unsigned stamp_fixture_remembered;
static int64_t stamp_fixture_delta[4]; /* [0]: step into the current epoch */
/* This node steps its network clock to `to` and starts the next epoch. */
static __attribute__((unused)) void stamp_fixture_step(uint64_t to)
{
    for (unsigned i = 3U; i > 0U; --i) stamp_fixture_delta[i] = stamp_fixture_delta[i - 1U];
    stamp_fixture_delta[0] = (int64_t)to - (int64_t)STAMP_FIXTURE_NOW;
    STAMP_FIXTURE_NOW = to;
    ++stamp_fixture_epoch;
    if (stamp_fixture_remembered < 4U) ++stamp_fixture_remembered;
    if (stamp_fixture_newer <= stamp_fixture_epoch) stamp_fixture_newer = 0U;
}
/* This node has known of a newer epoch for known_ms without adopting it. */
static __attribute__((unused)) void stamp_fixture_hear_newer(uint16_t epoch, uint64_t known_ms)
{
    stamp_fixture_newer = epoch;
    stamp_fixture_newer_known_ms = known_ms;
}
static uint8_t stamp_fixture_age(uint16_t epoch, uint64_t ms, int64_t *age)
{
    const uint64_t clock = STAMP_FIXTURE_READ;
    if (!clock) return 3U;
    if (epoch > stamp_fixture_epoch) {
        *age = epoch > stamp_fixture_newer ? 0 : (int64_t)stamp_fixture_newer_known_ms;
        return 1U;
    }
    const unsigned back = (unsigned)(stamp_fixture_epoch - epoch);
    if (back > stamp_fixture_remembered) return 2U;
    int64_t translated = (int64_t)ms;
    for (unsigned i = 0U; i < back; ++i) translated += stamp_fixture_delta[i];
    *age = (int64_t)clock - translated;
    return 0U;
}
SedsResult seds_router_network_stamp_age_ms(SedsRouter *r, uint16_t epoch, uint64_t ms, uint8_t *kind, int64_t *age)
{
    STAMP_FIXTURE_ASSERT_UNLOCKED();
    (void)r;
    int64_t value = 0;
    *kind = stamp_fixture_age(epoch, ms, &value);
    *age = value;
    return SEDS_OK;
}
SedsResult seds_router_network_stamp_fresh(SedsRouter *r, uint16_t epoch, uint64_t ms, uint64_t max_age, uint64_t max_ahead, bool *fresh)
{
    STAMP_FIXTURE_ASSERT_UNLOCKED();
    (void)r;
    int64_t age = 0;
    const uint8_t kind = stamp_fixture_age(epoch, ms, &age);
    *fresh = (kind == 0U && age >= -(int64_t)max_ahead && age <= (int64_t)max_age) ||
             (kind == 1U && (uint64_t)age <= max_age);
    return SEDS_OK;
}
#endif
