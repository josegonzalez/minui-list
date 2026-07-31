// Unit tests for the pure bottom-left hint decision helper. These have no
// SDL/display dependencies, so they run headless with the host compiler via
// `make test`.

#include "list_hint.h"

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

// setting values, matching the MinUI SDK convention
#define SETTING_NONE 0
#define SETTING_BRIGHTNESS 1
#define SETTING_VOLUME 2

// regression: issue 71 - the volume/brightness setting hint must show while the
// setting is being changed (previously it never did because the state was never
// updated). both volume and brightness must yield the hint.
static void test_active_setting_shows_hint(void)
{
    CHECK_EQ(BottomLeftHint_For(true, SETTING_VOLUME, false, false), BOTTOM_LEFT_SETTING_HINT, "volume active -> setting hint");
    CHECK_EQ(BottomLeftHint_For(true, SETTING_BRIGHTNESS, false, false), BOTTOM_LEFT_SETTING_HINT, "brightness active -> setting hint");
}

// no active setting falls back to the default power/sleep hint
static void test_no_setting_shows_sleep(void)
{
    CHECK_EQ(BottomLeftHint_For(true, SETTING_NONE, false, false), BOTTOM_LEFT_SLEEP, "no setting -> sleep");
}

// HDMI suppresses the on-screen settings hints, so the slot falls back to sleep
static void test_hdmi_suppresses_hint(void)
{
    CHECK_EQ(BottomLeftHint_For(true, SETTING_VOLUME, false, true), BOTTOM_LEFT_SLEEP, "volume active + hdmi -> sleep");
    CHECK_EQ(BottomLeftHint_For(true, SETTING_BRIGHTNESS, false, true), BOTTOM_LEFT_SLEEP, "brightness active + hdmi -> sleep");
}

// when the enable/action group occupies the slot, nothing else is drawn there,
// regardless of the setting state
static void test_left_group_takes_precedence(void)
{
    CHECK_EQ(BottomLeftHint_For(true, SETTING_VOLUME, true, false), BOTTOM_LEFT_NONE, "left group + volume -> none");
    CHECK_EQ(BottomLeftHint_For(true, SETTING_NONE, true, false), BOTTOM_LEFT_NONE, "left group + no setting -> none");
    CHECK_EQ(BottomLeftHint_For(true, SETTING_BRIGHTNESS, true, true), BOTTOM_LEFT_NONE, "left group wins over everything");
}

// --hide-hardware-group turns off the whole block, so nothing is drawn here
static void test_hidden_hardware_group(void)
{
    CHECK_EQ(BottomLeftHint_For(false, SETTING_VOLUME, false, false), BOTTOM_LEFT_NONE, "hardware group hidden + volume -> none");
    CHECK_EQ(BottomLeftHint_For(false, SETTING_NONE, false, false), BOTTOM_LEFT_NONE, "hardware group hidden + no setting -> none");
    CHECK_EQ(BottomLeftHint_For(false, SETTING_BRIGHTNESS, true, true), BOTTOM_LEFT_NONE, "hardware group hidden dominates");
}

int main(void)
{
    test_active_setting_shows_hint();
    test_no_setting_shows_sleep();
    test_hdmi_suppresses_hint();
    test_left_group_takes_precedence();
    test_hidden_hardware_group();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
