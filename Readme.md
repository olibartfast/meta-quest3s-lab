# Meta Quest 3 AR Lab

Quest 3 AR playground environment using the Meta XR SDK with C++(Native development, so no Unity, no Unreal).

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

## Create Project Structure

```bash
./scripts/create_project_structure.sh
```

This script creates the basic directory structure and template files for a new Meta Quest 3 AR application.

**Main Project Components:**
- Core application framework
- XR passthrough manager and spatial anchor system
- Android main entry point

## Build and Deploy

```bash
./scripts/build_deploy.sh
```

Builds the project and deploys it to the connected Quest 3 device.



## Troubleshooting

- **Device not detected**: Ensure udev rules are applied and you're in plugdev group
- **Permission denied**: Logout and login after running udev setup script
- **Build issues**: Verify all environment variables are set (restart terminal after setup)
