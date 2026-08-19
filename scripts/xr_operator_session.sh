#!/usr/bin/env bash

# Opens or closes a Meta XR Operator session against a connected headset.
#
# The API layer runs an MCP server inside the application process on the device,
# so the host reaches it through an adb port forward rather than over the
# network. Nothing here talks to the agent directly; .mcp.json points the agent
# at the forwarded port.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/toolchain_config.sh
source "$repo_root/scripts/toolchain_config.sh"

device_port=8720
host_port=8720
app=""
stop_session=false
prepare_only=false
verify_only=false

# Strict privacy allow-list: apps 05 through 10 can expose the physical room
# through passthrough or camera access, and Meta exposes no documented switch
# that removes Operator's capture tool. Do not start an Operator session for
# those applications at all.
operator_apps=(
    "01-openxr-bootstrap"
    "02-vulkan-stereo-triangle"
    "03-head-pose"
    "04-controller-input"
)

usage() {
    echo "Usage: $0 --app APP [--host-port PORT]" >&2
    echo "       $0 --stop [--host-port PORT]" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)
            if [[ $# -lt 2 ]]; then
                echo "--app requires an application name" >&2
                exit 2
            fi
            app="$2"
            shift 2
            ;;
        --host-port|--port)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires a port number" >&2
                exit 2
            fi
            host_port="$2"
            shift 2
            ;;
        --prepare-only)
            prepare_only=true
            shift
            ;;
        --verify-only)
            verify_only=true
            shift
            ;;
        --stop)
            stop_session=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ ! "$host_port" =~ ^[0-9]+$ ]] ||
   [[ "$host_port" -lt 1 || "$host_port" -gt 65535 ]]; then
    echo "--host-port must be a TCP port number" >&2
    exit 2
fi

if [[ "$prepare_only" == true && "$verify_only" == true ]]; then
    echo "--prepare-only and --verify-only are mutually exclusive." >&2
    exit 2
fi

quest_export_toolchain

if [[ "$stop_session" == true ]]; then
    if [[ -n "$app" ]]; then
        echo "--stop does not take --app." >&2
        exit 2
    fi
    # The streaming backend's adb reverse mapping is never torn down; do not
    # repeat that here.
    if [[ "$prepare_only" == true || "$verify_only" == true ]]; then
        echo "--stop cannot be combined with an internal phase flag." >&2
        exit 2
    fi
    if adb forward --list |
       awk -v port="tcp:$host_port" '$2 == port { found = 1 } END { exit !found }'; then
        adb forward --remove "tcp:$host_port"
        echo "Removed: tcp:$host_port"
    else
        echo "No forward was active for tcp:$host_port."
    fi
    exit 0
fi

if [[ -z "$app" ]]; then
    echo "Select an application with --app, or pass --stop." >&2
    usage
    exit 2
fi

app_supported=false
for operator_app in "${operator_apps[@]}"; do
    if [[ "$app" == "$operator_app" ]]; then
        app_supported=true
        break
    fi
done
if [[ "$app_supported" != true ]]; then
    echo "Meta XR Operator is available only for apps 01-04;" \
        "$app is outside the strict privacy allow-list." >&2
    exit 2
fi

device_count="$(adb devices | awk '$2 == "device" { count++ } END { print count + 0 }')"
if [[ "$device_count" -ne 1 ]]; then
    echo "Expected exactly one authorized device; found $device_count." >&2
    echo "Check 'adb devices -l'." >&2
    exit 1
fi

if [[ "$verify_only" != true ]]; then
    # Horizon OS clears this on every reboot, so set it before every launch
    # rather than documenting it as a one-time step.
    adb shell setprop debug.oculus.experimentalEnabled 1
    echo "Experimental features: enabled"

    adb shell setprop debug.meta_xr_operator.request_capture_permission 1
    echo "Screen capture: requested"
    echo "  The headset shows a capture consent prompt on first launch after"
    echo "  each install. It must be accepted in the headset by a person."

    # --host-port changes only the computer-side port. Operator always listens
    # on 8720 inside the application process.
    adb forward "tcp:$host_port" "tcp:$device_port"
    echo "Forward: tcp:$host_port -> device tcp:$device_port"
fi

if [[ "$prepare_only" == true ]]; then
    exit 0
fi

# An MCP server that accepts screen captures and injects controller input must
# not be reachable from the local network. adb forward only needs a loopback
# bind, so a wildcard bind is a finding, not a preference.
port_hex="$(printf '%04X' "$device_port")"
listening=""
for _ in {1..50}; do
    listening="$(adb shell cat /proc/net/tcp /proc/net/tcp6 2>/dev/null |
        awk -v port_hex=":$port_hex" \
            '$4 == "0A" && index($2, port_hex) { print $2 }' |
        sort -u)"
    if [[ -n "$listening" ]]; then
        break
    fi
    sleep 0.2
done

if [[ -z "$listening" ]]; then
    echo "Bind check failed: no listener on device port $device_port." >&2
    echo "  Keep the operator build running and focused, then retry." >&2
    exit 1
else
    for local_address in $listening; do
        address="${local_address%%:*}"
        case "$address" in
            0100007F|00000000000000000000000001000000)
                echo "Bind check: loopback ($local_address)"
                ;;
            *)
                echo "Bind check failed: port $device_port is bound to" \
                    "$local_address, not loopback." >&2
                echo "  Anyone on the same network could capture the headset" \
                    "view and inject input. Stop the application." >&2
                exit 1
                ;;
        esac
    done
fi

echo "Endpoint: http://localhost:$host_port/sse"
