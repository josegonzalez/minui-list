#include "list_filter.h"

#include <ctype.h>
#include <string.h>

// ci_char returns the lowercased form of a byte for case-insensitive matching.
static int ci_char(char c)
{
    return tolower((unsigned char)c);
}

bool ListFilter_Match(const char *name, const char *filter,
                      size_t *match_start, size_t *match_len)
{
    if (match_start != NULL)
        *match_start = 0;
    if (match_len != NULL)
        *match_len = 0;

    // an empty or missing filter matches everything (with an empty region)
    if (filter == NULL || filter[0] == '\0')
        return true;

    if (name == NULL || name[0] == '\0')
        return false;

    size_t name_len = strlen(name);
    size_t filter_len = strlen(filter);
    if (filter_len > name_len)
        return false;

    // case-insensitive substring search over every candidate offset
    for (size_t i = 0; i + filter_len <= name_len; i++)
    {
        size_t j = 0;
        while (j < filter_len && ci_char(name[i + j]) == ci_char(filter[j]))
            j++;

        if (j == filter_len)
        {
            if (match_start != NULL)
                *match_start = i;
            if (match_len != NULL)
                *match_len = filter_len;
            return true;
        }
    }

    return false;
}

bool ListFilter_ItemVisible(bool is_header, bool display_on_filter,
                            const char *name, const char *filter)
{
    // no active filter: everything is visible
    if (filter == NULL || filter[0] == '\0')
        return true;

    // a pinned row stays visible regardless of the filter
    if (display_on_filter)
        return true;

    // headers are excluded from matching, so they drop out while filtering
    if (is_header)
        return false;

    return ListFilter_Match(name, filter, NULL, NULL);
}
