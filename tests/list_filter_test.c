// Unit tests for the pure filter matching + visibility helpers. These have no
// SDL/display dependencies, so they run headless with the host compiler via
// `make test`.

#include "list_filter.h"

#include <stdio.h>

static int checks = 0;
static int failures = 0;

#define CHECK_EQ(actual, expected, msg)                                         \
    do                                                                          \
    {                                                                           \
        checks++;                                                               \
        long _a = (long)(actual);                                               \
        long _e = (long)(expected);                                             \
        if (_a != _e)                                                           \
        {                                                                       \
            failures++;                                                         \
            fprintf(stderr, "FAIL: %s (expected %ld, got %ld)\n", (msg), _e, _a); \
        }                                                                       \
    } while (0)

// CHECK_MATCH asserts the boolean result plus the reported match region in a
// single readable statement.
#define CHECK_MATCH(name, filter, exp_ok, exp_start, exp_len, msg)              \
    do                                                                          \
    {                                                                           \
        size_t _s = 999, _l = 999;                                             \
        bool _ok = ListFilter_Match((name), (filter), &_s, &_l);              \
        CHECK_EQ(_ok, (exp_ok), msg " (result)");                             \
        CHECK_EQ(_s, (exp_start), msg " (start)");                            \
        CHECK_EQ(_l, (exp_len), msg " (len)");                                \
    } while (0)

static void test_match_basic(void)
{
    CHECK_MATCH("Super Mario World", "mario", true, 6, 5, "match: substring mid");
    CHECK_MATCH("Super Mario World", "Super", true, 0, 5, "match: substring start");
    CHECK_MATCH("Super Mario World", "World", true, 12, 5, "match: substring end");
    CHECK_MATCH("Super Mario World", "xyz", false, 0, 0, "match: no substring");
}

static void test_match_case_insensitive(void)
{
    CHECK_MATCH("Zelda", "ZELDA", true, 0, 5, "match: upper filter");
    CHECK_MATCH("ZELDA", "zelda", true, 0, 5, "match: lower filter");
    CHECK_MATCH("MiXeD cAsE", "mixed", true, 0, 5, "match: mixed case");
}

static void test_match_empty_and_null(void)
{
    // empty/NULL filter matches everything with an empty region
    CHECK_MATCH("anything", "", true, 0, 0, "match: empty filter");
    CHECK_MATCH("anything", NULL, true, 0, 0, "match: NULL filter");
    CHECK_MATCH("", "", true, 0, 0, "match: empty name empty filter");
    // empty/NULL name only matches an empty filter
    CHECK_MATCH("", "a", false, 0, 0, "match: empty name non-empty filter");
    CHECK_MATCH(NULL, "a", false, 0, 0, "match: NULL name non-empty filter");
    CHECK_MATCH(NULL, NULL, true, 0, 0, "match: NULL name NULL filter");
}

static void test_match_edge(void)
{
    // filter longer than the name never matches
    CHECK_MATCH("ab", "abc", false, 0, 0, "match: filter longer than name");
    // first occurrence wins
    CHECK_MATCH("abab", "ab", true, 0, 2, "match: first occurrence");
    // spaces are literal (full keyboard can type them)
    CHECK_MATCH("final fantasy", "final fantasy", true, 0, 13, "match: with space");
    CHECK_MATCH("finalfantasy", "final fantasy", false, 0, 0, "match: space is literal");
    // NULL out-params are allowed
    checks++;
    if (ListFilter_Match("Sonic", "onic", NULL, NULL) != true)
    {
        failures++;
        fprintf(stderr, "FAIL: match: NULL out-params\n");
    }
}

static void test_item_visible(void)
{
    // empty filter: everything visible regardless of the other flags
    CHECK_EQ(ListFilter_ItemVisible(false, false, "Apple", ""), true, "visible: empty filter item");
    CHECK_EQ(ListFilter_ItemVisible(true, false, "Fruits", ""), true, "visible: empty filter header");
    CHECK_EQ(ListFilter_ItemVisible(false, false, "Apple", NULL), true, "visible: NULL filter");

    // active filter: match / non-match on a normal item
    CHECK_EQ(ListFilter_ItemVisible(false, false, "Apple", "app"), true, "visible: match");
    CHECK_EQ(ListFilter_ItemVisible(false, false, "Apple", "zzz"), false, "visible: no match");

    // headers drop out while filtering, unless pinned
    CHECK_EQ(ListFilter_ItemVisible(true, false, "Fruits", "app"), false, "visible: header hidden");
    CHECK_EQ(ListFilter_ItemVisible(true, true, "Fruits", "zzz"), true, "visible: pinned header shown");

    // pinned rows always show, even when they do not match
    CHECK_EQ(ListFilter_ItemVisible(false, true, "Add All", "zzz"), true, "visible: pinned item shown");
    CHECK_EQ(ListFilter_ItemVisible(false, true, "Add All", "add"), true, "visible: pinned item matching");
}

int main(void)
{
    test_match_basic();
    test_match_case_insensitive();
    test_match_empty_and_null();
    test_match_edge();
    test_item_visible();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
