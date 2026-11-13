# Builds the macOS port of BG3 WASD Everywhere (Apple Silicon) and symtest, a host check of its
# Mach-O, AArch64 and hotkey code. On Linux, only symtest is built.
NAME := bg3-wasd-everywhere
VERSION := 0.3.1
BUILD := build
LIBRARY := $(BUILD)/lib$(NAME).dylib
# Copied once, so edits to it survive rebuilds.
CONFIG := $(BUILD)/$(NAME).toml

CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Isrc -I../linux/src -DBG3WASD_VERSION=\"$(VERSION)\"
MAC_FLAGS := -arch arm64 -mmacosx-version-min=11.0
# Shared with the Linux port: the log, the key bindings in inputconfig_p1.json, the config file, the
# hotkey rules and the camera tweaks.
SHARED := ../linux/src/log.cpp ../linux/src/inputconfig.cpp ../linux/src/settings_file.cpp ../linux/src/hotkeys.cpp \
	../linux/src/camera_tweaks.cpp
HEADERS := $(wildcard src/*.hpp) $(wildcard ../linux/src/*.hpp) ../linux/src/keymap.inc
SOURCES := src/main.mm src/keycodes.cpp src/macho.cpp src/arm64.cpp src/patterns.cpp $(SHARED)
# What symtest checks on any host.
TESTED := src/macho.cpp src/arm64.cpp src/patterns.cpp src/keycodes.cpp ../linux/src/log.cpp \
	../linux/src/settings_file.cpp ../linux/src/hotkeys.cpp

LAUNCHER := $(BUILD)/$(NAME)-launch

ifeq ($(shell uname -s),Darwin)
all: $(LIBRARY) $(LAUNCHER) $(CONFIG) $(BUILD)/symtest
else
all: $(BUILD)/symtest
endif

$(BUILD)/stubs.o: src/stubs.S Makefile | $(BUILD)
	$(CC) $(MAC_FLAGS) -c -o $@ $<

# The game isn't linked against: the library finds its functions by name at runtime.
$(LIBRARY): $(SOURCES) $(BUILD)/stubs.o $(HEADERS) Makefile
	$(CXX) $(MAC_FLAGS) $(CXXFLAGS) -fvisibility=hidden -fvisibility-inlines-hidden -dynamiclib \
		-framework Foundation -framework AppKit -framework CoreGraphics -o $@ $(SOURCES) $(BUILD)/stubs.o

# Steam runs it from the launch options: see src/launch.cpp.
$(LAUNCHER): src/launch.cpp Makefile | $(BUILD)
	$(CXX) $(MAC_FLAGS) $(CXXFLAGS) -framework CoreFoundation -o $@ $<

$(CONFIG): | $(BUILD)
	cp $(NAME).toml $@

$(BUILD)/symtest: tests/symtest.cpp $(TESTED) $(HEADERS) Makefile | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/symtest.cpp $(TESTED)

# A stand-in for the game with the same symbols, to try the library without starting the game.
$(BUILD)/standin: tests/standin.mm Makefile | $(BUILD)
	$(CXX) $(MAC_FLAGS) -std=c++20 -O0 -g -Wall -Wextra -framework Foundation -framework AppKit \
		-framework CoreGraphics -o $@ $<

check: $(LIBRARY) $(BUILD)/standin
	sh tests/run-standin.sh $(LIBRARY) $(BUILD)/standin

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
