#ifndef LIST_SCROLL_H
#define LIST_SCROLL_H

// list_scroll provides the SDL-free math behind autoscrolling (marquee) list
// text. It only computes a horizontal pixel offset from elapsed time; the
// caller measures text, sets clip rects, and blits. Keeping it display-free
// means it can be unit tested with the host compiler (see tests/list_scroll_test.c).

// ScrollMethod selects how over-long text is animated.
// SCROLL_NONE keeps the existing ellipsis truncation.
// SCROLL_WRAP is a continuous looping marquee (text slides left, then repeats
// after a gap). SCROLL_PONG bounces left to reveal the end, then back again.
enum ScrollMethod
{
    SCROLL_NONE = 0,
    SCROLL_WRAP,
    SCROLL_PONG,
};

// ScrollConfig tunes the animation. All pixel values are expected to already be
// display-scaled by the caller (e.g. via SCALE1), so this module stays unit-free.
struct ScrollConfig
{
    // horizontal speed in pixels per second
    int speed_px_per_sec;
    // time to hold before scrolling begins (both methods)
    int start_pause_ms;
    // pong: time to hold at the far (fully-revealed) end
    int end_pause_ms;
    // wrap: gap between the end of the text and its repeated copy
    int gap_px;
    // do not scroll when the overflow is at or below this many pixels
    int overflow_threshold_px;
};

// ScrollMethod_Parse maps a string to a ScrollMethod. "wrap" and "pong" map to
// their methods; "false", the empty string, NULL, and anything unrecognized map
// to SCROLL_NONE so callers degrade gracefully.
enum ScrollMethod ScrollMethod_Parse(const char *s);

// TextScroll_Offset returns how many pixels to shift the text left for the
// current frame. `elapsed_ms` is the time since the animation started (i.e.
// since the row became selected). `text_width` is the full, untruncated text
// width and `viewport_width` is the visible region.
//
// Returns 0 when there is nothing to scroll: method is SCROLL_NONE, the config
// is missing/non-positive, or the overflow (text_width - viewport_width) is at
// or below overflow_threshold_px.
//
// WRAP: the returned offset is in [0, text_width + gap_px); the caller draws the
// text twice, at (x - offset) and (x - offset + text_width + gap_px).
// PONG: the returned offset is in [0, text_width - viewport_width]; the caller
// draws the text once at (x - offset).
int TextScroll_Offset(enum ScrollMethod method, const struct ScrollConfig *cfg,
                      unsigned int elapsed_ms, int text_width, int viewport_width);

#endif // LIST_SCROLL_H
