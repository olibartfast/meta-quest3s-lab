# Development Environment

## Current Applications

The repository contains eleven independently selectable repository-owned
Android applications under `apps/`, covering the native OpenXR lifecycle,
Quest Camera2 capture, RF-DETR detection, and the stereo capability probe.
`XrPassthrough` remains the preserved Meta/OpenGL ES baseline.

## Inspect the Toolchain

From the repository root, run:

```bash
./scripts/print_toolchain_config.sh --strict
```

The strict check enforces Java 21, SDK Platform 34, Build Tools 34.0.0, CMake 3.22.1, and NDK 29.0.14206865. `QUEST_ANDROID_SDK_ROOT` selects the canonical SDK, followed by `ANDROID_HOME`, then `ANDROID_SDK_ROOT`; all Android variables are normalized to that one root. `QUEST_JAVA_HOME` can select a nonstandard Java 21 installation. Gradle itself uses JDK 17 through `QUEST_GRADLE_JAVA_HOME`, matching AGP 8.2 compatibility and avoiding the Android JDK-image transform failure seen when AGP runs on JDK 21.

## Authoritative Documentation

Meta publishes [LLM-optimized developer resources](https://developers.meta.com/horizon/essentials/ai-solutions/) and a dedicated [Native & OpenXR documentation index](https://developers.meta.com/horizon/llmstxt/documentation/native/llms.txt/). Use that index for current Quest-specific Android, OpenXR, Vulkan, device, deployment, and debugging guidance. Use the Khronos OpenXR specification for cross-platform API semantics.

## Build

Use the repository-owned wrapper command:

```bash
./scripts/build_deploy.sh --app 01-openxr-bootstrap --build-only
./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle --build-only
./scripts/build_deploy.sh --app 09-quest-camera --build-only
./scripts/build_deploy.sh --app xrpassthrough --build-only
```

Application selection is explicit so the legacy baseline remains available. The bootstrap uses the shared root Gradle 8.5/AGP 8.2 workspace; the passthrough selection uses its preserved standalone build. APKs are written to:

```text
apps/01-openxr-bootstrap/build/outputs/apk/debug/01-openxr-bootstrap-debug.apk
apps/02-vulkan-stereo-triangle/build/outputs/apk/debug/02-vulkan-stereo-triangle-debug.apk
apps/09-quest-camera/build/outputs/apk/debug/09-quest-camera-debug.apk
XrPassthrough/Projects/Android/build/outputs/apk/debug/XrPassthrough-debug.apk
```

For measurements, use the optimized `benchmark` variant. It uses the release
native configuration, is non-debuggable, and is signed with the local debug
key only to permit laboratory installation:

```bash
./scripts/build_deploy.sh \
  --app 09-quest-camera \
  --variant benchmark \
  --build-only
```

Add `--perfetto-tracing` when a short ATRACE/Perfetto scheduling capture is
required. Leave it off for the matching disabled-instrumentation comparison.

### Experimental XR Operator build

The opt-in `operator` variant packages the experimental Meta XR Operator
OpenXR API layer into synthetic apps 01–04 only. It is debuggable, has an
`.operator` application-id suffix, and requests INTERNET so its in-process MCP
server can be reached over an ADB forward. Apps 05–10 are excluded because
they render passthrough or hold camera permissions; app 16 and the preserved
legacy target are also unavailable.

Download Meta XR Operator Standalone 205.1 through a Meta developer account.
Before first use, record its license, archive SHA-256, Android file tree, layer
manifest name, `library_path`, and `api_version` in
`specs/2026-08-19-xr-operator-adoption/requirements.md`. Then pin the reviewed
SHA-256 in `tools/xr_operator/prepare_xr_operator.sh` and stage the archive:

```bash
./tools/xr_operator/prepare_xr_operator.sh \
  --archive /path/to/meta-xr-operator-standalone.zip
```

Build-only validation does not contact a headset:

```bash
./scripts/build_deploy.sh \
  --app 01-openxr-bootstrap \
  --variant operator \
  --build-only
```

Without `--build-only`, deployment prepares the device properties and ADB
forward before launch, then verifies that the server is listening on device
port 8720 and is bound only to loopback. The project `.mcp.json` connects
directly to `http://localhost:8720/sse`; Meta recommends its proxy when
automatic reconnect and an offline tool list are required.

```bash
./scripts/build_deploy.sh \
  --app 01-openxr-bootstrap \
  --variant operator
./scripts/xr_operator_session.sh --stop
```

Meta documents Operator captures as compositing physical-environment and
virtual content, and does not document a switch that removes the capture tool.
The build and session scripts therefore reject apps 05–10 entirely. They do not
receive the layer, server, Operator manifest, or its added INTERNET permission.
This allow-list is the enforceable privacy boundary; controller injection and
state inspection through Operator are intentionally unavailable for those apps.

## Connect and Deploy

Enable Developer Mode and USB debugging on the Quest, connect it over USB, accept the authorization prompt in the headset, then verify exactly one device is available:

```bash
adb devices -l
./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle
```

The script builds, installs with `adb install -r`, and launches the selected
activity. Keep the headset awake and the controllers active during launch.
Inspect Milestone 2 runtime output with:

```bash
adb logcat -s VulkanStereoTriangle:V OpenXR:V '*:S'
```

## Power During Development

A computer USB port can maintain an ADB data connection while supplying less power than an active Quest 3 consumes. Passthrough, tracking, displays, CPU, and GPU workloads may therefore drain the battery slowly even while Android reports that it is charging.

Inspect the charging state with:

```bash
adb shell dumpsys battery
```

For long sessions, prefer a reputable USB-C Power Delivery charger rated for at least 18 W; a 30 W unit provides useful headroom. Use a USB-C cable rated for at least 60 W, long enough to avoid pulling on the headset connector. For simultaneous wired ADB and external power, use a Quest-compatible data cable with a separate power-injection port. Do not assume that a generic hub's PD input powers its downstream data port.

## ADB over Wi-Fi

The simplest long-running development setup is wireless ADB with the headset connected directly to its charger. The computer and Quest must be on the same trusted local network.

Start with USB connected, the headset unlocked, and the USB debugging prompt accepted:

```bash
adb devices -l
adb shell ip route
```

Find the `src` address on the `wlan0` route. For example, this route identifies `192.168.0.123` as the headset address:

```text
192.168.0.0/24 dev wlan0 proto kernel scope link src 192.168.0.123
```

Restart the headset's ADB daemon in TCP mode and connect to that address:

```bash
adb tcpip 5555
adb connect <QUEST_IP>:5555
adb devices -l
```

The final command initially shows the same headset twice: once by USB serial and once as `<QUEST_IP>:5555`. Unplug USB and verify that only the Wi-Fi transport remains:

```bash
adb devices -l
```

This repository's deployment script requires exactly one authorized ADB transport, so disconnect USB before running:

```bash
./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle
```

If a command must be run while both transports are present, select one explicitly:

```bash
adb -s <QUEST_IP>:5555 shell
adb -s <USB_SERIAL> shell
```

The Quest's DHCP address can change, and TCP mode may reset after a headset reboot. If reconnection fails, repeat the USB setup. Use either of the following cleanup commands as appropriate:

```bash
# Return the headset's ADB daemon to USB mode; the Wi-Fi connection will close.
adb -s <QUEST_IP>:5555 usb

# Or only remove the current computer's Wi-Fi connection.
adb disconnect <QUEST_IP>:5555
```

Avoid enabling or exposing TCP port 5555 on an untrusted network.

## Troubleshooting

- **Different SDK paths:** set `QUEST_ANDROID_SDK_ROOT` to the intended SDK. Setup, diagnostics, and deployment all align the Android variables.
- **No authorized device:** run `adb kill-server`, reconnect the headset, and accept its USB debugging prompt.
- **Linux USB permissions:** run `./scripts/udev_env_setup.sh`, then log out and back in after group changes.
- **Battery drains over USB:** use the power guidance above, close the XR application between tests, and let the headset cool if its reported temperature remains elevated.
- **Launch requires controllers:** wake both controllers and put on the headset before launching the NativeActivity.
- **Black compositor view:** this is expected from `01-openxr-bootstrap`, which submits zero layers in Milestone 1.
- **No triangle:** confirm the session reached `FOCUSED`, both eye swapchains
  were created, and no `VulkanStereoTriangle` errors preceded the frame loop.
- **Toolchain mismatch:** run `./scripts/setup_quest_dev_env.sh`; builds reject versions other than the repository pins.

## Verified Baseline

On 2026-07-22, the legacy application successfully built, installed, and launched on Quest 3. Milestone 1 still requires its documented three-cycle device acceptance run before `ROADMAP.md` can mark it complete.
