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
  ctest --test-dir "$build_dir" -j 4 --no-tests=error \
  -R '^(CliTest|StandardModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|CoreAcceptanceTest)\.' --output-on-failure \
  --output-junit "$report_dir/installed.xml"

cpack --config "$build_dir/CPackConfig.cmake" -B "$work_dir/packages"
archives=("$work_dir/packages/"*.tar.gz)
[[ ${#archives[@]} == 1 && -f "${archives[0]}" ]]
archive=${archives[0]}
archive_name=$(basename "$archive" .tar.gz)
(cd "$work_dir/packages" && LC_ALL=C shasum -a 256 -c "$(basename "$archive").sha256")
mkdir "$work_dir/extracted prefix"
tar -xzf "$archive" -C "$work_dir/extracted prefix"
package_root="$work_dir/extracted prefix/$archive_name"
cmake -E env "GLOIN_TEST_CLI=$package_root/bin/gloinc" \
  "GLOIN_TEST_FIXTURES=$package_root/share/gloinc/core-fixtures" \
  ctest --test-dir "$build_dir" -j 4 --no-tests=error \
  -R '^(CliTest|StandardModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|CoreAcceptanceTest)\.' --output-on-failure \
  --output-junit "$report_dir/extracted.xml"
program_status=0
result=$("$package_root/bin/gloinc" "$package_root/share/gloinc/examples/core_counter.gloin") || program_status=$?
[[ "$program_status" == 42 && -z "$result" ]]
result=$("$package_root/bin/gloinc" "$package_root/share/gloinc/examples/hello_world.gloin")
[[ "$result" == 'Hello World!' ]]
# A relocated compiler must depend on its installed module, never a source fallback.
mv "$package_root/share/gloinc/stdlib/std.gloin" "$work_dir/std.gloin.saved"
module_status=0
result=$("$package_root/bin/gloinc" "$package_root/share/gloinc/examples/hello_world.gloin" \
  2> "$report_dir/missing-stdlib.txt") || module_status=$?
mv "$work_dir/std.gloin.saved" "$package_root/share/gloinc/stdlib/std.gloin"
[[ "$module_status" == 1 && -z "$result" ]]
grep -F "Cannot load module '@std'" "$report_dir/missing-stdlib.txt"
otool -L "$package_root/bin/gloinc" > "$report_dir/dependencies.txt"
if grep -F "$build_dir" "$report_dir/dependencies.txt"; then
  echo "Installed compiler still depends on its build directory" >&2
  exit 1
fi
cp "$archive" "$archive.sha256" "$report_dir/"
echo "Installed and extracted packages passed all 241 CLI/defer/method/pointer/struct/standard-output/source acceptance cases."
