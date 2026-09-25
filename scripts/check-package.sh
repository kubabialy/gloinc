#!/usr/bin/env bash
# Verify installation and a relocated CPack archive with the real CLI acceptance suite.
set -euo pipefail
if [[ $# != 2 ]]; then
  echo "Usage: bash scripts/check-package.sh BUILD_DIR REPORT_DIR" >&2
  exit 2
fi
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
report_dir=$(cd "$2" && pwd)
work_dir=$(mktemp -d "$report_dir/work.XXXXXX")
cmake --install "$build_dir" --prefix "$work_dir/install prefix"
cmake -E env "GLOIN_TEST_CLI=$work_dir/install prefix/bin/gloinc" \
  "GLOIN_TEST_FIXTURES=$work_dir/install prefix/share/gloinc/core-fixtures" \
  "GLOIN_TEST_ARENA_RUNTIME=$work_dir/install prefix/lib/libgloin_runtime.dylib" \
  ctest --test-dir "$build_dir" -j 4 --no-tests=error \
  -R '^(CliTest|StandardModuleTest|StandardLibraryTest|StringLibraryTest|TextLibraryTest|NumericLibraryTest|IoLibraryTest|ContextLibraryTest|MathLibraryTest|TimeRandomLibraryTest|IntegratedExamplesTest|ModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|ArenaTest|CoreAcceptanceTest)\.' --output-on-failure \
  --output-junit "$report_dir/installed.xml"

cpack --config "$build_dir/CPackConfig.cmake" -B "$work_dir/packages"
archives=("$work_dir/packages/"*.tar.gz)
[[ ${#archives[@]} == 1 && -f "${archives[0]}" ]]
archive=${archives[0]}
archive_name=$(basename "$archive" .tar.gz)
if tar -tzf "$archive" | grep -F '/.DS_Store'; then
  echo "Package contains Finder metadata" >&2
  exit 1
fi
(cd "$work_dir/packages" && LC_ALL=C shasum -a 256 -c "$(basename "$archive").sha256")
mkdir "$work_dir/extracted prefix"
tar -xzf "$archive" -C "$work_dir/extracted prefix"
package_root="$work_dir/extracted prefix/$archive_name"
cmake -E env "GLOIN_TEST_CLI=$package_root/bin/gloinc" \
  "GLOIN_TEST_FIXTURES=$package_root/share/gloinc/core-fixtures" \
  "GLOIN_TEST_ARENA_RUNTIME=$package_root/lib/libgloin_runtime.dylib" \
  ctest --test-dir "$build_dir" -j 4 --no-tests=error \
  -R '^(CliTest|StandardModuleTest|StandardLibraryTest|StringLibraryTest|TextLibraryTest|NumericLibraryTest|IoLibraryTest|ContextLibraryTest|MathLibraryTest|TimeRandomLibraryTest|IntegratedExamplesTest|ModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|ArenaTest|CoreAcceptanceTest)\.' --output-on-failure \
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
[[ -f "$package_root/lib/libgloin_runtime.a" && -f "$package_root/include/gloin/arena_runtime.h" && -f "$package_root/include/gloin/stdlib_runtime.h" && -f "$package_root/include/gloin/io_runtime.h" && -f "$package_root/include/gloin/context_runtime.h" && -f "$package_root/include/gloin/math_runtime.h" && -f "$package_root/include/gloin/time_runtime.h" && -f "$package_root/include/gloin/random_runtime.h" ]]
[[ -f "$package_root/share/doc/gloinc/third_party/fast_float/LICENSE-MIT" && -f "$package_root/share/doc/gloinc/third_party/fast_float/README.md" ]]
[[ -f "$package_root/share/doc/gloinc/docs/site/0.0.3/index.html" && -f "$package_root/share/doc/gloinc/CONTRIBUTING.md" ]]
[[ -f "$package_root/share/gloinc/scripts/install-llvm.sh" && -f "$package_root/share/gloinc/examples/fixed_arrays.gloin" ]]
[[ "$("$package_root/bin/gloinc" --version)" == 'gloinc 0.0.3 (LLVM/MLIR 21.1.6)' ]]
[[ "$("$package_root/bin/gloinc" --jit "$package_root/share/gloinc/examples/fixed_arrays.gloin")" == 'sum = 42' ]]
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
otool -L "$package_root/bin/gloinc" "$package_root/lib/libgloin_runtime.dylib" > "$report_dir/dependencies.txt"
if awk -v dir="$build_dir" '/^[[:space:]]/ && index($0, dir) { print; found = 1 } END { exit !found }' "$report_dir/dependencies.txt"; then
  echo "Installed compiler still depends on its build directory" >&2
  exit 1
fi
cp "$archive" "$archive.sha256" "$report_dir/"
echo "Installed and extracted packages passed CLI, standard-library, module, arena, defer, method, pointer, struct, fixed-array, standard-output, and source acceptance cases."
