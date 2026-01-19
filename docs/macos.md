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
| Start | Enter |
| Select | ' (apostrophe) |
| Menu | Space |
| Power | Backspace |

The L1/L2/R1/R2/L3/R3 and Plus/Minus buttons are not mapped.

## Quitting

Use **Cmd+Q** to quit the application.

## Window

The application opens an 800x600 window (4:3 aspect ratio) which is resizable.
