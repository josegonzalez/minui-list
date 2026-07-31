#include "list_keyboard.h"

#include <string.h>

// keyboard_layout_lowercase is the default keyboard layout.
static const char *keyboard_layout_lowercase[LIST_KEYBOARD_ROWS][LIST_KEYBOARD_COLS] = {
    {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", ""},
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\", ""},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "", "", ""},
    {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "", "", "", ""},
    {"shift", "space", "enter", "", "", "", "", "", "", "", "", "", "", ""}};

// keyboard_layout_uppercase is the shifted keyboard layout.
static const char *keyboard_layout_uppercase[LIST_KEYBOARD_ROWS][LIST_KEYBOARD_COLS] = {
    {"~", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+", ""},
    {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}", "|", ""},
    {"A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\"", "", "", ""},
    {"Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", "", "", "", ""},
    {"shift", "space", "enter", "", "", "", "", "", "", "", "", "", "", ""}};

// keyboard_layout_special is the special-character keyboard layout. Some symbols
// are not supported by the font in use by MinUI, so they are omitted.
static const char *keyboard_layout_special[LIST_KEYBOARD_ROWS][LIST_KEYBOARD_COLS] = {
    {"~", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+", ""},
    {"{", "}", "|", "\\", "<", ">", "?", "\"", ";", ":", "[", "]", "\\", ""},
    {"±", "§", "¶", "©", "®", "™", "€", "£", "¥", "¢", "¤", "", "", ""},
    {"°", "•", "·", "†", "‡", "¬", "¦", "¡", "", "", "", "", "", ""},
    {"shift", "space", "enter", "", "", "", "", "", "", "", "", "", "", ""}};

// layout_for returns the table for a layout index, defaulting to lowercase for
// out-of-range values so callers always get a usable grid.
static const char *(*layout_for(int layout))[LIST_KEYBOARD_COLS]
{
    if (layout == 1)
        return keyboard_layout_uppercase;
    if (layout == 2)
        return keyboard_layout_special;
    return keyboard_layout_lowercase;
}

const char *ListKeyboard_KeyAt(int layout, int row, int col)
{
    if (row < 0 || row >= LIST_KEYBOARD_ROWS || col < 0 || col >= LIST_KEYBOARD_COLS)
        return "";
    return layout_for(layout)[row][col];
}

// count_row_length returns the number of non-empty cells in a row.
static int count_row_length(const char *(*layout)[LIST_KEYBOARD_COLS], int row)
{
    int length = 0;
    for (int i = 0; i < LIST_KEYBOARD_COLS; i++)
    {
        if (layout[row][i][0] != '\0')
            length++;
    }
    return length;
}

int ListKeyboard_RowLength(int layout, int row)
{
    if (row < 0 || row >= LIST_KEYBOARD_ROWS)
        return 0;
    return count_row_length(layout_for(layout), row);
}

// calculate_column_offset returns the centering offset between two rows.
static int calculate_column_offset(const char *(*layout)[LIST_KEYBOARD_COLS], int from_row, int to_row)
{
    int from_length = count_row_length(layout, from_row);
    int to_length = count_row_length(layout, to_row);
    return (to_length - from_length) / 2;
}

// adjust_offset_exit_last_row adjusts the offset when leaving the last row.
static int adjust_offset_exit_last_row(int offset, int column)
{
    if (column == 0)
        return offset - 1;
    if (column == 2)
        return offset + 1;
    return offset; // center stays fixed
}

// adjust_offset_enter_last_row adjusts the offset when entering the last row.
static int adjust_offset_enter_last_row(int offset, int col, int center)
{
    if (col > center)
        return offset - 1;
    if (col < center)
        return offset + 1;
    return offset;
}

// rescue snaps the cursor onto a valid, non-empty cell for its current row.
static void rescue(struct KeyboardCursor *cursor, const char *(*layout)[LIST_KEYBOARD_COLS])
{
    // clamp the row
    if (cursor->row < 0)
        cursor->row = 0;
    else if (cursor->row > LIST_KEYBOARD_ROWS - 1)
        cursor->row = LIST_KEYBOARD_ROWS - 1;

    // clamp the column
    if (cursor->col > LIST_KEYBOARD_COLS - 1)
        cursor->col = LIST_KEYBOARD_COLS - 1;

    // move left until we find a non-empty cell
    while (cursor->col >= 0 && layout[cursor->row][cursor->col][0] == '\0')
        cursor->col--;

    // if we went too far left, move right until we find a non-empty cell
    if (cursor->col < 0)
    {
        cursor->col = 0;
        while (layout[cursor->row][cursor->col][0] == '\0')
            cursor->col++;
    }
}

void ListKeyboard_Rescue(struct KeyboardCursor *cursor)
{
    rescue(cursor, layout_for(cursor->layout));
}

void ListKeyboard_Move(struct KeyboardCursor *cursor, enum ListKeyboardDir dir)
{
    const char *(*layout)[LIST_KEYBOARD_COLS] = layout_for(cursor->layout);
    const int max_row = LIST_KEYBOARD_ROWS;
    const int max_col = LIST_KEYBOARD_COLS;

    if (dir == LIST_KEYBOARD_UP)
    {
        if (cursor->row > 0)
        {
            int offset = calculate_column_offset(layout, cursor->row, cursor->row - 1);
            if (cursor->row == max_row - 1)
                offset = adjust_offset_exit_last_row(offset, cursor->col);
            cursor->col += offset;
            cursor->row--;
        }
        else
        {
            int offset = calculate_column_offset(layout, 0, max_row - 1);
            int row_length = count_row_length(layout, 0);
            int center = (row_length - 1) / 2;

            if (!((row_length & 1) == 0 && cursor->col == center - 1))
                offset = adjust_offset_enter_last_row(offset, cursor->col, center);
            cursor->col += offset;
            cursor->row = max_row - 1;
        }
        rescue(cursor, layout);
    }
    else if (dir == LIST_KEYBOARD_DOWN)
    {
        if (cursor->row < max_row - 1)
        {
            int offset = calculate_column_offset(layout, cursor->row, cursor->row + 1);
            int row_length = count_row_length(layout, cursor->row);
            int center = (row_length - 1) / 2;

            if (cursor->row + 1 == max_row - 1 && (cursor->col > center || ((row_length & 1) && cursor->col < center)))
                offset = adjust_offset_enter_last_row(offset, cursor->col, center);
            cursor->col += offset;
            cursor->row++;
        }
        else
        {
            int offset = calculate_column_offset(layout, max_row - 1, 0);
            offset = adjust_offset_exit_last_row(offset, cursor->col);
            cursor->col += offset;
            cursor->row = 0;
        }
        rescue(cursor, layout);
    }
    else if (dir == LIST_KEYBOARD_LEFT)
    {
        if (cursor->col > 0)
        {
            cursor->col--;
        }
        else
        {
            int last_col = max_col - 1;
            while (last_col >= 0 && layout[cursor->row][last_col][0] == '\0')
                last_col--;
            cursor->col = last_col;
        }
    }
    else if (dir == LIST_KEYBOARD_RIGHT)
    {
        const char *next_key = (cursor->col + 1 < max_col) ? layout[cursor->row][cursor->col + 1] : "";
        if (cursor->col + 1 >= max_col || *next_key == '\0')
            cursor->col = 0;
        else if (cursor->col < max_col - 1)
            cursor->col++;
    }
}

void ListKeyboard_CycleLayout(struct KeyboardCursor *cursor)
{
    cursor->layout = (cursor->layout + 1) % LIST_KEYBOARD_LAYOUTS;
    rescue(cursor, layout_for(cursor->layout));
}
