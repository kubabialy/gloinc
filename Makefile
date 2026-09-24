.PHONY: all build test check-core install package clean run

BUILD_DIR ?= build
BUILD_TESTING ?= ON
CMAKE_ARGS ?=
BUILD_ARGS ?=
CTEST_ARGS ?=
RUN_ARGS ?= examples/core_counter.gloin
INSTALL_PREFIX ?= $(HOME)/.local

all: build

build:
	cmake -S . -B "$(BUILD_DIR)" $(CMAKE_ARGS) -DBUILD_TESTING=$(BUILD_TESTING)
	cmake --build "$(BUILD_DIR)" $(BUILD_ARGS)

test:
	cmake -S . -B "$(BUILD_DIR)" $(CMAKE_ARGS) -DBUILD_TESTING=ON
	cmake --build "$(BUILD_DIR)" --target gloinc_test $(BUILD_ARGS)
	ctest --test-dir "$(BUILD_DIR)" -j 1 --output-on-failure $(CTEST_ARGS)

run: build
	"$(BUILD_DIR)/gloinc" $(RUN_ARGS)

check-core: build
	cmake --build "$(BUILD_DIR)" --target check-core

install: build
	cmake --install "$(BUILD_DIR)" --prefix "$(INSTALL_PREFIX)"

package: build
	cmake --build "$(BUILD_DIR)" --target package

clean:
	cmake --build "$(BUILD_DIR)" --target clean
