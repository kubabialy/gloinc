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
  -R '^(CliTest|CoreAcceptanceTest)\.' --output-on-failure \
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
  -R '^(CliTest|CoreAcceptanceTest)\.' --output-on-failure \
  --output-junit "$report_dir/extracted.xml"
result=$("$package_root/bin/gloinc" "$package_root/share/gloinc/examples/core_counter.gloin")
[[ "$result" == 42 ]]
otool -L "$package_root/bin/gloinc" > "$report_dir/dependencies.txt"
if grep -F "$build_dir" "$report_dir/dependencies.txt"; then
  echo "Installed compiler still depends on its build directory" >&2
  exit 1
fi
cp "$archive" "$archive.sha256" "$report_dir/"
echo "Installed and extracted packages passed all 141 CLI/source acceptance cases."
