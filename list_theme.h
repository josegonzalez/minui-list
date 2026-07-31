#ifndef LIST_THEME_H
#define LIST_THEME_H

// list_theme provides the SDL-free decision behind which text color a list row
// should use, given its selection and item state. Keeping it display-free means it
// can be unit tested with the host compiler (see tests/list_theme_test.c); the
// caller (minui-list.c) maps the returned role to an actual SDL_Color, choosing the
// NextUI theme color under -DPLATFORM_NEXTUI or the greyscale palette otherwise.

// ListTextRole enumerates the semantic text colors a list row can use. The color
// values live in the caller; this only captures which role applies.
typedef enum
{
    // unselected, enabled, selectable row (the normal list text)
    LIST_TEXT_NORMAL = 0,
    // unselected, disabled row (dimmed)
    LIST_TEXT_DISABLED,
    // header or unselectable row (muted), for any selection state
    LIST_TEXT_MUTED,
    // the currently selected, enabled row (drawn on the selection pill)
    LIST_TEXT_SELECTED,
    // the currently selected, disabled row
    LIST_TEXT_SELECTED_DISABLED,
} ListTextRole;

// ListTheme_RowTextRole decides which text role a row should use.
//
// is_selected: whether this row is the currently selected row.
// is_disabled: whether the item's disabled feature is set.
// is_muted: whether the item is a header or unselectable.
//
// Precedence matches the existing inline logic in minui-list.c: a muted
// (header/unselectable) row is always LIST_TEXT_MUTED regardless of selection or
// disabled state; otherwise a selected row is LIST_TEXT_SELECTED (or
// LIST_TEXT_SELECTED_DISABLED when disabled), and an unselected row is
// LIST_TEXT_NORMAL (or LIST_TEXT_DISABLED when disabled).
ListTextRole ListTheme_RowTextRole(int is_selected, int is_disabled, int is_muted);

#endif // LIST_THEME_H
