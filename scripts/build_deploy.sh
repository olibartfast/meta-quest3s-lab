#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/toolchain_config.sh
source "$script_dir/toolchain_config.sh"

app=""
backend="ondevice"
model_path="$repo_root/build/models/rfdetr/rfdetr-nano.onnx"
manifest_path="$repo_root/tools/rfdetr_export/model-manifest.json"
service_host="127.0.0.1"
service_port="48110"
replay_detections=""
build_only=false
vulkan_validation=false
perfetto_tracing=false
variant="debug"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)
            if [[ $# -lt 2 ]]; then
                echo "--app requires a value" >&2
                exit 2
            fi
            app="$2"
            shift 2
            ;;
        --build-only)
            build_only=true
            shift
            ;;
        --backend)
            if [[ $# -lt 2 ]]; then
                echo "--backend requires a value" >&2
                exit 2
            fi
            backend="$2"
            shift 2
            ;;
        --model)
            if [[ $# -lt 2 ]]; then
                echo "--model requires a value" >&2
                exit 2
            fi
            model_path="$2"
            shift 2
            ;;
        --manifest)
            if [[ $# -lt 2 ]]; then
                echo "--manifest requires a value" >&2
                exit 2
            fi
            manifest_path="$2"
            shift 2
            ;;
        --service-host)
            if [[ $# -lt 2 ]]; then
                echo "--service-host requires a value" >&2
                exit 2
            fi
            service_host="$2"
            shift 2
            ;;
        --service-port)
            if [[ $# -lt 2 ]]; then
                echo "--service-port requires a value" >&2
                exit 2
            fi
            service_port="$2"
            shift 2
            ;;
        --replay-detections)
            if [[ $# -lt 2 ]]; then
                echo "--replay-detections requires a value" >&2
                exit 2
            fi
            replay_detections="$2"
            shift 2
            ;;
        --vulkan-validation)
            vulkan_validation=true
            shift
            ;;
        --perfetto-tracing)
            perfetto_tracing=true
            shift
            ;;
        --variant)
            if [[ $# -lt 2 ]]; then
                echo "--variant requires a value" >&2
                exit 2
            fi
            variant="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 --app {01-openxr-bootstrap|02-vulkan-stereo-triangle|03-head-pose|04-controller-input|05-passthrough|06-spatial-object|07-hand-tracking|08-spatial-anchors|09-quest-camera|10-rfdetr-detection|16-stereo-probe|xrpassthrough} [--variant {debug|benchmark|operator}] [--build-only] [--vulkan-validation] [--perfetto-tracing] [--backend {ondevice|streaming|replay}] [--model PATH] [--manifest PATH] [--service-host HOST] [--service-port PORT] [--replay-detections PATH]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "$app" ]]; then
    echo "Select an application with --app." >&2
    echo "Usage: $0 --app {01-openxr-bootstrap|02-vulkan-stereo-triangle|03-head-pose|04-controller-input|05-passthrough|06-spatial-object|07-hand-tracking|08-spatial-anchors|09-quest-camera|10-rfdetr-detection|16-stereo-probe|xrpassthrough} [--variant {debug|benchmark|operator}] [--build-only] [--vulkan-validation] [--perfetto-tracing] [--backend {ondevice|streaming|replay}] [--model PATH] [--manifest PATH] [--service-host HOST] [--service-port PORT] [--replay-detections PATH]" >&2
    exit 2
fi

case "$variant" in
    debug)
        gradle_variant="Debug"
        ;;
    benchmark)
        gradle_variant="Benchmark"
        ;;
    operator)
        gradle_variant="Operator"
        ;;
    *)
        echo "Unknown variant: $variant" >&2
        echo "Available variants: debug, benchmark, operator" >&2
        exit 2
        ;;
esac

if [[ "$variant" == "operator" ]]; then
    case "$app" in
        01-openxr-bootstrap|02-vulkan-stereo-triangle|03-head-pose|04-controller-input)
            ;;
        *)
            echo "--variant operator is available only for apps 01-04;" \
                "$app is outside the strict privacy allow-list." >&2
            exit 2
            ;;
    esac
fi

quest_export_toolchain
"$script_dir/print_toolchain_config.sh" --strict

case "$app" in
    01-openxr-bootstrap)
        build_command=("$repo_root/gradlew" ":apps:01-openxr-bootstrap:assemble${gradle_variant}")
        apk_path="$repo_root/apps/01-openxr-bootstrap/build/outputs/apk/$variant/01-openxr-bootstrap-$variant.apk"
        application_id="com.olibartfast.questlab.openxrbootstrap"
        activity="android.app.NativeActivity"
        log_tag="OpenXRBootstrap"
        ;;
    02-vulkan-stereo-triangle)
        build_command=(
            "$repo_root/gradlew"
            ":apps:02-vulkan-stereo-triangle:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/02-vulkan-stereo-triangle/build/outputs/apk/$variant/02-vulkan-stereo-triangle-$variant.apk"
        application_id="com.olibartfast.questlab.vulkanstereotriangle"
        activity="android.app.NativeActivity"
        log_tag="VulkanStereoTriangle"
        ;;
    03-head-pose)
        build_command=(
            "$repo_root/gradlew"
            ":apps:03-head-pose:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/03-head-pose/build/outputs/apk/$variant/03-head-pose-$variant.apk"
        application_id="com.olibartfast.questlab.headpose"
        activity="android.app.NativeActivity"
        log_tag="HeadPose"
        ;;
    04-controller-input)
        build_command=(
            "$repo_root/gradlew"
            ":apps:04-controller-input:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/04-controller-input/build/outputs/apk/$variant/04-controller-input-$variant.apk"
        application_id="com.olibartfast.questlab.controllerinput"
        activity="android.app.NativeActivity"
        log_tag="ControllerInput"
        ;;
    05-passthrough)
        build_command=(
            "$repo_root/gradlew"
            ":apps:05-passthrough:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/05-passthrough/build/outputs/apk/$variant/05-passthrough-$variant.apk"
        application_id="com.olibartfast.questlab.passthrough"
        activity="android.app.NativeActivity"
        log_tag="PassthroughMR"
        ;;
    06-spatial-object)
        build_command=(
            "$repo_root/gradlew"
            ":apps:06-spatial-object:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/06-spatial-object/build/outputs/apk/$variant/06-spatial-object-$variant.apk"
        application_id="com.olibartfast.questlab.spatialobject"
        activity="android.app.NativeActivity"
        log_tag="SpatialObject"
        ;;
    07-hand-tracking)
        build_command=(
            "$repo_root/gradlew"
            ":apps:07-hand-tracking:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/07-hand-tracking/build/outputs/apk/$variant/07-hand-tracking-$variant.apk"
        application_id="com.olibartfast.questlab.handtracking"
        activity="android.app.NativeActivity"
        log_tag="HandTracking"
        ;;
    08-spatial-anchors)
        build_command=(
            "$repo_root/gradlew"
            ":apps:08-spatial-anchors:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/08-spatial-anchors/build/outputs/apk/$variant/08-spatial-anchors-$variant.apk"
        application_id="com.olibartfast.questlab.spatialanchors"
        activity="android.app.NativeActivity"
        log_tag="SpatialAnchors"
        ;;
    09-quest-camera)
        build_command=(
            "$repo_root/gradlew"
            ":apps:09-quest-camera:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/09-quest-camera/build/outputs/apk/$variant/09-quest-camera-$variant.apk"
        application_id="com.olibartfast.questlab.questcamera"
        activity=".QuestCameraActivity"
        log_tag="QuestCamera"
        ;;
    10-rfdetr-detection)
        "$repo_root/tools/rfdetr_inference/prepare_onnxruntime_android.sh"
        build_command=(
            "$repo_root/gradlew"
            ":apps:10-rfdetr-detection:assemble${gradle_variant}"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/10-rfdetr-detection/build/outputs/apk/$variant/10-rfdetr-detection-$variant.apk"
        application_id="com.olibartfast.questlab.rfdetrdetection"
        activity="com.olibartfast.questlab.questcamera.QuestCameraActivity"
        log_tag="RFDetrDetection"
        ;;
    16-stereo-probe)
        build_command=(
            "$repo_root/gradlew"
            ":apps:16-stereo-probe:assemble${gradle_variant}"
        )
        apk_path="$repo_root/apps/16-stereo-probe/build/outputs/apk/$variant/16-stereo-probe-$variant.apk"
        application_id="com.olibartfast.questlab.stereoprobe"
        activity="com.olibartfast.questlab.stereoprobe.StereoProbeActivity"
        log_tag="StereoProbe"
        ;;
    xrpassthrough)
        build_command=("$repo_root/XrPassthrough/Projects/Android/gradlew" "assemble${gradle_variant}")
        build_directory="$repo_root/XrPassthrough/Projects/Android"
        apk_path="$build_directory/build/outputs/apk/$variant/XrPassthrough-$variant.apk"
        application_id="com.oculus.xrpassthrough"
        activity="com.oculus.NativeActivity"
        log_tag="XrPassthrough"
        ;;
    *)
        echo "Unknown application: $app" >&2
        echo "Available applications: 01-openxr-bootstrap, 02-vulkan-stereo-triangle, 03-head-pose, 04-controller-input, 05-passthrough, 06-spatial-object, 07-hand-tracking, 08-spatial-anchors, 09-quest-camera, 10-rfdetr-detection, 16-stereo-probe, xrpassthrough" >&2
        exit 2
        ;;
esac

if [[ "$variant" == "benchmark" ]]; then
    application_id+=".benchmark"
fi

if [[ "$variant" == "operator" ]]; then
    application_id+=".operator"
    build_command+=("-PquestXrOperator=true")
fi

if [[ "$perfetto_tracing" == true ]]; then
    if [[ "$app" == "xrpassthrough" ]]; then
        echo "--perfetto-tracing is unavailable for the preserved legacy target." >&2
        exit 2
    fi
    build_command+=("-PquestPerfettoTracing=true")
fi

if [[ "$vulkan_validation" == true &&
      "$app" != "02-vulkan-stereo-triangle" &&
      "$app" != "03-head-pose" &&
      "$app" != "04-controller-input" &&
      "$app" != "05-passthrough" &&
      "$app" != "06-spatial-object" &&
      "$app" != "07-hand-tracking" &&
      "$app" != "08-spatial-anchors" &&
      "$app" != "09-quest-camera" &&
      "$app" != "10-rfdetr-detection" ]]; then
    echo "--vulkan-validation is supported only by Vulkan applications." >&2
    exit 2
fi

echo "Building $app ($variant) with Android SDK: $ANDROID_HOME"
if [[ -n "${build_directory:-}" ]]; then
    (
        cd "$build_directory"
        "${build_command[@]}"
    )
else
    "${build_command[@]}"
fi

if [[ ! -f "$apk_path" ]]; then
    echo "Expected APK was not produced: $apk_path" >&2
    exit 1
fi
echo "APK: $apk_path"

if [[ "$build_only" == true ]]; then
    exit 0
fi

device_count="$(adb devices | awk '$2 == "device" { count++ } END { print count + 0 }')"
if [[ "$device_count" -ne 1 ]]; then
    echo "Expected exactly one authorized device; found $device_count." >&2
    echo "Check 'adb devices -l' or use --build-only." >&2
    exit 1
fi

adb install -r "$apk_path"

if [[ "$variant" == "operator" ]]; then
    # The experimental and capture-consent properties must be set before the
    # application starts. This also creates the ADB forward consumed by the
    # project-scoped MCP configuration.
    "$script_dir/xr_operator_session.sh" --app "$app" --prepare-only
fi

if [[ "$app" == "16-stereo-probe" ]]; then
    adb shell pm grant "$application_id" android.permission.CAMERA
    adb shell pm grant \
        "$application_id" horizonos.permission.HEADSET_CAMERA
    adb shell am force-stop "$application_id"
fi

if [[ "$app" == "10-rfdetr-detection" ]]; then
    case "$backend" in
        ondevice|streaming|replay)
            ;;
        *)
            echo "Unknown detector backend: $backend" >&2
            echo "Available backends: ondevice, streaming, replay" >&2
            exit 2
            ;;
    esac
    adb shell pm grant "$application_id" android.permission.CAMERA
    adb shell pm grant \
        "$application_id" horizonos.permission.HEADSET_CAMERA
    adb shell run-as "$application_id" mkdir -p files
    submission_hz="2"
    expiry_ms="1500"
    if [[ "$backend" == "ondevice" ]]; then
        # RF-DETR Nano is substantially slower on mobile CPU than the trusted
        # link. Keep the queue sparse and retain the exact frame long enough
        # for a first unoptimized XNNPACK/CPU result to remain visible.
        submission_hz="0.25"
        expiry_ms="20000"
    fi
    printf '%s\n' \
        "backend=$backend" \
        "service_host=$service_host" \
        "service_port=$service_port" \
        "submission_hz=$submission_hz" \
        "expiry_ms=$expiry_ms" \
        "xnnpack_threads=4" | \
        adb exec-in \
            "run-as $application_id sh -c 'cat > files/detector.conf'"

    if [[ "$backend" == "ondevice" ]]; then
        if [[ ! -f "$model_path" || ! -f "$manifest_path" ]]; then
            echo "On-device backend requires the exported model and manifest." >&2
            echo "Model:    $model_path" >&2
            echo "Manifest: $manifest_path" >&2
            exit 1
        fi
        expected_model_sha256="$(
            sha256sum "$model_path" | awk '{print $1}'
        )"
        adb exec-in \
            "run-as $application_id sh -c 'cat > files/rfdetr-nano.onnx'" \
            < "$model_path"
        adb exec-in \
            "run-as $application_id sh -c 'cat > files/model-manifest.json'" \
            < "$manifest_path"
        installed_model_sha256="$(
            adb shell run-as "$application_id" \
                sha256sum files/rfdetr-nano.onnx | awk '{print $1}'
        )"
        if [[ "$installed_model_sha256" != "$expected_model_sha256" ]]; then
            echo "Device model checksum verification failed." >&2
            exit 1
        fi
        echo "Provisioned on-device model SHA-256: $installed_model_sha256"
    elif [[ "$backend" == "streaming" &&
            "$service_host" == "127.0.0.1" ]]; then
        adb reverse "tcp:$service_port" "tcp:$service_port"
    elif [[ "$backend" == "replay" ]]; then
        if [[ ! -f "$replay_detections" ]]; then
            echo "Replay backend requires --replay-detections PATH." >&2
            exit 1
        fi
        adb exec-in \
            "run-as $application_id sh -c 'cat > files/replay-detections.json'" \
            < "$replay_detections"
    fi
fi

adb shell am start -n "$application_id/$activity"

if [[ "$variant" == "operator" ]]; then
    "$script_dir/xr_operator_session.sh" --app "$app" --verify-only
fi

if [[ "$app" == "16-stereo-probe" ]]; then
    probe_started=false
    for _ in {1..50}; do
        if adb shell pidof "$application_id" > /dev/null; then
            probe_started=true
            break
        fi
        sleep 0.2
    done
    if [[ "$probe_started" != true ]]; then
        echo "APK installed, but Horizon did not start the probe." >&2
        echo "Wake and wear the headset, dismiss any launch-check dialog, then run:" >&2
        echo "  adb shell am start -n $application_id/$activity" >&2
        exit 1
    fi
fi

echo "Application launched. Inspect logs with:"
echo "  adb logcat -s $log_tag:V OpenXR:V '*:S'"
