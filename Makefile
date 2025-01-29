ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

CURRENT_WORKING_DIR = $(shell pwd)

PLATFORM ?= tg5040
LD_LIBRARY_PATH = $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)/lib/
PREFIX = $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)

-include minui/workspace/$(PLATFORM)/platform/makefile.env
SDL?=SDL

TARGET = minui-list
PRODUCT = $(TARGET)

INCDIR = -I. -Iplatform/$(PLATFORM)/include/ -Iminui/workspace/all/common/ -Iminui/workspace/$(PLATFORM)/platform/ -Iinclude/
SOURCE = $(TARGET).c minui/workspace/all/common/scaler.c minui/workspace/all/common/utils.c minui/workspace/all/common/api.c minui/workspace/$(PLATFORM)/platform/platform.c include/parson/parson.c

CC = $(CROSS_COMPILE)gcc
CFLAGS   = $(ARCH) -fomit-frame-pointer
CFLAGS  += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -DUSE_$(SDL) -Ofast -std=gnu99
FLAGS = -L$(LD_LIBRARY_PATH) -ldl -lmsettings $(LIBS) -l$(SDL) -l$(SDL)_image -l$(SDL)_ttf -lpthread -lm -lz

all: minui $(PREFIX)/include/msettings.h include/parson
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(CC) $(SOURCE) -o $(PRODUCT)-$(PLATFORM) $(CFLAGS) $(FLAGS)

setup: minui $(PREFIX)/include/msettings.h include/parson
ifeq ($(PLATFORM),my282)
	cd $(CURRENT_WORKING_DIR)/minui/workspace/$(PLATFORM)/libmstick && make
	cp $(CURRENT_WORKING_DIR)/minui/workspace/$(PLATFORM)/libmstick/libmstick.so $(CURRENT_WORKING_DIR)/platform/$(PLATFORM)/lib/
endif

clean:
	rm -rf $(PRODUCT)-$(PLATFORM)

minui:
	git clone https://github.com/shauninman/MinUI minui

platform/$(PLATFORM)/lib:
	mkdir -p platform/$(PLATFORM)/lib

platform/$(PLATFORM)/include:
	mkdir -p platform/$(PLATFORM)/include

# PREFIX is the path to the workspace
$(PREFIX)/include/msettings.h: platform/$(PLATFORM)/lib platform/$(PLATFORM)/include
	cd $(CURRENT_WORKING_DIR)/minui/workspace/$(PLATFORM)/libmsettings && make

include/parson:
	mkdir -p include
	git clone https://github.com/kgabis/parson.git include/parson
