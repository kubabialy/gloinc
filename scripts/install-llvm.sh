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
  export HOMEBREW_NO_AUTO_UPDATE=1
  brew tap-new --no-git gloinc/ci
  formula_path="$(brew --repository)/Library/Taps/gloinc/homebrew-ci/Formula/llvm.rb"
  curl --fail --location --retry 3 \
    https://raw.githubusercontent.com/Homebrew/homebrew-core/67ebbe50b3d4f240f8c34f998edac9a2898cba86/Formula/l/llvm.rb \
    --output "$formula_path"
  printf '%s  %s\n' e494baa9d256526a540ff093148682839d6b5c1356db030c00dbe4e672ce7391 "$formula_path" |
    shasum -a 256 --check
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
