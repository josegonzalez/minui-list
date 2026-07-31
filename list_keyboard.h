#ifndef LIST_KEYBOARD_H
#define LIST_KEYBOARD_H

// list_keyboard provides the SDL-free layout tables and cursor navigation behind
// the inline filter keyboard. The layouts and movement math are ported from the
// sibling minui-keyboard tool so the two share the same on-screen keyboard feel.
// Keeping it display-free means it can be unit tested with the host compiler
// (see tests/list_keyboard_test.c); the caller renders the grid and reads keys.

// Grid dimensions shared by every layout. Rows 0-3 hold characters; row 4 holds
// the "shift", "space", and "enter" keys. Unused cells are empty strings.
#define LIST_KEYBOARD_ROWS 5
#define LIST_KEYBOARD_COLS 14
#define LIST_KEYBOARD_LAYOUTS 3

// KeyboardCursor tracks the focused key and the active layout.
// layout: 0 = lowercase, 1 = uppercase, 2 = special.
struct KeyboardCursor
{
    int row;
    int col;
    int layout;
};

// ListKeyboardDir enumerates a single d-pad step.
enum ListKeyboardDir
{
    LIST_KEYBOARD_UP = 0,
    LIST_KEYBOARD_DOWN,
    LIST_KEYBOARD_LEFT,
    LIST_KEYBOARD_RIGHT,
};

// ListKeyboard_KeyAt returns the key string at (layout,row,col). Empty cells and
// any out-of-range coordinate return "" (never NULL). The special keys are the
// literal strings "shift", "space", and "enter".
const char *ListKeyboard_KeyAt(int layout, int row, int col);

// ListKeyboard_RowLength returns the number of non-empty cells in a row.
int ListKeyboard_RowLength(int layout, int row);

// ListKeyboard_Rescue snaps the cursor onto a valid, non-empty cell for its
// current row/layout (used after a layout change).
void ListKeyboard_Rescue(struct KeyboardCursor *cursor);

// ListKeyboard_Move advances the cursor one step in `dir`, mirroring
// minui-keyboard: up/down keep the cursor roughly column-centered and wrap
// vertically; left/right wrap within the row. The cursor always lands on a
// non-empty cell.
void ListKeyboard_Move(struct KeyboardCursor *cursor, enum ListKeyboardDir dir);

// ListKeyboard_CycleLayout advances to the next layout (wrapping 2 -> 0) and
// rescues the cursor onto a valid cell.
void ListKeyboard_CycleLayout(struct KeyboardCursor *cursor);

#endif // LIST_KEYBOARD_H
