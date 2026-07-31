// Unit tests for the pure list text-role decision helper. These have no
// SDL/display dependencies, so they run headless with the host compiler via
// `make test`. The caller (minui-list.c) maps the returned role to an actual
// SDL_Color (NextUI theme color or the greyscale palette).

#include "list_theme.h"

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

// an unselected, enabled, selectable row uses the normal list text
static void test_normal_row(void)
{
    CHECK_EQ(ListTheme_RowTextRole(0, 0, 0), LIST_TEXT_NORMAL, "unselected enabled -> normal");
}

// an unselected, disabled row is dimmed
static void test_disabled_row(void)
{
    CHECK_EQ(ListTheme_RowTextRole(0, 1, 0), LIST_TEXT_DISABLED, "unselected disabled -> disabled");
}

// the selected, enabled row draws on the selection pill
static void test_selected_row(void)
{
    CHECK_EQ(ListTheme_RowTextRole(1, 0, 0), LIST_TEXT_SELECTED, "selected enabled -> selected");
}

// the selected, disabled row
static void test_selected_disabled_row(void)
{
    CHECK_EQ(ListTheme_RowTextRole(1, 1, 0), LIST_TEXT_SELECTED_DISABLED, "selected disabled -> selected disabled");
}

// a muted (header/unselectable) row wins over selection and disabled state, so it
// always resolves to LIST_TEXT_MUTED
static void test_muted_wins(void)
{
    CHECK_EQ(ListTheme_RowTextRole(0, 0, 1), LIST_TEXT_MUTED, "unselected muted -> muted");
    CHECK_EQ(ListTheme_RowTextRole(0, 1, 1), LIST_TEXT_MUTED, "unselected disabled muted -> muted");
    CHECK_EQ(ListTheme_RowTextRole(1, 0, 1), LIST_TEXT_MUTED, "selected muted -> muted");
    CHECK_EQ(ListTheme_RowTextRole(1, 1, 1), LIST_TEXT_MUTED, "selected disabled muted -> muted");
}

int main(void)
{
    test_normal_row();
    test_disabled_row();
    test_selected_row();
    test_selected_disabled_row();
    test_muted_wins();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
