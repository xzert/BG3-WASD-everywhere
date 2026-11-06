# Builds the macOS probe of BG3 WASD Everywhere (Apple Silicon) and symtest, a host check of its
# Mach-O and AArch64 code. On Linux, only symtest is built.
NAME := bg3-wasd-everywhere
VERSION := 0.0.2-proto
BUILD := build
LIBRARY := $(BUILD)/lib$(NAME).dylib

CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Isrc -I../linux/src -DBG3WASD_VERSION=\"$(VERSION)\"
MAC_FLAGS := -arch arm64 -mmacosx-version-min=11.0
HEADERS := $(wildcard src/*.hpp) ../linux/src/log.hpp ../linux/src/inputconfig.hpp
# Shared with the Linux port.
SHARED := ../linux/src/log.cpp ../linux/src/inputconfig.cpp

LAUNCHER := $(BUILD)/$(NAME)-launch

ifeq ($(shell uname -s),Darwin)
all: $(LIBRARY) $(LAUNCHER) $(BUILD)/symtest
else
all: $(BUILD)/symtest
endif

$(BUILD)/stubs.o: src/stubs.S Makefile | $(BUILD)
	$(CC) $(MAC_FLAGS) -c -o $@ $<

# The game isn't linked against: the probe finds its functions by name at runtime.
$(LIBRARY): src/probe.mm src/macho.cpp src/arm64.cpp src/patterns.cpp $(SHARED) $(BUILD)/stubs.o $(HEADERS) Makefile
	$(CXX) $(MAC_FLAGS) $(CXXFLAGS) -fvisibility=hidden -fvisibility-inlines-hidden -dynamiclib \
		-framework Foundation -framework AppKit -o $@ \
		src/probe.mm src/macho.cpp src/arm64.cpp src/patterns.cpp $(SHARED) $(BUILD)/stubs.o

# Steam runs it from the launch options: see src/launch.cpp.
$(LAUNCHER): src/launch.cpp Makefile | $(BUILD)
	$(CXX) $(MAC_FLAGS) $(CXXFLAGS) -framework CoreFoundation -o $@ $<

$(BUILD)/symtest: tests/symtest.cpp src/macho.cpp src/arm64.cpp src/patterns.cpp $(HEADERS) Makefile | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/symtest.cpp src/macho.cpp src/arm64.cpp src/patterns.cpp

# A stand-in for the game with the same symbols, to try the probe without starting the game.
$(BUILD)/standin: tests/standin.mm Makefile | $(BUILD)
	$(CXX) $(MAC_FLAGS) -std=c++20 -O0 -g -Wall -Wextra -framework Foundation -framework AppKit -o $@ $<

check: $(LIBRARY) $(BUILD)/standin
	sh tests/run-standin.sh $(LIBRARY) $(BUILD)/standin

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
