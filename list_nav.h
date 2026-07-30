#ifndef LIST_NAV_H
#define LIST_NAV_H

#include <stdbool.h>

// ListNavItem is a minimal, SDL-free view of a list item used for alphabetic
// (letter-group) navigation. Only the first character of `name` is significant;
// header and unselectable items are skipped while navigating.
struct ListNavItem
{
    const char *name;
    bool is_header;
    bool unselectable;
};

// ListNav_PrevLetterIndex returns the index of the first item of the letter
// group preceding the selected item's group, wrapping to the last letter group
// at the start of the list. Returns `selected` unchanged when there is no other
// letter group (or the input is empty/out of range).
int ListNav_PrevLetterIndex(const struct ListNavItem *items, int item_count, int selected);

// ListNav_NextLetterIndex returns the index of the first item of the letter
// group following the selected item's group, wrapping to the first letter group
// at the end of the list. Returns `selected` unchanged when there is no other
// letter group (or the input is empty/out of range).
int ListNav_NextLetterIndex(const struct ListNavItem *items, int item_count, int selected);

#endif // LIST_NAV_H
