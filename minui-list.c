#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <msettings.h>
#include <getopt.h>
#include <parson/parson.h>
#ifdef USE_SDL2
#include <SDL2/SDL_ttf.h>
#else
#include <SDL/SDL_ttf.h>
#endif

#include "defines.h"
#include "api.h"
#include "utils.h"

SDL_Surface *screen = NULL;

enum list_result_t
{
    ExitCodeSuccess = 0,
    ExitCodeError = 1,
    ExitCodeCancelButton = 2,
    ExitCodeMenuButton = 3,
    ExitCodeActionButton = 4,
    ExitCodeParseError = 10,
    ExitCodeSerializeError = 11,
    ExitCodeKeyboardInterrupt = 130,
};
typedef int ExitCode;

// log_error logs a message to stderr for debugging purposes
void log_error(const char *msg)
{
    // Set stderr to unbuffered mode
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "%s\n", msg);
}

// log_info logs a message to stdout for debugging purposes
void log_info(const char *msg)
{
    // Set stdout to unbuffered mode
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("%s\n", msg);
}

// ListItem holds the configuration for a list item
struct ListItem
{
    // the name of the item
    char *name;
    // whether the item is enabled
    bool enabled;
    // whether the item has an enabled field
    bool has_enabled;
    // whether the item has an is_header field
    bool has_is_header;
    // whether the item has a selected_option field
    bool has_selected_option;
    // whether the item has a supports_enabling field
    bool has_supports_enabling;
    // whether the item has options field
    bool has_options;
    // whether the item is a header
    bool is_header;
    // the number of options for the item
    int option_count;
    // a list of char options for the item
    char **options;
    // the selected option index
    int selected_option;
    // whether the option supports enabling
    bool supports_enabling;
};

// ListState holds the state of the list
struct ListState
{
    // array of list items
    struct ListItem *items;
    // number of items in the list
    size_t item_count;
    // rendering state
    // index of first visible item
    int first_visible;
    // index of last visible item
    int last_visible;
    // index of currently selected item
    int selected;

    // whether or not any items in the list have options
    bool has_options;
};

// AppState holds the current state of the application
struct AppState
{
    // the exit code to return
    int exit_code;
    // whether the app should exit
    int quitting;
    // whether the screen needs to be redrawn
    int redraw;
    // whether to show the brightness or hardware state
    int show_brightness_setting;
    // the button to display on the Action button
    char action_button[1024];
    // the text to display on the Action button
    char action_text[1024];
    // the button to display on the Confirm button
    char confirm_button[1024];
    // the text to display on the Confirm button
    char confirm_text[1024];
    // the button to display on the Cancel button
    char cancel_button[1024];
    // the text to display on the Cancel button
    char cancel_text[1024];
    // the button to display on the Enable button
    char enable_button[1024];
    // the path to the JSON file
    char file[1024];
    // the format to read the input from
    char format[1024];
    // the key to the items array in the JSON file
    char item_key[1024];
    // the value to write to stdout (selected, state)
    char stdout_value[1024];
    // the title of the list page
    char title[1024];
    // the state of the list
    struct ListState *list_state;
};

char *read_stdin()
{
    // Read all of stdin into a string
    char *stdin_contents = NULL;
    size_t stdin_size = 0;
    size_t stdin_used = 0;
    char buffer[4096];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
    {
        if (stdin_contents == NULL)
        {
            stdin_size = bytes_read * 2;
            stdin_contents = malloc(stdin_size);
        }
        else if (stdin_used + bytes_read > stdin_size)
        {
            stdin_size *= 2;
            stdin_contents = realloc(stdin_contents, stdin_size);
        }

        memcpy(stdin_contents + stdin_used, buffer, bytes_read);
        stdin_used += bytes_read;
    }

    // Null terminate the string
    if (stdin_contents)
    {
        if (stdin_used == stdin_size)
        {
            stdin_contents = realloc(stdin_contents, stdin_size + 1);
        }
        stdin_contents[stdin_used] = '\0';
    }

    return stdin_contents;
}

char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate buffer with extra byte for null terminator
    char *contents = malloc(file_size + 1);
    if (!contents)
    {
        fclose(file);
        return NULL;
    }

    // Read file contents
    size_t bytes_read = fread(contents, 1, file_size, file);
    fclose(file);

    if (bytes_read != file_size)
    {
        free(contents);
        return NULL;
    }

    // Add null terminator
    contents[file_size] = '\0';

    return contents;
}

// ListState_New creates a new ListState from a JSON file
struct ListState *ListState_New(const char *filename, const char *format, const char *item_key, const char *title)
{
    struct ListState *state = malloc(sizeof(struct ListState));

    int max_row_count = MAIN_ROW_COUNT;
    if (strlen(title) > 0)
    {
        max_row_count -= 1;
    }

    if (strcmp(format, "text") == 0)
    {
        char *contents = NULL;
        if (strcmp(filename, "-") == 0)
        {
            contents = read_stdin();
        }
        else
        {
            contents = read_file(filename);
        }

        if (contents == NULL)
        {
            log_error("Failed to read file or stdin");
            free(state);
            return NULL;
        }

        // Count number of non-empty lines
        size_t item_count = 0;
        char *line_start = contents;
        while (*line_start != '\0')
        {
            char *line_end = strchr(line_start, '\n');
            if (!line_end)
            {
                line_end = line_start + strlen(line_start);
            }

            // Check if line has non-whitespace content
            char *p;
            for (p = line_start; p < line_end && isspace(*p); p++)
                ;
            if (p < line_end)
            {
                item_count++;
            }

            if (*line_end == '\0')
            {
                break;
            }
            line_start = line_end + 1;
        }

        // Allocate array for items
        state->items = malloc(sizeof(struct ListItem) * item_count);
        state->item_count = item_count;
        state->last_visible = (item_count < max_row_count) ? item_count : max_row_count;
        state->first_visible = 0;
        state->selected = 0;

        // Add non-empty lines to items array
        size_t item_index = 0;
        line_start = contents;
        while (*line_start != '\0')
        {
            char *line_end = strchr(line_start, '\n');
            if (!line_end)
            {
                line_end = line_start + strlen(line_start);
            }

            // Check if line has non-whitespace content
            char *p;
            for (p = line_start; p < line_end && isspace(*p); p++)
                ;
            if (p < line_end)
            {
                size_t line_len = line_end - line_start;
                char *line = malloc(line_len + 1);
                memcpy(line, line_start, line_len);
                line[line_len] = '\0';
                state->items[item_index].name = line;
                state->items[item_index].enabled = true;
                state->items[item_index].has_enabled = false;
                state->items[item_index].has_is_header = false;
                state->items[item_index].has_options = false;
                state->items[item_index].has_selected_option = false;
                state->items[item_index].is_header = false;
                state->items[item_index].option_count = 0;
                state->items[item_index].options = NULL;
                state->items[item_index].selected_option = 0;
                item_index++;
            }

            if (*line_end == '\0')
            {
                break;
            }
            line_start = line_end + 1;
        }

        free(contents);
        return state;
    }

    JSON_Value *root_value;
    if (strcmp(filename, "-") == 0)
    {
        char *contents = read_stdin();
        if (contents == NULL)
        {
            log_error("Failed to read stdin");
            free(state);
            return NULL;
        }

        root_value = json_parse_string_with_comments(contents);
        free(contents);
    }
    else
    {
        root_value = json_parse_file_with_comments(filename);
    }

    if (root_value == NULL)
    {
        log_error("Failed to parse JSON file");
        free(state);
        return NULL;
    }

    JSON_Array *items_array;
    if (strlen(item_key) == 0)
    {
        items_array = json_value_get_array(root_value);
    }
    else
    {
        items_array = json_value_get_array(json_object_get_value(json_value_get_object(root_value), item_key));
    }

    size_t item_count = json_array_get_count(items_array);

    state->items = malloc(sizeof(struct ListItem) * item_count);
    state->has_options = false;

    if (strlen(item_key) == 0)
    {
        for (size_t i = 0; i < item_count; i++)
        {
            const char *name = json_array_get_string(items_array, i);
            state->items[i].name = name ? strdup(name) : "";

            // set defaults for the other fields
            state->items[i].enabled = true;
            state->items[i].has_enabled = false;
            state->items[i].has_is_header = false;
            state->items[i].has_options = false;
            state->items[i].has_selected_option = false;
            state->items[i].has_supports_enabling = false;
            state->items[i].is_header = false;
            state->items[i].option_count = 0;
            state->items[i].options = NULL;
            state->items[i].selected_option = 0;
            state->items[i].supports_enabling = false;
        }
    }
    else
    {
        for (size_t i = 0; i < item_count; i++)
        {
            JSON_Object *item = json_array_get_object(items_array, i);

            const char *name = json_object_get_string(item, "name");
            state->items[i].name = name ? strdup(name) : "";

            // read in the options from the json object
            // if there are no options, set the options to an empty array
            // if there are options, treat them as a list of strings
            JSON_Array *options_array = json_object_get_array(item, "options");
            size_t options_count = json_array_get_count(options_array);
            state->items[i].options = malloc(sizeof(char *) * options_count);
            state->items[i].option_count = options_count;
            for (size_t j = 0; j < options_count; j++)
            {
                const char *option = json_array_get_string(options_array, j);
                state->items[i].options[j] = option ? strdup(option) : "";
            }

            if (options_count > 0)
            {
                state->has_options = true;
            }
            if (json_object_has_value(item, "selected_option"))
            {
                state->items[i].has_selected_option = true;
            }
            else
            {
                state->items[i].has_selected_option = false;
            }

            // read in the current option index from the json object
            // if there is no current option index, set it to 0
            // if there is a current option index, treat it as an integer
            if (json_object_has_value(item, "selected_option"))
            {
                state->items[i].selected_option = json_object_get_number(item, "selected_option");
                if (state->items[i].selected_option < 0)
                {
                    char error_message[256];
                    snprintf(error_message, sizeof(error_message), "Item %s has a selected option index of %d, which is less than 0. Setting to 0.", state->items[i].name, state->items[i].selected_option);
                    log_error(error_message);
                    state->items[i].selected_option = 0;
                }
                if (state->items[i].selected_option >= options_count)
                {
                    char error_message[256];
                    snprintf(error_message, sizeof(error_message), "Item %s has a selected option index of %d, which is greater than the number of options %d. Setting to last option.", state->items[i].name, state->items[i].selected_option, options_count);
                    log_error(error_message);
                    state->items[i].selected_option = options_count - 1;
                    if (state->items[i].selected_option < 0)
                    {
                        state->items[i].selected_option = 0;
                    }
                }
                state->items[i].has_selected_option = true;
            }
            else
            {
                state->items[i].selected_option = 0;
                state->items[i].has_selected_option = false;
            }

            // read in the is_header from the json object
            // if there is no is_header, set it to false
            // if there is a is_header, treat it as a boolean
            if (json_object_get_boolean(item, "is_header") == 1)
            {
                state->items[i].is_header = true;
                state->items[i].has_is_header = true;
            }
            else if (json_object_get_boolean(item, "is_header") == 0)
            {
                state->items[i].is_header = false;
                state->items[i].has_is_header = true;
            }
            else
            {
                state->items[i].is_header = false;
                state->items[i].has_is_header = false;
            }

            // read in the supports_enabling from the json object
            // if there is no supports_enabling, set it to false
            // if there is a supports_enabling, treat it as a boolean
            if (json_object_get_boolean(item, "supports_enabling") == 1)
            {
                state->items[i].supports_enabling = true;
                state->items[i].has_supports_enabling = true;
            }
            else if (json_object_get_boolean(item, "supports_enabling") == 0)
            {
                state->items[i].supports_enabling = false;
                state->items[i].has_supports_enabling = true;
            }
            else
            {
                state->items[i].supports_enabling = false;
                state->items[i].has_supports_enabling = false;
            }

            // read in the enabled from the json object
            // if there is no enabled, set it to true
            // if there is an enabled, treat it as a boolean
            if (json_object_get_boolean(item, "enabled") == 1)
            {
                state->items[i].enabled = true;
                state->items[i].has_enabled = true;
            }
            else if (json_object_get_boolean(item, "enabled") == 0)
            {
                state->items[i].enabled = false;
                state->items[i].has_enabled = true;
                if (!state->items[i].supports_enabling)
                {
                    char error_message[256];
                    snprintf(error_message, sizeof(error_message), "Item %s has no supports_enabling, but is disabled", state->items[i].name);
                    log_error(error_message);
                }
            }
            else
            {
                state->items[i].enabled = true;
                state->items[i].has_enabled = false;
            }
        }
    }
    state->last_visible = (item_count < max_row_count) ? item_count : max_row_count;
    state->first_visible = 0;
    state->selected = 0;
    state->item_count = item_count;

    json_value_free(root_value);
    return state;
}

// handle_input interprets input events and mutates app state
void handle_input(struct AppState *state)
{
    // do not redraw by default
    state->redraw = 0;

    PAD_poll();

    // discount the title from the max row count
    int max_row_count = MAIN_ROW_COUNT;
    if (strlen(state->title) > 0)
    {
        max_row_count -= 1;
    }

    bool is_action_button_pressed = false;
    bool is_cancel_button_pressed = false;
    bool is_confirm_button_pressed = false;
    bool is_enable_button_pressed = false;
    if (PAD_justReleased(BTN_A))
    {
        if (strcmp(state->action_button, "A") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "A") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "A") == 0)
        {
            is_cancel_button_pressed = true;
        }
        else if (strcmp(state->enable_button, "A") == 0)
        {
            is_enable_button_pressed = true;
        }
    }
    else if (PAD_justReleased(BTN_B))
    {
        if (strcmp(state->action_button, "B") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "B") == 0)
        {
            is_cancel_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "B") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->enable_button, "B") == 0)
        {
            is_enable_button_pressed = true;
        }
    }
    else if (PAD_justReleased(BTN_X))
    {
        if (strcmp(state->action_button, "X") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "X") == 0)
        {
            is_cancel_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "X") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->enable_button, "X") == 0)
        {
            is_enable_button_pressed = true;
        }
    }
    else if (PAD_justReleased(BTN_Y))
    {
        if (strcmp(state->action_button, "Y") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "Y") == 0)
        {
            is_cancel_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "Y") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->enable_button, "Y") == 0)
        {
            is_enable_button_pressed = true;
        }
    }

    if (is_action_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeActionButton;
        return;
    }

    if (is_cancel_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeCancelButton;
        return;
    }

    if (is_confirm_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeSuccess;
        return;
    }

    // if the enable button is pressed, toggle the enabled state of the currently selected item
    if (is_enable_button_pressed)
    {
        if (state->list_state->items[state->list_state->selected].supports_enabling)
        {
            state->redraw = 1;
            state->list_state->items[state->list_state->selected].enabled = !state->list_state->items[state->list_state->selected].enabled;
        }
        return;
    }

    if (PAD_justReleased(BTN_MENU))
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeMenuButton;
        return;
    }

    if (PAD_justRepeated(BTN_UP))
    {
        if (state->list_state->selected == 0 && !PAD_justPressed(BTN_UP))
        {
            state->redraw = 0;
        }
        else
        {
            state->list_state->selected -= 1;

            if (state->list_state->items[state->list_state->selected].is_header)
            {
                state->list_state->selected -= 1;
            }

            if (state->list_state->selected < 0)
            {
                state->list_state->selected = state->list_state->item_count - 1;
                int start = state->list_state->item_count - max_row_count;
                state->list_state->first_visible = (start < 0) ? 0 : start;
                state->list_state->last_visible = state->list_state->item_count;
            }
            else if (state->list_state->selected < state->list_state->first_visible)
            {
                state->list_state->first_visible -= 1;
                state->list_state->last_visible -= 1;
            }
            state->redraw = 1;
        }
    }
    else if (PAD_justRepeated(BTN_DOWN))
    {
        if (state->list_state->selected == state->list_state->item_count - 1 && !PAD_justPressed(BTN_DOWN))
        {
            state->redraw = 0;
        }
        else
        {
            state->list_state->selected += 1;

            if (state->list_state->items[state->list_state->selected].is_header)
            {
                state->list_state->selected += 1;
            }

            if (state->list_state->selected >= state->list_state->item_count)
            {
                state->list_state->selected = 0;
                state->list_state->first_visible = 0;
                state->list_state->last_visible = (state->list_state->item_count < max_row_count) ? state->list_state->item_count : max_row_count;
            }
            else if (state->list_state->selected >= state->list_state->last_visible)
            {
                state->list_state->first_visible += 1;
                state->list_state->last_visible += 1;
            }
            state->redraw = 1;
        }
    }
    else if (PAD_justRepeated(BTN_LEFT))
    {
        // if the state has options, cycle through the options
        if (state->list_state->has_options)
        {
            if (state->list_state->items[state->list_state->selected].enabled)
            {
                state->list_state->items[state->list_state->selected].selected_option -= 1;
                if (state->list_state->items[state->list_state->selected].selected_option < 0)
                {
                    state->list_state->items[state->list_state->selected].selected_option = state->list_state->items[state->list_state->selected].option_count - 1;
                }
            }
        }
        else
        {
            state->list_state->selected -= max_row_count;
            if (state->list_state->selected < 0)
            {
                state->list_state->selected = 0;
                state->list_state->first_visible = 0;
                state->list_state->last_visible = (state->list_state->item_count < max_row_count) ? state->list_state->item_count : max_row_count;
            }
            else if (state->list_state->selected < state->list_state->first_visible)
            {
                state->list_state->first_visible -= max_row_count;
                if (state->list_state->first_visible < 0)
                    state->list_state->first_visible = 0;
                state->list_state->last_visible = state->list_state->first_visible + max_row_count;
            }
        }
        state->redraw = 1;
    }
    else if (PAD_justRepeated(BTN_RIGHT))
    {
        // if the state has options, cycle through the options
        if (state->list_state->has_options)
        {
            if (state->list_state->items[state->list_state->selected].enabled)
            {
                state->list_state->items[state->list_state->selected].selected_option += 1;
                if (state->list_state->items[state->list_state->selected].selected_option >= state->list_state->items[state->list_state->selected].option_count)
                {
                    state->list_state->items[state->list_state->selected].selected_option = 0;
                }
            }
        }
        else
        {
            state->list_state->selected += max_row_count;
            if (state->list_state->selected >= state->list_state->item_count)
            {
                state->list_state->selected = state->list_state->item_count - 1;
                int start = state->list_state->item_count - max_row_count;
                state->list_state->first_visible = (start < 0) ? 0 : start;
                state->list_state->last_visible = state->list_state->item_count;
            }
            else if (state->list_state->selected >= state->list_state->last_visible)
            {
                state->list_state->last_visible += max_row_count;
                if (state->list_state->last_visible > state->list_state->item_count)
                    state->list_state->last_visible = state->list_state->item_count;
                state->list_state->first_visible = state->list_state->last_visible - max_row_count;
            }
        }
        state->redraw = 1;
    }
}

// detects if a string is a hex color
bool detect_hex_color(const char *hex)
{
    if (hex[0] != '#')
    {
        return false;
    }

    hex++;
    int r, g, b;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3)
    {
        return true;
    }

    return false;
}

// turns a hex color (e.g. #000000) into an SDL_Color
SDL_Color hex_to_sdl_color(const char *hex)
{
    SDL_Color color = {0, 0, 0, 255};

    // Skip # if present
    if (hex[0] == '#')
    {
        hex++;
    }

    // Parse RGB values from hex string
    int r, g, b;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3)
    {
        color.r = r;
        color.g = g;
        color.b = b;
    }

    return color;
}

// turns an SDL_Color into a uint32_t
uint32_t sdl_color_to_uint32(SDL_Color color)
{
    return (uint32_t)((color.r << 16) + (color.g << 8) + (color.b << 0));
}

// draw_screen interprets the app state and draws it to the screen
void draw_screen(SDL_Surface *screen, struct AppState *state, int ow)
{
    // draw the button group on the right
    // only two buttons can be displayed at a time
    GFX_blitButtonGroup((char *[]){state->cancel_button, state->cancel_text, state->confirm_button, state->confirm_text, NULL}, 1, screen, 1);

    // if there is a title specified, compute the space needed for it
    int has_top_margin = 0;
    if (strlen(state->title) > 0)
    {
        // draw the title
        SDL_Color text_color = COLOR_GRAY;
        SDL_Surface *text = TTF_RenderUTF8_Blended(font.medium, state->title, text_color);
        SDL_Rect pos = {
            SCALE1(PADDING + BUTTON_PADDING),
            SCALE1(PADDING + 4),
            text->w,
            text->h};
        has_top_margin = 1;
        SDL_BlitSurface(text, NULL, screen, &pos);
        SDL_FreeSurface(text);
    }

    // the rest of the function is just for drawing your app to the screen
    bool current_item_supports_enabling = false;
    bool current_item_is_enabled = false;
    bool current_item_is_header = false;
    int selected_row = state->list_state->selected - state->list_state->first_visible;
    for (int i = state->list_state->first_visible, j = 0; i < state->list_state->last_visible; i++, j++)
    {
        int available_width = (screen->w) - SCALE1(PADDING * 2);
        if (i == state->list_state->first_visible && !(j != selected_row))
        {
            available_width -= ow;
        }
        // compute the string representation of the current item
        // to include the current option if there are any options
        // the output should be in the format of:
        // item.name: <selected_option>
        // if there are no options, the output should be:
        // item.name
        char display_text[256];
        bool is_hex_color = false;
        if (state->list_state->items[i].option_count > 0 && !state->list_state->items[i].is_header)
        {
            char *selected_option = state->list_state->items[i].options[state->list_state->items[i].selected_option];
            snprintf(display_text, sizeof(display_text), "%s: %s", state->list_state->items[i].name, selected_option);
            is_hex_color = detect_hex_color(selected_option);
        }
        else
        {
            snprintf(display_text, sizeof(display_text), "%s", state->list_state->items[i].name);
        }

        SDL_Color text_color = COLOR_WHITE;
        if (!state->list_state->items[i].enabled)
        {
            text_color = (SDL_Color){TRIAD_DARK_GRAY};
        }
        if (state->list_state->items[i].is_header)
        {
            text_color = COLOR_LIGHT_TEXT;
        }

        int color_placeholder_height;
        TTF_SizeUTF8(font.medium, " ", NULL, &color_placeholder_height);

        char truncated_display_text[256];
        int text_width = GFX_truncateText(font.large, display_text, truncated_display_text, available_width, SCALE1(BUTTON_PADDING * 2));
        int max_width = MIN(available_width, text_width);
        if (j == selected_row)
        {
            text_color = COLOR_BLACK;
            current_item_is_enabled = state->list_state->items[i].enabled;
            if (!state->list_state->items[i].enabled)
            {
                text_color = (SDL_Color){TRIAD_LIGHT_GRAY};
            }
            if (state->list_state->items[i].supports_enabling)
            {
                current_item_supports_enabling = true;
            }
            if (state->list_state->items[i].is_header)
            {
                current_item_is_header = true;
                text_color = COLOR_LIGHT_TEXT;
            }
            if (is_hex_color)
            {
                max_width += color_placeholder_height + SCALE1(PADDING);
            }

            GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING + ((j + has_top_margin) * PILL_SIZE)), max_width, SCALE1(PILL_SIZE)});
        }

        SDL_Surface *text;
        text = TTF_RenderUTF8_Blended(font.large, truncated_display_text, text_color);

        SDL_Rect pos = {
            SCALE1(PADDING + BUTTON_PADDING),
            SCALE1(PADDING + ((i - state->list_state->first_visible + has_top_margin) * PILL_SIZE) + 4),
            text->w,
            text->h};
        SDL_BlitSurface(text, NULL, screen, &pos);
        SDL_FreeSurface(text);

        if (is_hex_color)
        {
            // get the hex color from the options array
            char *hex_color = state->list_state->items[i].options[state->list_state->items[i].selected_option];
            SDL_Color current_color = hex_to_sdl_color(hex_color);
            uint32_t color = SDL_MapRGBA(screen->format, current_color.r, current_color.g, current_color.b, 255);

            // Draw outline cube
            uint32_t outline_color = sdl_color_to_uint32(text_color);
            SDL_Rect outline_rect = {
                SCALE1(PADDING + BUTTON_PADDING) + text->w + SCALE1(PADDING),
                SCALE1(PADDING + ((i - state->list_state->first_visible + has_top_margin) * PILL_SIZE) + 5), color_placeholder_height,
                color_placeholder_height};
            SDL_FillRect(screen, &(SDL_Rect){outline_rect.x, outline_rect.y, outline_rect.w, outline_rect.h}, outline_color);

            // Draw color cube
            SDL_Rect color_rect = {
                SCALE1(PADDING + BUTTON_PADDING) + text->w + SCALE1(PADDING) + 2,
                SCALE1(PADDING + ((i - state->list_state->first_visible + has_top_margin) * PILL_SIZE) + 5) + 2, color_placeholder_height - 4,
                color_placeholder_height - 4};
            SDL_FillRect(screen, &(SDL_Rect){color_rect.x, color_rect.y, color_rect.w, color_rect.h}, color);
        }
    }

    char enable_button_text[256] = "Enable";
    if (current_item_is_enabled)
    {
        strncpy(enable_button_text, "Disable", sizeof(enable_button_text) - 1);
    }

    // draw the button group on the left
    // this should only display the enable button if the current item supports enabling
    // and should only display the action button if it is assigned to a button
    if (current_item_supports_enabling && strcmp(state->enable_button, "") != 0)
    {
        if (strcmp(state->action_button, "") != 0)
        {
            GFX_blitButtonGroup((char *[]){state->enable_button, enable_button_text, state->action_button, state->action_text, NULL}, 0, screen, 0);
        }
        else
        {
            GFX_blitButtonGroup((char *[]){state->enable_button, enable_button_text, NULL}, 0, screen, 0);
        }
    }
    else if (strcmp(state->action_button, "") != 0)
    {
        GFX_blitButtonGroup((char *[]){state->action_button, state->action_text, NULL}, 0, screen, 0);
    }

    // don't forget to reset the should_redraw flag
    state->redraw = 0;
}

// swallow_stdout_from_function swallows stdout from a function
// this is useful for suppressing output from a function
// that we don't want to see in the log file
// the InitSettings() function is an example of this (some implementations print to stdout)
void swallow_stdout_from_function(void (*func)(void))
{
    int original_stdout = dup(STDOUT_FILENO);
    int dev_null = open("/dev/null", O_WRONLY);

    dup2(dev_null, STDOUT_FILENO);
    close(dev_null);

    func();

    dup2(original_stdout, STDOUT_FILENO);
    close(original_stdout);
}

void signal_handler(int signal)
{
    // if the signal is a ctrl+c, exit with code 130
    if (signal == SIGINT)
    {
        exit(ExitCodeKeyboardInterrupt);
    }
    else
    {
        exit(ExitCodeError);
    }
}

// parse_arguments parses the arguments using getopt and updates the app state
// supports the following flags:
// - --action-button <button> (default: "X")
// - --action-text <text> (default: "ACTION")
// - --confirm-button <button> (default: "A")
// - --confirm-text <text> (default: "SELECT")
// - --cancel-button <button> (default: "B")
// - --cancel-text <text> (default: "BACK")
// - --enable-button <button> (default: "Y")
// - --file <path> (default: empty string)
// - --format <format> (default: "json")
// - --header <title> (default: empty string)
// - --item-key <key> (default: "items")
// - --stdout-value <value> (default: "selected")
bool parse_arguments(struct AppState *state, int argc, char *argv[])
{
    static struct option long_options[] = {
        {"action-button", required_argument, 0, 'a'},
        {"action-text", required_argument, 0, 'A'},
        {"confirm-button", required_argument, 0, 'b'},
        {"confirm-text", required_argument, 0, 'c'},
        {"cancel-button", required_argument, 0, 'B'},
        {"cancel-text", required_argument, 0, 'C'},
        {"enable-button", required_argument, 0, 'e'},
        {"file", required_argument, 0, 'f'},
        {"format", required_argument, 0, 'F'},
        {"item-key", required_argument, 0, 'i'},
        {"header", required_argument, 0, 'H'},
        {"stdout-value", required_argument, 0, 's'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "a:A:b:c:B:C:e:f:F:i:H:s:", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'a':
            strncpy(state->action_button, optarg, sizeof(state->action_button) - 1);
            break;
        case 'A':
            strncpy(state->action_text, optarg, sizeof(state->action_text) - 1);
            break;
        case 'b':
            strncpy(state->confirm_button, optarg, sizeof(state->confirm_button) - 1);
            break;
        case 'B':
            strncpy(state->cancel_button, optarg, sizeof(state->cancel_button) - 1);
            break;
        case 'c':
            strncpy(state->confirm_text, optarg, sizeof(state->confirm_text) - 1);
            break;
        case 'C':
            strncpy(state->cancel_text, optarg, sizeof(state->cancel_text) - 1);
            break;
        case 'e':
            strncpy(state->enable_button, optarg, sizeof(state->enable_button) - 1);
            break;
        case 'f':
            strncpy(state->file, optarg, sizeof(state->file) - 1);
            break;
        case 'F':
            strncpy(state->format, optarg, sizeof(state->format) - 1);
            break;
        case 'i':
            strncpy(state->item_key, optarg, sizeof(state->item_key) - 1);
            break;
        case 'H':
            strncpy(state->title, optarg, sizeof(state->title) - 1);
            break;
        case 's':
            strncpy(state->stdout_value, optarg, sizeof(state->stdout_value) - 1);
            break;
        default:
            return false;
        }
    }

    if (strcmp(state->format, "") == 0)
    {
        strncpy(state->format, "json", sizeof(state->format) - 1);
    }

    if (strcmp(state->stdout_value, "") == 0)
    {
        strncpy(state->stdout_value, "selected", sizeof(state->stdout_value) - 1);
    }

    // Apply default values for certain buttons and texts
    if (strcmp(state->action_button, "") == 0)
    {
        strncpy(state->action_button, "X", sizeof(state->action_button) - 1);
    }

    if (strcmp(state->action_text, "") == 0)
    {
        strncpy(state->action_text, "ACTION", sizeof(state->action_text) - 1);
    }

    if (strcmp(state->cancel_button, "") == 0)
    {
        strncpy(state->cancel_button, "B", sizeof(state->cancel_button) - 1);
    }

    if (strcmp(state->confirm_text, "") == 0)
    {
        strncpy(state->confirm_text, "SELECT", sizeof(state->confirm_text) - 1);
    }

    if (strcmp(state->cancel_text, "") == 0)
    {
        strncpy(state->cancel_text, "BACK", sizeof(state->cancel_text) - 1);
    }

    if (strcmp(state->enable_button, "") == 0)
    {
        strncpy(state->enable_button, "Y", sizeof(state->enable_button) - 1);
    }

    // validate that hardware buttons aren't assigned to more than once
    bool a_button_assigned = false;
    bool b_button_assigned = false;
    bool x_button_assigned = false;
    bool y_button_assigned = false;
    if (strcmp(state->action_button, "A") == 0)
    {
        a_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "A") == 0)
    {
        if (a_button_assigned)
        {
            log_error("A button cannot be assigned to more than one button");
            return false;
        }

        a_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "A") == 0)
    {
        if (a_button_assigned)
        {
            log_error("A button cannot be assigned to more than one button");
            return false;
        }

        a_button_assigned = true;
    }
    if (strcmp(state->enable_button, "A") == 0)
    {
        if (a_button_assigned)
        {
            log_error("A button cannot be assigned to more than one button");
            return false;
        }

        a_button_assigned = true;
    }

    if (strcmp(state->action_button, "B") == 0)
    {
        b_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "B") == 0)
    {
        if (b_button_assigned)
        {
            log_error("B button cannot be assigned to more than one button");
            return false;
        }

        b_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "B") == 0)
    {
        if (b_button_assigned)
        {
            log_error("B button cannot be assigned to more than one button");
            return false;
        }

        b_button_assigned = true;
    }
    if (strcmp(state->enable_button, "B") == 0)
    {
        if (b_button_assigned)
        {
            log_error("B button cannot be assigned to more than one button");
            return false;
        }

        b_button_assigned = true;
    }

    if (strcmp(state->action_button, "X") == 0)
    {
        x_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "X") == 0)
    {
        if (x_button_assigned)
        {
            log_error("X button cannot be assigned to more than one button");
            return false;
        }

        x_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "X") == 0)
    {
        if (x_button_assigned)
        {
            log_error("X button cannot be assigned to more than one button");
            return false;
        }

        x_button_assigned = true;
    }
    if (strcmp(state->enable_button, "X") == 0)
    {
        if (x_button_assigned)
        {
            log_error("X button cannot be assigned to more than one button");
            return false;
        }

        x_button_assigned = true;
    }

    if (strcmp(state->action_button, "Y") == 0)
    {
        y_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "Y") == 0)
    {
        if (y_button_assigned)
        {
            log_error("Y button cannot be assigned to more than one button");
            return false;
        }
        y_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "Y") == 0)
    {
        if (y_button_assigned)
        {
            log_error("Y button cannot be assigned to more than one button");
            return false;
        }
        y_button_assigned = true;
    }
    if (strcmp(state->enable_button, "Y") == 0)
    {
        if (y_button_assigned)
        {
            log_error("Y button cannot be assigned to more than one button");
            return false;
        }
        y_button_assigned = true;
    }

    // validate that the confirm and cancel buttons are valid
    if (strcmp(state->confirm_button, "A") != 0 && strcmp(state->confirm_button, "B") != 0 && strcmp(state->confirm_button, "X") != 0 && strcmp(state->confirm_button, "Y") != 0)
    {
        log_error("Invalid confirm button provided");
        return false;
    }
    if (strcmp(state->cancel_button, "A") != 0 && strcmp(state->cancel_button, "B") != 0 && strcmp(state->cancel_button, "X") != 0 && strcmp(state->cancel_button, "Y") != 0)
    {
        log_error("Invalid cancel button provided");
        return false;
    }

    if (strlen(state->file) == 0)
    {
        log_error("No file provided");
        return false;
    }

    if (strlen(state->format) == 0)
    {
        log_error("No format provided");
        return false;
    }
    // validate format, and only allow json or text
    if (strcmp(state->format, "json") != 0 && strcmp(state->format, "text") != 0)
    {
        log_error("Invalid format provided");
        return false;
    }

    return true;
}

// init initializes the app state
// everything is placed here as MinUI sometimes logs to stdout
// and the logging happens depending on the platform
void init()
{
    // set the cpu speed to the menu speed
    // this is done here to ensure we downclock
    // the menu (no need to draw power unnecessarily)
    PWR_setCPUSpeed(CPU_SPEED_MENU);

    // initialize:
    // - the screen, allowing us to draw to it
    // - input from the pad/joystick/buttons/etc.
    // - power management
    // - sync hardware settings (brightness, hdmi, speaker, etc.)
    if (screen == NULL)
    {
        screen = GFX_init(MODE_MAIN);
    }
    PAD_init();
    PWR_init();
    InitSettings();
}

// destruct cleans up the app state in reverse order
void destruct()
{
    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();
}

// main is the entry point for the app
int main(int argc, char *argv[])
{
    // swallow all stdout from init calls
    // MinUI will sometimes randomly log to stdout
    swallow_stdout_from_function(init);

    signal(SIGINT, signal_handler);

    // Initialize app state
    char default_action_button[1024] = "X";
    char default_action_text[1024] = "ACTION";
    char default_cancel_button[1024] = "B";
    char default_cancel_text[1024] = "BACK";
    char default_enable_button[1024] = "Y";
    char default_confirm_button[1024] = "A";
    char default_confirm_text[1024] = "SELECT";
    char default_file[1024] = "";
    char default_format[1024] = "json";
    char default_item_key[1024] = "";
    char default_stdout_value[1024] = "selected";
    char default_title[1024] = "";
    struct AppState state = {
        .exit_code = ExitCodeSuccess,
        .quitting = 0,
        .redraw = 1,
        .show_brightness_setting = 0,
        .list_state = NULL};

    // assign the default values to the app state
    strncpy(state.action_button, default_action_button, sizeof(state.action_button) - 1);
    strncpy(state.action_text, default_action_text, sizeof(state.action_text) - 1);
    strncpy(state.cancel_button, default_cancel_button, sizeof(state.cancel_button) - 1);
    strncpy(state.cancel_text, default_cancel_text, sizeof(state.cancel_text) - 1);
    strncpy(state.confirm_button, default_confirm_button, sizeof(state.confirm_button) - 1);
    strncpy(state.confirm_text, default_confirm_text, sizeof(state.confirm_text) - 1);
    strncpy(state.enable_button, default_enable_button, sizeof(state.enable_button) - 1);
    strncpy(state.file, default_file, sizeof(state.file) - 1);
    strncpy(state.format, default_format, sizeof(state.format) - 1);
    strncpy(state.item_key, default_item_key, sizeof(state.item_key) - 1);
    strncpy(state.stdout_value, default_stdout_value, sizeof(state.stdout_value) - 1);
    strncpy(state.title, default_title, sizeof(state.title) - 1);

    // parse the arguments
    parse_arguments(&state, argc, argv);

    state.list_state = ListState_New(state.file, state.format, state.item_key, state.title);
    if (state.list_state == NULL)
    {
        log_error("Failed to create list state");
        return ExitCodeError;
    }

    // get initial wifi state
    int was_online = PLAT_isOnline();

    // draw the screen at least once
    // handle_input sets state.redraw to 0 if no key is pressed
    int was_ever_drawn = 0;

    while (!state.quitting)
    {
        // start the frame to ensure GFX_sync() works
        // on devices that don't support vsync
        GFX_startFrame();

        // handle turning the on/off screen on/off
        // as well as general power management
        PWR_update(&state.redraw, NULL, NULL, NULL);

        // check if the device is on wifi
        // redraw if the wifi state changed
        // and then update our state
        int is_online = PLAT_isOnline();
        if (was_online != is_online)
        {
            state.redraw = 1;
        }
        was_online = is_online;

        // handle any input events
        handle_input(&state);

        // force a redraw if the screen was never drawn
        if (!was_ever_drawn && !state.redraw)
        {
            state.redraw = 1;
            was_ever_drawn = 1;
        }

        // redraw the screen if there has been a change
        if (state.redraw)
        {
            // clear the screen at the beginning of each loop
            GFX_clear(screen);

            // draw the hardware information in the top-right
            int ow = GFX_blitHardwareGroup(screen, state.show_brightness_setting);

            // draw the setting hints
            if (state.show_brightness_setting)
            {
                GFX_blitHardwareHints(screen, state.show_brightness_setting);
            }

            // your draw logic goes here
            draw_screen(screen, &state, ow);

            // Takes the screen buffer and displays it on the screen
            GFX_flip(screen);
        }
        else
        {
            // Slows down the frame rate to match the refresh rate of the screen
            // when the screen is not being redrawn
            GFX_sync();
        }
    }

    if (state.exit_code == ExitCodeSuccess || state.exit_code == ExitCodeActionButton)
    {
        if (strcmp(state.stdout_value, "selected") == 0)
        {
            log_info(state.list_state->items[state.list_state->selected].name);
        }
    }

    if (strcmp(state.stdout_value, "state") == 0)
    {
        JSON_Value *root_value = json_value_init_object();
        JSON_Object *root_object = json_value_get_object(root_value);
        char *serialized_string = NULL;
        json_object_set_number(root_object, "selected", state.list_state->selected);

        JSON_Array *items = json_array(json_value_init_array());
        for (int i = 0; i < state.list_state->item_count; i++)
        {
            JSON_Value *val = json_value_init_object();
            JSON_Object *obj = json_value_get_object(val);
            if (json_object_dotset_string(obj, "name", state.list_state->items[i].name) == JSONFailure)
            {
                log_error("Failed to set name");
                return ExitCodeSerializeError;
            }
            if (state.list_state->items[i].has_is_header)
            {
                if (json_object_dotset_boolean(obj, "is_header", state.list_state->items[i].is_header))
                {
                    log_error("Failed to set enabled");
                    return ExitCodeSerializeError;
                }
            }
            else
            {
                if ((state.list_state->items[i].has_enabled || state.list_state->items[i].has_supports_enabling) && json_object_dotset_boolean(obj, "enabled", state.list_state->items[i].enabled))
                {
                    log_error("Failed to set enabled");
                    return ExitCodeSerializeError;
                }
                if ((state.list_state->items[i].has_options) && json_object_dotset_number(obj, "selected_option", state.list_state->items[i].selected_option) == JSONFailure)
                {
                    log_error("Failed to set selected_option");
                    return ExitCodeSerializeError;
                }
                if (state.list_state->items[i].has_supports_enabling && json_object_dotset_boolean(obj, "supports_enabling", state.list_state->items[i].supports_enabling) == JSONFailure)
                {
                    log_error("Failed to set supports_enabling");
                    return ExitCodeSerializeError;
                }

                if (state.list_state->items[i].has_options)
                {
                    JSON_Array *options = json_array(json_value_init_array());
                    for (int j = 0; j < state.list_state->items[i].option_count; j++)
                    {
                        JSON_Value *option = json_value_init_string(state.list_state->items[i].options[j]);
                        if (json_array_append_value(options, option) == JSONFailure)
                        {
                            log_error("Failed to append option");
                            return ExitCodeSerializeError;
                        }
                    }
                    if (json_object_dotset_value(obj, "options", json_array_get_wrapping_value(options)) == JSONFailure)
                    {
                        log_error("Failed to set options");
                        return ExitCodeSerializeError;
                    }
                }
            }

            JSON_Value *item_value = json_object_get_wrapping_value(obj);
            if (json_array_append_value(items, item_value) == JSONFailure)
            {
                log_error("Failed to append item");
                return ExitCodeSerializeError;
            }
        }

        JSON_Value *items_value = json_array_get_wrapping_value(items);
        if (json_object_dotset_value(root_object, state.item_key, items_value) == JSONFailure)
        {
            log_error("Failed to set items");
            return ExitCodeSerializeError;
        }

        root_value = json_object_get_wrapping_value(root_object);

        serialized_string = json_serialize_to_string_pretty(root_value);
        if (serialized_string == NULL)
        {
            log_error("Failed to serialize");
            return ExitCodeSerializeError;
        }

        log_info(serialized_string);

        json_free_serialized_string(serialized_string);
        json_value_free(root_value);
    }

    swallow_stdout_from_function(destruct);

    // exit the program
    return state.exit_code;
}
