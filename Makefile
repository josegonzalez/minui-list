CURRENT_WORKING_DIR = $(shell pwd)

PLATFORM ?= tg5040
MINUI_VERSION ?= v20251023-0
NEXTUI_VERSION ?= v6.14.0
MY355_NEXTUI_VERSION ?= my355-latest
H700_VERSION ?= h700-rc3

# WORKSPACE is the upstream workspace directory name and the runtime device id
# baked into the binary via -DPLATFORM. It matches PLATFORM for every platform
# except the NextUI variants, whose PLATFORM carries a "-nextui" suffix (e.g.
# tg5040-nextui) while their upstream workspace and on-device id remain the bare
# device (e.g. tg5040), so that .system/.userdata paths resolve on the device.
WORKSPACE = $(PLATFORM)
# IS_NEXTUI is set for platforms that build against a NextUI SDK/toolchain.
IS_NEXTUI =

# Determine upstream repository based on platform
ifeq ($(PLATFORM),tg5040-nextui)
  UPSTREAM_REPO = https://github.com/loveRetro/NextUI
  UPSTREAM_VERSION = $(NEXTUI_VERSION)
  WORKSPACE = tg5040
  IS_NEXTUI = 1
else ifeq ($(PLATFORM),my355-nextui)
  UPSTREAM_REPO = https://github.com/loveRetro/NextUI
  UPSTREAM_VERSION = $(MY355_NEXTUI_VERSION)
  WORKSPACE = my355
  IS_NEXTUI = 1
else ifeq ($(PLATFORM),tg5050-nextui)
  UPSTREAM_REPO = https://github.com/loveRetro/NextUI
  UPSTREAM_VERSION = $(NEXTUI_VERSION)
  WORKSPACE = tg5050
  IS_NEXTUI = 1
else ifeq ($(PLATFORM),h700-nextui)
  UPSTREAM_REPO = https://github.com/pvaibhav/NextUI
  UPSTREAM_VERSION = $(H700_VERSION)
  WORKSPACE = h700
  IS_NEXTUI = 1
else
  UPSTREAM_REPO = https://github.com/shauninman/MinUI
  UPSTREAM_VERSION = $(MINUI_VERSION)
endif

# macOS native build configuration
ifeq ($(PLATFORM),macos)
  CC = clang
  SDL_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf)
  SDL_LIBS = $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf)
  PREFIX = $(CURRENT_WORKING_DIR)/platforms/macos
  PLATFORM_DIR = platforms/macos/platform
  LD_LIBRARY_PATH =
  -include platforms/macos/platform/makefile.env
else
  ifeq (,$(CROSS_COMPILE))
    # the host-only targets below do not cross-compile, so they don't need a toolchain
    ifeq (,$(filter test clean print-%,$(MAKECMDGOALS)))
      $(error missing CROSS_COMPILE for this toolchain)
    endif
  endif
  CC = $(CROSS_COMPILE)gcc
  PREFIX = $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)
  PLATFORM_DIR = minui/workspace/$(WORKSPACE)/platform
  LD_LIBRARY_PATH = $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)/lib/
  -include minui/workspace/$(WORKSPACE)/platform/makefile.env
endif
SDL?=SDL

TARGET = minui-list
PRODUCT = $(TARGET)

# macOS-specific configuration
ifeq ($(PLATFORM),macos)
  INCDIR = -I. -Iplatforms/macos/include/ -Iminui/workspace/all/common/ -Iplatforms/macos/platform/ -Iinclude/ $(SDL_CFLAGS)
  SOURCE = $(TARGET).c list_filter.c list_hint.c list_image.c list_keyboard.c list_nav.c list_scroll.c list_theme.c minui/workspace/all/common/scaler.c minui/workspace/all/common/utils.c minui/workspace/all/common/api.c platforms/macos/platform/platform.c include/parson/parson.c
  CFLAGS = $(ARCH) -fomit-frame-pointer
  CFLAGS += $(INCDIR) -DPLATFORM=\"$(WORKSPACE)\" -DUSE_$(SDL) -O3 -std=gnu99 -Wno-tautological-constant-out-of-range-compare -Wno-asm-operand-widths
  FLAGS = $(LIBS) $(SDL_LIBS) -lpthread -lm -lz
else
  INCDIR = -I. -Iplatform/$(PLATFORM)/include/ -Iminui/workspace/all/common/ -Iminui/workspace/$(WORKSPACE)/platform/ -Iinclude/
  SOURCE = $(TARGET).c list_filter.c list_hint.c list_image.c list_keyboard.c list_nav.c list_scroll.c list_theme.c minui/workspace/all/common/scaler.c minui/workspace/all/common/utils.c minui/workspace/all/common/api.c minui/workspace/$(WORKSPACE)/platform/platform.c include/parson/parson.c
  FLAGS = -L$(LD_LIBRARY_PATH) -ldl -lmsettings $(LIBS) -l$(SDL) -l$(SDL)_image -l$(SDL)_ttf -lpthread -lm -lz
  # NextUI toolchains install libmsettings and the GLES stack to /opt/nextui.
  # api.c resamples audio through libsamplerate on every NextUI target. tg5050
  # and my355 additionally need the standalone mali blob linked explicitly
  # because their libGLESv2 is a stub that pulls symbols from libmali; tg5040
  # and h700 resolve libGLESv2 symbols directly.
  ifneq (,$(IS_NEXTUI))
    INCDIR += -I/opt/nextui/include
    NEXTUI_GL_LIBS = -lGLESv2 -lsamplerate
    ifneq (,$(filter $(PLATFORM),tg5050-nextui my355-nextui))
      NEXTUI_GL_LIBS = -lGLESv2 -lmali -lsamplerate
    endif
    FLAGS += -L/opt/nextui/lib $(NEXTUI_GL_LIBS)
    CFLAGS += $(INCDIR) -DPLATFORM=\"$(WORKSPACE)\" -DPLATFORM_NEXTUI
    SOURCE += minui/workspace/all/common/config.c
  else
    CFLAGS = $(ARCH) -fomit-frame-pointer
    CFLAGS += $(INCDIR) -DPLATFORM=\"$(WORKSPACE)\" -DUSE_$(SDL) -Ofast -std=gnu99
  endif
endif

# Build targets
ifeq ($(PLATFORM),macos)
all: minui include/parson
	$(CC) $(SOURCE) -o $(PRODUCT)-$(PLATFORM) $(CFLAGS) $(FLAGS)
else
all: minui $(PREFIX)/include/msettings.h include/parson
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(CC) $(SOURCE) -o $(PRODUCT)-$(PLATFORM) $(CFLAGS) $(FLAGS)
endif

# Setup target - macOS doesn't need libmsettings
ifeq ($(PLATFORM),macos)
setup: minui include/parson
else
setup: minui $(PREFIX)/include/msettings.h include/parson
ifeq ($(PLATFORM),my282)
	cd $(CURRENT_WORKING_DIR)/minui/workspace/$(PLATFORM)/libmstick && make
	cp $(CURRENT_WORKING_DIR)/minui/workspace/$(PLATFORM)/libmstick/libmstick.so $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)/lib/
endif
endif

clean:
	rm -rf $(PRODUCT)-$(PLATFORM)

# Print the value of any make variable, e.g. `make print-UPSTREAM_REPO PLATFORM=tg5040-nextui`
# Used by tests/makefile.bats to assert the per-platform build wiring.
print-%:
	@echo '$*=$($*)'

# Build and run the pure-C unit tests with the host compiler (no SDL required)
TEST_CC ?= cc
test:
	mkdir -p tmp
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_nav_test.c list_nav.c -o tmp/list_nav_test
	./tmp/list_nav_test
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_scroll_test.c list_scroll.c -o tmp/list_scroll_test
	./tmp/list_scroll_test
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_hint_test.c list_hint.c -o tmp/list_hint_test
	./tmp/list_hint_test
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_image_test.c list_image.c -o tmp/list_image_test
	./tmp/list_image_test
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_filter_test.c list_filter.c -o tmp/list_filter_test
	./tmp/list_filter_test
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_keyboard_test.c list_keyboard.c -o tmp/list_keyboard_test
	./tmp/list_keyboard_test
	$(TEST_CC) -std=gnu99 -Wall -Wextra -I. tests/list_theme_test.c list_theme.c -o tmp/list_theme_test
	./tmp/list_theme_test

# macOS resource setup - copies MinUI assets to the SDCARD_PATH location
setup-resources: minui
ifeq ($(PLATFORM),macos)
	mkdir -p /tmp/FAKESD/.system/res
	cp minui/skeleton/SYSTEM/res/assets@2x.png /tmp/FAKESD/.system/res/
	cp minui/skeleton/SYSTEM/res/BPreplayBold-unhinted.otf /tmp/FAKESD/.system/res/
	@echo "Resources installed to /tmp/FAKESD/.system/res"
else
	@echo "setup-resources is only needed for macOS builds"
endif

minui:
	git clone --branch $(UPSTREAM_VERSION) $(UPSTREAM_REPO) minui

platform/$(PLATFORM)/lib:
	mkdir -p platform/$(PLATFORM)/lib

platform/$(PLATFORM)/include:
	mkdir -p platform/$(PLATFORM)/include

# PREFIX is the path to the workspace (not used for macOS)
ifneq ($(PLATFORM),macos)
$(PREFIX)/include/msettings.h: platform/$(PLATFORM)/lib platform/$(PLATFORM)/include
	cd $(CURRENT_WORKING_DIR)/minui/workspace/$(WORKSPACE)/libmsettings && make
endif

include/parson:
	mkdir -p include
	git clone https://github.com/kgabis/parson.git include/parson
