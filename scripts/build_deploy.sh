#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/toolchain_config.sh
source "$script_dir/toolchain_config.sh"

app=""
build_only=false
vulkan_validation=false
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
        --vulkan-validation)
            vulkan_validation=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 --app {01-openxr-bootstrap|02-vulkan-stereo-triangle|03-head-pose|04-controller-input|05-passthrough|06-spatial-object|07-hand-tracking|08-spatial-anchors|09-quest-camera|16-stereo-probe|xrpassthrough} [--build-only] [--vulkan-validation]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "$app" ]]; then
    echo "Select an application with --app." >&2
    echo "Usage: $0 --app {01-openxr-bootstrap|02-vulkan-stereo-triangle|03-head-pose|04-controller-input|05-passthrough|06-spatial-object|07-hand-tracking|08-spatial-anchors|09-quest-camera|16-stereo-probe|xrpassthrough} [--build-only] [--vulkan-validation]" >&2
    exit 2
fi

quest_export_toolchain
"$script_dir/print_toolchain_config.sh" --strict

case "$app" in
    01-openxr-bootstrap)
        build_command=("$repo_root/gradlew" ":apps:01-openxr-bootstrap:assembleDebug")
        apk_path="$repo_root/apps/01-openxr-bootstrap/build/outputs/apk/debug/01-openxr-bootstrap-debug.apk"
        application_id="com.olibartfast.questlab.openxrbootstrap"
        activity="android.app.NativeActivity"
        log_tag="OpenXRBootstrap"
        ;;
    02-vulkan-stereo-triangle)
        build_command=(
            "$repo_root/gradlew"
            ":apps:02-vulkan-stereo-triangle:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/02-vulkan-stereo-triangle/build/outputs/apk/debug/02-vulkan-stereo-triangle-debug.apk"
        application_id="com.olibartfast.questlab.vulkanstereotriangle"
        activity="android.app.NativeActivity"
        log_tag="VulkanStereoTriangle"
        ;;
    03-head-pose)
        build_command=(
            "$repo_root/gradlew"
            ":apps:03-head-pose:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/03-head-pose/build/outputs/apk/debug/03-head-pose-debug.apk"
        application_id="com.olibartfast.questlab.headpose"
        activity="android.app.NativeActivity"
        log_tag="HeadPose"
        ;;
    04-controller-input)
        build_command=(
            "$repo_root/gradlew"
            ":apps:04-controller-input:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/04-controller-input/build/outputs/apk/debug/04-controller-input-debug.apk"
        application_id="com.olibartfast.questlab.controllerinput"
        activity="android.app.NativeActivity"
        log_tag="ControllerInput"
        ;;
    05-passthrough)
        build_command=(
            "$repo_root/gradlew"
            ":apps:05-passthrough:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/05-passthrough/build/outputs/apk/debug/05-passthrough-debug.apk"
        application_id="com.olibartfast.questlab.passthrough"
        activity="android.app.NativeActivity"
        log_tag="PassthroughMR"
        ;;
    06-spatial-object)
        build_command=(
            "$repo_root/gradlew"
            ":apps:06-spatial-object:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/06-spatial-object/build/outputs/apk/debug/06-spatial-object-debug.apk"
        application_id="com.olibartfast.questlab.spatialobject"
        activity="android.app.NativeActivity"
        log_tag="SpatialObject"
        ;;
    07-hand-tracking)
        build_command=(
            "$repo_root/gradlew"
            ":apps:07-hand-tracking:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/07-hand-tracking/build/outputs/apk/debug/07-hand-tracking-debug.apk"
        application_id="com.olibartfast.questlab.handtracking"
        activity="android.app.NativeActivity"
        log_tag="HandTracking"
        ;;
    08-spatial-anchors)
        build_command=(
            "$repo_root/gradlew"
            ":apps:08-spatial-anchors:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/08-spatial-anchors/build/outputs/apk/debug/08-spatial-anchors-debug.apk"
        application_id="com.olibartfast.questlab.spatialanchors"
        activity="android.app.NativeActivity"
        log_tag="SpatialAnchors"
        ;;
    09-quest-camera)
        build_command=(
            "$repo_root/gradlew"
            ":apps:09-quest-camera:assembleDebug"
        )
        if [[ "$vulkan_validation" == true ]]; then
            build_command+=("-PquestVulkanValidation=true")
        fi
        apk_path="$repo_root/apps/09-quest-camera/build/outputs/apk/debug/09-quest-camera-debug.apk"
        application_id="com.olibartfast.questlab.questcamera"
        activity=".QuestCameraActivity"
        log_tag="QuestCamera"
        ;;
    16-stereo-probe)
        build_command=(
            "$repo_root/gradlew"
            ":apps:16-stereo-probe:assembleDebug"
        )
        apk_path="$repo_root/apps/16-stereo-probe/build/outputs/apk/debug/16-stereo-probe-debug.apk"
        application_id="com.olibartfast.questlab.stereoprobe"
        activity="com.olibartfast.questlab.stereoprobe.StereoProbeActivity"
        log_tag="StereoProbe"
        ;;
    xrpassthrough)
        build_command=("$repo_root/XrPassthrough/Projects/Android/gradlew" assembleDebug)
        build_directory="$repo_root/XrPassthrough/Projects/Android"
        apk_path="$build_directory/build/outputs/apk/debug/XrPassthrough-debug.apk"
        application_id="com.oculus.xrpassthrough"
        activity="com.oculus.NativeActivity"
        log_tag="XrPassthrough"
        ;;
    *)
        echo "Unknown application: $app" >&2
        echo "Available applications: 01-openxr-bootstrap, 02-vulkan-stereo-triangle, 03-head-pose, 04-controller-input, 05-passthrough, 06-spatial-object, 07-hand-tracking, 08-spatial-anchors, 09-quest-camera, 16-stereo-probe, xrpassthrough" >&2
        exit 2
        ;;
esac

if [[ "$vulkan_validation" == true &&
      "$app" != "02-vulkan-stereo-triangle" &&
      "$app" != "03-head-pose" &&
      "$app" != "04-controller-input" &&
      "$app" != "05-passthrough" &&
      "$app" != "06-spatial-object" &&
      "$app" != "07-hand-tracking" &&
      "$app" != "08-spatial-anchors" &&
      "$app" != "09-quest-camera" ]]; then
    echo "--vulkan-validation is supported only by Vulkan applications." >&2
    exit 2
fi

echo "Building $app with Android SDK: $ANDROID_HOME"
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

if [[ "$app" == "16-stereo-probe" ]]; then
    adb shell pm grant "$application_id" android.permission.CAMERA
    adb shell pm grant \
        "$application_id" horizonos.permission.HEADSET_CAMERA
    adb shell am force-stop "$application_id"
fi
adb shell am start -n "$application_id/$activity"

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
