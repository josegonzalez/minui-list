#ifndef LIST_FILTER_H
#define LIST_FILTER_H

#include <stdbool.h>
#include <stddef.h>

// list_filter provides the SDL-free matching and visibility rules behind the
// inline keyboard filter. Keeping it display-free means it can be unit tested
// with the host compiler (see tests/list_filter_test.c); the caller applies the
// results to the item list and highlights the matched region.

// ListFilter_Match reports whether `filter` appears in `name` as a
// case-insensitive substring. An empty or NULL filter matches everything.
//
// When the call returns true and `match_start`/`match_len` are non-NULL, they
// receive the byte offset and length of the first matched region within `name`
// (for an empty filter that is offset 0, length 0). On a non-match they are set
// to 0. A NULL/empty `name` only matches an empty filter.
bool ListFilter_Match(const char *name, const char *filter,
                      size_t *match_start, size_t *match_len);

// ListFilter_ItemVisible decides whether an item should be shown while a filter
// is active. The rules, in order:
//   - an empty (or NULL) filter shows everything;
//   - an item with display_on_filter is always shown (the pinned row case);
//   - a header is hidden (headers are excluded from matching);
//   - otherwise the item is shown when its name matches the filter.
bool ListFilter_ItemVisible(bool is_header, bool display_on_filter,
                            const char *name, const char *filter);

#endif // LIST_FILTER_H
