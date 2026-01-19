CURRENT_WORKING_DIR = $(shell pwd)

PLATFORM ?= tg5040
MINUI_VERSION ?= v20251023-0

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
    $(error missing CROSS_COMPILE for this toolchain)
  endif
  CC = $(CROSS_COMPILE)gcc
  PREFIX = $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)
  PLATFORM_DIR = minui/workspace/$(PLATFORM)/platform
  LD_LIBRARY_PATH = $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)/lib/
  -include minui/workspace/$(PLATFORM)/platform/makefile.env
endif
SDL?=SDL

TARGET = minui-list
PRODUCT = $(TARGET)

# macOS-specific configuration
ifeq ($(PLATFORM),macos)
  INCDIR = -I. -Iplatforms/macos/include/ -Iminui/workspace/all/common/ -Iplatforms/macos/platform/ -Iinclude/ $(SDL_CFLAGS)
  SOURCE = $(TARGET).c minui/workspace/all/common/scaler.c minui/workspace/all/common/utils.c minui/workspace/all/common/api.c platforms/macos/platform/platform.c include/parson/parson.c
  CFLAGS = $(ARCH) -fomit-frame-pointer
  CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -DUSE_$(SDL) -Ofast -std=gnu99 -Wno-tautological-constant-out-of-range-compare
  FLAGS = $(LIBS) $(SDL_LIBS) -lpthread -lm -lz
else
  INCDIR = -I. -Iplatform/$(PLATFORM)/include/ -Iminui/workspace/all/common/ -Iminui/workspace/$(PLATFORM)/platform/ -Iinclude/
  SOURCE = $(TARGET).c minui/workspace/all/common/scaler.c minui/workspace/all/common/utils.c minui/workspace/all/common/api.c minui/workspace/$(PLATFORM)/platform/platform.c include/parson/parson.c
  CFLAGS = $(ARCH) -fomit-frame-pointer
  CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -DUSE_$(SDL) -Ofast -std=gnu99
  FLAGS = -L$(LD_LIBRARY_PATH) -ldl -lmsettings $(LIBS) -l$(SDL) -l$(SDL)_image -l$(SDL)_ttf -lpthread -lm -lz
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
	git clone --branch $(MINUI_VERSION) https://github.com/shauninman/MinUI minui

platform/$(PLATFORM)/lib:
	mkdir -p platform/$(PLATFORM)/lib

platform/$(PLATFORM)/include:
	mkdir -p platform/$(PLATFORM)/include

# PREFIX is the path to the workspace (not used for macOS)
ifneq ($(PLATFORM),macos)
$(PREFIX)/include/msettings.h: platform/$(PLATFORM)/lib platform/$(PLATFORM)/include
	cd $(CURRENT_WORKING_DIR)/minui/workspace/$(PLATFORM)/libmsettings && make
endif

include/parson:
	mkdir -p include
	git clone https://github.com/kgabis/parson.git include/parson
