#!/usr/bin/env bash

# Builds the QuestLab OpenXR API layer packaging probe and stages it in the
# layout that scripts/build_deploy.sh --variant operator consumes. That layout
# deliberately matches the Meta XR Operator standalone bundle, so a successful
# probe run proves the packaging contract without needing that bundle.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
# shellcheck source=scripts/toolchain_config.sh
source "$repo_root/scripts/toolchain_config.sh"

output_root="$repo_root/build/dependencies/questlab-probe-layer"
abi="arm64-v8a"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            if [[ $# -lt 2 ]]; then
                echo "--output requires a path" >&2
                exit 2
            fi
            output_root="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [--output DIR]" >&2
            exit 2
            ;;
    esac
done

output_root="$(realpath -m -- "$output_root")"
dependencies_root="$(realpath -m -- "$repo_root/build/dependencies")"
case "$output_root/" in
    "$dependencies_root/"?*) ;;
    *)
        echo "--output must be below $dependencies_root" >&2
        exit 2
        ;;
esac

quest_export_toolchain

ndk_root="$(quest_sdk_root)/ndk/$QUEST_NDK_VERSION"
toolchain_file="$ndk_root/build/cmake/android.toolchain.cmake"
if [[ ! -f "$toolchain_file" ]]; then
    echo "Android CMake toolchain file is missing: $toolchain_file" >&2
    echo "Run ./scripts/setup_quest_dev_env.sh first." >&2
    exit 1
fi

# Track whatever loader the applications depend on rather than pinning a second
# copy of the version, so the probe cannot drift from the loader it runs against.
loader_version="$(sed -n \
    's/.*org\.khronos\.openxr:openxr_loader_for_android:\([0-9.]*\).*/\1/p' \
    "$repo_root/apps/01-openxr-bootstrap/build.gradle" | head -1)"
if [[ -z "$loader_version" ]]; then
    echo "Could not read the OpenXR loader version from app 01." >&2
    exit 1
fi

mapfile -t openxr_config_dirs < <(find "$HOME/.gradle/caches/transforms-3" \
    -type d \
    -path "*openxr_loader_for_android-$loader_version*" \
    -path "*libs/android.$abi/cmake/openxr" \
    2>/dev/null | sort)
if [[ "${#openxr_config_dirs[@]}" -eq 0 ]]; then
    echo "OpenXR loader $loader_version was not found in the Gradle cache." >&2
    echo "Build any application once so Gradle resolves the AAR, then retry." >&2
    exit 1
fi
openxr_config_dir="${openxr_config_dirs[0]}"

build_dir="$repo_root/build/probe-layer/$abi"
cmake \
    -S "$script_dir/probe_layer" \
    -B "$build_dir" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
    -DANDROID_ABI="$abi" \
    -DANDROID_PLATFORM="android-$QUEST_COMPILE_SDK" \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DOpenXR_DIR="$openxr_config_dir" \
    > /dev/null
cmake --build "$build_dir" --parallel > /dev/null

layer_library="$build_dir/libopenxr_probe_layer.so"
if [[ ! -f "$layer_library" ]]; then
    echo "Expected layer library was not produced: $layer_library" >&2
    exit 1
fi

# The loader reads layer manifests from this exact asset path. The "1" is the
# OpenXR major version, not the loader package version; any other value is
# silently ignored.
manifest_dir="$output_root/assets/openxr/1/api_layers/implicit.d"
library_dir="$output_root/jni/$abi"
rm -rf "$output_root"
mkdir -p "$manifest_dir" "$library_dir"
cp "$layer_library" "$library_dir/"
cp "$script_dir/probe_layer/manifest/questlab_probe_layer.json" "$manifest_dir/"

echo "Probe layer: $output_root"
echo "Library: $library_dir/libopenxr_probe_layer.so"
echo "Manifest: $manifest_dir/questlab_probe_layer.json"
