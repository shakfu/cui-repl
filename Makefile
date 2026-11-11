# Frontend to CMake. Overrides: make BUILD=out TYPE=Debug CMAKE_ARGS=...
BUILD ?= build
TYPE ?= Release
PREFIX ?= $(HOME)/.local
CMAKE_ARGS ?=
GENERATOR := $(if $(shell command -v ninja),-G Ninja,)

all: $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD)

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD) $(GENERATOR) -DCMAKE_BUILD_TYPE=$(TYPE) $(CMAKE_ARGS)

test: all
	ctest --test-dir $(BUILD) --output-on-failure -j 4

# Build and run: make run ARGS='-C ~/src'
run: all
	./$(BUILD)/cui-repl $(ARGS)

install: all
	cmake --install $(BUILD) --prefix $(PREFIX) --strip

clean:
	rm -rf $(BUILD)

.PHONY: all test run install clean

# cmake writes CMakeCache.txt before it fails; without this a failed configure
# leaves a cache behind and the next make skips configure.
.DELETE_ON_ERROR:
