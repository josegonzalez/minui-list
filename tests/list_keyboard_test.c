// Unit tests for the pure filter-keyboard layout + cursor helpers. These have no
// SDL/display dependencies, so they run headless with the host compiler via
// `make test`.

#include "list_keyboard.h"

#include <stdio.h>
#include <string.h>

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

#define CHECK_STR(actual, expected, msg)                                        \
    do                                                                          \
    {                                                                           \
        checks++;                                                               \
        const char *_a = (actual);                                              \
        const char *_e = (expected);                                            \
        if (strcmp(_a, _e) != 0)                                                \
        {                                                                       \
            failures++;                                                         \
            fprintf(stderr, "FAIL: %s (expected \"%s\", got \"%s\")\n", (msg), _e, _a); \
        }                                                                       \
    } while (0)

static void test_key_at(void)
{
    CHECK_STR(ListKeyboard_KeyAt(0, 0, 0), "`", "keyat: lower row0 col0");
    CHECK_STR(ListKeyboard_KeyAt(0, 1, 0), "q", "keyat: lower row1 col0");
    CHECK_STR(ListKeyboard_KeyAt(0, 2, 0), "a", "keyat: lower row2 col0");
    CHECK_STR(ListKeyboard_KeyAt(0, 3, 0), "z", "keyat: lower row3 col0");
    CHECK_STR(ListKeyboard_KeyAt(0, 4, 0), "shift", "keyat: row4 shift");
    CHECK_STR(ListKeyboard_KeyAt(0, 4, 1), "space", "keyat: row4 space");
    CHECK_STR(ListKeyboard_KeyAt(0, 4, 2), "enter", "keyat: row4 enter");
    CHECK_STR(ListKeyboard_KeyAt(1, 1, 0), "Q", "keyat: upper row1 col0");
    // empty cells and out-of-range coordinates return ""
    CHECK_STR(ListKeyboard_KeyAt(0, 4, 3), "", "keyat: empty cell");
    CHECK_STR(ListKeyboard_KeyAt(0, -1, 0), "", "keyat: negative row");
    CHECK_STR(ListKeyboard_KeyAt(0, 0, 99), "", "keyat: col out of range");
}

static void test_row_length(void)
{
    CHECK_EQ(ListKeyboard_RowLength(0, 0), 13, "rowlen: lower row0");
    CHECK_EQ(ListKeyboard_RowLength(0, 1), 13, "rowlen: lower row1");
    CHECK_EQ(ListKeyboard_RowLength(0, 2), 11, "rowlen: lower row2");
    CHECK_EQ(ListKeyboard_RowLength(0, 3), 10, "rowlen: lower row3");
    CHECK_EQ(ListKeyboard_RowLength(0, 4), 3, "rowlen: row4 specials");
    CHECK_EQ(ListKeyboard_RowLength(0, -1), 0, "rowlen: out of range");
}

// move applies a single step and returns the key now under the cursor.
static const char *move(struct KeyboardCursor *c, enum ListKeyboardDir dir)
{
    ListKeyboard_Move(c, dir);
    return ListKeyboard_KeyAt(c->layout, c->row, c->col);
}

static void test_left_right_wrap(void)
{
    struct KeyboardCursor c = {.row = 2, .col = 0, .layout = 0};
    // left from col0 wraps to the last non-empty cell (row2 length 11 -> col10 = "'")
    CHECK_STR(move(&c, LIST_KEYBOARD_LEFT), "'", "left: wrap to last non-empty");
    CHECK_EQ(c.col, 10, "left: wrapped column");
    // right from the last non-empty cell wraps back to col0
    CHECK_STR(move(&c, LIST_KEYBOARD_RIGHT), "a", "right: wrap to first");
    CHECK_EQ(c.col, 0, "right: wrapped column");
    // right mid-row advances by one
    CHECK_STR(move(&c, LIST_KEYBOARD_RIGHT), "s", "right: advance");
}

static void test_up_down_transitions(void)
{
    // down from (0,0) stays column-aligned onto row1 "q"
    struct KeyboardCursor c = {.row = 0, .col = 0, .layout = 0};
    CHECK_STR(move(&c, LIST_KEYBOARD_DOWN), "q", "down: (0,0)->q");
    CHECK_EQ(c.row, 1, "down: row advanced");

    // down from (3,0) lands on the specials row, rescued onto "shift"
    c = (struct KeyboardCursor){.row = 3, .col = 0, .layout = 0};
    CHECK_STR(move(&c, LIST_KEYBOARD_DOWN), "shift", "down: (3,0)->shift");
    CHECK_EQ(c.row, 4, "down: reached specials row");

    // up from (4,0) "shift" re-centers onto row3 "c"
    c = (struct KeyboardCursor){.row = 4, .col = 0, .layout = 0};
    CHECK_STR(move(&c, LIST_KEYBOARD_UP), "c", "up: shift->c");
    CHECK_EQ(c.row, 3, "up: row decreased");

    // up from the top row wraps to the specials row
    c = (struct KeyboardCursor){.row = 0, .col = 0, .layout = 0};
    CHECK_STR(move(&c, LIST_KEYBOARD_UP), "shift", "up: wrap to specials");
    CHECK_EQ(c.row, 4, "up: wrapped to last row");

    // down from the specials row wraps back to the top row
    c = (struct KeyboardCursor){.row = 4, .col = 0, .layout = 0};
    move(&c, LIST_KEYBOARD_DOWN);
    CHECK_EQ(c.row, 0, "down: wrap to top row");
}

// test_never_empty walks the cursor through many steps and asserts it never
// lands on an empty cell, from every layout.
static void test_never_empty(void)
{
    enum ListKeyboardDir dirs[] = {LIST_KEYBOARD_DOWN, LIST_KEYBOARD_RIGHT,
                                   LIST_KEYBOARD_UP, LIST_KEYBOARD_LEFT,
                                   LIST_KEYBOARD_DOWN, LIST_KEYBOARD_DOWN,
                                   LIST_KEYBOARD_RIGHT, LIST_KEYBOARD_RIGHT};
    for (int layout = 0; layout < LIST_KEYBOARD_LAYOUTS; layout++)
    {
        struct KeyboardCursor c = {.row = 0, .col = 0, .layout = layout};
        for (int i = 0; i < 40; i++)
        {
            ListKeyboard_Move(&c, dirs[i % 8]);
            checks++;
            const char *key = ListKeyboard_KeyAt(c.layout, c.row, c.col);
            if (key[0] == '\0')
            {
                failures++;
                fprintf(stderr, "FAIL: never-empty landed on empty (layout %d, step %d, row %d, col %d)\n",
                        layout, i, c.row, c.col);
                break;
            }
        }
    }
}

static void test_cycle_layout(void)
{
    struct KeyboardCursor c = {.row = 1, .col = 0, .layout = 0};
    ListKeyboard_CycleLayout(&c);
    CHECK_EQ(c.layout, 1, "cycle: 0->1");
    ListKeyboard_CycleLayout(&c);
    CHECK_EQ(c.layout, 2, "cycle: 1->2");
    ListKeyboard_CycleLayout(&c);
    CHECK_EQ(c.layout, 0, "cycle: 2->0 wraps");
    // after cycling, the cursor is still on a non-empty cell
    CHECK_EQ(ListKeyboard_KeyAt(c.layout, c.row, c.col)[0] != '\0', 1, "cycle: cursor valid");
}

static void test_rescue(void)
{
    // an out-of-range cursor is snapped back onto a real key
    struct KeyboardCursor c = {.row = 3, .col = 13, .layout = 0};
    ListKeyboard_Rescue(&c);
    CHECK_EQ(ListKeyboard_KeyAt(c.layout, c.row, c.col)[0] != '\0', 1, "rescue: lands on non-empty");
    CHECK_EQ(c.col <= 9, 1, "rescue: column clamped into row3 range");
}

int main(void)
{
    test_key_at();
    test_row_length();
    test_left_right_wrap();
    test_up_down_transitions();
    test_never_empty();
    test_cycle_layout();
    test_rescue();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
