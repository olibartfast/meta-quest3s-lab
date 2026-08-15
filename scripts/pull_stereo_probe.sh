#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
package="com.olibartfast.questlab.stereoprobe"
report_name="stereo-probe-report.json"
output="$repo_root/build/stereo-probe/$report_name"
physical_baseline=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            if [[ $# -lt 2 ]]; then
                echo "--output requires a path" >&2
                exit 2
            fi
            output="$2"
            shift 2
            ;;
        --physical-baseline-meters)
            if [[ $# -lt 2 ]]; then
                echo "--physical-baseline-meters requires a value" >&2
                exit 2
            fi
            physical_baseline="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [--output PATH] [--physical-baseline-meters METERS]" >&2
            exit 2
            ;;
    esac
done

device_count="$(adb devices | awk '$2 == "device" { count++ } END { print count + 0 }')"
if [[ "$device_count" -ne 1 ]]; then
    echo "Expected exactly one authorized device; found $device_count." >&2
    exit 1
fi

mkdir -p "$(dirname "$output")"
temporary_report="$(mktemp)"
trap 'rm -f "$temporary_report"' EXIT

adb exec-out run-as "$package" cat "files/$report_name" > "$temporary_report"
python3 -m json.tool "$temporary_report" > "$output"
echo "Report: $output"

evaluation_command=(
    python3
    "$repo_root/tools/stereo_probe_report/evaluate.py"
    "$output"
)
if [[ -n "$physical_baseline" ]]; then
    evaluation_command+=(
        --physical-baseline-meters
        "$physical_baseline"
    )
fi
"${evaluation_command[@]}"
