#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
version="1.21.0"
archive_sha256="8b675e9680b8cc02dca706a5e3b4e35cc8506de5bdf206fdae68081cbd804414"
archive_url="https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/${version}/onnxruntime-android-${version}.aar"
download_dir="$repo_root/build/downloads"
archive_path="$download_dir/onnxruntime-android-${version}.aar"
destination="$repo_root/build/dependencies/onnxruntime-android-${version}"

verify_archive() {
    [[ -f "$archive_path" ]] || return 1
    local actual
    actual="$(sha256sum "$archive_path" | awk '{print $1}')"
    [[ "$actual" == "$archive_sha256" ]]
}

if [[ -f "$destination/headers/onnxruntime_cxx_api.h" &&
      -f "$destination/jni/arm64-v8a/libonnxruntime.so" ]]; then
    echo "ONNX Runtime Android $version is ready: $destination"
    exit 0
fi

mkdir -p "$download_dir" "$repo_root/build/dependencies"
if ! verify_archive; then
    temporary_archive="$(mktemp "$download_dir/.onnxruntime-android.XXXXXX")"
    trap 'rm -f "$temporary_archive"' EXIT
    curl -fL --retry 3 -o "$temporary_archive" "$archive_url"
    actual_sha256="$(sha256sum "$temporary_archive" | awk '{print $1}')"
    if [[ "$actual_sha256" != "$archive_sha256" ]]; then
        echo "ONNX Runtime Android checksum mismatch." >&2
        echo "Expected: $archive_sha256" >&2
        echo "Actual:   $actual_sha256" >&2
        exit 1
    fi
    mv "$temporary_archive" "$archive_path"
    trap - EXIT
fi

if [[ -e "$destination" ]]; then
    echo "Incomplete ONNX Runtime directory exists: $destination" >&2
    echo "Remove that generated directory and rerun this script." >&2
    exit 1
fi

staging="$(mktemp -d "$repo_root/build/dependencies/.onnxruntime-android.XXXXXX")"
trap 'rm -rf "$staging"' EXIT
unzip -q "$archive_path" -d "$staging"
if [[ ! -f "$staging/headers/onnxruntime_cxx_api.h" ||
      ! -f "$staging/jni/arm64-v8a/libonnxruntime.so" ]]; then
    echo "ONNX Runtime Android archive layout is incomplete." >&2
    exit 1
fi
mv "$staging" "$destination"
trap - EXIT

echo "ONNX Runtime Android $version prepared: $destination"
echo "Archive SHA-256: $archive_sha256"
