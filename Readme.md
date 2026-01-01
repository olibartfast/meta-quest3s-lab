# Meta Quest 3 AR Lab

Quest 3 AR application using the Meta XR SDK with C++.

## Quick Start

1. **Development Environment Setup**
   ```bash
   ./setup_quest_dev_env.sh
   ```

2. **Device Connection Setup**
   ```bash
   ./udev_env_setup.sh
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

#### `setup_quest_dev_env.sh`
Installs and configures the complete Android development environment:
- Android NDK r29
- Android SDK with command line tools
- Build tools and platform tools
- Java OpenJDK 21
- CMake and Ninja build system

#### `udev_env_setup.sh`
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

## Troubleshooting

- **Device not detected**: Ensure udev rules are applied and you're in plugdev group
- **Permission denied**: Logout and login after running udev setup script
- **Build issues**: Verify all environment variables are set (restart terminal after setup)