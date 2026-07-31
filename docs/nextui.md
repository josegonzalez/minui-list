# NextUI builds

Some devices run NextUI, a fork of MinUI, instead of (or in addition to) MinUI. NextUI ships a different SDK, so those devices need a binary built against a NextUI toolchain. These NextUI-specific binaries carry a `-nextui` suffix in their platform id and artifact name.

## Platform matrix

| Platform id     | Firmware | Upstream repo              | Version / ref (Makefile var)          | Workspace dir | Toolchain image                       |
|-----------------|----------|----------------------------|---------------------------------------|---------------|---------------------------------------|
| `tg5040-nextui` | NextUI   | `loveRetro/NextUI`         | `v6.14.0` (`NEXTUI_VERSION`)          | `tg5040`      | `savant/minui-toolchain:tg5040-nextui` |
| `my355-nextui`  | NextUI   | `loveRetro/NextUI`         | `my355-latest` (`MY355_NEXTUI_VERSION`) | `my355`     | `savant/minui-toolchain:my355-nextui`  |
| `tg5050-nextui` | NextUI   | `loveRetro/NextUI`         | `v6.14.0` (`NEXTUI_VERSION`)          | `tg5050`      | `savant/minui-toolchain:tg5050-nextui` |
| `h700-nextui`   | NextUI   | `pvaibhav/NextUI`          | `h700-rc3` (`H700_VERSION`)           | `h700`        | `savant/minui-toolchain:h700-nextui`   |

`tg5040` and `my355` also have MinUI builds (`minui-list-tg5040`, `minui-list-my355`) since those devices run both firmwares. `tg5050` and `h700` are NextUI-only.

The `my355` workspace only exists on the `my355-latest` branch of `loveRetro/NextUI` (no tagged release contains it), so it uses its own version variable.

## How the build is wired

The Makefile keeps the build platform id (`PLATFORM`) separate from the upstream workspace directory and the on-device id:

- `WORKSPACE` is the upstream workspace directory name and the runtime device id. It equals `PLATFORM` for every platform except the `-nextui` variants, where it is the bare device (for example `PLATFORM=tg5040-nextui` builds `WORKSPACE=tg5040`).
- `IS_NEXTUI` is set for NextUI platforms. It gates the `/opt/nextui` include and lib paths, the `-DPLATFORM_NEXTUI` define, the extra `config.c` source, and the GLES link flags.

`-DPLATFORM` is compiled into the on-device `SYSTEM_PATH` and `USERDATA_PATH` (`.system/<PLATFORM>` and `.userdata/<PLATFORM>`), so it is driven by `WORKSPACE`. A `tg5040-nextui` binary therefore reports `tg5040` at runtime and resolves the same on-card paths as the device firmware.

The source-level NextUI differences all live in `minui-list.c`, gated by `-DPLATFORM_NEXTUI`:

- `PLAT_isOnline` is mapped to `PWR_isOnline`, which NextUI's SDK uses for online detection.
- the UI is re-colored from the user's NextUI theme (see [Theming](#theming) below).

### GLES and audio libraries

NextUI toolchains install `libmsettings` and the GLES stack under `/opt/nextui`. Every NextUI target links `libsamplerate`, which `api.c` uses to resample audio. The linked libraries differ per device (`NEXTUI_GL_LIBS`):

- `tg5040-nextui` and `h700-nextui`: `-lGLESv2 -lsamplerate`
- `tg5050-nextui` and `my355-nextui`: `-lGLESv2 -lmali -lsamplerate` (their `libGLESv2` is a stub backed by a standalone mali blob that must be linked explicitly)

## Theming

NextUI lets the user pick theme colors and a font (stored in `minuisettings.txt`). The `-nextui` binaries honor that theme, so a `minui-list` page matches the rest of the NextUI menu instead of the fixed greyscale MinUI palette. MinUI and macOS builds are unaffected: every theme reference is confined to `#ifdef PLATFORM_NEXTUI` helpers in `minui-list.c`, so those builds still use the greyscale palette and compile unchanged.

Nothing new has to be initialized. The existing `GFX_init(MODE_MAIN)` call already runs the NextUI SDK's `CFG_init`, which reads the theme and populates `THEME_COLOR1..7` (screen-mapped) and `THEME_COLOR1_255..7_255` (packed `0xRRGGBBAA`), the themed `font.*`, and the themed clear color. The app just references those globals when drawing. NextUI's color slots (`config.h`) map to the UI as follows:

| Theme slot (default)              | Where it is used                                                        |
|-----------------------------------|-------------------------------------------------------------------------|
| `COLOR_MAIN` (white)              | the selected-row pill and the focused filter-keyboard key               |
| `COLOR_ACCENT` (magenta)          | the filter match-highlight rectangle                                    |
| `COLOR_ACCENT2` (dark navy)       | the option-value track pill, the keyboard input field, unfocused keys   |
| `COLOR_LIST_TEXT` (white)         | unselected row text, option-value text, the title, keyboard input/labels |
| `COLOR_LIST_TEXT_SELECTED` (black)| selected row text, match-highlight text, focused-key text               |
| `COLOR_HINT` (white)              | hardware/button hints (already themed by the SDK's `GFX_blitButton*`)   |
| `COLOR_BACKGROUND` (black)        | the default background fill, when no per-item background is set          |

The disabled and header/unselectable rows keep static greys, matching NextUI's own menus. Fonts follow the theme automatically: when no `--font-*` override is given, `open_fonts()` falls back to the SDK `font.*`, which `GFX_init` loaded from the themed font. The background falls back to `COLOR_BACKGROUND` only when an item sets no `background_color`/`background_image`; explicit `--background-color`, JSON `background_color`, and `background_image` still override it. Under the default theme the result looks the same as the MinUI greyscale; the theme only diverges once the user customizes it.

The row-color precedence (which text role a row uses) is factored into the SDL-free `list_theme.c` module and unit tested by `tests/list_theme_test.c` (run via `make test`). `tests/makefile.bats` asserts `list_theme.c` is compiled into every platform.

## Building

Build a NextUI variant with its platform id inside the matching toolchain:

```bash
PLATFORM=tg5040-nextui make setup
PLATFORM=tg5040-nextui make
```

This produces `minui-list-tg5040-nextui`.

## Testing the wiring

`tests/makefile.bats` asserts the per-platform Makefile wiring (upstream repo, version, workspace, `-DPLATFORM_NEXTUI`, device id, sources, and GLES libs) by introspecting the Makefile with `make print-<VAR> PLATFORM=<p>`. It needs neither a toolchain nor a cloned upstream tree:

```bash
bats tests/makefile.bats
```

The CI matrix builds every NextUI binary in its `savant/minui-toolchain:<device>-nextui` container, which is the integration test for the full compile and link.
