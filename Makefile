.PHONY: all build test clean run

BUILD_DIR ?= build
BUILD_TESTING ?= ON
CMAKE_ARGS ?=
BUILD_ARGS ?=
CTEST_ARGS ?=

all: build

build:
	cmake -S . -B "$(BUILD_DIR)" $(CMAKE_ARGS) -DBUILD_TESTING=$(BUILD_TESTING)
	cmake --build "$(BUILD_DIR)" $(BUILD_ARGS)

test:
	cmake -S . -B "$(BUILD_DIR)" $(CMAKE_ARGS) -DBUILD_TESTING=ON
	cmake --build "$(BUILD_DIR)" --target gloinc_test $(BUILD_ARGS)
	ctest --test-dir "$(BUILD_DIR)" -j 1 --output-on-failure $(CTEST_ARGS)

run: build
	"$(BUILD_DIR)/gloinc"

clean:
	cmake --build "$(BUILD_DIR)" --target clean
