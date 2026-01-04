.PHONY: all build test clean run

all: build

build:
	cmake -S . -B build
	cmake --build build

test:
	cmake -S . -B build
	cmake --build build --target gloinc_test
	./build/gloinc_test

run: build
	./build/gloinc

clean:
	rm -rf build
