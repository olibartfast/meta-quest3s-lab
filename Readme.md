# Meta Quest 3 AR Lab

Quest 3 AR playground environment using the Meta XR SDK with C++(Native development, so currently no Unity, no Unreal).

## Quick Start

1. **Development Environment Setup**
   ```bash
   ./scripts/setup_quest_dev_env.sh
   ```

2. **Device Connection Setup**
   ```bash
   ./scripts/udev_env_setup.sh
   ```

3. **Verify Device Connection**
   ```bash
   adb devices
   ```

## Environment Setup

### Prerequisites
- Linux development machine
- Meta Quest 3 device
- USB-C cable for device connection

### Automated Setup Scripts

#### `scripts/setup_quest_dev_env.sh`
Installs and configures the complete Android development environment:
- Android NDK r29
- Android SDK with command line tools
- Build tools and platform tools
- Java OpenJDK 21
- CMake and Ninja build system

#### `scripts/udev_env_setup.sh`
Configures device connection and debugging:
- Sets up udev rules for Meta Quest devices (vendor ID 2833)
- Adds user to plugdev group
- Installs ADB and scrcpy for device interaction

## Architecture Overview

**Core Technologies:**
- **Android NDK** - Native C++ compilation
- **OpenXR** - Cross-platform XR runtime API
- **Vulkan** - High-performance rendering (optimal for Quest 3)
- **CMake** - Cross-platform build system
- **Meta XR SDK** - Quest-specific XR features

## Development Workflow

1. Run setup scripts to prepare development environment
2. Connect Quest 3 device via USB-C
3. Enable Developer Mode and USB Debugging on device
4. Verify connection with `adb devices`
5. Build and deploy applications using NDK/CMake

## Build and Deploy

```bash
./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle
```

Builds and deploys the Vulkan stereo triangle. Use `--app 01-openxr-bootstrap`
for the empty-frame lifecycle application, `--app 03-head-pose` for the
tracking and coordinate-space visualization, `--app 04-controller-input` for
controller rays and selection, `--app 05-passthrough` for native Vulkan mixed
reality, `--app 06-spatial-object` through `--app 08-spatial-anchors` for the
spatial interaction examples, `--app 09-quest-camera` for Camera2 preview,
`--app 10-rfdetr-detection` for deployable on-device RF-DETR world detection,
or `--app xrpassthrough` for the preserved legacy baseline. Add `--build-only`
to skip ADB deployment.

The RF-DETR app builds, installs, provisions the model into app-private storage,
verifies its checksum, grants the Quest camera permissions, and launches with:

```bash
./scripts/build_deploy.sh --app 10-rfdetr-detection
```

See `docs/rfdetr-detection.md` for controls and streaming/replay selection.

Use the optimized, locally signed measurement variant with:

```bash
./scripts/build_deploy.sh \
  --app 09-quest-camera \
  --variant benchmark \
  --perfetto-tracing
```

Shared bounded telemetry reports one structured CPU-phase snapshot per second.
See `docs/performance-validation.md` for host tests, schema details, Perfetto,
MQDH/OVR Metrics correlation, and privacy-safe evidence capture.



## Troubleshooting

- **Device not detected**: Ensure udev rules are applied and you're in plugdev group
- **Permission denied**: Logout and login after running udev setup script
- **Build issues**: Verify all environment variables are set (restart terminal after setup)

### Other Link and Resources
- [Start develop with Unity](https://developers.meta.com/horizon/documentation/unity/unity-depthapi-overview/)
- [Start develop with Unreal](https://developers.meta.com/horizon/documentation/unreal/unreal-create-and-configure-new-project)
