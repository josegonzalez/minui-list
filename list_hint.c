#include "list_hint.h"

enum BottomLeftHint BottomLeftHint_For(bool show_hardware_group,
                                       int show_setting,
                                       bool has_left_button_group,
                                       bool hdmi_connected)
{
    // the hardware group is hidden, or the enable/action group already owns the
    // bottom-left slot, so nothing is drawn here
    if (!show_hardware_group || has_left_button_group)
        return BOTTOM_LEFT_NONE;

    // a volume/brightness setting is being changed - show its hint, unless HDMI
    // is connected (the on-screen settings hints are suppressed on HDMI)
    if (show_setting != 0 && !hdmi_connected)
        return BOTTOM_LEFT_SETTING_HINT;

    // otherwise fall back to the default power/sleep hint
    return BOTTOM_LEFT_SLEEP;
}
