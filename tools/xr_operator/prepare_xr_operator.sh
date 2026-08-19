#!/usr/bin/env bash

# Stages the Meta XR Operator standalone bundle for the `operator` build type.
#
# The bundle is distributed behind a Meta developer login, so it cannot be
# fetched unattended. Download it once by hand and pass it with --archive; this
# script verifies the checksum and normalises the contents into the layout the
# root build.gradle consumes:
#
#   build/dependencies/meta-xr-operator-<version>/
#       jni/arm64-v8a/<layer>.so
#       assets/openxr/1/api_layers/implicit.d/<layer>.json
#
# build/ is gitignored. Never commit the shared object or the manifest: the
# bundle is covered by Meta's SDK licence.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

version="205.1"
abi="arm64-v8a"
archive=""

# Record the checksum of the archive you downloaded here, and in the workstream
# spec, before this script will stage anything. It is intentionally empty: the
# download is manual, so the pin has to be established by a person who can say
# where the file came from.
expected_sha256=""

usage() {
    echo "Usage: $0 --archive PATH" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --archive)
            if [[ $# -lt 2 ]]; then
                echo "--archive requires a path" >&2
                exit 2
            fi
            archive="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "$archive" ]]; then
    echo "Download the Meta XR Operator standalone bundle, then pass it with --archive." >&2
    echo "  https://developers.meta.com/horizon/downloads/package/meta-xr-operator-standalone/" >&2
    usage
    exit 2
fi

if [[ ! -f "$archive" ]]; then
    echo "Archive does not exist: $archive" >&2
    exit 1
fi

actual_sha256="$(sha256sum "$archive" | awk '{print $1}')"

if [[ -z "$expected_sha256" ]]; then
    echo "No checksum is pinned for the Meta XR Operator bundle." >&2
    echo "Downloaded archive: $archive" >&2
    echo "SHA-256: $actual_sha256" >&2
    echo "Record that value in this script and in" >&2
    echo "specs/2026-08-19-xr-operator-adoption/requirements.md, then re-run." >&2
    exit 1
fi

if [[ "$actual_sha256" != "$expected_sha256" ]]; then
    echo "Archive checksum verification failed." >&2
    echo "Expected: $expected_sha256" >&2
    echo "Actual:   $actual_sha256" >&2
    exit 1
fi

staging_dir="$(mktemp -d)"
trap 'rm -rf "$staging_dir"' EXIT

case "$archive" in
    *.zip|*.aar) unzip -q "$archive" -d "$staging_dir" ;;
    *.tgz|*.tar.gz) tar -xzf "$archive" -C "$staging_dir" ;;
    *)
        echo "Unrecognised archive format: $archive" >&2
        exit 1
        ;;
esac

# Meta documents the filenames and the android/ directory but not a stable
# archive root, so locate exactly one of each and fail on absence or ambiguity.
mapfile -t layer_libraries < <(find "$staging_dir" -type f \
    -name "libXrApiLayer_METAX_operator*.so" -path "*/android/*")
if [[ "${#layer_libraries[@]}" -ne 1 ]]; then
    echo "Expected one Android Operator library; found" \
        "${#layer_libraries[@]}." >&2
    exit 1
fi
layer_library="${layer_libraries[0]}"

mapfile -t layer_manifests < <(find "$staging_dir" -type f \
    -name "XrApiLayer_METAX_operator*.json" -path "*/android/*" \
    -exec grep -l '"api_layer"' {} + 2>/dev/null)
if [[ "${#layer_manifests[@]}" -ne 1 ]]; then
    echo "Expected one Android Operator manifest; found" \
        "${#layer_manifests[@]}." >&2
    exit 1
fi
layer_manifest="${layer_manifests[0]}"

library_description="$(file -b "$layer_library")"
if [[ "$library_description" != *"ARM aarch64"* ]]; then
    echo "The Android layer is not an arm64 shared object." >&2
    echo "File: $layer_library" >&2
    echo "Type: $library_description" >&2
    exit 1
fi

# The loader reads layer manifests from this exact asset path. The "1" is the
# OpenXR major version, not the bundle version; any other value is ignored
# without an error.
# library_path is resolved against the native library directory and then
# stat'ed, so it must name the shared object with no directory component.
readarray -t manifest_fields < <(python3 -c '
import json
import sys

layer = json.load(open(sys.argv[1], encoding="utf-8"))["api_layer"]
print(layer["name"])
print(layer["library_path"])
print(layer["api_version"])
' "$layer_manifest")
declared_name="${manifest_fields[0]:-}"
declared_library="${manifest_fields[1]:-}"
declared_api_version="${manifest_fields[2]:-}"
if [[ "$declared_name" != "XR_APILAYER_METAX_operator" ]]; then
    echo "Unexpected API layer name: $declared_name" >&2
    exit 1
fi
if [[ "$declared_library" != "$(basename "$layer_library")" ]]; then
    echo "The layer manifest declares library_path '$declared_library' but the" >&2
    echo "bundle ships '$(basename "$layer_library")'. The loader would not" >&2
    echo "resolve the layer." >&2
    exit 1
fi
if [[ -z "$declared_api_version" ]]; then
    echo "The API layer manifest does not declare api_version." >&2
    exit 1
fi

output_root="$repo_root/build/dependencies/meta-xr-operator-$version"
manifest_dir="$output_root/assets/openxr/1/api_layers/implicit.d"
library_dir="$output_root/jni/$abi"
rm -rf "$output_root"
mkdir -p "$manifest_dir" "$library_dir"
cp "$layer_library" "$library_dir/"
cp "$layer_manifest" "$manifest_dir/"

echo "Operator bundle: $output_root"
echo "Library: $library_dir/$(basename "$layer_library")"
echo "Manifest: $manifest_dir/$(basename "$layer_manifest")"
echo "API layer: $declared_name (OpenXR $declared_api_version)"
