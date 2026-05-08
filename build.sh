#!/usr/bin/env bash
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SRC_ROOT/build"

need_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing tool: $1" 1>&2
		return 1
	fi
	return 0
}

missing=0
need_cmd cmake || missing=1
need_cmd pkg-config || missing=1
need_cmd protoc || missing=1
need_cmd grpc_cpp_plugin || missing=1
need_cmd bash || missing=1

if test "$missing" -ne 0; then
	echo "" 1>&2
	echo "Some dependencies are missing." 1>&2
	echo "Install suggestions (examples):" 1>&2
	echo "- Debian/Ubuntu: sudo apt-get install cmake pkg-config protobuf-compiler grpc-proto-tools" 1>&2
	echo "- Arch: sudo pacman -S cmake pkgconf protobuf grpc" 1>&2
	echo "- Fedora: sudo dnf install cmake pkgconf-pkg-config protobuf-compiler grpc-plugins" 1>&2
	exit 1
fi

mkdir -p -- "$BUILD_DIR"

cmake -S "$SRC_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"
