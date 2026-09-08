#!/usr/bin/env bash
set -euo pipefail

# Reproduce the tested Homebrew release instead of following the moving llvm formula.
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo "This installer supports Apple Silicon macOS. See docs/toolchain.md." >&2
  exit 1
fi

toolchain_prefix="$(brew --prefix)/opt/llvm"
if [[ ! -x "$toolchain_prefix/bin/llvm-config" ]] ||
   [[ "$("$toolchain_prefix/bin/llvm-config" --version)" != 21.1.6 ]]; then
  if brew list --versions llvm >/dev/null 2>&1; then
    echo "A different llvm release is installed. Use a separate Homebrew environment for 21.1.6." >&2
    exit 1
  fi
  if brew list --versions z3 >/dev/null 2>&1 &&
     [[ ! -f "$(brew --prefix)/opt/z3/lib/libz3.4.15.dylib" ]]; then
    echo "An incompatible Z3 is installed. LLVM 21.1.6 needs the Z3 4.15 ABI." >&2
    exit 1
  fi
  export HOMEBREW_NO_AUTO_UPDATE=1
  tap_path="$(brew --repository)/Library/Taps/gloinc/homebrew-ci"
  if [[ ! -d "$tap_path" ]]; then
    brew tap-new --no-git gloinc/ci
  fi
  formula_path="$tap_path/Formula/llvm.rb"
  curl --fail --location --retry 3 \
    https://raw.githubusercontent.com/Homebrew/homebrew-core/67ebbe50b3d4f240f8c34f998edac9a2898cba86/Formula/l/llvm.rb \
    --output "$formula_path"
  printf '%s  %s\n' e494baa9d256526a540ff093148682839d6b5c1356db030c00dbe4e672ce7391 "$formula_path" |
    shasum -a 256 --check
  curl --fail --location --retry 3 \
    https://raw.githubusercontent.com/Homebrew/homebrew-core/67ebbe50b3d4f240f8c34f998edac9a2898cba86/Formula/z/z3.rb \
    --output "$tap_path/Formula/z3.rb"
  printf '%s  %s\n' 0d964c254f409894415b8629803aed513a7052e8e901d02d06593d43bc188d67 "$tap_path/Formula/z3.rb" |
    shasum -a 256 --check
  # Bind the verified historical formula to its matching ABI dependency in this tap.
  ruby -e 'path = ARGV.fetch(0); source = File.read(path); dependency = %q(depends_on "z3");
    abort "Unexpected LLVM formula" unless source.scan(dependency).length == 1;
    File.write(path, source.sub(dependency, %q(depends_on "gloinc/ci/z3")))' "$formula_path"
  brew install --force-bottle gloinc/ci/z3
  brew install --force-bottle gloinc/ci/llvm
  toolchain_prefix="$(brew --prefix gloinc/ci/llvm)"
fi

test "$("$toolchain_prefix/bin/llvm-config" --version)" = 21.1.6
test -f "$toolchain_prefix/lib/cmake/mlir/MLIRConfig.cmake"
test -f "$toolchain_prefix/lib/libMLIR.dylib"
test -f "$toolchain_prefix/lib/libMLIRExecutionEngineShared.dylib"
for tool in mlir-tblgen mlir-opt mlir-runner; do
  test -x "$toolchain_prefix/bin/$tool"
done
echo "GLOIN_LLVM_PREFIX=$toolchain_prefix"
if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "GLOIN_LLVM_PREFIX=$toolchain_prefix" >> "$GITHUB_ENV"
fi
