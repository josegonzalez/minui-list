#include "list_nav.h"

#include <ctype.h>
#include <stddef.h>

// nav_skip reports whether an item should be ignored while navigating.
static bool nav_skip(const struct ListNavItem *item)
{
    return item->is_header || item->unselectable;
}

// nav_letter returns the uppercased first letter of an item's name.
static int nav_letter(const struct ListNavItem *item)
{
    if (item->name == NULL)
        return 0;
    return toupper((unsigned char)item->name[0]);
}

int ListNav_PrevLetterIndex(const struct ListNavItem *items, int item_count, int selected)
{
    if (items == NULL || item_count <= 0 || selected < 0 || selected >= item_count)
        return selected;

    int current_letter = nav_letter(&items[selected]);
    int target = -1;

    // search backwards for the first item with a different letter
    for (int i = selected - 1; i >= 0; i--)
    {
        if (nav_skip(&items[i]))
            continue;

        int letter = nav_letter(&items[i]);
        if (letter != current_letter)
        {
            // found a different letter - walk back to the first item of its group
            target = i;
            for (int j = i - 1; j >= 0; j--)
            {
                if (nav_skip(&items[j]))
                    continue;
                if (nav_letter(&items[j]) == letter)
                    target = j;
                else
                    break;
            }
            break;
        }
    }

    // wrap: jump to the last letter group from the end of the list
    if (target == -1)
    {
        for (int i = item_count - 1; i >= 0; i--)
        {
            if (nav_skip(&items[i]))
                continue;

            int letter = nav_letter(&items[i]);
            if (letter != current_letter)
            {
                target = i;
                for (int j = i - 1; j >= 0; j--)
                {
                    if (nav_skip(&items[j]))
                        continue;
                    if (nav_letter(&items[j]) == letter)
                        target = j;
                    else
                        break;
                }
                break;
            }
        }
    }

    return (target != -1) ? target : selected;
}

int ListNav_NextLetterIndex(const struct ListNavItem *items, int item_count, int selected)
{
    if (items == NULL || item_count <= 0 || selected < 0 || selected >= item_count)
        return selected;

    int current_letter = nav_letter(&items[selected]);
    int target = -1;

    // search forwards for the first item with a different letter; on a sorted
    // list that item is already the first of the next letter group
    for (int i = selected + 1; i < item_count; i++)
    {
        if (nav_skip(&items[i]))
            continue;

        int letter = nav_letter(&items[i]);
        if (letter != current_letter)
        {
            target = i;
            break;
        }
    }

    // wrap: jump to the first letter group from the beginning of the list
    if (target == -1)
    {
        for (int i = 0; i < item_count; i++)
        {
            if (nav_skip(&items[i]))
                continue;

            int letter = nav_letter(&items[i]);
            if (letter != current_letter)
            {
                target = i;
                break;
            }
        }
    }

    return (target != -1) ? target : selected;
}
