#include "list_scroll.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum ScrollMethod ScrollMethod_Parse(const char *s)
{
    if (s == NULL)
        return SCROLL_NONE;
    if (strcmp(s, "wrap") == 0)
        return SCROLL_WRAP;
    if (strcmp(s, "pong") == 0)
        return SCROLL_PONG;
    // "false", "", and anything unrecognized disable scrolling
    return SCROLL_NONE;
}

// clamp_nonneg returns v, or 0 when v is negative.
static int clamp_nonneg(int v)
{
    return v < 0 ? 0 : v;
}

int TextScroll_Offset(enum ScrollMethod method, const struct ScrollConfig *cfg,
                      unsigned int elapsed_ms, int text_width, int viewport_width)
{
    if (method == SCROLL_NONE || cfg == NULL)
        return 0;
    if (cfg->speed_px_per_sec <= 0 || text_width <= 0 || viewport_width <= 0)
        return 0;

    int overflow = text_width - viewport_width;
    if (overflow <= 0 || overflow <= cfg->overflow_threshold_px)
        return 0;

    uint64_t speed = (uint64_t)cfg->speed_px_per_sec;
    unsigned int start_pause = (unsigned int)clamp_nonneg(cfg->start_pause_ms);

    if (method == SCROLL_WRAP)
    {
        // continuous loop: an initial pause, then slide left forever, wrapping
        // after the text plus a trailing gap. two copies (drawn by the caller)
        // make the wrap seamless.
        int period = text_width + clamp_nonneg(cfg->gap_px);
        if (period <= 0)
            return 0;

        uint64_t traveled = 0;
        if (elapsed_ms > start_pause)
            traveled = speed * (uint64_t)(elapsed_ms - start_pause) / 1000u;

        return (int)(traveled % (uint64_t)period);
    }

    // SCROLL_PONG: pause, ramp to the end, pause, ramp back to the start, repeat.
    unsigned int end_pause = (unsigned int)clamp_nonneg(cfg->end_pause_ms);
    uint64_t travel_ms = (uint64_t)overflow * 1000u / speed;

    uint64_t cycle = (uint64_t)start_pause + travel_ms + (uint64_t)end_pause + travel_ms;
    if (cycle == 0)
        return 0;

    uint64_t t = (uint64_t)elapsed_ms % cycle;
    uint64_t p1_end = start_pause;             // hold at start
    uint64_t p2_end = p1_end + travel_ms;      // ramp toward the end
    uint64_t p3_end = p2_end + end_pause;      // hold at the end

    if (t < p1_end)
        return 0;
    if (t < p2_end)
    {
        uint64_t off = speed * (t - p1_end) / 1000u;
        return off > (uint64_t)overflow ? overflow : (int)off;
    }
    if (t < p3_end)
        return overflow;

    // ramp back toward the start
    uint64_t off = speed * (t - p3_end) / 1000u;
    if (off > (uint64_t)overflow)
        off = (uint64_t)overflow;
    return overflow - (int)off;
}
