#ifndef LIST_HINT_H
#define LIST_HINT_H

#include <stdbool.h>

// list_hint provides the SDL-free decision behind what minui-list draws in the
// bottom-left pill slot each frame. Keeping it display-free means it can be unit
// tested with the host compiler (see tests/list_hint_test.c); the caller does the
// actual blitting.

// BottomLeftHint enumerates what belongs in the bottom-left pill slot.
enum BottomLeftHint
{
    // the slot is not drawn here: either the hardware group is hidden, or the
    // enable/action button group already occupies it (drawn elsewhere)
    BOTTOM_LEFT_NONE = 0,
    // the volume/brightness settings hint (GFX_blitHardwareHints)
    BOTTOM_LEFT_SETTING_HINT,
    // the default POWER/MENU + SLEEP pill (GFX_blitButtonGroup)
    BOTTOM_LEFT_SLEEP,
};

// BottomLeftHint_For decides what belongs in the bottom-left pill slot.
//
// show_hardware_group: whether the hardware group is shown at all (the
//   --hide-hardware-group flag turns it off).
// show_setting: the active settings overlay, matching the MinUI SDK convention
//   returned by PWR_update: 0 = none, 1 = brightness, 2 = volume.
// has_left_button_group: whether an enable/action button group already occupies
//   the bottom-left slot (drawn by draw_screen).
// hdmi_connected: whether HDMI is connected, which suppresses the on-screen
//   settings hints.
//
// Returns BOTTOM_LEFT_NONE when the hardware group is hidden or the slot is
// taken by the enable/action group; BOTTOM_LEFT_SETTING_HINT when a
// volume/brightness setting is active and HDMI is not connected; otherwise
// BOTTOM_LEFT_SLEEP.
enum BottomLeftHint BottomLeftHint_For(bool show_hardware_group,
                                       int show_setting,
                                       bool has_left_button_group,
                                       bool hdmi_connected);

#endif // LIST_HINT_H
