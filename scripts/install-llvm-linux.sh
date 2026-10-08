#!/usr/bin/env bash
set -euo pipefail

# apt.llvm.org publishes the shared LLVM/MLIR libraries required by gloinc.
# The exact package revision below is the tested Ubuntu 24.04 release.
if [[ "$(uname -s)" != Linux ]] ||
   [[ "$(uname -m)" != x86_64 && "$(uname -m)" != aarch64 ]]; then
  echo "This installer supports 64-bit x86 and ARM Linux. See docs/toolchain.md." >&2
  exit 1
fi
source /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_CODENAME" != noble ]]; then
  echo "This installer supports Ubuntu 24.04. See docs/toolchain.md." >&2
  exit 1
fi

toolchain_prefix=/usr/lib/llvm-21
apt_version='1:21.1.8~++20251221032922+2078da43e25a-1~exp1~20251221153059.70'
if [[ ! -x "$toolchain_prefix/bin/llvm-config" ]] ||
   [[ "$("$toolchain_prefix/bin/llvm-config" --version)" != 21.1.8 ]]; then
  sudo env DEBIAN_FRONTEND=noninteractive apt-get update -qq
  sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    ca-certificates curl gnupg
  key_dir=$(mktemp -d)
  trap 'rm -rf "$key_dir"' EXIT
  curl --fail --location --retry 3 \
    https://apt.llvm.org/llvm-snapshot.gpg.key --output "$key_dir/llvm.key"
  fingerprint=$(gpg --show-keys --with-colons "$key_dir/llvm.key" |
    awk -F: '$1 == "fpr" { print $10; exit }')
  if [[ "$fingerprint" != 6084F3CF814B57C1CF12EFD515CF4D18AF4F7421 ]]; then
    echo "Unexpected apt.llvm.org signing key fingerprint" >&2
    exit 1
  fi
  gpg --dearmor --output "$key_dir/llvm.gpg" "$key_dir/llvm.key"
  sudo install -m 0644 "$key_dir/llvm.gpg" /usr/share/keyrings/apt.llvm.org.gpg
  printf '%s\n' \
    'deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main' |
    sudo tee /etc/apt/sources.list.d/llvm-21.list > /dev/null
  sudo env DEBIAN_FRONTEND=noninteractive apt-get update -qq
  sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    "llvm-21-dev=$apt_version" "libmlir-21-dev=$apt_version" \
    "mlir-21-tools=$apt_version" "clang-21=$apt_version"
fi

test "$("$toolchain_prefix/bin/llvm-config" --version)" = 21.1.8
test -f "$toolchain_prefix/lib/cmake/mlir/MLIRConfig.cmake"
test -f "$toolchain_prefix/lib/libMLIR.so"
test -f "$toolchain_prefix/lib/libMLIRExecutionEngineShared.so"
test -f "$toolchain_prefix/lib/libLLVM.so"
for tool in mlir-tblgen mlir-opt mlir-runner clang++; do
  test -x "$toolchain_prefix/bin/$tool"
done
echo "GLOIN_LLVM_PREFIX=$toolchain_prefix"
if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "GLOIN_LLVM_PREFIX=$toolchain_prefix" >> "$GITHUB_ENV"
fi
