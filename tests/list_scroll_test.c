// Unit tests for the pure text-autoscroll (marquee) offset helpers. These have
// no SDL/display dependencies, so they run headless with the host compiler via
// `make test`.

#include "list_scroll.h"

#include <stdio.h>

static int checks = 0;
static int failures = 0;

#define CHECK_EQ(actual, expected, msg)                                         \
    do                                                                          \
    {                                                                           \
        checks++;                                                               \
        int _a = (actual);                                                      \
        int _e = (expected);                                                    \
        if (_a != _e)                                                           \
        {                                                                       \
            failures++;                                                         \
            fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", (msg), _e, _a); \
        }                                                                       \
    } while (0)

// 100 px/sec keeps the arithmetic easy: 1px per 10ms, 100px per 1000ms.
static const struct ScrollConfig CFG = {
    .speed_px_per_sec = 100,
    .start_pause_ms = 1000,
    .end_pause_ms = 1000,
    .gap_px = 40,
    .overflow_threshold_px = 4,
};

static void test_parse(void)
{
    CHECK_EQ(ScrollMethod_Parse("wrap"), SCROLL_WRAP, "parse: wrap");
    CHECK_EQ(ScrollMethod_Parse("pong"), SCROLL_PONG, "parse: pong");
    CHECK_EQ(ScrollMethod_Parse("false"), SCROLL_NONE, "parse: false");
    CHECK_EQ(ScrollMethod_Parse(""), SCROLL_NONE, "parse: empty");
    CHECK_EQ(ScrollMethod_Parse(NULL), SCROLL_NONE, "parse: NULL");
    CHECK_EQ(ScrollMethod_Parse("bounce"), SCROLL_NONE, "parse: unknown");
    CHECK_EQ(ScrollMethod_Parse("WRAP"), SCROLL_NONE, "parse: case-sensitive");
}

static void test_guards(void)
{
    // NONE never scrolls
    CHECK_EQ(TextScroll_Offset(SCROLL_NONE, &CFG, 5000, 200, 100), 0, "none: no scroll");
    // NULL config
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, NULL, 5000, 200, 100), 0, "wrap: NULL cfg");
    // non-positive dimensions
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 5000, 0, 100), 0, "wrap: zero text width");
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 5000, 200, 0), 0, "wrap: zero viewport");
    // text fits (no overflow)
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 5000, 100, 100), 0, "wrap: viewport == text");
    // overflow at/below threshold does not scroll (overflow 2 and 4, threshold 4)
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 5000, 102, 100), 0, "wrap: below threshold");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 5000, 104, 100), 0, "pong: at threshold");
    // zero speed
    struct ScrollConfig slow = CFG;
    slow.speed_px_per_sec = 0;
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &slow, 5000, 200, 100), 0, "wrap: zero speed");
}

static void test_wrap(void)
{
    // text 200, viewport 100 -> overflow 100, period = 200 + gap(40) = 240
    // start pause holds at 0
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 0, 200, 100), 0, "wrap: t=0 paused");
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 1000, 200, 100), 0, "wrap: t=start_pause boundary paused");
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 1010, 200, 100), 1, "wrap: 10ms after pause -> 1px");
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 2000, 200, 100), 100, "wrap: 1000ms of travel -> 100px");
    // full period travelled wraps back to 0 (1000 pause + 2400ms travel = 240px)
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 3400, 200, 100), 0, "wrap: one period wraps to 0");
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 3410, 200, 100), 1, "wrap: just past wrap -> 1px");
    // huge elapsed stays bounded and correct (exercises 64-bit math, no overflow)
    CHECK_EQ(TextScroll_Offset(SCROLL_WRAP, &CFG, 4000000000u, 200, 100), 60, "wrap: large elapsed bounded");
}

static void test_pong(void)
{
    // text 200, viewport 100 -> overflow 100, travel_ms = 1000, cycle = 4000
    // phases: [0,1000) hold@0, [1000,2000) ramp up, [2000,3000) hold@100, [3000,4000) ramp down
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 0, 200, 100), 0, "pong: t=0 hold start");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 500, 200, 100), 0, "pong: mid start pause");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 1000, 200, 100), 0, "pong: ramp-up start");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 1500, 200, 100), 50, "pong: mid ramp-up");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 1999, 200, 100), 99, "pong: end of ramp-up clamps under overflow");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 2000, 200, 100), 100, "pong: hold at end");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 2500, 200, 100), 100, "pong: mid end pause");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 3000, 200, 100), 100, "pong: ramp-down start");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 3500, 200, 100), 50, "pong: mid ramp-down (symmetric)");
    // cycle repeats
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 4000, 200, 100), 0, "pong: cycle wraps to 0");
    CHECK_EQ(TextScroll_Offset(SCROLL_PONG, &CFG, 5500, 200, 100), 50, "pong: second cycle mid ramp-up");
    // never exceeds overflow across a full sweep
    for (unsigned int t = 0; t < 4000; t += 7)
    {
        int off = TextScroll_Offset(SCROLL_PONG, &CFG, t, 200, 100);
        if (off < 0 || off > 100)
        {
            failures++;
            fprintf(stderr, "FAIL: pong offset out of range at t=%u (got %d)\n", t, off);
        }
        checks++;
    }
}

int main(void)
{
    test_parse();
    test_guards();
    test_wrap();
    test_pong();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
