#!/usr/bin/env bash
set -euo pipefail

# Simple Arch Linux helper to install dependencies and cross-build the project
# Usage: sudo ./scripts/build-windows-arch.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"
TOOLCHAIN="$REPO_ROOT/mingw-w64-x86_64.cmake"
BUILD_DIR="$REPO_ROOT/build-windows"

if [[ $(id -u) -ne 0 ]]; then
  echo "This script will install packages via pacman; please run as root or with sudo."
  echo "Re-run with: sudo $0"
  exit 1
fi

echo "Installing required packages (mingw-w64, cmake, ninja, base-devel)..."
pacman -Syu --noconfirm --needed mingw-w64-gcc mingw-w64-crt mingw-w64-headers cmake ninja make pkgconf base-devel

echo "Creating build directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "Configuring CMake with toolchain: $TOOLCHAIN"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release

echo "Building project"
cmake --build "$BUILD_DIR" --config Release -- -j$(nproc)

echo "Build complete. Windows artifacts are in: $BUILD_DIR"
echo "Test the produced .exe using Wine or copy to a Windows machine."
