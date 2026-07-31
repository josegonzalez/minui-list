#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <msettings.h>
#include <parson/parson.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#ifdef USE_SDL2
#include <SDL2/SDL_ttf.h>
#else
#include <SDL/SDL_ttf.h>
#endif

#include "defines.h"
#include "api.h"
#include "utils.h"

#include "list_filter.h"
#include "list_hint.h"
#include "list_image.h"
#include "list_keyboard.h"
#include "list_nav.h"
#include "list_scroll.h"
#include "list_theme.h"

// the largest image column width is a third of the screen width, per issue #13
#define IMAGE_MAX_WIDTH_DIVISOR 3

// the accent color used to highlight the matched portion of a filtered item's
// name; the greyscale MinUI palette has no accent, so this reads on both the
// white selected-row pill and the dark unselected rows
#define TRIAD_FILTER_HIGHLIGHT 0xf5, 0xc2, 0x42

// Platform compatibility: tg5050 (NextUI) uses PWR_isOnline instead of PLAT_isOnline
#ifdef PLATFORM_NEXTUI
#define PLAT_isOnline PWR_isOnline
#endif

// Theme helpers. The -nextui builds honor the user's NextUI theme colors
// (COLOR_MAIN..COLOR_BACKGROUND, exposed by the SDK as THEME_COLOR* /
// THEME_COLOR*_255 after GFX_init loads the theme), while the MinUI/macOS builds
// keep the greyscale palette. Every NextUI-only symbol (THEME_COLOR*,
// uintToColour, GFX_blitPillDark/Color, RGB_WHITE) is confined to these helpers
// behind PLATFORM_NEXTUI so the other builds compile unchanged. The mapping from
// list row state to a text role lives in list_theme.c (unit tested); these helpers
// turn a role or draw intent into the actual color/blit.

// theme_row_text_color maps a list row's text role to its color. Only the normal
// and selected roles are theme-driven; the muted/disabled greys are shared with
// MinUI (NextUI keeps static greys for those too).
static SDL_Color theme_row_text_color(ListTextRole role)
{
    switch (role)
    {
    case LIST_TEXT_SELECTED:
#ifdef PLATFORM_NEXTUI
        return uintToColour(THEME_COLOR5_255);
#else
        return COLOR_BLACK;
#endif
    case LIST_TEXT_NORMAL:
#ifdef PLATFORM_NEXTUI
        return uintToColour(THEME_COLOR4_255);
#else
        return COLOR_WHITE;
#endif
    case LIST_TEXT_DISABLED:
        return (SDL_Color){TRIAD_DARK_GRAY};
    case LIST_TEXT_SELECTED_DISABLED:
        return (SDL_Color){TRIAD_LIGHT_GRAY};
    case LIST_TEXT_MUTED:
    default:
        return COLOR_LIGHT_TEXT;
    }
}

// theme_title_text_color returns the title color. Over a background image the
// title stays white (drawn on a black pill for legibility over arbitrary art);
// otherwise it follows the theme's list text color.
static SDL_Color theme_title_text_color(bool has_bg_image)
{
    if (has_bg_image)
        return COLOR_WHITE;
#ifdef PLATFORM_NEXTUI
    return uintToColour(THEME_COLOR4_255);
#else
    return COLOR_GRAY;
#endif
}

// theme_accent_u32 returns the filter match-highlight fill color: the theme accent
// under NextUI, the app's gold accent under MinUI.
static uint32_t theme_accent_u32(SDL_Surface *dst)
{
#ifdef PLATFORM_NEXTUI
    (void)dst;
    return THEME_COLOR2;
#else
    return SDL_MapRGB(dst->format, TRIAD_FILTER_HIGHLIGHT);
#endif
}

// theme_accent_text_color returns the text color drawn over the accent highlight.
// There is no theme "text on accent" slot, so the selected-text foreground (the
// theme's readable foreground) is the closest match under NextUI.
static SDL_Color theme_accent_text_color(void)
{
#ifdef PLATFORM_NEXTUI
    return uintToColour(THEME_COLOR5_255);
#else
    return COLOR_BLACK;
#endif
}

// theme_background_u32 returns the default background fill. NextUI honors the
// theme background; MinUI fills black. Explicit per-item background colors/images
// still override this at the call site.
static uint32_t theme_background_u32(SDL_Surface *dst)
{
#ifdef PLATFORM_NEXTUI
    (void)dst;
    return THEME_COLOR7;
#else
    return SDL_MapRGBA(dst->format, 0, 0, 0, 255);
#endif
}

// blit_selected_pill draws the selection pill: tinted with the theme's main color
// under NextUI, the plain white pill asset under MinUI.
static void blit_selected_pill(int asset, SDL_Surface *dst, SDL_Rect *rect)
{
#ifdef PLATFORM_NEXTUI
    GFX_blitPillDark(asset, dst, rect);
#else
    GFX_blitPill(asset, dst, rect);
#endif
}

// blit_value_track_pill draws the darker full-width track behind a selected row's
// option value: tinted with the theme's secondary accent under NextUI.
static void blit_value_track_pill(int asset, SDL_Surface *dst, SDL_Rect *rect)
{
#ifdef PLATFORM_NEXTUI
    GFX_blitPillColor(asset, dst, rect, THEME_COLOR3, RGB_WHITE);
#else
    GFX_blitPill(asset, dst, rect);
#endif
}

// theme_kb_input_bg / theme_kb_input_text color the filter keyboard's input field
// (secondary accent track with list text under NextUI).
static uint32_t theme_kb_input_bg(SDL_Surface *dst)
{
#ifdef PLATFORM_NEXTUI
    (void)dst;
    return THEME_COLOR3;
#else
    return SDL_MapRGB(dst->format, TRIAD_DARK_GRAY);
#endif
}

static SDL_Color theme_kb_input_text(void)
{
#ifdef PLATFORM_NEXTUI
    return uintToColour(THEME_COLOR4_255);
#else
    return COLOR_WHITE;
#endif
}

// theme_kb_key_bg / theme_kb_key_text color a keyboard key. A focused key mirrors
// the selection pill (main + selected text); an unfocused key uses the secondary
// accent track with the list text.
static uint32_t theme_kb_key_bg(SDL_Surface *dst, bool focused)
{
#ifdef PLATFORM_NEXTUI
    (void)dst;
    return focused ? THEME_COLOR1 : THEME_COLOR3;
#else
    return focused ? SDL_MapRGB(dst->format, TRIAD_WHITE)
                   : SDL_MapRGB(dst->format, TRIAD_DARK_GRAY);
#endif
}

static SDL_Color theme_kb_key_text(bool focused)
{
#ifdef PLATFORM_NEXTUI
    return focused ? uintToColour(THEME_COLOR5_255) : uintToColour(THEME_COLOR4_255);
#else
    return focused ? COLOR_BLACK : COLOR_WHITE;
#endif
}

SDL_Surface *screen = NULL;

enum list_result_t
{
    ExitCodeSuccess = 0,
    ExitCodeError = 1,
    ExitCodeCancelButton = 2,
    ExitCodeMenuButton = 3,
    ExitCodeActionButton = 4,
    ExitCodeInactionButton = 5,
    ExitCodeStartButton = 6,
    ExitCodeParseError = 10,
    ExitCodeSerializeError = 11,
    ExitCodeTimeout = 124,
    ExitCodeKeyboardInterrupt = 130,
    ExitCodeSigterm = 143,
};
typedef int ExitCode;

#define OPTION_PADDING 8

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

struct ListItemFeature
{
    // the background color to use for the list
    char background_color[1024];
    // path to the background image to use for the list
    char background_image[1024];
    // whether the background image exists
    bool background_image_exists;
    // whether the item can be disabled
    bool can_disable;
    // the confirm text to display on the confirm button
    char confirm_text[1024];
    // whether the item is disabled
    bool disabled;
    // whether to draw arrows around the item
    bool draw_arrows;
    // whether to hide the action button when the item is selected
    bool hide_action;
    // whether to hide the cancel button when the item is selected
    bool hide_cancel;
    // whether to hide the confirm button when the item is selected
    bool hide_confirm;
    // whether to show the confirm button when the default option is selected
    bool show_confirm;
    // whether or not the item is a header
    bool is_header;
    // whether or not the item is unselectable
    bool unselectable;
    // whether the item stays visible even when it does not match an active filter
    bool display_on_filter;
    // alignment of the item text ('left', 'center', 'right')
    char alignment[1024];

    // whether the item has a background_color field
    bool has_background_color;
    // whether the item has a background_image field
    bool has_background_image;
    // whether the item has a can_disable field
    bool has_can_disable;
    // whether the item has a confirm_text field
    bool has_confirm_text;
    // whether the item has a disabled field
    bool has_disabled;
    // whether the item has a draw_arrows field
    bool has_draw_arrows;
    // whether the item has a hide_action field
    bool has_hide_action;
    // whether the item has a hide_cancel field
    bool has_hide_cancel;
    // whether the item has a hide_confirm field
    bool has_hide_confirm;
    // whether the item has a show_confirm field
    bool has_show_confirm;
    // whether the item has a is_header field
    bool has_is_header;
    // whether the item has a unselectable field
    bool has_unselectable;
    // whether the item has a display_on_filter field
    bool has_display_on_filter;
    // whether the item has a alignment field
    bool has_alignment;
};

// ListItem holds the configuration for a list item
struct ListItem
{
    // the name of the item
    char *name;
    // whether the item has features
    bool has_features;
    // whether the item has options field
    bool has_options;
    // whether the item has a selected field
    bool has_selected;
    // the number of options for the item
    int option_count;
    // a list of char options for the item
    char **options;
    // the selected option index
    int selected;
    // the initial selected option index
    int initial_selected;
    // the features of the item
    struct ListItemFeature features;

    // whether the item specifies a right-hand-side image (image/images, in the
    // item object or under features)
    bool has_image;
    // the per-resolution image variants (heap array sized to image_variant_count)
    struct ImageVariant *image_variants;
    // the number of image variants
    int image_variant_count;

    // the image path selected for the active screen resolution, fixed for the
    // run (empty when no variant matches and there is no "default")
    char resolved_path[1024];
    // the path image_surface was loaded from (empty when nothing is cached)
    char image_active_path[1024];
    // the cached, pre-scaled image surface (NULL when nothing is loaded)
    SDL_Surface *image_surface;
};

// ListState holds the state of the list
struct ListState
{
    // array of list items
    struct ListItem *items;
    // number of items in the list
    size_t item_count;

    // the filtered view: visible[k] is the source index of the k-th visible item.
    // With no active filter this is the identity (visible[k] == k) and
    // visible_count == item_count, so first_visible/last_visible/selected below
    // behave exactly as they did before filtering existed. The indices below are
    // positions into visible[], not raw source indices.
    int *visible;
    // number of currently visible items (length of the meaningful prefix of visible)
    int visible_count;

    // rendering state
    // display position of the first visible row
    int first_visible;
    // display position one past the last visible row
    int last_visible;
    // display position of the currently selected item (-1 when nothing matches)
    int selected;

    // whether or not any items in the list have options
    bool has_options;
};

// Fonts holds the fonts for the list
struct Fonts
{
    // the large font to use for the list
    TTF_Font *large;
    // the medium font to use for the list
    TTF_Font *medium;
    // the path to the default font to use for the list
    char *default_font;
    // the path to the large font to use for the list
    char *large_font;
    // the path to the medium font to use for the list
    char *medium_font;
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
    // whether to show the hardware group
    int show_hardware_group;
    // which hardware settings overlay to show, matching the MinUI SDK
    // convention written back by PWR_update: 0 = none, 1 = brightness, 2 = volume
    int show_brightness_setting;
    // the button to display on the Action button
    char action_button[1024];
    // the text to display on the Action button
    char action_text[1024];
    // the background image to display
    char background_image[1024];
    // the background color to display
    char background_color[1024];
    // the screen resolution ("WIDTHxHEIGHT") used to pick per-item images from
    // an "images" map; empty means auto-detect from the device resolution
    char screen_resolution[32];
    // a full path to a fallback image shown when an item that specifies an image
    // has no currently-existing resolved file; empty means no fallback
    char fallback_image[1024];
    // the button to display on the Confirm button
    char confirm_button[1024];
    // the text to display on the Confirm button
    char confirm_text[1024];
    // the button to display on the Cancel button
    char cancel_button[1024];
    // the text to display on the Cancel button
    char cancel_text[1024];
    // whether to always display confirm button
    bool always_show_confirm;
    // whether to disable sleep
    bool disable_auto_sleep;
    // whether alphabetic scroll (L1/R1 letter jumping) is enabled
    bool alphabetic_scroll;
    // whether the inline filter keyboard feature is allowed at all
    bool allow_filter;
    // the button that toggles the filter keyboard (e.g. "SELECT", "L1", "R1")
    char filter_button[1024];
    // whether the filter keyboard is shown at startup
    bool display_filter_keyboard;
    // the initial filter text (from --filter-input)
    char filter_input[1024];
    // a file path to also write the filter value to on exit (empty = none)
    char filter_text_file[1024];
    // whether the filter keyboard is currently shown
    bool filter_keyboard_active;
    // the current filter text being typed / applied
    char filter_text[1024];
    // the filter keyboard cursor (row/col/layout)
    struct KeyboardCursor filter_cursor;
    // the row count saved before the keyboard shrank the list (restored on close)
    int saved_max_row_count;
    // how to autoscroll over-long selected item text ('false', 'wrap', 'pong')
    char scroll_method[1024];
    // timestamp (ms) when the current row's autoscroll animation started
    uint32_t scroll_anim_start_ms;
    // the selected index the autoscroll clock is tracking (-1 = none yet)
    int scroll_anim_selected;
    // the selected option index the autoscroll clock is tracking (-1 = none yet)
    int scroll_anim_option;
    // whether a row is currently autoscrolling (drives per-frame redraws)
    bool scroll_active;
    // maximum number of visible list rows
    int max_row_count;
    // the button to display on the Enable button
    char enable_button[1024];
    // the path to the JSON file
    char file[1024];
    // the format to read the input from
    char format[1024];
    // the key to the items array in the JSON file
    char item_key[1024];
    // the title of the list page
    char title[1024];
    // the title alignment ('left', 'center', 'right')
    char title_alignment[1024];
    // the location to write the value to
    char write_location[1024];
    // the value to write (selected, state)
    char write_value[1024];
    // the fonts to use for the list
    struct Fonts fonts;
    // the initially selected item index (-1 = not set)
    int initial_selected;
    // the state of the list
    struct ListState *list_state;
};

bool has_left_button_group(struct AppState *app_state, struct ListState *list_state)
{
    bool is_action_hidden = false;
    bool is_enable_hidden = false;

    // nothing is selected (e.g. an active filter matched no items)
    if (list_state->selected < 0)
    {
        return false;
    }

    if (strcmp(app_state->action_button, "") == 0 || list_state->items[list_state->visible[list_state->selected]].features.hide_action)
    {
        is_action_hidden = true;
    }

    if (strcmp(app_state->enable_button, "") == 0 || !list_state->items[list_state->visible[list_state->selected]].features.can_disable)
    {
        is_enable_hidden = true;
    }

    if (is_action_hidden && is_enable_hidden)
    {
        return false;
    }

    return true;
}

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

// compare_items_alphabetic compares ListItem structs by name (case-insensitive)
static int compare_items_alphabetic(const void *a, const void *b)
{
    const struct ListItem *item_a = (const struct ListItem *)a;
    const struct ListItem *item_b = (const struct ListItem *)b;
    return strcasecmp(item_a->name, item_b->name);
}

// validate_features_images checks the optional "images" map on an item's
// "features" object: it must be an object whose values are all strings. Keys are
// left unvalidated so new resolution strings stay forward-compatible. Writes a
// human-readable message into err and returns false on the first problem;
// returns true when the features object is NULL or the key is absent or
// well-formed.
static bool validate_features_images(JSON_Object *features, const char *item_desc, char *err, size_t err_size)
{
    if (features == NULL)
        return true;

    JSON_Value *images_value = json_object_get_value(features, "images");
    if (images_value == NULL)
        return true;

    if (json_value_get_type(images_value) != JSONObject)
    {
        snprintf(err, err_size, "%s features.images must be an object mapping resolution to path", item_desc);
        return false;
    }

    JSON_Object *images = json_value_get_object(images_value);
    size_t images_count = json_object_get_count(images);
    for (size_t k = 0; k < images_count; k++)
    {
        if (json_value_get_type(json_object_get_value_at(images, k)) != JSONString)
        {
            const char *key = json_object_get_name(images, k);
            snprintf(err, err_size, "%s features.images entry '%s' must be a string", item_desc, key ? key : "");
            return false;
        }
    }

    return true;
}

// ListItem_InitImage resets a list item's per-item image fields to their empty
// defaults. Called from every item-construction branch so items that do not use
// images (text format, string arrays, objects without image keys) are safe to
// render and free.
static void ListItem_InitImage(struct ListItem *item)
{
    item->has_image = false;
    item->image_variants = NULL;
    item->image_variant_count = 0;
    item->resolved_path[0] = '\0';
    item->image_active_path[0] = '\0';
    item->image_surface = NULL;
}

// ListItem_UpsertVariant sets or replaces the path for a resolution key in the
// item's variant array, growing it as needed (mirroring the options heap array).
// Empty resolutions or paths are ignored, so callers can pass optional keys
// directly.
static void ListItem_UpsertVariant(struct ListItem *item, const char *resolution, const char *path)
{
    if (resolution == NULL || resolution[0] == '\0' || path == NULL || path[0] == '\0')
        return;

    for (int k = 0; k < item->image_variant_count; k++)
    {
        if (strcmp(item->image_variants[k].resolution, resolution) == 0)
        {
            strncpy(item->image_variants[k].path, path, sizeof(item->image_variants[k].path) - 1);
            item->image_variants[k].path[sizeof(item->image_variants[k].path) - 1] = '\0';
            return;
        }
    }

    struct ImageVariant *grown = realloc(item->image_variants, sizeof(struct ImageVariant) * (item->image_variant_count + 1));
    if (grown == NULL)
        return;
    item->image_variants = grown;

    struct ImageVariant *variant = &item->image_variants[item->image_variant_count];
    memset(variant, 0, sizeof(*variant));
    strncpy(variant->resolution, resolution, sizeof(variant->resolution) - 1);
    strncpy(variant->path, path, sizeof(variant->path) - 1);
    item->image_variant_count++;
}

// ListItem_ReadImages reads the optional "images" map from an item's "features"
// object into the item's variant array. Keys are resolution strings ("default"
// or "WIDTHxHEIGHT"); the "default" entry is used when no exact match is found.
static void ListItem_ReadImages(struct ListItem *item, JSON_Object *features)
{
    if (features == NULL)
        return;

    JSON_Object *images = json_object_get_object(features, "images");
    if (images == NULL)
        return;

    size_t images_count = json_object_get_count(images);
    for (size_t k = 0; k < images_count; k++)
    {
        const char *resolution = json_object_get_name(images, k);
        const char *path = json_value_get_string(json_object_get_value_at(images, k));
        ListItem_UpsertVariant(item, resolution, path);
    }
}

// ListState_New creates a new ListState from a JSON file
// ListState_InitVisibleIdentity allocates the filtered-view index and sets it to
// the identity mapping (every item visible, in source order) so an unfiltered
// list behaves exactly as it did before filtering existed.
static void ListState_InitVisibleIdentity(struct ListState *state)
{
    size_t n = state->item_count > 0 ? state->item_count : 1;
    state->visible = malloc(sizeof(int) * n);
    state->visible_count = (int)state->item_count;
    for (size_t i = 0; i < state->item_count; i++)
    {
        state->visible[i] = (int)i;
    }
}

struct ListState *ListState_New(const char *filename, const char *format, const char *item_key, const char *confirm_text, const char *default_background_image, const char *default_background_color, struct AppState *app_state)
{
    struct ListState *state = malloc(sizeof(struct ListState));
    state->selected = -1;
    state->first_visible = 0;
    state->last_visible = 0;
    state->visible = NULL;
    state->visible_count = 0;

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
                state->items[item_index].has_features = false;
                state->items[item_index].has_options = false;
                state->items[item_index].has_selected = false;
                state->items[item_index].option_count = 0;
                state->items[item_index].options = NULL;
                state->items[item_index].selected = 0;
                state->items[item_index].initial_selected = 0;
                ListItem_InitImage(&state->items[item_index]);
                state->items[item_index].features = (struct ListItemFeature){
                    .background_color = "",
                    .background_image = "",
                    .background_image_exists = false,
                    .can_disable = false,
                    .confirm_text = "",
                    .disabled = false,
                    .draw_arrows = false,
                    .hide_action = false,
                    .hide_cancel = false,
                    .hide_confirm = false,
                    .show_confirm = false,
                    .is_header = false,
                    .unselectable = false,
                    .display_on_filter = false,
                    .alignment = "",
                    .has_background_color = false,
                    .has_background_image = false,
                    .has_can_disable = false,
                    .has_confirm_text = false,
                    .has_draw_arrows = false,
                    .has_disabled = false,
                    .has_hide_action = false,
                    .has_hide_cancel = false,
                    .has_hide_confirm = false,
                    .has_show_confirm = false,
                    .has_is_header = false,
                    .has_unselectable = false,
                    .has_display_on_filter = false,
                    .has_alignment = false,
                };
                strncpy(state->items[item_index].features.alignment, "left", sizeof(state->items[item_index].features.alignment) - 1);
                strncpy(state->items[item_index].features.confirm_text, confirm_text, sizeof(state->items[item_index].features.confirm_text) - 1);
                if (default_background_image != NULL)
                {
                    strncpy(state->items[item_index].features.background_image, default_background_image, sizeof(state->items[item_index].features.background_image) - 1);
                    if (access(default_background_image, F_OK) != -1)
                    {
                        state->items[item_index].features.background_image_exists = true;
                    }
                }
                if (default_background_color != NULL)
                {
                    strncpy(state->items[item_index].features.background_color, default_background_color, sizeof(state->items[item_index].features.background_color) - 1);
                }

                item_index++;
            }

            if (*line_end == '\0')
            {
                break;
            }
            line_start = line_end + 1;
        }

        free(contents);
        ListState_InitVisibleIdentity(state);
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

    // Check for alphabetic_scroll in root JSON object
    JSON_Object *root_object = json_value_get_object(root_value);
    if (root_object != NULL && json_object_has_value(root_object, "alphabetic_scroll"))
    {
        if (json_object_get_boolean(root_object, "alphabetic_scroll") == 1)
        {
            app_state->alphabetic_scroll = true;
        }
    }

    // Check for scroll_method in root JSON object. When present it takes
    // precedence over the --scroll-method flag. An unrecognized value is not a
    // hard error: ScrollMethod_Parse maps it to SCROLL_NONE at render time.
    if (root_object != NULL && json_object_has_value(root_object, "scroll_method"))
    {
        const char *scroll_method = json_object_get_string(root_object, "scroll_method");
        if (scroll_method != NULL)
        {
            strncpy(app_state->scroll_method, scroll_method, sizeof(app_state->scroll_method) - 1);
            app_state->scroll_method[sizeof(app_state->scroll_method) - 1] = '\0';
        }
    }

    // decide array form vs object form from the root JSON type, not from whether
    // --item-key was supplied. a root object is the object form (items live under
    // item_key, "items" by default); a root array is the array-of-strings form and
    // ignores item_key.
    bool use_object_form = json_value_get_type(root_value) == JSONObject;

    JSON_Array *items_array;
    if (use_object_form)
    {
        items_array = json_value_get_array(json_object_get_value(json_value_get_object(root_value), item_key));
    }
    else
    {
        items_array = json_value_get_array(root_value);
    }

    size_t item_count = json_array_get_count(items_array);

    // validate each element's shape before allocating or rendering.
    // invalid input (objects in a top-level array, or non-object/nameless
    // items under an item key) would otherwise yield empty item names that
    // crash the renderer, so fail here the way other malformed input does.
    for (size_t i = 0; i < item_count; i++)
    {
        JSON_Value *element = json_array_get_value(items_array, i);
        char error_message[256];
        error_message[0] = '\0';

        if (!use_object_form)
        {
            if (json_value_get_type(element) != JSONString)
            {
                snprintf(error_message, sizeof(error_message), "Array item %zu is not a string; use --item-key for object lists", i);
            }
            else
            {
                const char *element_name = json_value_get_string(element);
                if (element_name == NULL || element_name[0] == '\0')
                {
                    snprintf(error_message, sizeof(error_message), "Array item %zu has an empty name", i);
                }
            }
        }
        else
        {
            if (json_value_get_type(element) != JSONObject)
            {
                snprintf(error_message, sizeof(error_message), "Item %zu under key '%s' is not an object", i, item_key);
            }
            else
            {
                JSON_Object *element_object = json_value_get_object(element);
                const char *element_name = json_object_get_string(element_object, "name");
                if (element_name == NULL || element_name[0] == '\0')
                {
                    snprintf(error_message, sizeof(error_message), "Item %zu under key '%s' is missing a name", i, item_key);
                }
                else
                {
                    char item_desc[160];
                    snprintf(item_desc, sizeof(item_desc), "Item %zu under key '%s'", i, item_key);
                    validate_features_images(json_object_get_object(element_object, "features"), item_desc, error_message, sizeof(error_message));
                }
            }
        }

        if (error_message[0] != '\0')
        {
            log_error(error_message);
            json_value_free(root_value);
            free(state);
            return NULL;
        }
    }

    state->items = malloc(sizeof(struct ListItem) * item_count);
    state->has_options = false;

    if (!use_object_form)
    {
        for (size_t i = 0; i < item_count; i++)
        {
            const char *name = json_array_get_string(items_array, i);
            state->items[i].name = name ? strdup(name) : "";

            // set defaults for the other fields
            state->items[i].has_features = false;
            state->items[i].has_options = false;
            state->items[i].has_selected = false;
            state->items[i].option_count = 0;
            state->items[i].options = NULL;
            state->items[i].selected = 0;
            state->items[i].initial_selected = 0;
            ListItem_InitImage(&state->items[i]);
            state->items[i].features = (struct ListItemFeature){
                .background_color = "",
                .background_image = "",
                .background_image_exists = false,
                .can_disable = false,
                .confirm_text = "",
                .disabled = false,
                .draw_arrows = false,
                .hide_action = false,
                .hide_cancel = false,
                .hide_confirm = false,
                .show_confirm = false,
                .is_header = false,
                .unselectable = false,
                .display_on_filter = false,
                .alignment = "",
                .has_background_color = false,
                .has_background_image = false,
                .has_can_disable = false,
                .has_confirm_text = false,
                .has_disabled = false,
                .has_draw_arrows = false,
                .has_hide_action = false,
                .has_hide_cancel = false,
                .has_hide_confirm = false,
                .has_show_confirm = false,
                .has_is_header = false,
                .has_unselectable = false,
                .has_display_on_filter = false,
                .has_alignment = false,
            };
            strncpy(state->items[i].features.alignment, "left", sizeof(state->items[i].features.alignment) - 1);
            strncpy(state->items[i].features.confirm_text, confirm_text, sizeof(state->items[i].features.confirm_text) - 1);
            if (default_background_image != NULL)
            {
                strncpy(state->items[i].features.background_image, default_background_image, sizeof(state->items[i].features.background_image) - 1);
                if (access(default_background_image, F_OK) != -1)
                {
                    state->items[i].features.background_image_exists = true;
                }
            }
            if (default_background_color != NULL)
            {
                strncpy(state->items[i].features.background_color, default_background_color, sizeof(state->items[i].features.background_color) - 1);
            }
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
                state->items[i].has_options = true;
            }
            else
            {
                state->items[i].has_options = false;
            }

            // read in the current option index from the json object
            // if there is no current option index, set it to 0
            // if there is a current option index, treat it as an integer
            if (json_object_has_value(item, "selected"))
            {
                state->items[i].selected = json_object_get_number(item, "selected");
                if (state->items[i].selected < 0)
                {
                    char error_message[256];
                    snprintf(error_message, sizeof(error_message), "Item %s has a selected option index of %d, which is less than 0. Setting to 0.", state->items[i].name, state->items[i].selected);
                    log_error(error_message);
                    state->items[i].selected = 0;
                }
                if (state->items[i].selected >= options_count)
                {
                    char error_message[256];
                    snprintf(error_message, sizeof(error_message), "Item %s has a selected option index of %d, which is greater than the number of options %zu. Setting to last option.", state->items[i].name, state->items[i].selected, options_count);
                    log_error(error_message);
                    state->items[i].selected = options_count - 1;
                    if (state->items[i].selected < 0)
                    {
                        state->items[i].selected = 0;
                    }
                }
                state->items[i].has_selected = true;
            }
            else
            {
                state->items[i].selected = 0;
                state->items[i].has_selected = false;
            }

            state->items[i].initial_selected = state->items[i].selected;
            ListItem_InitImage(&state->items[i]);

            state->items[i].features = (struct ListItemFeature){
                .background_color = "",
                .background_image = "",
                .background_image_exists = false,
                .can_disable = false,
                .confirm_text = "",
                .disabled = false,
                .draw_arrows = false,
                .hide_action = false,
                .hide_cancel = false,
                .hide_confirm = false,
                .show_confirm = false,
                .is_header = false,
                .unselectable = false,
                .display_on_filter = false,
                .alignment = "",
                .has_background_color = false,
                .has_background_image = false,
                .has_can_disable = false,
                .has_confirm_text = false,
                .has_disabled = false,
                .has_draw_arrows = false,
                .has_hide_action = false,
                .has_hide_cancel = false,
                .has_hide_confirm = false,
                .has_show_confirm = false,
                .has_is_header = false,
                .has_unselectable = false,
                .has_display_on_filter = false,
                .has_alignment = false,
            };
            strncpy(state->items[i].features.alignment, "left", sizeof(state->items[i].features.alignment) - 1);
            strncpy(state->items[i].features.confirm_text, confirm_text, sizeof(state->items[i].features.confirm_text) - 1);
            state->items[i].has_features = false;
            if (json_object_has_value(item, "features"))
            {
                state->items[i].has_features = true;
                JSON_Object *features = json_object_get_object(item, "features");

                // read in the background_image from the json object
                // if there is no background_image, set it to ""
                // if there is a background_image, treat it as a string
                const char *background_image = json_object_get_string(features, "background_image");
                if (background_image != NULL)
                {
                    strncpy(state->items[i].features.background_image, background_image, sizeof(state->items[i].features.background_image) - 1);
                    if (access(background_image, F_OK) != -1)
                    {
                        state->items[i].features.background_image_exists = true;
                    }
                    state->items[i].features.has_background_image = true;
                }
                else
                {
                    if (default_background_image != NULL)
                    {
                        strncpy(state->items[i].features.background_image, default_background_image, sizeof(state->items[i].features.background_image) - 1);
                        if (access(default_background_image, F_OK) != -1)
                        {
                            state->items[i].features.background_image_exists = true;
                        }
                        state->items[i].features.has_background_image = true;
                    }
                    else
                    {
                        state->items[i].features.has_background_image = false;
                    }
                }

                // read in the background_color from the json object
                // if there is no background_color, set it to ""
                // if there is a background_color, treat it as a string
                const char *background_color = json_object_get_string(features, "background_color");
                if (background_color != NULL)
                {
                    strncpy(state->items[i].features.background_color, background_color, sizeof(state->items[i].features.background_color) - 1);
                    state->items[i].features.has_background_color = true;
                }
                else
                {
                    if (default_background_color != NULL)
                    {
                        strncpy(state->items[i].features.background_color, default_background_color, sizeof(state->items[i].features.background_color) - 1);
                        state->items[i].features.has_background_color = true;
                    }
                    else
                    {
                        state->items[i].features.has_background_color = false;
                    }
                }

                // read in the can_disable from the json object
                // if there is no can_disable, set it to false
                // if there is a can_disable, treat it as a boolean
                if (json_object_get_boolean(features, "can_disable") == 1)
                {
                    state->items[i].features.can_disable = true;
                    state->items[i].features.has_can_disable = true;
                }
                else if (json_object_get_boolean(features, "can_disable") == 0)
                {
                    state->items[i].features.can_disable = false;
                    state->items[i].features.has_can_disable = true;
                }
                else
                {
                    state->items[i].features.can_disable = false;
                    state->items[i].features.has_can_disable = false;
                }

                // read in the disabled from the json object
                // if there is no disabled, set it to false
                // if there is an disabled, treat it as a boolean
                if (json_object_get_boolean(features, "disabled") == 1)
                {
                    state->items[i].features.disabled = true;
                    state->items[i].features.has_disabled = true;
                }
                else if (json_object_get_boolean(features, "disabled") == 0)
                {
                    state->items[i].features.disabled = false;
                    state->items[i].features.has_disabled = true;
                    if (!state->items[i].features.can_disable)
                    {
                        char error_message[256];
                        snprintf(error_message, sizeof(error_message), "Item %s has no can_disable, but is disabled", state->items[i].name);
                        log_error(error_message);
                    }
                }
                else
                {
                    state->items[i].features.disabled = false;
                    state->items[i].features.has_disabled = false;
                }

                // read in the draw_arrows from the json object
                // if there is no draw_arrows, set it to false
                // if there is a draw_arrows, treat it as a boolean
                if (json_object_get_boolean(features, "draw_arrows") == 1)
                {
                    state->items[i].features.draw_arrows = true;
                    state->items[i].features.has_draw_arrows = true;
                }
                else if (json_object_get_boolean(features, "draw_arrows") == 0)
                {
                    state->items[i].features.draw_arrows = false;
                    state->items[i].features.has_draw_arrows = true;
                }
                else
                {
                    state->items[i].features.draw_arrows = false;
                    state->items[i].features.has_draw_arrows = false;
                }

                // read in the hide_action from the json object
                // if there is no hide_action, set it to false
                // if there is a hide_action, treat it as a boolean
                if (json_object_get_boolean(features, "hide_action") == 1)
                {
                    state->items[i].features.hide_action = true;
                    state->items[i].features.has_hide_action = true;
                }
                else if (json_object_get_boolean(features, "hide_action") == 0)
                {
                    state->items[i].features.hide_action = false;
                    state->items[i].features.has_hide_action = true;
                }
                else
                {
                    state->items[i].features.hide_action = false;
                    state->items[i].features.has_hide_action = false;
                }

                // read in the hide_cancel from the json object
                // if there is no hide_cancel, set it to false
                // if there is a hide_cancel, treat it as a boolean
                if (json_object_get_boolean(features, "hide_cancel") == 1)
                {
                    state->items[i].features.hide_cancel = true;
                    state->items[i].features.has_hide_cancel = true;
                }
                else if (json_object_get_boolean(features, "hide_cancel") == 0)
                {
                    state->items[i].features.hide_cancel = false;
                    state->items[i].features.has_hide_cancel = true;
                }
                else
                {
                    state->items[i].features.hide_cancel = false;
                    state->items[i].features.has_hide_cancel = false;
                }

                // read in the hide_confirm from the json object
                // if there is no hide_confirm, set it to false
                // if there is a hide_confirm, treat it as a boolean
                if (json_object_get_boolean(features, "hide_confirm") == 1)
                {
                    state->items[i].features.hide_confirm = true;
                    state->items[i].features.has_hide_confirm = true;
                }
                else if (json_object_get_boolean(features, "hide_confirm") == 0)
                {
                    state->items[i].features.hide_confirm = false;
                    state->items[i].features.has_hide_confirm = true;
                }
                else
                {
                    state->items[i].features.hide_confirm = false;
                    state->items[i].features.has_hide_confirm = false;
                }

                // read in the show_confirm from the json object
                // if there is no show_confirm, set it to false
                // if there is a show_confirm, treat it as a boolean
                if (json_object_get_boolean(features, "show_confirm") == 1)
                {
                    state->items[i].features.show_confirm = true;
                    state->items[i].features.has_show_confirm = true;
                }
                else if (json_object_get_boolean(features, "show_confirm") == 0)
                {
                    state->items[i].features.show_confirm = false;
                    state->items[i].features.has_show_confirm = true;
                }
                else
                {
                    state->items[i].features.show_confirm = false;
                    state->items[i].features.has_show_confirm = false;
                }

                // read in the unselectable from the json object
                // if there is no unselectable, set it to false
                // if there is a unselectable, treat it as a boolean
                if (json_object_get_boolean(features, "unselectable") == 1)
                {
                    state->items[i].features.unselectable = true;
                    state->items[i].features.has_unselectable = true;
                }
                else if (json_object_get_boolean(features, "unselectable") == 0)
                {
                    state->items[i].features.unselectable = false;
                    state->items[i].features.has_unselectable = true;
                }
                else
                {
                    state->items[i].features.unselectable = false;
                    state->items[i].features.has_unselectable = false;
                }

                // read in the is_header from the json object
                // if there is no is_header, set it to false
                // if there is a is_header, treat it as a boolean
                // headers are not selectable, so this has to go last such that we can set the unselectable flag
                if (json_object_get_boolean(features, "is_header") == 1)
                {
                    state->items[i].features.is_header = true;
                    state->items[i].features.has_is_header = true;
                    state->items[i].features.unselectable = true;
                }
                else if (json_object_get_boolean(features, "is_header") == 0)
                {
                    state->items[i].features.is_header = false;
                    state->items[i].features.has_is_header = true;
                }
                else
                {
                    state->items[i].features.is_header = false;
                    state->items[i].features.has_is_header = false;
                }

                // read in the display_on_filter from the json object
                // if there is no display_on_filter, set it to false
                // if there is a display_on_filter, treat it as a boolean
                if (json_object_get_boolean(features, "display_on_filter") == 1)
                {
                    state->items[i].features.display_on_filter = true;
                    state->items[i].features.has_display_on_filter = true;
                }
                else if (json_object_get_boolean(features, "display_on_filter") == 0)
                {
                    state->items[i].features.display_on_filter = false;
                    state->items[i].features.has_display_on_filter = true;
                }
                else
                {
                    state->items[i].features.display_on_filter = false;
                    state->items[i].features.has_display_on_filter = false;
                }

                // read in the alignment from the json object
                // if there is no alignment, set it to 'left'
                // if there is a alignment, it should be 'left', 'center', or 'right'
                const char *alignment = json_object_get_string(features, "alignment");
                if (alignment != NULL)
                {
                    if (strcmp(alignment, "left") == 0 || strcmp(alignment, "center") == 0 || strcmp(alignment, "right") == 0)
                    {
                        strncpy(state->items[i].features.alignment, alignment, sizeof(state->items[i].features.alignment) - 1);
                        state->items[i].features.has_alignment = true;
                    }
                    else
                    {
                        char error_message[256];
                        snprintf(error_message, sizeof(error_message), "Item %s has invalid alignment %s. Must be 'left', 'center', or 'right'. Using default (left).", state->items[i].name, alignment);
                        log_error(error_message);
                        strncpy(state->items[i].features.alignment, "left", sizeof(state->items[i].features.alignment) - 1);
                        state->items[i].features.has_alignment = false;
                    }
                }
                else
                {
                    strncpy(state->items[i].features.alignment, "left", sizeof(state->items[i].features.alignment) - 1);
                    state->items[i].features.has_alignment = false;
                }

                // read in the alignment from the json object
                // if there is no alignment, set it to 'left'
                // if there is a alignment, it should be 'left', 'center', or 'right'
                const char *confirm_text = json_object_get_string(features, "confirm_text");
                if (confirm_text != NULL)
                {
                    if (strlen(confirm_text) > 0)
                    {
                        strncpy(state->items[i].features.confirm_text, confirm_text, sizeof(state->items[i].features.confirm_text) - 1);
                        state->items[i].features.has_confirm_text = true;
                    }
                }
            }
            else
            {
                if (default_background_image != NULL)
                {
                    strncpy(state->items[i].features.background_image, default_background_image, sizeof(state->items[i].features.background_image) - 1);
                    if (access(default_background_image, F_OK) != -1)
                    {
                        state->items[i].features.background_image_exists = true;
                    }
                    state->items[i].features.has_background_image = true;
                }
                else
                {
                    state->items[i].features.has_background_image = false;
                }

                if (default_background_color != NULL)
                {
                    strncpy(state->items[i].features.background_color, default_background_color, sizeof(state->items[i].features.background_color) - 1);
                    state->items[i].features.has_background_color = true;
                }
                else
                {
                    state->items[i].features.has_background_color = false;
                }
            }

            // build the right-hand-side image variants for this item from
            // features.images
            ListItem_ReadImages(&state->items[i], json_object_get_object(item, "features"));
            state->items[i].has_image = state->items[i].image_variant_count > 0;
        }
    }

    state->item_count = item_count;

    // read the requested initial selection from JSON (if any)
    if (use_object_form)
    {
        JSON_Object *root_obj = json_value_get_object(root_value);
        if (root_obj && json_object_has_value(root_obj, "selected"))
        {
            state->selected = (int)json_object_get_number(root_obj, "selected");
        }
    }

    ListState_InitVisibleIdentity(state);

    json_value_free(root_value);
    return state;
}

// ListState_InitView validates selection and computes the initial visible window.
// It operates on display positions (indices into visible[]); with no active
// filter visible[] is the identity, so this behaves exactly as before.
void ListState_InitView(struct ListState *state, int max_row_count)
{
    // validate selection: must point to a selectable, currently-visible item
    int first_valid = -1;
    bool selection_is_valid = false;
    for (int k = 0; k < state->visible_count; k++)
    {
        struct ListItem *item = &state->items[state->visible[k]];
        bool valid_for_selection = !item->features.is_header &&
            !item->features.unselectable;

        if (valid_for_selection)
        {
            if (first_valid < 0)
            {
                first_valid = k;
            }
            if (k == state->selected)
            {
                selection_is_valid = true;
            }
        }
    }

    if (!selection_is_valid)
    {
        state->selected = (first_valid >= 0) ? first_valid : -1;
    }

    // compute the visible window over the display positions
    int count = state->visible_count;
    if (count <= max_row_count)
    {
        state->first_visible = 0;
        state->last_visible = count;
    }
    else
    {
        // try to center the selection (selected is >= 0 here since count > 0
        // and, when nothing selectable matched, first_valid handling above left
        // selected at -1 only when count is small enough for the branch above)
        int anchor = state->selected >= 0 ? state->selected : 0;
        state->first_visible = anchor - max_row_count / 2;
        if (state->first_visible < 0)
        {
            state->first_visible = 0;
        }

        state->last_visible = state->first_visible + max_row_count;
        if (state->last_visible > count)
        {
            state->last_visible = count;
            state->first_visible = state->last_visible - max_row_count;
        }
    }
}

// ListState_ApplyFilter rebuilds the filtered view for the given filter text,
// preserving the current selection when it stays visible (otherwise moving to
// the first selectable visible item, or -1 when nothing matches), and reframes
// the visible window.
void ListState_ApplyFilter(struct ListState *state, const char *filter, int max_row_count)
{
    // remember the currently-selected source item so we can keep it selected
    int prev_source = -1;
    if (state->selected >= 0 && state->selected < state->visible_count)
    {
        prev_source = state->visible[state->selected];
    }

    // rebuild the set of visible source indices
    state->visible_count = 0;
    for (size_t i = 0; i < state->item_count; i++)
    {
        struct ListItem *item = &state->items[i];
        if (ListFilter_ItemVisible(item->features.is_header,
                                   item->features.display_on_filter,
                                   item->name, filter))
        {
            state->visible[state->visible_count++] = (int)i;
        }
    }

    // map the previous selection to its new display position, if still visible
    state->selected = -1;
    if (prev_source >= 0)
    {
        for (int k = 0; k < state->visible_count; k++)
        {
            if (state->visible[k] == prev_source)
            {
                state->selected = k;
                break;
            }
        }
    }

    // validate the selection and reframe the window
    ListState_InitView(state, max_row_count);
}

// ListState_ResolveImages selects each item's right-hand image path for the
// active screen resolution. It must run after display init so FIXED_WIDTH and
// FIXED_HEIGHT reflect the real device. The resolution key is the
// --screen-resolution override when set, otherwise the device's native
// WIDTHxHEIGHT.
void ListState_ResolveImages(struct ListState *state, struct AppState *app_state)
{
    char resolution[32];
    if (app_state->screen_resolution[0] != '\0')
    {
        strncpy(resolution, app_state->screen_resolution, sizeof(resolution) - 1);
        resolution[sizeof(resolution) - 1] = '\0';
    }
    else
    {
        snprintf(resolution, sizeof(resolution), "%dx%d", FIXED_WIDTH, FIXED_HEIGHT);
    }

    for (size_t i = 0; i < state->item_count; i++)
    {
        struct ListItem *item = &state->items[i];
        item->resolved_path[0] = '\0';
        if (!item->has_image)
            continue;

        int idx = ImageVariant_SelectIndex(item->image_variants, item->image_variant_count, resolution);
        if (idx >= 0)
        {
            strncpy(item->resolved_path, item->image_variants[idx].path, sizeof(item->resolved_path) - 1);
            item->resolved_path[sizeof(item->resolved_path) - 1] = '\0';
        }
    }
}

// alphabetic_jump_target builds an SDL-free navigation view of the list and
// returns the index to jump to for an alphabetic (L1/R1) letter jump. `forward`
// selects next-letter (true) or previous-letter (false). Returns the current
// selection unchanged when there is nowhere to jump.
static int alphabetic_jump_target(struct ListState *state, bool forward)
{
    // operate over the currently-visible items so letter jumps respect the
    // active filter; the returned value is a display position into visible[]
    if (state->visible_count == 0 || state->selected < 0)
        return state->selected;

    struct ListNavItem *nav = malloc(state->visible_count * sizeof(*nav));
    if (nav == NULL)
        return state->selected;

    for (int k = 0; k < state->visible_count; k++)
    {
        struct ListItem *item = &state->items[state->visible[k]];
        nav[k].name = item->name;
        nav[k].is_header = item->features.is_header;
        nav[k].unselectable = item->features.unselectable;
    }

    int target = forward
                     ? ListNav_NextLetterIndex(nav, state->visible_count, state->selected)
                     : ListNav_PrevLetterIndex(nav, state->visible_count, state->selected);

    free(nav);
    return target;
}

// image_effective_path is defined alongside the other drawing helpers below but
// is also polled here in handle_input, so it needs a forward declaration.
void image_effective_path(struct ListItem *item, const char *fallback_image, char *out, size_t out_size);

// filter_button_mask is defined with the other argument parsing below, but the
// keyboard input handlers here need it, so it needs a forward declaration.
static int filter_button_mask(const char *name);

// the number of key rows in the filter keyboard (4 character rows + specials)
#define FILTER_KB_DRAW_ROWS LIST_KEYBOARD_ROWS

// FilterKeyboardGeom holds the on-screen geometry of the filter keyboard,
// computed from the medium font so input handling (the list-row budget) and
// drawing agree on where the keyboard sits.
struct FilterKeyboardGeom
{
    int key_size;   // square key edge length
    int col_spacing;
    int row_spacing;
    int input_h;    // input field height
    int input_y;    // input field top
    int grid_y;     // first key-row top
    int block_top;  // top of the whole keyboard block
    int list_rows;  // list rows that fit above the block
};

// filter_keyboard_font returns the font used to draw the keyboard. The small SDK
// font keeps the keyboard compact so more of the filtered list stays visible.
static TTF_Font *filter_keyboard_font(void)
{
    return (font.small != NULL) ? font.small : font.medium;
}

// filter_keyboard_geom computes the keyboard block geometry against the current
// screen surface and keyboard font.
static struct FilterKeyboardGeom filter_keyboard_geom(struct AppState *state)
{
    (void)state;
    struct FilterKeyboardGeom g = {0};
    int w = 0, h = 0;
    TTF_Font *kb_font = filter_keyboard_font();
    if (kb_font != NULL)
    {
        TTF_SizeUTF8(kb_font, "shift", &w, &h);
    }
    if (h <= 0)
    {
        h = SCALE1(FONT_SMALL);
    }

    g.key_size = h + SCALE1(4);
    g.col_spacing = SCALE1(3);
    g.row_spacing = SCALE1(3);
    g.input_h = h + SCALE1(6);

    int bottom_reserved = SCALE1(PADDING + PILL_SIZE); // the bottom button-hint bar
    int grid_h = FILTER_KB_DRAW_ROWS * g.key_size + (FILTER_KB_DRAW_ROWS - 1) * g.row_spacing;
    int block_h = g.input_h + SCALE1(3) + grid_h;

    int screen_h = (screen != NULL) ? screen->h : (FIXED_HEIGHT);
    g.block_top = screen_h - bottom_reserved - block_h - SCALE1(PADDING);
    g.input_y = g.block_top;
    g.grid_y = g.input_y + g.input_h + SCALE1(3);

    int list_top = SCALE1(PADDING);
    int avail = g.block_top - SCALE1(PADDING) - list_top;
    g.list_rows = (avail > 0) ? avail / SCALE1(PILL_SIZE) : 0;
    if (g.list_rows < 1)
    {
        g.list_rows = 1;
    }
    return g;
}

// filter_text_append appends s to the current filter text, guarding the buffer.
static void filter_text_append(struct AppState *state, const char *s)
{
    size_t len = strlen(state->filter_text);
    size_t add = strlen(s);
    if (len + add < sizeof(state->filter_text))
    {
        memcpy(state->filter_text + len, s, add + 1);
    }
}

// apply_filter_text rebuilds the filtered view from the current filter text.
static void apply_filter_text(struct AppState *state)
{
    ListState_ApplyFilter(state->list_state, state->filter_text, state->max_row_count);
}

// open_filter_keyboard shows the keyboard, shrinking the list row budget to the
// space above it (saving the full budget so it can be restored on close).
static void open_filter_keyboard(struct AppState *state)
{
    struct FilterKeyboardGeom g = filter_keyboard_geom(state);
    int rows = g.list_rows;
    if (strlen(state->title) > 0 && rows > 1)
    {
        rows -= 1;
    }
    if (rows > state->max_row_count)
    {
        rows = state->max_row_count;
    }
    if (rows < 1)
    {
        rows = 1;
    }

    state->saved_max_row_count = state->max_row_count;
    state->max_row_count = rows;
    state->filter_keyboard_active = true;
    ListKeyboard_Rescue(&state->filter_cursor);
    ListState_InitView(state->list_state, state->max_row_count);
    state->redraw = 1;
}

// close_filter_keyboard hides the keyboard and restores the full row budget.
static void close_filter_keyboard(struct AppState *state)
{
    state->filter_keyboard_active = false;
    state->max_row_count = state->saved_max_row_count;
    ListState_InitView(state->list_state, state->max_row_count);
    state->redraw = 1;
}

// handle_filter_keyboard_input drives the on-screen keyboard: the d-pad moves the
// cursor, A activates the focused key (shift cycles layout, space types a space,
// enter closes), B backspaces, X clears, the toggle button closes, and MENU quits.
static void handle_filter_keyboard_input(struct AppState *state)
{
    state->redraw = 1;

    if (PAD_justReleased(BTN_MENU))
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeMenuButton;
        return;
    }

    if (PAD_justReleased(filter_button_mask(state->filter_button)))
    {
        close_filter_keyboard(state);
        return;
    }

    if (PAD_justRepeated(BTN_UP))
    {
        ListKeyboard_Move(&state->filter_cursor, LIST_KEYBOARD_UP);
    }
    else if (PAD_justRepeated(BTN_DOWN))
    {
        ListKeyboard_Move(&state->filter_cursor, LIST_KEYBOARD_DOWN);
    }
    else if (PAD_justRepeated(BTN_LEFT))
    {
        ListKeyboard_Move(&state->filter_cursor, LIST_KEYBOARD_LEFT);
    }
    else if (PAD_justRepeated(BTN_RIGHT))
    {
        ListKeyboard_Move(&state->filter_cursor, LIST_KEYBOARD_RIGHT);
    }
    else if (PAD_justReleased(BTN_A))
    {
        const char *key = ListKeyboard_KeyAt(state->filter_cursor.layout,
                                             state->filter_cursor.row,
                                             state->filter_cursor.col);
        if (key[0] != '\0')
        {
            if (strcmp(key, "shift") == 0)
            {
                ListKeyboard_CycleLayout(&state->filter_cursor);
            }
            else if (strcmp(key, "space") == 0)
            {
                filter_text_append(state, " ");
                apply_filter_text(state);
            }
            else if (strcmp(key, "enter") == 0)
            {
                close_filter_keyboard(state);
            }
            else
            {
                filter_text_append(state, key);
                apply_filter_text(state);
            }
        }
    }
    else if (PAD_justReleased(BTN_B))
    {
        size_t len = strlen(state->filter_text);
        if (len > 0)
        {
            // drop the last UTF-8 code point (special characters are multi-byte)
            size_t i = len - 1;
            while (i > 0 && ((unsigned char)state->filter_text[i] & 0xC0) == 0x80)
            {
                i--;
            }
            state->filter_text[i] = '\0';
            apply_filter_text(state);
        }
    }
    else if (PAD_justReleased(BTN_X))
    {
        state->filter_text[0] = '\0';
        apply_filter_text(state);
    }
    else
    {
        state->redraw = 0;
    }
}

// handle_input interprets input events and mutates app state
void handle_input(struct AppState *state)
{
    // do not redraw by default
    state->redraw = 0;

    if (state->list_state->selected >= 0 &&
        !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image_exists && state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image[0] != '\0')
    {
        if (access(state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image, F_OK) != -1)
        {
            state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image_exists = true;
            state->redraw = 1;
        }
    }

    // poll visible items' right-hand images so a missing file that later appears
    // (or a fallback that swaps in or out) forces a redraw; draw_screen then
    // reloads the cached surface from the new effective path
    for (int k = state->list_state->first_visible; k < state->list_state->last_visible; k++)
    {
        struct ListItem *item = &state->list_state->items[state->list_state->visible[k]];
        if (!item->has_image)
            continue;

        char effective[1024];
        image_effective_path(item, state->fallback_image, effective, sizeof(effective));
        if (strcmp(effective, item->image_active_path) != 0)
        {
            state->redraw = 1;
        }
    }

    PAD_poll();

    int max_row_count = state->max_row_count;

    // filter keyboard: when active, route all input to it; otherwise the
    // configured toggle button opens it. Only active when filtering is allowed.
    if (state->allow_filter)
    {
        if (state->filter_keyboard_active)
        {
            handle_filter_keyboard_input(state);
            return;
        }
        if (PAD_justReleased(filter_button_mask(state->filter_button)))
        {
            open_filter_keyboard(state);
            return;
        }
    }

    // with an active filter that matched nothing there is no selected item, so
    // only MENU/quit remains meaningful (the keyboard toggle was handled above)
    if (state->list_state->selected < 0)
    {
        if (PAD_justReleased(BTN_MENU))
        {
            state->redraw = 0;
            state->quitting = 1;
            state->exit_code = ExitCodeMenuButton;
        }
        return;
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

    if (is_action_button_pressed && !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_action)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeActionButton;
        return;
    }

    if (is_cancel_button_pressed && !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_cancel)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeCancelButton;
        return;
    }

    bool force_hide_confirm = false;
    if (!state->always_show_confirm && !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.show_confirm && state->list_state->items[state->list_state->visible[state->list_state->selected]].has_options && state->list_state->items[state->list_state->visible[state->list_state->selected]].initial_selected == state->list_state->items[state->list_state->visible[state->list_state->selected]].selected)
    {
        force_hide_confirm = true;
    }

    if (!state->always_show_confirm && state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_confirm)
    {
        force_hide_confirm = true;
    }

    if (is_confirm_button_pressed && !force_hide_confirm)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeSuccess;
        return;
    }

    // if the enable button is pressed, toggle the enabled state of the currently selected item
    if (is_enable_button_pressed)
    {
        if (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.can_disable)
        {
            state->redraw = 1;
            state->list_state->items[state->list_state->visible[state->list_state->selected]].features.disabled = !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.disabled;
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
            while (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable)
            {
                state->list_state->selected -= 1;
                if (state->list_state->selected < 0)
                {
                    break;
                }
            }

            if (state->list_state->selected < 0)
            {
                state->list_state->selected = state->list_state->visible_count - 1;
                while (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable)
                {
                    state->list_state->selected -= 1;
                }

                int start = state->list_state->visible_count - max_row_count;
                state->list_state->first_visible = (start < 0) ? 0 : start;
                state->list_state->last_visible = state->list_state->visible_count;
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
        if (state->list_state->selected == state->list_state->visible_count - 1 && !PAD_justPressed(BTN_DOWN))
        {
            state->redraw = 0;
        }
        else
        {
            state->list_state->selected += 1;
            while (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable)
            {
                state->list_state->selected += 1;
                if (state->list_state->selected >= state->list_state->visible_count)
                {
                    break;
                }
            }

            if (state->list_state->selected >= state->list_state->visible_count)
            {
                state->list_state->selected = 0;
                while (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable)
                {
                    state->list_state->selected += 1;
                }

                state->list_state->first_visible = 0;
                state->list_state->last_visible = (state->list_state->visible_count < max_row_count) ? state->list_state->visible_count : max_row_count;
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
            if (!state->list_state->items[state->list_state->visible[state->list_state->selected]].features.disabled)
            {
                state->list_state->items[state->list_state->visible[state->list_state->selected]].selected -= 1;
                if (state->list_state->items[state->list_state->visible[state->list_state->selected]].selected < 0)
                {
                    state->list_state->items[state->list_state->visible[state->list_state->selected]].selected = state->list_state->items[state->list_state->visible[state->list_state->selected]].option_count - 1;
                }
            }
        }
        else
        {
            state->list_state->selected -= max_row_count;
            if (state->list_state->selected < 0)
            {
                state->list_state->selected = 0;
            }

            while (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable)
            {
                state->list_state->selected -= 1;
                if (state->list_state->selected < 0)
                {
                    state->list_state->selected = 0;
                    break;
                }
            }

            if (state->list_state->selected == 0)
            {
                while (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable)
                {
                    state->list_state->selected += 1;
                    if (state->list_state->selected >= state->list_state->visible_count)
                    {
                        state->list_state->selected = state->list_state->visible_count - 1;
                        break;
                    }
                }
            }

            if (state->list_state->selected < 0)
            {
                state->list_state->selected = 0;
                state->list_state->first_visible = 0;
                state->list_state->last_visible = (state->list_state->visible_count < max_row_count) ? state->list_state->visible_count : max_row_count;
            }
            else if (state->list_state->selected < state->list_state->first_visible)
            {
                state->list_state->first_visible -= max_row_count;
                if (state->list_state->first_visible < 0)
                {
                    state->list_state->first_visible = 0;
                }
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
            if (!state->list_state->items[state->list_state->visible[state->list_state->selected]].features.disabled)
            {
                state->list_state->items[state->list_state->visible[state->list_state->selected]].selected += 1;
                if (state->list_state->items[state->list_state->visible[state->list_state->selected]].selected >= state->list_state->items[state->list_state->visible[state->list_state->selected]].option_count)
                {
                    state->list_state->items[state->list_state->visible[state->list_state->selected]].selected = 0;
                }
            }
        }
        else
        {
            state->list_state->selected += max_row_count;
            if (state->list_state->selected >= state->list_state->visible_count)
            {
                state->list_state->selected = state->list_state->visible_count - 1;
            }
            while (state->list_state->selected < (int)state->list_state->visible_count &&
                   (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.is_header || state->list_state->items[state->list_state->visible[state->list_state->selected]].features.unselectable))
            {
                state->list_state->selected += 1;
            }

            if (state->list_state->selected >= (int)state->list_state->visible_count)
            {
                state->list_state->selected = state->list_state->visible_count - 1;
                int start = state->list_state->visible_count - max_row_count;
                state->list_state->first_visible = (start < 0) ? 0 : start;
                state->list_state->last_visible = state->list_state->visible_count;
            }
            else if (state->list_state->selected >= state->list_state->last_visible)
            {
                state->list_state->last_visible += max_row_count;
                if (state->list_state->last_visible > state->list_state->visible_count)
                {
                    state->list_state->last_visible = state->list_state->visible_count;
                }
                state->list_state->first_visible = state->list_state->last_visible - max_row_count;
            }
        }
        state->redraw = 1;
    }
    else if (state->alphabetic_scroll && PAD_justRepeated(BTN_L1))
    {
        // jump to the previous letter group; recompute the window so the new
        // selection is framed correctly, including on wrap-around to the end
        int target = alphabetic_jump_target(state->list_state, false);
        if (target != state->list_state->selected)
        {
            state->list_state->selected = target;
            ListState_InitView(state->list_state, max_row_count);
            state->redraw = 1;
        }
    }
    else if (state->alphabetic_scroll && PAD_justRepeated(BTN_R1))
    {
        // jump to the next letter group; recompute the window so the new
        // selection is framed correctly, including on wrap-around to the start
        int target = alphabetic_jump_target(state->list_state, true);
        if (target != state->list_state->selected)
        {
            state->list_state->selected = target;
            ListState_InitView(state->list_state, max_row_count);
            state->redraw = 1;
        }
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
// scale_surface manually scales a surface to a new width and height for SDL1
SDL_Surface *scale_surface(SDL_Surface *surface,
                           Uint16 width, Uint16 height)
{
    SDL_Surface *scaled = SDL_CreateRGBSurface(surface->flags,
                                               width,
                                               height,
                                               surface->format->BitsPerPixel,
                                               surface->format->Rmask,
                                               surface->format->Gmask,
                                               surface->format->Bmask,
                                               surface->format->Amask);

    int bpp = surface->format->BytesPerPixel;
    int *v = (int *)malloc(bpp * sizeof(int));

    for (int x = 0; x < width; x++)
    {
        for (int y = 0; y < height; y++)
        {
            int xo1 = x * surface->w / width;
            int xo2 = MAX((x + 1) * surface->w / width, xo1 + 1);
            int yo1 = y * surface->h / height;
            int yo2 = MAX((y + 1) * surface->h / height, yo1 + 1);
            int n = (xo2 - xo1) * (yo2 - yo1);

            for (int i = 0; i < bpp; i++)
                v[i] = 0;

            for (int xo = xo1; xo < xo2; xo++)
                for (int yo = yo1; yo < yo2; yo++)
                {
                    Uint8 *ps =
                        (Uint8 *)surface->pixels + yo * surface->pitch + xo * bpp;
                    for (int i = 0; i < bpp; i++)
                        v[i] += ps[i];
                }

            Uint8 *pd = (Uint8 *)scaled->pixels + y * scaled->pitch + x * bpp;
            for (int i = 0; i < bpp; i++)
                pd[i] = v[i] / n;
        }
    }

    free(v);

    return scaled;
}

// image_effective_path computes the path an item should currently display: its
// resolved per-resolution path when that file exists, otherwise the global
// fallback image when that exists, otherwise empty. Items without an image spec
// always resolve to empty.
void image_effective_path(struct ListItem *item, const char *fallback_image, char *out, size_t out_size)
{
    out[0] = '\0';
    if (!item->has_image)
        return;

    if (item->resolved_path[0] != '\0' && access(item->resolved_path, F_OK) != -1)
    {
        strncpy(out, item->resolved_path, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    if (fallback_image != NULL && fallback_image[0] != '\0' && access(fallback_image, F_OK) != -1)
    {
        strncpy(out, fallback_image, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

// ensure_item_image (re)loads and caches an item's scaled right-hand image when
// the effective path changes. It frees any previously cached surface and records
// image_active_path so a stable path (including an empty path or a load failure)
// is not retried every frame. max_w/max_h bound the scaled size.
void ensure_item_image(struct ListItem *item, const char *effective, int max_w, int max_h)
{
    if (strcmp(effective, item->image_active_path) == 0)
        return;

    if (item->image_surface != NULL)
    {
        SDL_FreeSurface(item->image_surface);
        item->image_surface = NULL;
    }

    strncpy(item->image_active_path, effective, sizeof(item->image_active_path) - 1);
    item->image_active_path[sizeof(item->image_active_path) - 1] = '\0';

    if (effective[0] == '\0')
        return;

    SDL_Surface *surface = IMG_Load(effective);
    if (surface == NULL)
        return;

    int dst_w = 0;
    int dst_h = 0;
    if (!ImageFit_Scale(surface->w, surface->h, max_w, max_h, &dst_w, &dst_h))
    {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Surface *scaled;
    if (dst_w == surface->w && dst_h == surface->h)
    {
        scaled = surface;
    }
    else
    {
#ifdef USE_SDL2
        scaled = SDL_CreateRGBSurfaceWithFormat(0, dst_w, dst_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (scaled != NULL)
        {
            // copy source pixels (including alpha) rather than compositing, so
            // the scaled copy keeps transparency for blending over the row
            SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
            SDL_BlitScaled(surface, NULL, scaled, NULL);
        }
#else
        scaled = scale_surface(surface, dst_w, dst_h);
#endif
        SDL_FreeSurface(surface);
    }

    if (scaled == NULL)
        return;

#ifdef USE_SDL2
    SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
#endif
    item->image_surface = scaled;
}

// draw_background draws the background of the list
bool draw_background(SDL_Surface *screen, struct AppState *state)
{
    // nothing selected (e.g. an active filter matched no items): plain background
    if (state->list_state->selected < 0)
    {
        SDL_FillRect(screen, NULL, theme_background_u32(screen));
        return false;
    }

    // render a background color. an explicit per-item or --background-color value
    // (both populate features.background_color) wins; otherwise fall back to the
    // theme background (black on MinUI, COLOR_BACKGROUND on NextUI).
    if (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_color[0] != '\0')
    {
        char hex_color[1024] = "#000000";
        strncpy(hex_color, state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_color, sizeof(hex_color));
        SDL_Color background_color = hex_to_sdl_color(hex_color);
        uint32_t color = SDL_MapRGBA(screen->format, background_color.r, background_color.g, background_color.b, 255);
        SDL_FillRect(screen, NULL, color);
    }
    else
    {
        SDL_FillRect(screen, NULL, theme_background_u32(screen));
    }

    bool should_draw_background_image = false;
    if (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image_exists && access(state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image, F_OK) != -1)
    {
        should_draw_background_image = true;
    }

    // check if there is an image and it is accessible
    if (should_draw_background_image)
    {
        SDL_Surface *surface = IMG_Load(state->list_state->items[state->list_state->visible[state->list_state->selected]].features.background_image);
        if (surface)
        {
            int imgW = surface->w, imgH = surface->h;

            // Compute scale factor
            float scaleX = (float)(FIXED_WIDTH - 2 * PADDING) / imgW;
            float scaleY = (float)(FIXED_HEIGHT - 2 * PADDING) / imgH;
            float scale = (scaleX < scaleY) ? scaleX : scaleY;

            // Ensure upscaling only when the image is smaller than the screen
            if (imgW * scale < FIXED_WIDTH - 2 * PADDING && imgH * scale < FIXED_HEIGHT - 2 * PADDING)
            {
                scale = (scaleX > scaleY) ? scaleX : scaleY;
            }

            // Compute target dimensions
            int dstW = imgW * scale;
            int dstH = imgH * scale;

            int dstX = (FIXED_WIDTH - dstW) / 2;
            int dstY = (FIXED_HEIGHT - dstH) / 2;
            if (imgW == FIXED_WIDTH && imgH == FIXED_HEIGHT)
            {
                dstW = FIXED_WIDTH;
                dstH = FIXED_HEIGHT;
                dstX = 0;
                dstY = 0;
            }

            // Compute destination rectangle
            SDL_Rect dstRect = {dstX, dstY, dstW, dstH};
#ifdef USE_SDL2
            SDL_BlitScaled(surface, NULL, screen, &dstRect);
#else
            if (imgW == FIXED_WIDTH && imgH == FIXED_HEIGHT)
            {
                SDL_BlitSurface(surface, NULL, screen, &dstRect);
            }
            else
            {
                SDL_Surface *scaled = scale_surface(surface, dstW, dstH);
                SDL_BlitSurface(scaled, NULL, screen, &dstRect);
                SDL_FreeSurface(scaled);
            }
#endif
            SDL_FreeSurface(surface);
        }
    }

    return should_draw_background_image;
}

// draw_match_highlight paints an accent rectangle behind the matched portion of
// text_str and re-renders that substring in black over it, so the filter match
// stands out on both selected and unselected rows. It is a no-op when there is
// no active filter or the (already-truncated) text does not contain the match.
static void draw_match_highlight(SDL_Surface *screen, TTF_Font *font,
                                 const char *text_str, const char *filter,
                                 int base_x, int base_y)
{
    if (font == NULL || filter == NULL || filter[0] == '\0' || text_str == NULL)
        return;

    size_t ms = 0, ml = 0;
    if (!ListFilter_Match(text_str, filter, &ms, &ml) || ml == 0)
        return;

    char prefix[256];
    char match[256];
    if (ms >= sizeof(prefix) || ml >= sizeof(match))
        return;
    memcpy(prefix, text_str, ms);
    prefix[ms] = '\0';
    memcpy(match, text_str + ms, ml);
    match[ml] = '\0';

    int prefix_w = 0, match_w = 0, match_h = 0;
    TTF_SizeUTF8(font, prefix, &prefix_w, NULL);
    TTF_SizeUTF8(font, match, &match_w, &match_h);
    if (match_w <= 0 || match_h <= 0)
        return;

    SDL_Rect hl = {base_x + prefix_w, base_y, match_w, match_h};
    SDL_FillRect(screen, &hl, theme_accent_u32(screen));

    SDL_Surface *m = TTF_RenderUTF8_Blended(font, match, theme_accent_text_color());
    if (m != NULL)
    {
        SDL_Rect mp = {base_x + prefix_w, base_y, m->w, m->h};
        SDL_BlitSurface(m, NULL, screen, &mp);
        SDL_FreeSurface(m);
    }
}

// draw_filter_keyboard renders the on-screen filter keyboard: an input field
// showing the current filter text, then the key grid for the active layout with
// the focused key inverted. Ported from the sibling minui-keyboard tool.
static void draw_filter_keyboard(SDL_Surface *screen, struct AppState *state)
{
    struct FilterKeyboardGeom g = filter_keyboard_geom(state);
    TTF_Font *kb_font = filter_keyboard_font();

    // input field background
    SDL_Rect input_bg = {SCALE1(PADDING), g.input_y, screen->w - SCALE1(PADDING) * 2, g.input_h};
    SDL_FillRect(screen, &input_bg, theme_kb_input_bg(screen));

    // current filter text, clipped to the field and tail-aligned so the most
    // recently typed characters stay visible
    if (state->filter_text[0] != '\0' && kb_font != NULL)
    {
        SDL_Surface *input = TTF_RenderUTF8_Blended(kb_font, state->filter_text, theme_kb_input_text());
        if (input != NULL)
        {
            int inner_x = SCALE1(PADDING + BUTTON_PADDING);
            int inner_w = input_bg.w - SCALE1(BUTTON_PADDING * 2);
            int ip_x = inner_x;
            if (input->w > inner_w)
            {
                ip_x = input_bg.x + input_bg.w - SCALE1(BUTTON_PADDING) - input->w;
            }
            SDL_Rect ip = {ip_x, g.input_y + (g.input_h - input->h) / 2, input->w, input->h};
            SDL_SetClipRect(screen, &input_bg);
            SDL_BlitSurface(input, NULL, screen, &ip);
            SDL_SetClipRect(screen, NULL);
            SDL_FreeSurface(input);
        }
    }

    // the special keys are wider than the character keys
    int shift_w = 0, space_w = 0, enter_w = 0;
    if (kb_font != NULL)
    {
        TTF_SizeUTF8(kb_font, "shift", &shift_w, NULL);
        TTF_SizeUTF8(kb_font, "space", &space_w, NULL);
        TTF_SizeUTF8(kb_font, "enter", &enter_w, NULL);
    }
    int special_key_width = shift_w;
    if (space_w > special_key_width)
        special_key_width = space_w;
    if (enter_w > special_key_width)
        special_key_width = enter_w;
    special_key_width += g.col_spacing * 4;

    for (int row = 0; row < LIST_KEYBOARD_ROWS; row++)
    {
        int len = ListKeyboard_RowLength(state->filter_cursor.layout, row);
        int total_width;
        if (row == LIST_KEYBOARD_ROWS - 1)
        {
            total_width = (special_key_width * 3) + (2 * g.col_spacing);
        }
        else
        {
            total_width = (len * g.key_size) + ((len - 1) * g.col_spacing);
        }
        int start_x = (screen->w - total_width) / 2;

        for (int col = 0; col < len; col++)
        {
            const char *key = ListKeyboard_KeyAt(state->filter_cursor.layout, row, col);
            if (key[0] == '\0')
                continue;

            bool focused = (row == state->filter_cursor.row && col == state->filter_cursor.col);
            int cur_w = g.key_size;
            if (strcmp(key, "shift") == 0 || strcmp(key, "space") == 0 || strcmp(key, "enter") == 0)
            {
                cur_w = special_key_width;
            }

            SDL_Rect key_pos = {
                start_x + col * (cur_w + g.col_spacing),
                g.grid_y + row * (g.key_size + g.row_spacing),
                cur_w,
                g.key_size};

            Uint32 bg = theme_kb_key_bg(screen, focused);
            SDL_FillRect(screen, &key_pos, bg);

            if (kb_font != NULL)
            {
                SDL_Color tc = theme_kb_key_text(focused);
                SDL_Surface *kt = TTF_RenderUTF8_Blended(kb_font, key, tc);
                if (kt != NULL)
                {
                    SDL_Rect tp = {
                        key_pos.x + (cur_w - kt->w) / 2,
                        key_pos.y + (g.key_size - kt->h) / 2,
                        kt->w,
                        kt->h};
                    SDL_BlitSurface(kt, NULL, screen, &tp);
                    SDL_FreeSurface(kt);
                }
            }
        }
    }
}

// draw_screen interprets the app state and draws it to the screen
void draw_screen(SDL_Surface *screen, struct AppState *state, int ow, bool should_draw_background_image)
{
    // draw the button group on the right. when the filter keyboard is open the
    // hints describe the keyboard controls; when nothing is selected (an active
    // filter matched nothing) there is no confirm/cancel to show
    if (state->filter_keyboard_active)
    {
        GFX_blitButtonGroup((char *[]){"A", "TYPE", "X", "CLEAR", NULL}, 1, screen, 1);
    }
    else if (state->list_state->selected >= 0)
    {
        bool force_hide_confirm = false;
        if (!state->always_show_confirm && !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.show_confirm && state->list_state->items[state->list_state->visible[state->list_state->selected]].has_options && state->list_state->items[state->list_state->visible[state->list_state->selected]].initial_selected == state->list_state->items[state->list_state->visible[state->list_state->selected]].selected)
        {
            force_hide_confirm = true;
        }

        if (!state->always_show_confirm && state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_confirm)
        {
            force_hide_confirm = true;
        }

        // only two buttons can be displayed at a time
        if (force_hide_confirm)
        {
            if (!state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_cancel)
            {
                GFX_blitButtonGroup((char *[]){state->cancel_button, state->cancel_text, NULL}, 1, screen, 1);
            }
        }
        else if (state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_cancel)
        {
            GFX_blitButtonGroup((char *[]){state->confirm_button, state->list_state->items[state->list_state->visible[state->list_state->selected]].features.confirm_text, NULL}, 1, screen, 1);
        }
        else
        {
            GFX_blitButtonGroup((char *[]){state->cancel_button, state->cancel_text, state->confirm_button, state->list_state->items[state->list_state->visible[state->list_state->selected]].features.confirm_text, NULL}, 1, screen, 1);
        }
    }

    // if there is a title specified, compute the space needed for it
    int initial_list_y_padding = 0;
    if (strlen(state->title) > 0)
    {
        // Truncate title to avoid battery/wifi icon interference
        int title_available_width = screen->w - SCALE1(PADDING * 3) - ow; // 3 paddings: left, right, and between title and icon pill
        char truncated_title_text[256];
        int title_width = GFX_truncateText(state->fonts.medium, state->title, truncated_title_text, title_available_width, SCALE1(BUTTON_PADDING * 2));

        // compute the x position of the title based on the alignment
        int title_x_pos;
        const char *title_alignment = state->title_alignment[0] != '\0' ? state->title_alignment : "left";
        if (strcmp(title_alignment, "center") == 0)
        {
            title_x_pos = (screen->w - title_width) / 2 + SCALE1(BUTTON_PADDING);
            int title_interference = title_width - (title_available_width - ow - SCALE1(PADDING)); // extra ow and padding account for centered text, i.e. available width is offset by ow and padding on both sides of screen
            if (title_interference > 0)
            {
                title_x_pos -= title_interference / 2;
            }
        }
        else if (strcmp(title_alignment, "right") == 0)
        {
            title_x_pos = screen->w - title_width - ow - SCALE1(PADDING * 2) + SCALE1(BUTTON_PADDING);
        }
        else // left (default)
        {
            title_x_pos = SCALE1(PADDING + BUTTON_PADDING);
        }

        initial_list_y_padding = PILL_SIZE;
        if (should_draw_background_image)
        {
            int pill_width = MIN(title_available_width, title_width);
            // Calculate pill position based on alignment
            int pill_x_pos;
            if (strcmp(title_alignment, "center") == 0)
            {
                pill_x_pos = (screen->w - pill_width) / 2;
            }
            else if (strcmp(title_alignment, "right") == 0)
            {
                pill_x_pos = screen->w - pill_width - SCALE1(PADDING);
            }
            else // left (default)
            {
                pill_x_pos = SCALE1(PADDING);
            }

            GFX_blitPill(ASSET_BLACK_PILL, screen, &(SDL_Rect){pill_x_pos, SCALE1(PADDING), pill_width, SCALE1(PILL_SIZE)});

            initial_list_y_padding = PILL_SIZE + (PILL_SIZE / 2);
        }

        // draw the title
        SDL_Color text_color = theme_title_text_color(should_draw_background_image);
        SDL_Surface *text = TTF_RenderUTF8_Blended(state->fonts.medium, truncated_title_text, text_color);
        SDL_Rect pos = {
            title_x_pos,
            SCALE1(PADDING + 4),
            text->w,
            text->h};
        SDL_BlitSurface(text, NULL, screen, &pos);
        SDL_FreeSurface(text);
    }

    // the rest of the function is just for drawing your app to the screen
    bool current_item_supports_enabling = false;
    bool current_item_is_enabled = false;
    bool current_item_is_header = false;
    int selected_row = state->list_state->selected - state->list_state->first_visible;

    // autoscroll configuration for the selected row's over-long name; cleared
    // each frame and re-armed below whenever a row is actually scrolling
    enum ScrollMethod scroll_method = ScrollMethod_Parse(state->scroll_method);
    struct ScrollConfig scroll_config = {
        .speed_px_per_sec = SCALE1(40),
        .start_pause_ms = 1000,
        .end_pause_ms = 1000,
        .gap_px = SCALE1(40),
        .overflow_threshold_px = SCALE1(4),
    };
    state->scroll_active = false;

    for (int k = state->list_state->first_visible, j = 0; k < state->list_state->last_visible; k++, j++)
    {
        // k is the display position; i is the source index it maps to
        int i = state->list_state->visible[k];
        int available_width = (screen->w) - SCALE1(PADDING * 2);
        bool in_top_row_no_title = (j == 0 && strlen(state->title) == 0);
        // Account for the space taken up by ow and it's padding
        if (in_top_row_no_title)
        {
            available_width -= (ow + SCALE1(PADDING));
        }
        // compute the string representation of the current item
        // to include the current option if there are any options
        // the output should be in the format of:
        // item.name: <selected>
        // if there are no options, the output should be:
        // item.name
        char display_text[256];
        char display_selected_text[256];
        char *alignment = state->list_state->items[i].features.alignment;
        bool is_hex_color = false;
        strncpy(display_selected_text, "", sizeof(display_selected_text));
        if (state->list_state->items[i].option_count > 0)
        {
            char *selected = state->list_state->items[i].options[state->list_state->items[i].selected];
            is_hex_color = detect_hex_color(selected);
            if (strcmp(alignment, "left") == 0)
            {
                snprintf(display_text, sizeof(display_text), "%s", state->list_state->items[i].name);
                if (state->list_state->items[i].features.draw_arrows)
                {
                    snprintf(display_selected_text, sizeof(display_selected_text), "‹ %s ›", selected);
                }
                else
                {
                    snprintf(display_selected_text, sizeof(display_selected_text), "%s", selected);
                }
            }
            else
            {
                if (state->list_state->items[i].features.draw_arrows)
                {
                    snprintf(display_text, sizeof(display_text), "%s: ‹ %s ›", state->list_state->items[i].name, selected);
                }
                else
                {
                    snprintf(display_text, sizeof(display_text), "%s: %s", state->list_state->items[i].name, selected);
                }
            }
        }
        else
        {
            snprintf(display_text, sizeof(display_text), "%s", state->list_state->items[i].name);
        }

        // resolve the row text color from its state (selected/disabled/muted).
        // ListTheme_RowTextRole captures the precedence; theme_row_text_color maps
        // the role to the greyscale palette or, on -nextui builds, the theme colors.
        SDL_Color text_color = theme_row_text_color(ListTheme_RowTextRole(
            j == selected_row,
            state->list_state->items[i].features.disabled,
            state->list_state->items[i].features.is_header || state->list_state->items[i].features.unselectable));

        int color_placeholder_height;
        TTF_SizeUTF8(state->fonts.medium, " ", NULL, &color_placeholder_height);
        int color_box_space = 0;
        if (is_hex_color)
        {
            color_box_space += (color_placeholder_height + SCALE1(PADDING));
            available_width -= color_box_space;
        }

        // load this item's right-hand image (if any) and reserve a column for it
        // on the right, shrinking the content area so the name truncates to fit.
        // the image is capped at a third of the screen width and the row height.
        int image_col_space = 0;
        {
            struct ListItem *image_item = &state->list_state->items[i];
            char image_effective[1024];
            image_effective_path(image_item, state->fallback_image, image_effective, sizeof(image_effective));
            ensure_item_image(image_item, image_effective, screen->w / IMAGE_MAX_WIDTH_DIVISOR, SCALE1(PILL_SIZE - 4));
            if (image_item->image_surface != NULL)
            {
                image_col_space = image_item->image_surface->w + SCALE1(PADDING);
                available_width -= image_col_space;
            }
        }

        char truncated_display_text[256];
        int text_width = GFX_truncateText(state->fonts.large, display_text, truncated_display_text, available_width, SCALE1(BUTTON_PADDING * 2));

        // Decide whether this (selected) row should autoscroll its over-long
        // name instead of showing the "..." ellipsis. Only the selected,
        // selectable, non-hex row scrolls; every other row keeps the truncated
        // text unchanged.
        bool row_scrolls = false;
        int scroll_offset = 0;
        int scroll_full_width = 0;
        int scroll_viewport = 0;
        if (j == selected_row && scroll_method != SCROLL_NONE && !is_hex_color &&
            !state->list_state->items[i].features.is_header &&
            !state->list_state->items[i].features.unselectable)
        {
            TTF_SizeUTF8(state->fonts.large, display_text, &scroll_full_width, NULL);
            scroll_viewport = available_width - SCALE1(BUTTON_PADDING * 2);

            // keep the marquee clear of a right-aligned option value, if present
            if (strcmp(display_selected_text, "") != 0)
            {
                int value_width = 0;
                TTF_SizeUTF8(state->fonts.large, display_selected_text, &value_width, NULL);
                int value_left = screen->w - value_width - SCALE1(PADDING + BUTTON_PADDING) - color_box_space - image_col_space;
                int value_viewport = value_left - SCALE1(PADDING + BUTTON_PADDING) - SCALE1(PADDING);
                if (value_viewport < scroll_viewport)
                {
                    scroll_viewport = value_viewport;
                }
            }

            if (scroll_viewport > 0 && (scroll_full_width - scroll_viewport) > scroll_config.overflow_threshold_px)
            {
                // reset the animation clock when the selected row or its option changes
                if (state->scroll_anim_selected != state->list_state->selected ||
                    state->scroll_anim_option != state->list_state->items[i].selected)
                {
                    state->scroll_anim_selected = state->list_state->selected;
                    state->scroll_anim_option = state->list_state->items[i].selected;
                    state->scroll_anim_start_ms = SDL_GetTicks();
                }
                uint32_t scroll_elapsed = SDL_GetTicks() - state->scroll_anim_start_ms;
                scroll_offset = TextScroll_Offset(scroll_method, &scroll_config, scroll_elapsed, scroll_full_width, scroll_viewport);
                row_scrolls = true;
            }
        }

        int pill_width = MIN(available_width, text_width) + color_box_space;
        if (row_scrolls)
        {
            // pin the pill to the full available width so the marquee frame is stable
            pill_width = available_width + color_box_space;
        }

        if (j == selected_row)
        {
            // text_color is already resolved above (with is_selected set); the
            // selected branch only records the row's state for input handling.
            current_item_is_enabled = state->list_state->items[i].features.disabled;
            if (state->list_state->items[i].features.is_header || state->list_state->items[i].features.unselectable)
            {
                current_item_is_header = true;
            }
            if (state->list_state->items[i].features.can_disable)
            {
                current_item_supports_enabling = true;
            }

            // Calculate pill position based on alignment
            int pill_x_pos;
            if (strcmp(alignment, "center") == 0)
            {
                pill_x_pos = (screen->w - image_col_space - pill_width) / 2;
            }
            else if (strcmp(alignment, "right") == 0)
            {
                pill_x_pos = screen->w - pill_width - SCALE1(PADDING) - image_col_space;
            }
            else // left (default)
            {
                pill_x_pos = SCALE1(PADDING);
            }

            // Adjust for the pill position in the top row without title
            if (in_top_row_no_title)
            {
                if (strcmp(alignment, "center") == 0)
                {
                    int interference = pill_width - (available_width - ow - SCALE1(PADDING)); // extra ow and padding account for centered text, i.e. available width is offset by ow and padding on both sides of screen
                    if (interference > 0)
                    {
                        pill_x_pos -= interference / 2;
                    }
                }
                else if (strcmp(alignment, "right") == 0)
                {
                    pill_x_pos -= (ow + SCALE1(PADDING));
                }
            }

            // a scrolling marquee always renders left-origin, so pin its pill left
            if (row_scrolls)
            {
                pill_x_pos = SCALE1(PADDING);
            }

            if (strcmp(display_selected_text, "") != 0)
            {
                blit_value_track_pill(ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){pill_x_pos, SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding), screen->w - SCALE1(PADDING + BUTTON_MARGIN) - image_col_space, SCALE1(PILL_SIZE)});
            }

            blit_selected_pill(ASSET_WHITE_PILL, screen, &(SDL_Rect){pill_x_pos, SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding), pill_width, SCALE1(PILL_SIZE)});
        }

        SDL_Surface *text;
        if (row_scrolls)
        {
            // render the full, untruncated name for the marquee
            text = TTF_RenderUTF8_Blended(state->fonts.large, display_text, text_color);
        }
        else
        {
            text = TTF_RenderUTF8_Blended(state->fonts.large, truncated_display_text, text_color);
        }
        int text_surface_width = text->w;

        // Calculate text position based on alignment
        int text_x_pos;
        int shadow_x_pos;
        if (strcmp(alignment, "center") == 0)
        {
            text_x_pos = (screen->w - text->w - color_box_space - image_col_space) / 2;
            shadow_x_pos = text_x_pos - 2;
        }
        else if (strcmp(alignment, "right") == 0)
        {
            text_x_pos = screen->w - text->w - SCALE1(PADDING + BUTTON_PADDING) - color_box_space - image_col_space;
            shadow_x_pos = screen->w - text->w - SCALE1(2 + PADDING + BUTTON_PADDING) - color_box_space - image_col_space;
        }
        else // left (default)
        {
            text_x_pos = SCALE1(PADDING + BUTTON_PADDING);
            shadow_x_pos = SCALE1(2 + PADDING + BUTTON_PADDING);
        }

        // Adjust for the pill position in the top row without title
        if (in_top_row_no_title)
        {
            if (strcmp(alignment, "center") == 0)
            {
                int interference = pill_width - (available_width - ow - SCALE1(PADDING)); // extra ow and padding account for centered text, i.e. available width is offset by ow and padding on both sides of screen
                if (interference > 0)
                {
                    text_x_pos -= interference / 2;
                }
            }
            else if (strcmp(alignment, "right") == 0)
            {
                text_x_pos -= (ow + SCALE1(PADDING));
            }
        }

        // a scrolling marquee always renders left-origin
        if (row_scrolls)
        {
            text_x_pos = SCALE1(PADDING + BUTTON_PADDING);
        }

        int text_y_pos = SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding + 4);
        SDL_Rect pos = {
            text_x_pos,
            text_y_pos,
            text->w,
            text->h};

        if (row_scrolls)
        {
            // clip the marquee to the pill interior and blit the full text at the
            // animated offset; wrap draws a second copy after a gap so the loop
            // is seamless
            int clip_y_pos = SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding);
            SDL_Rect scroll_clip = {text_x_pos, clip_y_pos, scroll_viewport, SCALE1(PILL_SIZE)};
            SDL_SetClipRect(screen, &scroll_clip);

            SDL_Rect scroll_pos = {text_x_pos - scroll_offset, text_y_pos, text->w, text->h};
            SDL_BlitSurface(text, NULL, screen, &scroll_pos);
            if (scroll_method == SCROLL_WRAP)
            {
                SDL_Rect scroll_pos_2 = {text_x_pos - scroll_offset + scroll_full_width + scroll_config.gap_px, text_y_pos, text->w, text->h};
                SDL_BlitSurface(text, NULL, screen, &scroll_pos_2);
            }

            SDL_SetClipRect(screen, NULL);
            state->scroll_active = true;
        }
        else
        {
            // draw the text as a black shadow
            if (should_draw_background_image && j != selected_row)
            {
                // COLOR_BLACK
                SDL_Surface *accent_text;
                accent_text = TTF_RenderUTF8_Blended(state->fonts.large, truncated_display_text, COLOR_BLACK);
                SDL_Rect accent_pos = {
                    shadow_x_pos,
                    SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding + 4 + 2),
                    accent_text->w,
                    accent_text->h};
                SDL_BlitSurface(accent_text, NULL, screen, &accent_pos);
                SDL_FreeSurface(accent_text);
            }

            SDL_BlitSurface(text, NULL, screen, &pos);

            // highlight the matched portion of the name while filtering (the
            // marquee path above renders the full string and is left unhighlighted)
            if (state->allow_filter && state->filter_text[0] != '\0' && !is_hex_color)
            {
                draw_match_highlight(screen, state->fonts.large, truncated_display_text,
                                     state->filter_text, text_x_pos, text_y_pos);
            }
        }

        SDL_FreeSurface(text);

        int initial_cube_x_pos = text_x_pos + text_surface_width;

        // draw the selected option text
        if (strcmp(display_selected_text, "") != 0)
        {
            initial_cube_x_pos = screen->w - SCALE1(PADDING + BUTTON_PADDING) - color_box_space - image_col_space;
            if (j != 0 || strlen(state->title) > 0)
            {
                SDL_Color selected_text_color = theme_row_text_color(
                    (state->list_state->items[i].features.disabled || state->list_state->items[i].features.unselectable)
                        ? LIST_TEXT_MUTED
                        : LIST_TEXT_NORMAL);
                SDL_Surface *selected_text;
                selected_text = TTF_RenderUTF8_Blended(state->fonts.large, display_selected_text, selected_text_color);
                pos = (SDL_Rect){screen->w - selected_text->w - SCALE1(PADDING + BUTTON_PADDING) - color_box_space - image_col_space, pos.y, selected_text->w, selected_text->h};
                SDL_BlitSurface(selected_text, NULL, screen, &pos);
                SDL_FreeSurface(selected_text);
            }
        }

        if (is_hex_color)
        {
            // get the hex color from the options array
            char *hex_color = state->list_state->items[i].options[state->list_state->items[i].selected];
            SDL_Color current_color = hex_to_sdl_color(hex_color);
            uint32_t color = SDL_MapRGBA(screen->format, current_color.r, current_color.g, current_color.b, 255);

            // Draw outline cube
            uint32_t outline_color = sdl_color_to_uint32(text_color);
            SDL_Rect outline_rect = {
                initial_cube_x_pos + SCALE1(PADDING),
                SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding + 5), color_placeholder_height,
                color_placeholder_height};
            SDL_FillRect(screen, &(SDL_Rect){outline_rect.x, outline_rect.y, outline_rect.w, outline_rect.h}, outline_color);

            // Draw color cube
            SDL_Rect color_rect = {
                initial_cube_x_pos + SCALE1(PADDING) + 2,
                SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding + 5) + 2, color_placeholder_height - 4,
                color_placeholder_height - 4};
            SDL_FillRect(screen, &(SDL_Rect){color_rect.x, color_rect.y, color_rect.w, color_rect.h}, color);
        }

        // draw the per-item right-hand image, vertically centered in the row and
        // anchored to the right edge (left of the hardware group on the top row)
        if (state->list_state->items[i].image_surface != NULL)
        {
            SDL_Surface *image_surface = state->list_state->items[i].image_surface;
            int image_right_edge = screen->w - SCALE1(PADDING);
            if (in_top_row_no_title)
            {
                image_right_edge -= (ow + SCALE1(PADDING));
            }
            int row_top = SCALE1(PADDING + (j * PILL_SIZE) + initial_list_y_padding);
            SDL_Rect image_pos = {
                image_right_edge - image_surface->w,
                row_top + (SCALE1(PILL_SIZE) - image_surface->h) / 2,
                image_surface->w,
                image_surface->h};
            SDL_BlitSurface(image_surface, NULL, screen, &image_pos);
        }
    }

    // free cached image surfaces for items scrolled out of view to bound memory
    // to roughly the visible window; they reload on demand when scrolled back in
    for (size_t i = 0; i < state->list_state->item_count; i++)
    {
        // an item is on screen when its source index appears in the currently
        // displayed window of visible[]; skip freeing those
        bool onscreen = false;
        for (int k = state->list_state->first_visible; k < state->list_state->last_visible; k++)
        {
            if (state->list_state->visible[k] == (int)i)
            {
                onscreen = true;
                break;
            }
        }
        if (onscreen)
            continue;
        struct ListItem *offscreen = &state->list_state->items[i];
        if (offscreen->image_surface != NULL)
        {
            SDL_FreeSurface(offscreen->image_surface);
            offscreen->image_surface = NULL;
            offscreen->image_active_path[0] = '\0';
        }
    }

    char enable_button_text[256] = "Enable";
    if (current_item_is_enabled)
    {
        strncpy(enable_button_text, "Disable", sizeof(enable_button_text) - 1);
    }

    // draw the button group on the left
    // when the filter keyboard is open its controls occupy the bottom-left; when
    // nothing is selected there is no enable/action group to show
    if (state->filter_keyboard_active)
    {
        GFX_blitButtonGroup((char *[]){"B", "DELETE", state->filter_button, "DONE", NULL}, 0, screen, 0);
    }
    else if (state->list_state->selected >= 0)
    {
        // this should only display the enable button if the current item supports enabling
        // and should only display the action button if it is assigned to a button
        if (current_item_supports_enabling && strcmp(state->enable_button, "") != 0)
        {
            if (strcmp(state->action_button, "") != 0 && !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_action)
            {
                GFX_blitButtonGroup((char *[]){state->enable_button, enable_button_text, state->action_button, state->action_text, NULL}, 0, screen, 0);
            }
            else
            {
                GFX_blitButtonGroup((char *[]){state->enable_button, enable_button_text, NULL}, 0, screen, 0);
            }
        }
        else if (strcmp(state->action_button, "") != 0 && !state->list_state->items[state->list_state->visible[state->list_state->selected]].features.hide_action)
        {
            GFX_blitButtonGroup((char *[]){state->action_button, state->action_text, NULL}, 0, screen, 0);
        }
    }

    // draw the filter keyboard overlay on top of the list when it is open
    if (state->filter_keyboard_active)
    {
        draw_filter_keyboard(screen, state);
    }

    // don't forget to reset the should_redraw flag
    state->redraw = 0;
}

bool open_fonts(struct AppState *state)
{
    if (state->fonts.default_font != NULL)
    {
        // check if the font path is valid
        if (access(state->fonts.default_font, F_OK) == -1)
        {
            log_error("Invalid font path provided");
            return false;
        }
    }

    if (state->fonts.large_font != NULL)
    {
        // check if the font path is valid
        if (access(state->fonts.large_font, F_OK) == -1)
        {
            log_error("Invalid font path provided");
            return false;
        }

        state->fonts.large = TTF_OpenFont(state->fonts.large_font, SCALE1(FONT_LARGE));
        if (state->fonts.large == NULL)
        {
            log_error("Failed to open large font");
            return false;
        }
        TTF_SetFontStyle(state->fonts.large, TTF_STYLE_BOLD);
    }
    else if (state->fonts.default_font != NULL)
    {
        state->fonts.large = TTF_OpenFont(state->fonts.default_font, SCALE1(FONT_LARGE));
        if (state->fonts.large == NULL)
        {
            log_error("Failed to open default font");
            return false;
        }
        TTF_SetFontStyle(state->fonts.large, TTF_STYLE_BOLD);
    }
    else
    {
        state->fonts.large = font.large;
    }

    if (state->fonts.medium_font != NULL)
    {
        // check if the font path is valid
        if (access(state->fonts.medium_font, F_OK) == -1)
        {
            log_error("Invalid font path provided");
            return false;
        }
        state->fonts.medium = TTF_OpenFont(state->fonts.medium_font, SCALE1(FONT_MEDIUM));
        if (state->fonts.medium == NULL)
        {
            log_error("Failed to open medium font");
            return false;
        }
        TTF_SetFontStyle(state->fonts.medium, TTF_STYLE_BOLD);
    }
    else if (state->fonts.default_font != NULL)
    {
        state->fonts.medium = TTF_OpenFont(state->fonts.default_font, SCALE1(FONT_MEDIUM));
        if (state->fonts.medium == NULL)
        {
            log_error("Failed to open default font");
            return false;
        }
        TTF_SetFontStyle(state->fonts.medium, TTF_STYLE_BOLD);
    }
    else
    {
        state->fonts.medium = font.medium;
    }

    return true;
}

static void set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        return;
    }

    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

// suppress_output suppresses stdout and stderr
// returns a single integer containing both file descriptors
int suppress_output(void)
{
    int stdout_fd = dup(STDOUT_FILENO);
    int stderr_fd = dup(STDERR_FILENO);

    // Prevent child processes started while stdout is suppressed from
    // inheriting the saved descriptors and keeping command-substitution
    // pipes open after the main process exits.
    set_cloexec(stdout_fd);
    set_cloexec(stderr_fd);

    int dev_null_fd = open("/dev/null", O_WRONLY);
    dup2(dev_null_fd, STDOUT_FILENO);
    dup2(dev_null_fd, STDERR_FILENO);
    close(dev_null_fd);

    return (stdout_fd << 16) | stderr_fd;
}

// restore_output restores stdout and stderr to the original file descriptors
void restore_output(int saved_fds)
{
    int stdout_fd = (saved_fds >> 16) & 0xFFFF;
    int stderr_fd = saved_fds & 0xFFFF;

    fflush(stdout);
    fflush(stderr);

    dup2(stdout_fd, STDOUT_FILENO);
    dup2(stderr_fd, STDERR_FILENO);

    close(stdout_fd);
    close(stderr_fd);
}

// swallow_stdout_from_function swallows stdout from a function
// this is useful for suppressing output from a function
// that we don't want to see in the log file
// the InitSettings() function is an example of this (some implementations print to stdout)
void swallow_stdout_from_function(void (*func)(void))
{
    int saved_fds = suppress_output();

    func();

    restore_output(saved_fds);
}

static volatile sig_atomic_t signal_exit_code = 0;

void signal_handler(int signal)
{
    if (signal == SIGINT)
        signal_exit_code = ExitCodeKeyboardInterrupt;
    else if (signal == SIGTERM)
        signal_exit_code = ExitCodeSigterm;
    else
        signal_exit_code = ExitCodeError;
}

// filter_button_mask maps a --filter-button name to its PAD button mask, or
// BTN_NONE for an unsupported name. Only non-face, non-menu buttons are allowed
// so the toggle never collides with the A/B/X keyboard controls.
static int filter_button_mask(const char *name)
{
    if (name == NULL)
        return BTN_NONE;
    if (strcmp(name, "SELECT") == 0)
        return BTN_SELECT;
    if (strcmp(name, "START") == 0)
        return BTN_START;
    if (strcmp(name, "L1") == 0)
        return BTN_L1;
    if (strcmp(name, "R1") == 0)
        return BTN_R1;
    if (strcmp(name, "L2") == 0)
        return BTN_L2;
    if (strcmp(name, "R2") == 0)
        return BTN_R2;
    return BTN_NONE;
}

// parse_arguments parses the arguments using getopt and updates the app state
// supports the following flags:
// - --action-button <button> (default: "")
// - --action-text <text> (default: "ACTION")
// - --background-image <path> (default: empty string)
// - --background-color <hex> (default: empty string)
// - --confirm-button <button> (default: "A")
// - --confirm-text <text> (default: "SELECT")
// - --cancel-button <button> (default: "B")
// - --cancel-text <text> (default: "BACK")
// - --enable-button <button> (default: "Y")
// - --always-show-confirm (default: false)
// - --disable-auto-sleep (default: false)
// - --font-default <path> (default: empty string)
// - --font-large <path> (default: empty string)
// - --font-medium <path> (default: empty string)
// - --format <format> (default: "json")
// - --hide-hardware-group (default: false)
// - --title <title> (default: empty string)
// - --title-alignment <alignment> (default: "left")
// - --item-key <key> (default: "items")
// - --scroll-method <method> (default: "false")
// - --write-location <location> (default: "-")
// - --write-value <value> (default: "selected")
// - --allow-filter <true|false> (default: false)
// - --filter-button <button> (default: "SELECT")
// - --display-filter-keyboard <true|false> (default: false)
// - --filter-input <text> (default: empty string)
// - --filter-text-file <path> (default: empty string)
bool parse_arguments(struct AppState *state, int argc, char *argv[])
{
    // long-only options use val codes above the ASCII range so they need no
    // short flag (the short option alphabet is nearly exhausted)
    enum
    {
        OPT_SCREEN_RESOLUTION = 1000,
        OPT_FALLBACK_IMAGE,
        OPT_ALLOW_FILTER,
        OPT_FILTER_BUTTON,
        OPT_DISPLAY_FILTER_KEYBOARD,
        OPT_FILTER_INPUT,
        OPT_FILTER_TEXT_FILE,
    };
    static struct option long_options[] = {
        {"action-button", required_argument, 0, 'a'},
        {"action-text", required_argument, 0, 'A'},
        {"background-image", required_argument, 0, 'b'},
        {"background-color", required_argument, 0, 'B'},
        {"confirm-button", required_argument, 0, 'c'},
        {"confirm-text", required_argument, 0, 'C'},
        {"cancel-button", required_argument, 0, 'd'},
        {"cancel-text", required_argument, 0, 'D'},
        {"enable-button", required_argument, 0, 'e'},
        {"file", required_argument, 0, 'f'},
        {"font-default", required_argument, 0, 'l'},
        {"font-large", required_argument, 0, 'L'},
        {"font-medium", required_argument, 0, 'M'},
        {"format", required_argument, 0, 'F'},
        {"hide-hardware-group", no_argument, 0, 'H'},
        {"item-key", required_argument, 0, 'K'},
        {"title", required_argument, 0, 't'},
        {"title-alignment", required_argument, 0, 'T'},
        {"scroll-method", required_argument, 0, 'r'},
        {"write-location", required_argument, 0, 'w'},
        {"write-value", required_argument, 0, 'W'},
        {"selected", required_argument, 0, 's'},
        {"always-show-confirm", no_argument, 0, 'X'},
        {"disable-auto-sleep", no_argument, 0, 'U'},
        {"alphabetic-scroll", no_argument, 0, 'S'},
        {"screen-resolution", required_argument, 0, OPT_SCREEN_RESOLUTION},
        {"fallback-image", required_argument, 0, OPT_FALLBACK_IMAGE},
        {"allow-filter", required_argument, 0, OPT_ALLOW_FILTER},
        {"filter-button", required_argument, 0, OPT_FILTER_BUTTON},
        {"display-filter-keyboard", required_argument, 0, OPT_DISPLAY_FILTER_KEYBOARD},
        {"filter-input", required_argument, 0, OPT_FILTER_INPUT},
        {"filter-text-file", required_argument, 0, OPT_FILTER_TEXT_FILE},
        {0, 0, 0, 0}};

    int opt;
    char *font_path_default = NULL;
    char *font_path_large = NULL;
    char *font_path_medium = NULL;
    while ((opt = getopt_long(argc, argv, "a:A:b:B:c:C:d:D:e:f:F:l:L:M:K:r:s:t:T:w:W:XUHS", long_options, NULL)) != -1)
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
            strncpy(state->background_image, optarg, sizeof(state->background_image));
            break;
        case 'B':
            strncpy(state->background_color, optarg, sizeof(state->background_color));
            break;
        case 'c':
            strncpy(state->confirm_button, optarg, sizeof(state->confirm_button) - 1);
            break;
        case 'C':
            strncpy(state->confirm_text, optarg, sizeof(state->confirm_text) - 1);
            break;
        case 'd':
            strncpy(state->cancel_button, optarg, sizeof(state->cancel_button) - 1);
            break;
        case 'D':
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
        case 'H':
            state->show_hardware_group = 0;
            break;
        case 'l':
            strncpy(state->fonts.default_font, optarg, sizeof(state->fonts.default_font) - 1);
            break;
        case 'L':
            strncpy(state->fonts.large_font, optarg, sizeof(state->fonts.large_font) - 1);
            break;
        case 'M':
            strncpy(state->fonts.medium_font, optarg, sizeof(state->fonts.medium_font) - 1);
            break;
        case 'K':
            strncpy(state->item_key, optarg, sizeof(state->item_key) - 1);
            break;
        case 's':
            state->initial_selected = atoi(optarg);
            break;
        case 't':
            strncpy(state->title, optarg, sizeof(state->title) - 1);
            break;
        case 'T':
            strncpy(state->title_alignment, optarg, sizeof(state->title_alignment) - 1);
            break;
        case 'r':
            strncpy(state->scroll_method, optarg, sizeof(state->scroll_method) - 1);
            break;
        case 'w':
            strncpy(state->write_location, optarg, sizeof(state->write_location) - 1);
            break;
        case 'W':
            strncpy(state->write_value, optarg, sizeof(state->write_value) - 1);
            break;
        case 'U':
            state->disable_auto_sleep = true;
            break;
        case 'X':
            state->always_show_confirm = true;
            break;
        case 'S':
            state->alphabetic_scroll = true;
            break;
        case OPT_SCREEN_RESOLUTION:
            strncpy(state->screen_resolution, optarg, sizeof(state->screen_resolution) - 1);
            break;
        case OPT_FALLBACK_IMAGE:
            strncpy(state->fallback_image, optarg, sizeof(state->fallback_image) - 1);
            break;
        case OPT_ALLOW_FILTER:
            if (strcmp(optarg, "true") == 0)
            {
                state->allow_filter = true;
            }
            else if (strcmp(optarg, "false") == 0)
            {
                state->allow_filter = false;
            }
            else
            {
                log_error("Invalid allow-filter value provided. Please provide 'true' or 'false'.");
                return false;
            }
            break;
        case OPT_FILTER_BUTTON:
            strncpy(state->filter_button, optarg, sizeof(state->filter_button) - 1);
            break;
        case OPT_DISPLAY_FILTER_KEYBOARD:
            if (strcmp(optarg, "true") == 0)
            {
                state->display_filter_keyboard = true;
            }
            else if (strcmp(optarg, "false") == 0)
            {
                state->display_filter_keyboard = false;
            }
            else
            {
                log_error("Invalid display-filter-keyboard value provided. Please provide 'true' or 'false'.");
                return false;
            }
            break;
        case OPT_FILTER_INPUT:
            strncpy(state->filter_input, optarg, sizeof(state->filter_input) - 1);
            break;
        case OPT_FILTER_TEXT_FILE:
            strncpy(state->filter_text_file, optarg, sizeof(state->filter_text_file) - 1);
            break;
        default:
            return false;
        }
    }

    // validate title alignment
    if (strcmp(state->title_alignment, "left") != 0 && strcmp(state->title_alignment, "center") != 0 && strcmp(state->title_alignment, "right") != 0)
    {
        log_error("Invalid title alignment provided. Please provide a value of 'left', 'center', or 'right'.");
        return false;
    }

    // validate scroll method
    if (strcmp(state->scroll_method, "false") != 0 && strcmp(state->scroll_method, "wrap") != 0 && strcmp(state->scroll_method, "pong") != 0)
    {
        log_error("Invalid scroll method provided. Please provide a value of 'false', 'wrap', or 'pong'.");
        return false;
    }

    // validate screen resolution format (WIDTHxHEIGHT), when provided
    if (state->screen_resolution[0] != '\0')
    {
        int resolution_width = 0;
        int resolution_height = 0;
        char resolution_extra = '\0';
        if (sscanf(state->screen_resolution, "%dx%d%c", &resolution_width, &resolution_height, &resolution_extra) != 2 || resolution_width <= 0 || resolution_height <= 0)
        {
            log_error("Invalid screen resolution provided. Please provide a value of the form WIDTHxHEIGHT, e.g. '1280x720'.");
            return false;
        }
    }

    if (strcmp(state->format, "") == 0)
    {
        strncpy(state->format, "json", sizeof(state->format) - 1);
    }

    if (strcmp(state->write_value, "") == 0)
    {
        strncpy(state->write_value, "selected", sizeof(state->write_value) - 1);
    }

    // Apply default values for certain buttons and texts
    if (strcmp(state->action_button, "") == 0)
    {
        strncpy(state->action_button, "", sizeof(state->action_button) - 1);
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

    // a hardware button may be assigned to more than one role; presses are
    // resolved at runtime by a fixed precedence (action > cancel/confirm >
    // enable), so no uniqueness check is required here

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

    // validate the filter toggle button; only non-face, non-menu buttons are
    // allowed so it never collides with the A/B/X keyboard controls
    if (filter_button_mask(state->filter_button) == BTN_NONE)
    {
        log_error("Invalid filter button provided. Please provide one of 'SELECT', 'START', 'L1', 'R1', 'L2', or 'R2'.");
        return false;
    }

    if (strlen(state->file) == 0)
    {
        log_error("No input provided");
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
    static int destructed = 0;
    if (destructed)
        return;
    destructed = 1;

    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();
}

// write_to_file writes some text to a file
int write_to_file(const char *filename, const char *text)
{
    FILE *file = fopen(filename, "w");
    if (file == NULL)
    {
        log_error("Failed to open write location");
        return ExitCodeError;
    }

    int num_elements = strlen(text) / sizeof(text[0]);
    fwrite(text, sizeof(char), num_elements, file);
    fclose(file);

    return ExitCodeSuccess;
}

// write_output writes the final text to the write location
int write_output(struct AppState *state)
{
    if (strcmp(state->write_value, "selected") == 0)
    {
        if (state->exit_code != ExitCodeSuccess && state->exit_code != ExitCodeActionButton)
        {
            return state->exit_code;
        }

        // nothing is selected (e.g. an active filter matched no items)
        if (state->list_state->selected < 0)
        {
            return state->exit_code;
        }

        if (strcmp(state->write_location, "-") == 0)
        {
            log_info(state->list_state->items[state->list_state->visible[state->list_state->selected]].name);
        }
        else
        {
            write_to_file(state->write_location, state->list_state->items[state->list_state->visible[state->list_state->selected]].name);
        }
        return state->exit_code;
    }

    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    // map the selected display position back to its source index for output
    int selected_source_index = (state->list_state->selected >= 0)
                                    ? state->list_state->visible[state->list_state->selected]
                                    : -1;
    json_object_set_number(root_object, "selected", selected_source_index);

    JSON_Array *items = json_array(json_value_init_array());
    for (int i = 0; i < state->list_state->item_count; i++)
    {
        JSON_Value *val = json_value_init_object();
        JSON_Object *obj = json_value_get_object(val);

        JSON_Value *features_val = json_value_init_object();
        JSON_Object *features = json_value_get_object(features_val);
        if (json_object_dotset_string(obj, "name", state->list_state->items[i].name) == JSONFailure)
        {
            log_error("Failed to set name");
            return ExitCodeSerializeError;
        }

        if (state->list_state->items[i].features.has_alignment)
        {
            if (json_object_dotset_string(features, "alignment", state->list_state->items[i].features.alignment) == JSONFailure)
            {
                log_error("Failed to set alignment");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_confirm_text)
        {
            if (json_object_dotset_string(features, "confirm_text", state->list_state->items[i].features.confirm_text) == JSONFailure)
            {
                log_error("Failed to set confirm_text");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_can_disable)
        {
            if (json_object_dotset_boolean(features, "can_disable", state->list_state->items[i].features.can_disable) == JSONFailure)
            {
                log_error("Failed to set can_disable");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_disabled || state->list_state->items[i].features.has_can_disable)
        {
            if (json_object_dotset_boolean(features, "disabled", state->list_state->items[i].features.disabled))
            {
                log_error("Failed to set enabled");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_draw_arrows)
        {
            if (json_object_dotset_boolean(features, "draw_arrows", state->list_state->items[i].features.draw_arrows))
            {
                log_error("Failed to set draw_arrows");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_hide_action)
        {
            if (json_object_dotset_boolean(features, "hide_action", state->list_state->items[i].features.hide_action))
            {
                log_error("Failed to set hide_action");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_hide_cancel)
        {
            if (json_object_dotset_boolean(features, "hide_cancel", state->list_state->items[i].features.hide_cancel))
            {
                log_error("Failed to set hide_cancel");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_hide_confirm)
        {
            if (json_object_dotset_boolean(features, "hide_confirm", state->list_state->items[i].features.hide_confirm))
            {
                log_error("Failed to set hide_confirm");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_show_confirm)
        {
            if (json_object_dotset_boolean(features, "show_confirm", state->list_state->items[i].features.show_confirm))
            {
                log_error("Failed to set show_confirm");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_is_header)
        {
            if (json_object_dotset_boolean(features, "is_header", state->list_state->items[i].features.is_header))
            {
                log_error("Failed to set is_header");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_unselectable)
        {
            if (json_object_dotset_boolean(features, "unselectable", state->list_state->items[i].features.unselectable))
            {
                log_error("Failed to set unselectable");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].features.has_display_on_filter)
        {
            if (json_object_dotset_boolean(features, "display_on_filter", state->list_state->items[i].features.display_on_filter))
            {
                log_error("Failed to set display_on_filter");
                return ExitCodeSerializeError;
            }
        }

        if (state->list_state->items[i].has_options)
        {
            if (json_object_dotset_number(obj, "selected", state->list_state->items[i].selected) == JSONFailure)
            {
                log_error("Failed to set selected");
                return ExitCodeSerializeError;
            }

            JSON_Array *options = json_array(json_value_init_array());
            for (int j = 0; j < state->list_state->items[i].option_count; j++)
            {
                JSON_Value *option = json_value_init_string(state->list_state->items[i].options[j]);
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

        // this should always go last
        if (state->list_state->items[i].has_features)
        {
            JSON_Value *features_value = json_object_get_wrapping_value(features);
            if (json_object_dotset_value(obj, "features", features_value) == JSONFailure)
            {
                log_error("Failed to set features");
                return ExitCodeSerializeError;
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
    if (json_object_dotset_value(root_object, state->item_key, items_value) == JSONFailure)
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

    if (strcmp(state->write_location, "-") == 0)
    {
        log_info(serialized_string);
    }
    else
    {
        write_to_file(state->write_location, serialized_string);
    }

    json_free_serialized_string(serialized_string);
    json_value_free(root_value);

    return state->exit_code;
}

// main is the entry point for the app
int main(int argc, char *argv[])
{
    // Initialize app state
    char default_action_button[1024] = "";
    char default_action_text[1024] = "ACTION";
    char default_background_image[1024] = "";
    char default_background_color[1024] = "#000000";
    char default_cancel_button[1024] = "B";
    char default_cancel_text[1024] = "BACK";
    char default_enable_button[1024] = "Y";
    char default_confirm_button[1024] = "A";
    char default_confirm_text[1024] = "SELECT";
    char default_file[1024] = "";
    char default_format[1024] = "json";
    char default_item_key[1024] = "items";
    char default_write_value[1024] = "selected";
    char default_title[1024] = "";
    char default_title_alignment[1024] = "left";
    char default_scroll_method[1024] = "false";
    char default_write_location[1024] = "-";
    char default_filter_button[1024] = "SELECT";
    struct AppState state = {
        .exit_code = ExitCodeSuccess,
        .quitting = 0,
        .redraw = 1,
        .show_hardware_group = 1,
        .show_brightness_setting = 0,
        .always_show_confirm = false,
        .disable_auto_sleep = false,
        .initial_selected = -1,
        .scroll_anim_selected = -1,
        .scroll_anim_option = -1,
        .fonts = {
            .large = NULL,
            .medium = NULL,
            .default_font = NULL,
            .large_font = NULL,
            .medium_font = NULL,
        },
        .list_state = NULL};

    // assign the default values to the app state
    strncpy(state.action_button, default_action_button, sizeof(state.action_button) - 1);
    strncpy(state.action_text, default_action_text, sizeof(state.action_text) - 1);
    strncpy(state.background_image, default_background_image, sizeof(state.background_image));
    strncpy(state.background_color, default_background_color, sizeof(state.background_color));
    strncpy(state.cancel_button, default_cancel_button, sizeof(state.cancel_button) - 1);
    strncpy(state.cancel_text, default_cancel_text, sizeof(state.cancel_text) - 1);
    strncpy(state.confirm_button, default_confirm_button, sizeof(state.confirm_button) - 1);
    strncpy(state.confirm_text, default_confirm_text, sizeof(state.confirm_text) - 1);
    strncpy(state.enable_button, default_enable_button, sizeof(state.enable_button) - 1);
    strncpy(state.file, default_file, sizeof(state.file) - 1);
    strncpy(state.format, default_format, sizeof(state.format) - 1);
    strncpy(state.item_key, default_item_key, sizeof(state.item_key) - 1);
    strncpy(state.write_value, default_write_value, sizeof(state.write_value) - 1);
    strncpy(state.title, default_title, sizeof(state.title) - 1);
    strncpy(state.title_alignment, default_title_alignment, sizeof(state.title_alignment) - 1);
    strncpy(state.scroll_method, default_scroll_method, sizeof(state.scroll_method) - 1);
    strncpy(state.write_location, default_write_location, sizeof(state.write_location) - 1);
    strncpy(state.filter_button, default_filter_button, sizeof(state.filter_button) - 1);

    // parse the arguments
    if (!parse_arguments(&state, argc, argv))
    {
        return ExitCodeError;
    }

    state.list_state = ListState_New(state.file, state.format, state.item_key, state.confirm_text, state.background_image, state.background_color, &state);
    if (state.list_state == NULL)
    {
        log_error("Failed to create list state");
        return ExitCodeError;
    }

    // CLI --selected overrides JSON selected
    if (state.initial_selected >= 0)
    {
        state.list_state->selected = state.initial_selected;
    }

    // Sort items alphabetically if alphabetic_scroll is enabled
    if (state.alphabetic_scroll && state.list_state->item_count > 0)
    {
        // remember the selected item's name so we can find it after sorting
        const char *selected_name = NULL;
        if (state.list_state->selected >= 0 && state.list_state->selected < (int)state.list_state->item_count)
        {
            selected_name = state.list_state->items[state.list_state->selected].name;
        }

        qsort(state.list_state->items,
              state.list_state->item_count,
              sizeof(struct ListItem),
              compare_items_alphabetic);

        // restore selection to the same item by name
        if (selected_name != NULL)
        {
            state.list_state->selected = -1;
            for (size_t i = 0; i < state.list_state->item_count; i++)
            {
                if (strcmp(state.list_state->items[i].name, selected_name) == 0)
                {
                    state.list_state->selected = (int)i;
                    break;
                }
            }
        }
    }

    // swallow all stdout from init calls
    // MinUI will sometimes randomly log to stdout
    // NOTE: must call init() before using MAIN_ROW_COUNT, as it depends
    // on runtime platform detection (e.g. is_brick on tg5040)
    swallow_stdout_from_function(init);
    atexit(destruct);

    // compute max_row_count after init, since MAIN_ROW_COUNT may depend on runtime state
    state.max_row_count = MAIN_ROW_COUNT;
    if (strlen(state.title) > 0)
    {
        state.max_row_count -= 1;
    }

    // validate selection and compute initial visible window
    ListState_InitView(state.list_state, state.max_row_count);

    // resolve per-item images now that init() has run and FIXED_WIDTH/HEIGHT
    // reflect the real device resolution
    ListState_ResolveImages(state.list_state, &state);

    if (state.list_state->item_count == 0 || state.list_state->selected < 0)
    {
        log_error("No selectable items found");
        return ExitCodeError;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!open_fonts(&state))
    {
        log_error("Failed to open fonts");
        return ExitCodeError;
    }

    // seed the initial filter and optionally open the keyboard, now that the
    // list, fonts, and screen are ready. This runs after the selectable-items
    // guard so an initial filter that matches nothing does not abort startup;
    // the main loop and draw path tolerate an empty (selected == -1) result.
    if (state.allow_filter)
    {
        if (state.filter_input[0] != '\0')
        {
            strncpy(state.filter_text, state.filter_input, sizeof(state.filter_text) - 1);
            ListState_ApplyFilter(state.list_state, state.filter_text, state.max_row_count);
        }
        if (state.display_filter_keyboard)
        {
            open_filter_keyboard(&state);
        }
    }

    // get initial wifi state
    int was_online = PLAT_isOnline();

    // draw the screen at least once
    // handle_input sets state.redraw to 0 if no key is pressed
    int was_ever_drawn = 0;

    if (state.disable_auto_sleep)
    {
        PWR_disableAutosleep();
    }

    while (!state.quitting && !signal_exit_code)
    {
        // start the frame to ensure GFX_sync() works
        // on devices that don't support vsync
        GFX_startFrame();

        // handle turning the on/off screen on/off
        // as well as general power management
        // the second argument receives the active settings overlay
        // (0 = none, 1 = brightness, 2 = volume) so the volume/brightness
        // bar and hint can be drawn while the user changes those settings
        PWR_update(&state.redraw, &state.show_brightness_setting, NULL, NULL);
        bool power_redraw = false;
        if (state.redraw)
        {
            power_redraw = true;
        }

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

        // force a redraw if the power state changed
        if (power_redraw)
        {
            state.redraw = 1;
        }

        // keep redrawing while a row is autoscrolling so the marquee animates.
        // this is self-sustaining: draw_screen re-arms scroll_active each frame
        // it draws a scrolling row, and clears it once nothing is scrolling.
        if (state.scroll_active)
        {
            state.redraw = 1;
        }

        // redraw the screen if there has been a change
        if (state.redraw)
        {
            // clear the screen at the beginning of each loop
            GFX_clear(screen);

            bool should_draw_background_image = draw_background(screen, &state);

            int ow = 0;
            if (state.show_hardware_group)
            {
                // draw the hardware information in the top-right (battery/wifi,
                // or the volume/brightness bar while a setting is being changed)
                ow = GFX_blitHardwareGroup(screen, state.show_brightness_setting);
            }

            // decide what belongs in the bottom-left pill slot
            switch (BottomLeftHint_For(state.show_hardware_group, state.show_brightness_setting, has_left_button_group(&state, state.list_state), GetHDMI()))
            {
            case BOTTOM_LEFT_SETTING_HINT:
                // draw the volume/brightness setting hint
                GFX_blitHardwareHints(screen, state.show_brightness_setting);
                break;
            case BOTTOM_LEFT_SLEEP:
                GFX_blitButtonGroup((char *[]){BTN_SLEEP == BTN_POWER ? "POWER" : "MENU", "SLEEP", NULL}, 0, screen, 0);
                break;
            case BOTTOM_LEFT_NONE:
                break;
            }

            // your draw logic goes here
            draw_screen(screen, &state, ow, should_draw_background_image);

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

    if (signal_exit_code)
    {
        swallow_stdout_from_function(destruct);
        return signal_exit_code;
    }

    int exit_code = write_output(&state);

    // when filtering is enabled, emit the final filter value: to --filter-text-file
    // if set, and always as the last line of stderr. This runs regardless of the
    // exit code (signal-driven exits are handled above and skip this).
    if (state.allow_filter)
    {
        if (state.filter_text_file[0] != '\0')
        {
            write_to_file(state.filter_text_file, state.filter_text);
        }
        fprintf(stderr, "%s\n", state.filter_text);
    }

    if (exit_code != ExitCodeSuccess)
    {
        return exit_code;
    }

    swallow_stdout_from_function(destruct);

    // exit the program
    return state.exit_code;
}
