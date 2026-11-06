# Builds the Linux port of BG3WASD: an LD_PRELOAD library plus a signature self-test.
VERSION := 0.1.0
BUILD := build

CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra \
	-DBG3WASD_VERSION=\"$(VERSION)\"
# The C++ runtime is linked statically with its symbols hidden: the game process loads other
# libraries that use libstdc++, and the Steam Linux Runtime container may ship an older one.
SO_LDFLAGS := -shared -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-z,defs
LDLIBS := -ldl -pthread

SRC := main.cpp camera.cpp input.cpp log.cpp memory.cpp settings.cpp signatures.cpp
OBJ := $(SRC:%.cpp=$(BUILD)/%.o)
HEADERS := $(wildcard src/*.hpp) src/keymap.inc

all: $(BUILD)/libbg3wasd.so $(BUILD)/selftest $(BUILD)/BG3WASD.toml

$(BUILD)/libbg3wasd.so: $(OBJ)
	$(CXX) $(SO_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/selftest: tests/selftest.cpp $(BUILD)/signatures.o
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Copied once; later builds keep your edits.
$(BUILD)/BG3WASD.toml: | $(BUILD)
	cp ../package/BG3WASD.toml $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
