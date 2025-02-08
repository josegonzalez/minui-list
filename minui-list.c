#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <msettings.h>
#include <argp.h>
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
    char *name;
};

// ListState holds the state of the list
struct ListState
{
    struct ListItem *items; // array of list items
    size_t item_count;      // number of items in the list
    // rendering
    int first_visible; // index of first visible item
    int last_visible;  // index of last visible item
    int selected;      // index of currently selected item
};

// AppState holds the current state of the application
struct AppState
{
    int exit_code;                // the exit code to return
    int quitting;                 // whether the app should exit
    int redraw;                   // whether the screen needs to be redrawn
    int show_brightness_setting;  // whether to show the brightness or hardware state
    char confirm_button[1024];    // the button to display on the Confirm button
    char confirm_text[1024];      // the text to display on the Confirm button
    char cancel_button[1024];     // the button to display on the Cancel button
    char cancel_text[1024];       // the text to display on the Cancel button
    char file[1024];              // the path to the JSON file
    char format[1024];            // the format to read the input from
    char item_key[1024];          // the key to the items array in the JSON file
    char title[1024];             // the title of the list page
    struct ListState *list_state; // the state of the list
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

        root_value = json_parse_string(contents);
        free(contents);
    }
    else
    {
        root_value = json_parse_file(filename);
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

    if (strlen(item_key) == 0)
    {
        for (size_t i = 0; i < item_count; i++)
        {
            const char *name = json_array_get_string(items_array, i);
            state->items[i].name = name ? strdup(name) : "";
        }
    }
    else
    {
        for (size_t i = 0; i < item_count; i++)
        {
            JSON_Object *item = json_array_get_object(items_array, i);

            const char *name = json_object_get_string(item, "name");
            state->items[i].name = name ? strdup(name) : "";
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

    bool is_cancel_button_pressed = false;
    if (strcmp(state->cancel_button, "A") == 0 && PAD_justReleased(BTN_A))
    {
        is_cancel_button_pressed = true;
    }
    else if (strcmp(state->cancel_button, "B") == 0 && PAD_justReleased(BTN_B))
    {
        is_cancel_button_pressed = true;
    }
    else if (strcmp(state->cancel_button, "X") == 0 && PAD_justReleased(BTN_X))
    {
        is_cancel_button_pressed = true;
    }
    else if (strcmp(state->cancel_button, "Y") == 0 && PAD_justReleased(BTN_Y))
    {
        is_cancel_button_pressed = true;
    }

    if (is_cancel_button_pressed || PAD_justReleased(BTN_MENU))
    {
        state->redraw = 0;
        state->quitting = 1;
        if (is_cancel_button_pressed)
        {
            state->exit_code = 2;
        }
        else
        {
            state->exit_code = 3;
        }
        return;
    }

    bool is_confirm_button_pressed = false;
    if (strcmp(state->confirm_button, "A") == 0 && PAD_justReleased(BTN_A))
    {
        is_confirm_button_pressed = true;
    }
    else if (strcmp(state->confirm_button, "B") == 0 && PAD_justReleased(BTN_B))
    {
        is_confirm_button_pressed = true;
    }
    else if (strcmp(state->confirm_button, "X") == 0 && PAD_justReleased(BTN_X))
    {
        is_confirm_button_pressed = true;
    }
    else if (strcmp(state->confirm_button, "Y") == 0 && PAD_justReleased(BTN_Y))
    {
        is_confirm_button_pressed = true;
    }

    if (is_confirm_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = EXIT_SUCCESS;
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
    if (PAD_justRepeated(BTN_LEFT))
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
        state->redraw = 1;
    }
    else if (PAD_justRepeated(BTN_RIGHT))
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
        state->redraw = 1;
    }
}

// draw_screen interprets the app state and draws it to the screen
void draw_screen(SDL_Surface *screen, struct AppState *state, int ow)
{
    // draw the button group on the button-right
    // only two buttons can be displayed at a time
    GFX_blitButtonGroup((char *[]){state->cancel_button, state->cancel_text, state->confirm_button, state->confirm_text, NULL}, 1, screen, 1);

    // if there is a title specified, compute the space needed for it
    int has_top_margin = 0;
    if (strlen(state->title) > 0)
    {
        // draw the title
        SDL_Color text_color = COLOR_WHITE;
        SDL_Surface *text = TTF_RenderUTF8_Blended(font.large, state->title, text_color);
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
    int selected_row = state->list_state->selected - state->list_state->first_visible;
    for (int i = state->list_state->first_visible, j = 0; i < state->list_state->last_visible; i++, j++)
    {
        int available_width = (screen->w) - SCALE1(PADDING * 2);
        if (i == state->list_state->first_visible && !(j != selected_row))
        {
            available_width -= ow;
        }

        SDL_Color text_color = COLOR_WHITE;
        char display_name[256];
        int text_width = GFX_truncateText(font.large, state->list_state->items[i].name, display_name, available_width, SCALE1(BUTTON_PADDING * 2));
        int max_width = MIN(available_width, text_width);
        if (j == selected_row)
        {
            GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING + ((j + has_top_margin) * PILL_SIZE)), max_width, SCALE1(PILL_SIZE)});
            text_color = COLOR_BLACK;
        }
        SDL_Surface *text = TTF_RenderUTF8_Blended(font.large, state->list_state->items[i].name, text_color);

        SDL_Rect pos = {
            SCALE1(PADDING + BUTTON_PADDING),
            SCALE1(PADDING + ((i - state->list_state->first_visible + has_top_margin) * PILL_SIZE) + 4),
            text->w,
            text->h};
        SDL_BlitSurface(text, NULL, screen, &pos);
        SDL_FreeSurface(text);
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
        exit(130);
    }
    else
    {
        exit(1);
    }
}

// parse_arguments parses the arguments using argp and updates the app state
// supports the following flags:
// - --confirm-button <button> (default: "A")
// - --confirm-text <text> (default: "SELECT")
// - --cancel-button <button> (default: "B")
// - --cancel-text <text> (default: "BACK")
// - --file <path> (default: empty string)
// - --format <format> (default: "json")
// - --header <title> (default: empty string)
// - --item-key <key> (default: "items")
bool parse_arguments(struct AppState *state, int argc, char *argv[])
{
    struct argp_option options[] = {
        {"confirm-button", 'b', "BUTTON", 0, "Button to display on the Confirm button", 0},
        {"confirm-text", 'c', "TEXT", 0, "Text to display on the Confirm button", 0},
        {"cancel-button", 'B', "BUTTON", 0, "Button to display on the Cancel button", 0},
        {"cancel-text", 'C', "TEXT", 0, "Text to display on the Cancel button", 0},
        {"file", 'f', "FILE", 0, "Path to the JSON file", 0},
        {"format", 'F', "FORMAT", 0, "Format to read the input from", 0},
        {"item-key", 'i', "KEY", 0, "Key to the items array in the JSON file", 0},
        {"header", 'H', "TITLE", 0, "Title to display at the top of the screen", 0},
        {0}};

    struct arguments
    {
        char *cancel_button;
        char *cancel_text;
        char *confirm_button;
        char *confirm_text;
        char *file;
        char *format;
        char *header;
        char *item_key;
    };

    error_t parse_opt(int key, char *arg, struct argp_state *argp_state)
    {
        struct arguments *arguments = argp_state->input;

        switch (key)
        {
        case 'b':
            arguments->confirm_button = arg;
            break;
        case 'B':
            arguments->cancel_button = arg;
            break;
        case 'c':
            arguments->confirm_text = arg;
            break;
        case 'C':
            arguments->cancel_text = arg;
            break;
        case 'f':
            arguments->file = arg;
            break;
        case 'F':
            arguments->format = arg;
            break;
        case 'i':
            arguments->item_key = arg;
            break;
        case 'H':
            arguments->header = arg;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
        }
        return 0;
    }

    struct argp argp = {options, parse_opt, 0, 0};

    struct arguments arguments = {0};
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    // Apply parsed arguments to state
    if (arguments.confirm_button)
    {
        strncpy(state->confirm_button, arguments.confirm_button, sizeof(state->confirm_button) - 1);
    }

    if (arguments.cancel_button)
    {
        strncpy(state->cancel_button, arguments.cancel_button, sizeof(state->cancel_button) - 1);
    }

    if (arguments.confirm_text)
    {
        strncpy(state->confirm_text, arguments.confirm_text, sizeof(state->confirm_text) - 1);
    }

    if (arguments.cancel_text)
    {
        strncpy(state->cancel_text, arguments.cancel_text, sizeof(state->cancel_text) - 1);
    }

    if (arguments.file)
    {
        strncpy(state->file, arguments.file, sizeof(state->file) - 1);
    }

    if (arguments.format)
    {
        strncpy(state->format, arguments.format, sizeof(state->format) - 1);
    }

    if (arguments.header)
    {
        strncpy(state->title, arguments.header, sizeof(state->title) - 1);
    }

    if (arguments.item_key)
    {
        strncpy(state->item_key, arguments.item_key, sizeof(state->item_key) - 1);
    }

    if (strcmp(state->confirm_button, "") == 0)
    {
        strncpy(state->confirm_button, "A", sizeof(state->confirm_button) - 1);
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
    char default_cancel_button[1024] = "B";
    char default_cancel_text[1024] = "BACK";
    char default_confirm_button[1024] = "A";
    char default_confirm_text[1024] = "SELECT";
    char default_file[1024] = "";
    char default_format[1024] = "json";
    char default_item_key[1024] = "";
    char default_title[1024] = "";
    struct AppState state = {
        .exit_code = EXIT_SUCCESS,
        .quitting = 0,
        .redraw = 1,
        .show_brightness_setting = 0,
        .list_state = NULL};

    // assign the default values to the app state
    strncpy(state.cancel_button, default_cancel_button, sizeof(state.cancel_button) - 1);
    strncpy(state.cancel_text, default_cancel_text, sizeof(state.cancel_text) - 1);
    strncpy(state.confirm_button, default_confirm_button, sizeof(state.confirm_button) - 1);
    strncpy(state.confirm_text, default_confirm_text, sizeof(state.confirm_text) - 1);
    strncpy(state.file, default_file, sizeof(state.file) - 1);
    strncpy(state.format, default_format, sizeof(state.format) - 1);
    strncpy(state.item_key, default_item_key, sizeof(state.item_key) - 1);
    strncpy(state.title, default_title, sizeof(state.title) - 1);

    // parse the arguments
    parse_arguments(&state, argc, argv);

    state.list_state = ListState_New(state.file, state.format, state.item_key, state.title);
    if (state.list_state == NULL)
    {
        log_error("Failed to create list state");
        return EXIT_FAILURE;
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

    if (state.exit_code == EXIT_SUCCESS)
    {
        log_info(state.list_state->items[state.list_state->selected].name);
    }

    swallow_stdout_from_function(destruct);

    // exit the program
    return state.exit_code;
}
