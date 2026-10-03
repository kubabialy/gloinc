#!/usr/bin/env bash
# Verify installation and a relocated CPack archive with the real CLI acceptance suite.
set -euo pipefail
if [[ $# != 2 ]]; then
  echo "Usage: bash scripts/check-package.sh BUILD_DIR REPORT_DIR" >&2
  exit 2
fi
build_dir=$(cd "$1" && pwd)
case "$(uname -s)" in
  Darwin)
    runtime_suffix=dylib
    llvm_version=21.1.6
    package_platform=macos-arm64
    checksum_check=(shasum -a 256 -c)
    dependency_report=(otool -L)
    ;;
  Linux)
    runtime_suffix=so
    llvm_version=21.1.8
    package_platform="linux-$(uname -m)"
    checksum_check=(sha256sum -c)
    dependency_report=(ldd)
    ;;
  *)
    echo "Package acceptance supports macOS and Linux" >&2
    exit 2
    ;;
esac
mkdir -p "$2"
report_dir=$(cd "$2" && pwd)
work_dir=$(mktemp -d "$report_dir/work.XXXXXX")
cmake --install "$build_dir" --prefix "$work_dir/install prefix"
cmake -E env "GLOIN_TEST_CLI=$work_dir/install prefix/bin/gloinc" \
  "GLOIN_TEST_FIXTURES=$work_dir/install prefix/share/gloinc/core-fixtures" \
  "GLOIN_TEST_ARENA_RUNTIME=$work_dir/install prefix/lib/libgloin_runtime.$runtime_suffix" \
  ctest --test-dir "$build_dir" -j 4 --no-tests=error \
  -R '^(CliTest|StandardModuleTest|StandardLibraryTest|StringLibraryTest|TextLibraryTest|NumericLibraryTest|IoLibraryTest|NetworkLibraryTest|ContextLibraryTest|MathLibraryTest|TimeRandomLibraryTest|IntegratedExamplesTest|ModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|ArenaTest|CoreAcceptanceTest)\.' --output-on-failure \
  --output-junit "$report_dir/installed.xml"

cpack --config "$build_dir/CPackConfig.cmake" -B "$work_dir/packages"
archives=("$work_dir/packages/"*.tar.gz)
[[ ${#archives[@]} == 1 && -f "${archives[0]}" ]]
archive=${archives[0]}
archive_name=$(basename "$archive" .tar.gz)
[[ "$archive_name" == "gloinc-0.1.0-$package_platform" ]]
if tar -tzf "$archive" | grep -F '/.DS_Store'; then
  echo "Package contains Finder metadata" >&2
  exit 1
fi
(cd "$work_dir/packages" && LC_ALL=C "${checksum_check[@]}" "$(basename "$archive").sha256")
mkdir "$work_dir/extracted prefix"
tar -xzf "$archive" -C "$work_dir/extracted prefix"
package_root="$work_dir/extracted prefix/$archive_name"
cmake -E env "GLOIN_TEST_CLI=$package_root/bin/gloinc" \
  "GLOIN_TEST_FIXTURES=$package_root/share/gloinc/core-fixtures" \
  "GLOIN_TEST_ARENA_RUNTIME=$package_root/lib/libgloin_runtime.$runtime_suffix" \
  ctest --test-dir "$build_dir" -j 4 --no-tests=error \
  -R '^(CliTest|StandardModuleTest|StandardLibraryTest|StringLibraryTest|TextLibraryTest|NumericLibraryTest|IoLibraryTest|NetworkLibraryTest|ContextLibraryTest|MathLibraryTest|TimeRandomLibraryTest|IntegratedExamplesTest|ModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|ArenaTest|CoreAcceptanceTest)\.' --output-on-failure \
  --output-junit "$report_dir/extracted.xml"
program_status=0
result=$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/core_counter.gloin") || program_status=$?
[[ "$program_status" == 42 && -z "$result" ]]
result=$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/hello_world.gloin")
[[ "$result" == 'Hello World!' ]]
result=$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/arena_lab.gloin")
[[ "$result" == 'arena lab: ok' ]]
result=$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/module_lab.gloin")
[[ "$result" == 'module lab: ok' ]]
[[ -f "$package_root/lib/libgloin_runtime.a" && -f "$package_root/include/gloin/arena_runtime.h" && -f "$package_root/include/gloin/memory_runtime.h" && -f "$package_root/include/gloin/stdlib_runtime.h" && -f "$package_root/include/gloin/io_runtime.h" && -f "$package_root/include/gloin/net_runtime.h" && -f "$package_root/include/gloin/context_runtime.h" && -f "$package_root/include/gloin/math_runtime.h" && -f "$package_root/include/gloin/time_runtime.h" && -f "$package_root/include/gloin/random_runtime.h" ]]
[[ -f "$package_root/share/doc/gloinc/third_party/fast_float/LICENSE-MIT" && -f "$package_root/share/doc/gloinc/third_party/fast_float/README.md" ]]
[[ -f "$package_root/share/doc/gloinc/docs/site/0.1.0/index.html" && -f "$package_root/share/doc/gloinc/CONTRIBUTING.md" ]]
[[ -f "$package_root/share/gloinc/scripts/install-llvm.sh" && -f "$package_root/share/gloinc/scripts/install-llvm-linux.sh" && -f "$package_root/share/gloinc/examples/fixed_arrays.gloin" && -f "$package_root/share/gloinc/examples/pointer_offsets.gloin" ]]
[[ -f "$package_root/share/gloinc/stdlib/vector.gloin" ]]
[[ -f "$package_root/share/gloinc/stdlib/slices.gloin" ]]
[[ -f "$package_root/share/gloinc/stdlib/memory.gloin" ]]
[[ -f "$package_root/share/gloinc/stdlib/net.gloin" && -f "$package_root/share/gloinc/stdlib/http.gloin" ]]
[[ -f "$package_root/share/doc/gloinc/docs/results.md" && -f "$package_root/share/doc/gloinc/docs/raw-memory.md" ]]
[[ -f "$package_root/share/doc/gloinc/docs/networking.md" ]]
[[ -f "$package_root/share/doc/gloinc/docs/site/development/index.html" ]]
[[ -f "$package_root/share/doc/gloinc/docs/release-0.1.0.md" ]]
[[ -f "$package_root/share/doc/gloinc/docs/release-notes-0.1.0.md" ]]
"$package_root/bin/gloinc" --check "$package_root/share/gloinc/examples/strip_nuls.gloin"
for example in generic_structs generic_functions generic_methods enums slices vector fixed_vector custom_arena; do
  source_file="$package_root/share/gloinc/examples/$example.gloin"
  [[ -f "$source_file" ]]
  "$package_root/bin/gloinc" --check "$source_file"
  expected_status=42
  if [[ "$example" == slices ]]; then expected_status=28; fi
  example_status=0
  "$package_root/bin/gloinc" --jit "$source_file" || example_status=$?
  [[ "$example_status" == "$expected_status" ]]
done
result_example="$package_root/share/gloinc/examples/result_handling.gloin"
[[ -f "$result_example" ]]
"$package_root/bin/gloinc" --check "$result_example"
result_status=0
result_output=$("$package_root/bin/gloinc" --jit "$result_example") || result_status=$?
[[ "$result_status" == 84 && "$result_output" == 'The answer was computed successfully.' ]]
network_example="$package_root/share/gloinc/examples/network_http.gloin"
[[ -f "$network_example" ]]
[[ "$("$package_root/bin/gloinc" --jit "$network_example")" == 'nonblocking HTTP round trip succeeded' ]]
[[ "$("$package_root/bin/gloinc" --version)" == "gloinc 0.1.0 (LLVM/MLIR $llvm_version)" ]]
[[ "$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/fixed_arrays.gloin")" == 'sum = 42' ]]
"$package_root/bin/gloinc" -O2 -o "$work_dir/extracted prefix/pointer-offsets" \
  "$package_root/share/gloinc/examples/pointer_offsets.gloin"
offset_status=0
"$work_dir/extracted prefix/pointer-offsets" || offset_status=$?
[[ "$offset_status" == 32 ]]
[[ ! -e "$package_root/share/doc/gloinc/SPEC.md" && ! -e "$package_root/share/doc/gloinc/SPEC-TODO.md" && ! -e "$package_root/share/doc/gloinc/OpenCode.md" ]]
"$package_root/bin/gloinc" -o "$work_dir/extracted prefix/native-hello" \
  "$package_root/share/gloinc/examples/hello_world.gloin"
[[ "$("$work_dir/extracted prefix/native-hello")" == 'Hello World!' ]]
(
  cd "$work_dir/extracted prefix"
  "$package_root/bin/gloinc" "$package_root/share/gloinc/examples/hello_world.gloin"
  [[ -x a.out && "$(./a.out)" == 'Hello World!' ]]
)
# A relocated compiler must depend on its installed module, never a source fallback.
mv "$package_root/share/gloinc/stdlib/std.gloin" "$work_dir/std.gloin.saved"
module_status=0
result=$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/hello_world.gloin" \
  2> "$report_dir/missing-stdlib.txt") || module_status=$?
mv "$work_dir/std.gloin.saved" "$package_root/share/gloinc/stdlib/std.gloin"
[[ "$module_status" == 1 && -z "$result" ]]
grep -F "Cannot load module '@std'" "$report_dir/missing-stdlib.txt"
mv "$package_root/share/gloinc/stdlib/arena.gloin" "$work_dir/arena.gloin.saved"
module_status=0
result=$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/arena_lab.gloin" \
  2> "$report_dir/missing-arena.txt") || module_status=$?
mv "$work_dir/arena.gloin.saved" "$package_root/share/gloinc/stdlib/arena.gloin"
[[ "$module_status" == 1 && -z "$result" ]]
grep -F "Cannot load module '@arena'" "$report_dir/missing-arena.txt"
"${dependency_report[@]}" "$package_root/bin/gloinc" "$package_root/lib/libgloin_runtime.$runtime_suffix" > "$report_dir/dependencies.txt"
if awk -v dir="$build_dir" '/^[[:space:]]/ && index($0, dir) { print; found = 1 } END { exit !found }' "$report_dir/dependencies.txt"; then
  echo "Installed compiler still depends on its build directory" >&2
  exit 1
fi
cp "$archive" "$archive.sha256" "$report_dir/"
echo "Installed and extracted packages passed CLI, standard-library, module, arena, defer, method, pointer, struct, fixed-array, standard-output, and source acceptance cases."
