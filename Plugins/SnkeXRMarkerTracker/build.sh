#!/bin/bash
#
# Build libmarker_tracker_client.so for Android arm64-v8a (Unreal Engine plugin).
#
# Prerequisites:
#   - ANDROID_HOME or ANDROID_NDK environment variable set
#   - CMake >= 3.21, Ninja or Make, host C/C++ compiler (gcc/g++)
#
# Usage:
#   ./build.sh                 # full build (host tools + proto gen + Android .so)
#   ./build.sh --clean         # wipe build dirs and rebuild from scratch

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GODOT_HOST_TOOLS="../../godot/SnkeXRMarkerTracker/build-host-tools"
LOCAL_HOST_TOOLS="build-host-tools"
ANDROID_BUILD_DIR="build-android-arm64-v8a"
ARCH="arm64-v8a"

# ---------- helpers ---------------------------------------------------------

die() { echo "ERROR: $*" >&2; exit 1; }

resolve_ndk() {
  # Source Android env from Godot project if available
  if [ -f "../../godot/SnkeXRMarkerTracker/scripts/source_android_env.sh" ]; then
    source "../../godot/SnkeXRMarkerTracker/scripts/source_android_env.sh" 2>/dev/null || true
  fi

  if [ -n "$ANDROID_NDK" ] && [ -d "$ANDROID_NDK" ]; then
    echo "Using ANDROID_NDK=$ANDROID_NDK"
    return
  fi

  if [ -n "$ANDROID_HOME" ] && [ -d "$ANDROID_HOME/ndk" ]; then
    NDK_VER=$(ls -t "$ANDROID_HOME/ndk" 2>/dev/null | head -n1)
    [ -n "$NDK_VER" ] || die "No NDK versions found in $ANDROID_HOME/ndk/"
    export ANDROID_NDK="$ANDROID_HOME/ndk/$NDK_VER"
    echo "Auto-detected ANDROID_NDK=$ANDROID_NDK"
    return
  fi

  die "Set ANDROID_NDK or ANDROID_HOME before running this script."
}

# ---------- host tools (protoc + grpc_cpp_plugin) ---------------------------

HOST_TOOLS_DIR=""  # will be set by ensure_host_tools

ensure_host_tools() {
  # 1) Try reusing Godot's pre-built host tools
  local godot_abs
  godot_abs="$(cd "$SCRIPT_DIR" && cd "$GODOT_HOST_TOOLS" 2>/dev/null && pwd)" || true

  if [ -n "$godot_abs" ] \
     && [ -f "$godot_abs/_deps/grpc-build/third_party/protobuf/protoc" ] \
     && [ -f "$godot_abs/_deps/grpc-build/grpc_cpp_plugin" ]; then
    echo "Reusing Godot host tools at $godot_abs"
    HOST_TOOLS_DIR="$godot_abs"
    return
  fi

  # 2) Fall back to building our own
  echo "Godot host tools not found – building locally in $LOCAL_HOST_TOOLS ..."

  if [ -f "$LOCAL_HOST_TOOLS/_deps/grpc-build/third_party/protobuf/protoc" ] \
     && [ -f "$LOCAL_HOST_TOOLS/_deps/grpc-build/grpc_cpp_plugin" ]; then
    echo "Local host tools already built."
    HOST_TOOLS_DIR="$SCRIPT_DIR/$LOCAL_HOST_TOOLS"
    return
  fi

  mkdir -p "$LOCAL_HOST_TOOLS"

  # Use native host compiler – clear any cross-compile env
  (
    unset CC CXX CFLAGS CXXFLAGS CMAKE_SYSTEM_NAME
    export CC=gcc CXX=g++

    cmake -S . -B "$LOCAL_HOST_TOOLS" \
      -DCMAKE_C_COMPILER=gcc \
      -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_SYSTEM_NAME=Linux \
      -DgRPC_BUILD_TESTS=OFF \
      -Dprotobuf_BUILD_TESTS=OFF \
      -Dprotobuf_BUILD_PROTOC_BINARIES=ON \
      -DgRPC_ZLIB_PROVIDER=module \
      -DgRPC_ABSL_PROVIDER=module \
      -DgRPC_CARES_PROVIDER=module \
      -DgRPC_RE2_PROVIDER=module \
      -DgRPC_SSL_PROVIDER=module \
      -DgRPC_PROTOBUF_PROVIDER=module \
      -DABSL_PROPAGATE_CXX_STD=ON \
      -DABSL_USE_EXTERNAL_GOOGLETEST=OFF \
      -DCMAKE_BUILD_TYPE=Release
  )

  cmake --build "$LOCAL_HOST_TOOLS" --target protoc grpc_cpp_plugin -j"$(nproc)"

  HOST_TOOLS_DIR="$SCRIPT_DIR/$LOCAL_HOST_TOOLS"
  echo "Host tools built successfully."
}

# ---------- proto generation ------------------------------------------------

generate_protos() {
  local PROTOC="$HOST_TOOLS_DIR/_deps/grpc-build/third_party/protobuf/protoc"
  local GRPC_PLUGIN="$HOST_TOOLS_DIR/_deps/grpc-build/grpc_cpp_plugin"
  local PB_WKT="$HOST_TOOLS_DIR/_deps/grpc-src/third_party/protobuf/src"

  [ -x "$PROTOC" ]      || die "protoc not found at $PROTOC"
  [ -x "$GRPC_PLUGIN" ] || die "grpc_cpp_plugin not found at $GRPC_PLUGIN"

  # Resolve API directory (Cobra-sdk/api)
  local API_DIR
  API_DIR="$(cd "$SCRIPT_DIR/../../../../api" && pwd)" || die "Cannot resolve API directory"
  local API_PARENT
  API_PARENT="$(dirname "$API_DIR")"

  local OUT_DIR="$ANDROID_BUILD_DIR/generated"
  mkdir -p "$OUT_DIR"

  echo "Generating proto C++ sources → $OUT_DIR"

  "$PROTOC" \
    -I "$API_PARENT" \
    -I "$PB_WKT" \
    --cpp_out="$OUT_DIR" \
    --grpc_out="$OUT_DIR" \
    --plugin=protoc-gen-grpc="$GRPC_PLUGIN" \
    "$API_DIR/common/v1/errors.proto" \
    "$API_DIR/marker_tracker/v1/common.proto" \
    "$API_DIR/marker_tracker/v1/marker_tracker_service.proto"

  # Reorganise: protoc writes to generated/api/... but CMake expects generated/...
  if [ -d "$OUT_DIR/api" ]; then
    find "$OUT_DIR/api" -type f | while read -r f; do
      rel="${f#$OUT_DIR/api/}"
      mkdir -p "$OUT_DIR/$(dirname "$rel")"
      mv "$f" "$OUT_DIR/$rel"
    done
    rm -rf "$OUT_DIR/api"

    # Fix include paths in generated code
    find "$OUT_DIR" -type f \( -name "*.pb.cc" -o -name "*.pb.h" -o -name "*.grpc.pb.cc" -o -name "*.grpc.pb.h" \) \
      -exec sed -i 's|#include "api/|#include "|g' {} \;

    echo "Proto files generated and reorganised."
  else
    die "Expected generated/api/ directory not created by protoc"
  fi
}

# ---------- Android .so build -----------------------------------------------

build_android() {
  echo "=========================================="
  echo "Building libmarker_tracker_client.so"
  echo "  ABI  : $ARCH"
  echo "  NDK  : $ANDROID_NDK"
  echo "=========================================="

  mkdir -p "$ANDROID_BUILD_DIR"

  cmake -S . -B "$ANDROID_BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ARCH" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release

  cmake --build "$ANDROID_BUILD_DIR" -j"$(nproc)"

  local OUTPUT_DIR="Unreal Engine/SnkeXRMarkerTracker/Lib/Android/arm64-v8a"
  if [ -f "$OUTPUT_DIR/libmarker_tracker_client.so" ]; then
    local SIZE
    SIZE=$(du -h "$OUTPUT_DIR/libmarker_tracker_client.so" | cut -f1)
    echo ""
    echo "Build succeeded: $OUTPUT_DIR/libmarker_tracker_client.so ($SIZE)"
  else
    die "Output .so not found at $OUTPUT_DIR/libmarker_tracker_client.so"
  fi
}

# ---------- main ------------------------------------------------------------

if [ "${1:-}" = "--clean" ]; then
  echo "Cleaning build directories..."
  rm -rf "$ANDROID_BUILD_DIR" "$LOCAL_HOST_TOOLS"
  echo "Clean complete."
  [ "${2:-}" = "--build" ] || exit 0
fi

resolve_ndk
ensure_host_tools
generate_protos
build_android

echo ""
echo "Done."
