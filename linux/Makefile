# Builds the Linux port of BG3 WASD Everywhere: an LD_PRELOAD library plus a signature self-test.
NAME := bg3-wasd-everywhere
VERSION := 0.3.0-dev
BUILD := build
LIBRARY := $(BUILD)/lib$(NAME).so
CONFIG := $(BUILD)/$(NAME).toml

CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra \
	-DBG3WASD_VERSION=\"$(VERSION)\"
# The C++ runtime is linked statically with its symbols hidden: the game process loads other
# libraries that use libstdc++, and the Steam Linux Runtime container may ship an older one.
SO_LDFLAGS := -shared -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-z,defs \
	-Wl,--version-script=exports.map
LDLIBS := -ldl -pthread

SRC := main.cpp camera.cpp camera_tweaks.cpp hotkeys.cpp input.cpp inputconfig.cpp log.cpp memory.cpp settings.cpp \
	settings_file.cpp signatures.cpp
OBJ := $(SRC:%.cpp=$(BUILD)/%.o)
HEADERS := $(wildcard src/*.hpp) src/keymap.inc

all: $(LIBRARY) $(BUILD)/selftest $(CONFIG)

# The constructor in main.cpp that starts the mod can run before the static initializers of the
# other files, so the sources must not have any: objects with constructors are function-local
# statics. Older compilers need static initializers even for std::string and std::vector.
$(LIBRARY): $(OBJ) exports.map
	$(CXX) $(SO_LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
	@if nm $@ | grep '_GLOBAL__sub_I_.*\.cpp$$'; then \
		echo "error: $@ has static initializers, listed above"; rm -f $@; exit 1; fi

$(BUILD)/selftest: tests/selftest.cpp $(BUILD)/signatures.o $(BUILD)/inputconfig.o $(BUILD)/log.o
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^ -pthread

$(BUILD)/%.o: src/%.cpp $(HEADERS) Makefile | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Copied once; later builds keep your edits.
$(CONFIG): | $(BUILD)
	cp $(NAME).toml $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
