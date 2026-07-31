#include "list_theme.h"

ListTextRole ListTheme_RowTextRole(int is_selected, int is_disabled, int is_muted)
{
    // a header/unselectable row is muted regardless of selection or disabled state
    if (is_muted)
        return LIST_TEXT_MUTED;

    // the selected row draws on the selection pill
    if (is_selected)
        return is_disabled ? LIST_TEXT_SELECTED_DISABLED : LIST_TEXT_SELECTED;

    // an unselected row uses the normal (or dimmed, when disabled) list text
    return is_disabled ? LIST_TEXT_DISABLED : LIST_TEXT_NORMAL;
}
