#!/usr/bin/env bash

set -euo pipefail

readonly ORT_VERSION="1.21.0"
readonly ORT_ARCHIVE="onnxruntime-linux-x64-${ORT_VERSION}.tgz"
readonly ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_ARCHIVE}"
readonly ORT_SHA256="7485c7e7aac6501b27e353dcbe068e45c61ab51fbaf598d13970dfae669d20bf"

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dependency_root="${1:-$repository_root/build/dependencies}"
install_root="$dependency_root/onnxruntime-linux-x64-${ORT_VERSION}"
archive_path="$dependency_root/$ORT_ARCHIVE"

if [[ -f "$install_root/include/onnxruntime_cxx_api.h" ]]; then
    printf '%s\n' "$install_root"
    exit 0
fi

mkdir -p "$dependency_root"
curl --fail --location --output "$archive_path.tmp" "$ORT_URL"
actual_sha256="$(sha256sum "$archive_path.tmp" | awk '{print $1}')"
if [[ "$actual_sha256" != "$ORT_SHA256" ]]; then
    printf 'ONNX Runtime archive checksum mismatch: expected %s, actual %s\n' \
        "$ORT_SHA256" "$actual_sha256" >&2
    exit 1
fi
mv "$archive_path.tmp" "$archive_path"

temporary_directory="$(mktemp -d "$dependency_root/onnxruntime-extract.XXXXXX")"
trap 'rm -rf "$temporary_directory"' EXIT
tar -xzf "$archive_path" -C "$temporary_directory"
if [[ -e "$install_root" ]]; then
    printf 'Refusing to overwrite partial ONNX Runtime directory: %s\n' \
        "$install_root" >&2
    exit 1
fi
mv "$temporary_directory/onnxruntime-linux-x64-${ORT_VERSION}" "$install_root"
printf '%s\n' "$install_root"
