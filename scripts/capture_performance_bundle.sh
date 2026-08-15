#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

app=""
duration_seconds=60
scenario=""
refresh_rate_hz=""
output_dir=""

usage() {
    echo "Usage: $0 --app APP --scenario ID --refresh-rate-hz HZ [--duration-seconds N] [--output DIR]" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)
            app="${2:-}"
            shift 2
            ;;
        --scenario)
            scenario="${2:-}"
            shift 2
            ;;
        --refresh-rate-hz)
            refresh_rate_hz="${2:-}"
            shift 2
            ;;
        --duration-seconds)
            duration_seconds="${2:-}"
            shift 2
            ;;
        --output)
            output_dir="${2:-}"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "$app" || -z "$scenario" || -z "$refresh_rate_hz" ]]; then
    usage
    exit 2
fi
if [[ ! "$duration_seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "--duration-seconds must be a positive integer" >&2
    exit 2
fi
if [[ ! "$refresh_rate_hz" =~ ^(72|90|120)$ ]]; then
    echo "--refresh-rate-hz must be 72, 90, or 120" >&2
    exit 2
fi
if [[ ! "$scenario" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "--scenario may contain only letters, digits, dot, underscore, and dash" >&2
    exit 2
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required to create and validate the performance bundle" >&2
    exit 1
fi

case "$app" in
    01-openxr-bootstrap)
        package="com.olibartfast.questlab.openxrbootstrap.benchmark"
        log_tag="OpenXRBootstrap"
        ;;
    02-vulkan-stereo-triangle)
        package="com.olibartfast.questlab.vulkanstereotriangle.benchmark"
        log_tag="VulkanStereoTriangle"
        ;;
    03-head-pose)
        package="com.olibartfast.questlab.headpose.benchmark"
        log_tag="HeadPose"
        ;;
    04-controller-input)
        package="com.olibartfast.questlab.controllerinput.benchmark"
        log_tag="ControllerInput"
        ;;
    05-passthrough)
        package="com.olibartfast.questlab.passthrough.benchmark"
        log_tag="PassthroughMR"
        ;;
    06-spatial-object)
        package="com.olibartfast.questlab.spatialobject.benchmark"
        log_tag="SpatialObject"
        ;;
    07-hand-tracking)
        package="com.olibartfast.questlab.handtracking.benchmark"
        log_tag="HandTracking"
        ;;
    08-spatial-anchors)
        package="com.olibartfast.questlab.spatialanchors.benchmark"
        log_tag="SpatialAnchors"
        ;;
    09-quest-camera)
        package="com.olibartfast.questlab.questcamera.benchmark"
        log_tag="QuestCamera"
        ;;
    *)
        echo "Performance capture supports repository-owned apps 01 through 09" >&2
        exit 2
        ;;
esac

device_count="$(
    adb devices | awk '$2 == "device" { count++ } END { print count + 0 }'
)"
if [[ "$device_count" -ne 1 ]]; then
    echo "Expected exactly one authorized device; found $device_count" >&2
    exit 1
fi

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
if [[ -z "$output_dir" ]]; then
    output_dir="$repo_root/build/performance/$timestamp-$app-$scenario"
elif [[ "$output_dir" != /* ]]; then
    output_dir="$repo_root/$output_dir"
fi
mkdir -p "$output_dir"

git_sha="$(git -C "$repo_root" rev-parse HEAD)"
device_serial="$(adb get-serialno)"
device_model="$(adb shell getprop ro.product.model | tr -d '\r')"
horizon_os="$(adb shell getprop ro.build.version.incremental | tr -d '\r')"
if [[ "$device_serial" == *:* ]]; then
    tether_state="wireless-adb"
else
    tether_state="usb-adb"
fi

adb shell dumpsys battery > "$output_dir/battery.txt"
adb shell getprop > "$output_dir/device-properties.txt"
git -C "$repo_root" status --short > "$output_dir/git-status.txt"

power_state="battery"
if grep -q "USB powered: true" "$output_dir/battery.txt"; then
    power_state="usb-powered"
elif grep -q "AC powered: true" "$output_dir/battery.txt"; then
    power_state="ac-powered"
elif grep -q "Wireless powered: true" "$output_dir/battery.txt"; then
    power_state="wireless-powered"
fi

jq -n \
    --arg schema "questlab.performance.run.v1" \
    --arg captured_at "$timestamp" \
    --arg git_sha "$git_sha" \
    --arg app "$app" \
    --arg package "$package" \
    --arg scenario "$scenario" \
    --arg device_serial "$device_serial" \
    --arg device_model "$device_model" \
    --arg horizon_os "$horizon_os" \
    --arg power_state "$power_state" \
    --arg tether_state "$tether_state" \
    --argjson refresh_rate_hz "$refresh_rate_hz" \
    --argjson duration_seconds "$duration_seconds" \
    '{
        schema: $schema,
        captured_at_utc: $captured_at,
        git_sha: $git_sha,
        app: $app,
        package: $package,
        build_type: "benchmark",
        instrumentation: {
            bounded_telemetry: true,
            atrace: true
        },
        device: {
            serial: $device_serial,
            model: $device_model,
            horizon_os: $horizon_os
        },
        refresh_rate_hz: $refresh_rate_hz,
        power_state: $power_state,
        tether_state: $tether_state,
        scenario_id: $scenario,
        requested_duration_seconds: $duration_seconds,
        privacy: {
            media_collected: false,
            camera_or_depth_payload_collected: false,
            review_status: "not-required-no-media"
        }
    }' > "$output_dir/run-manifest.json"

"$script_dir/build_deploy.sh" \
    --app "$app" \
    --variant benchmark \
    --perfetto-tracing

set +e
timeout --signal=INT "${duration_seconds}s" \
    adb logcat \
        -T 1 \
        -v epoch \
        -s "$log_tag:V" VrApi:V OpenXR:V '*:S' \
        > "$output_dir/logcat.txt"
logcat_status=$?
set -e
if [[ "$logcat_status" -ne 0 &&
      "$logcat_status" -ne 124 &&
      "$logcat_status" -ne 130 ]]; then
    echo "logcat capture failed with status $logcat_status" >&2
    exit "$logcat_status"
fi

sed -n 's/^.*PERF //p' "$output_dir/logcat.txt" |
while IFS= read -r record; do
    jq -c \
        --arg git_sha "$git_sha" \
        --arg app "$app" \
        --arg package "$package" \
        --arg device "$device_model" \
        --arg horizon_os "$horizon_os" \
        --arg power_state "$power_state" \
        --arg tether_state "$tether_state" \
        --arg scenario "$scenario" \
        --argjson refresh_rate_hz "$refresh_rate_hz" \
        '. + {
            run_identity: {
                git_sha: $git_sha,
                app: $app,
                package: $package,
                build_type: "benchmark",
                instrumentation_enabled: true,
                device: $device,
                horizon_os: $horizon_os,
                refresh_rate_hz: $refresh_rate_hz,
                power_state: $power_state,
                tether_state: $tether_state
            },
            scenario_id: $scenario,
            runtime_metrics: {
                source: "external-tool-correlation-required",
                fps: null,
                stale: null,
                app_gpu_milliseconds: null,
                gpu_percent: null,
                cpu_level: null,
                gpu_level: null
            }
        }' <<< "$record"
done > "$output_dir/records.jsonl"

while IFS= read -r record; do
    jq -e . >/dev/null <<< "$record"
done < "$output_dir/records.jsonl"

echo "Performance bundle: $output_dir"
echo "No screenshots, recordings, camera pixels, or depth payloads were collected."
