# macOS Build

This document describes how to build and run minui-list natively on macOS.

## Prerequisites

Install SDL2 dependencies via Homebrew:

```bash
brew install sdl2 sdl2_image sdl2_ttf pkg-config
```

## Building

```bash
# Build for macOS
PLATFORM=macos make

# Set up the resource directory (required for running)
PLATFORM=macos make setup-resources
```

## Running

```bash
./minui-list-macos --format text --file input.txt
```

The application requires MinUI resources to be present at `/tmp/FAKESD/.system/res/`. The `setup-resources` target copies these automatically from the MinUI repository.

## Keyboard Mappings

The following keyboard keys are mapped to controller buttons:

| Button | Keyboard Key |
|--------|--------------|
| D-Pad Up | Arrow Up |
| D-Pad Down | Arrow Down |
| D-Pad Left | Arrow Left |
| D-Pad Right | Arrow Right |
| A | S |
| B | A |
| X | W |
| Y | Q |
| L1 | E |
| R1 | R |
| Start | Enter |
| Select | ' (apostrophe) |
| Menu | Space |
| Power | Backspace |
| Minus | - (hyphen) |
| Plus | = (equals) |

L1 (E) and R1 (R) drive alphabetical scrolling when `--alphabetic-scroll` is enabled: R1 jumps
to the next letter group and L1 to the previous one, wrapping around at the ends of the list.

Minus (-) and Plus (=) adjust the volume and raise the settings overlay: the level bar in the
top-right and a hint pill in the bottom-left. On macOS these are wired only so the overlay can be
exercised; there is no backend to actually change the system volume, so the bar shows a fixed
representative level. Brightness uses the Menu (Space) modifier on device, but minui-list exits on
Menu release, so brightness is not a clean interaction on macOS.

The L2/R2/L3/R3 buttons are not mapped.

## Quitting

Use **Cmd+Q** to quit the application.

## Window

The application opens an 800x600 window (4:3 aspect ratio) which is resizable.
