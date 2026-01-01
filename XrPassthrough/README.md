# OpenXR Passthrough Sample

## Overview
Passthrough is a method that allows users to view their physical environment through a light-blocking VR headset. The `XR_FB_passthrough` feature allows:
* An application to request that passthrough be composited with the application content.
* An application to specify the compositing and blending rules between passthrough and VR content.
* An application to apply styles, such as color mapping and edge rendering, to passthrough.
* An application to provide a geometry to be used in place of the user's physical environment. Camera images will be projected onto the surface provided by the application. In some cases where a part of the environment, such as a desk, can be approximated well, this provides a better visual experience.

## The Sample
This sample demonstrates the use of both still and animated styles, as well as selective and projected passthrough.

---

# Build and Deployment Setup Guide

## Prerequisites

### System Requirements
- **Linux development machine** (Ubuntu/Debian recommended)
- **Meta Quest 3/3S device** with Developer Mode enabled
- **USB-C cable** for device connection

### Development Environment Setup

1. **Navigate to repository root**
   ```bash
   cd /path/to/meta-quest3s-lab
   ```

2. **Install development dependencies**
   ```bash
   ./scripts/setup_quest_dev_env.sh
   ```
   This script installs:
   - Android SDK and NDK r29
   - Build tools and platform tools  
   - Java OpenJDK 21
   - CMake and build dependencies

3. **Configure device connection**
   ```bash
   ./scripts/udev_env_setup.sh
   ```
   Sets up USB rules and permissions for Quest devices.

4. **Restart terminal or reload environment**
   ```bash
   source ~/.bashrc
   ```

## Device Setup

### Enable Developer Mode
1. Open **Meta Quest mobile app**
2. Go to **Menu > Devices > [Your Device] > Developer Mode**
3. Toggle **Developer Mode** ON
4. Accept the developer agreement

### Enable USB Debugging
1. Put on your Quest headset
2. Navigate to **Settings > System > Developer**
3. Enable **USB Debugging**
4. Enable **Guardian Boundary Developer Settings** (recommended)

## Building the Application

### 1. Verify Device Connection
```bash
adb devices
```
Should display your Quest device (e.g., `3487C10GCT066Y device`)

### 2. Navigate to Build Directory
```bash
cd XrPassthrough/Projects/Android
```

### 3. Build the APK
```bash
# Clean previous build (optional)
./gradlew clean

# Build debug APK
./gradlew assembleDebug
```

### 4. Locate Generated APK
The built APK will be at:
```
build/outputs/apk/debug/XrPassthrough-debug.apk
```

## Deployment

### Install to Device
```bash
adb install -r build/outputs/apk/debug/XrPassthrough-debug.apk
```

### Launch Application
- **From headset**: Find "XrPassthrough" in the Apps Library
- **From command line**:
  ```bash
  adb shell am start -n com.oculus.xrpassthrough/.MainActivity
  ```

### Verify Installation
```bash
adb shell pm list packages | grep xrpassthrough
```

## Project Structure

```
XrPassthrough/
├── CMakeLists.txt              # Main build configuration
├── Include/                    # Required header files
│   ├── OVR_Math.h             # Oculus math library
│   ├── OVR_Types.h            # Oculus type definitions
│   ├── xr_linear.h            # OpenXR linear algebra
│   └── meta_openxr_preview/   # Meta OpenXR extensions
├── Src/                       # Application source code
│   ├── XrPassthrough.cpp      # Main application logic
│   ├── XrPassthroughGl.cpp    # OpenGL rendering
│   └── XrPassthroughInput.cpp # Input handling
├── Projects/Android/          # Android build system
│   ├── AndroidManifest.xml
│   ├── build.gradle
│   └── gradlew
└── assets/                    # Application resources
```

## Dependencies

### Automatically Downloaded Headers
The build system automatically includes:
- **OpenXR SDK headers** (`xr_linear.h`)
- **Meta OpenXR extensions** (`openxr_oculus_helpers.h`)
- **OVR Math library** (`OVR_Math.h`, `OVR_Types.h`)

### Native Libraries
- `OpenXR::openxr_loader` - OpenXR runtime
- `android_native_app_glue` - Android NDK framework
- `EGL`, `GLESv3` - Graphics libraries

## Troubleshooting

### Device Connection Issues
```bash
# Restart ADB if device not detected
adb kill-server && adb start-server
adb devices

# Check USB device recognition
lsusb | grep -i meta
```

### Build Issues
- **Missing dependencies**: Re-run `./scripts/setup_quest_dev_env.sh`
- **Java version errors**: Ensure Java 21 is installed
- **NDK not found**: Verify `ANDROID_NDK_HOME` environment variable
- **Permission errors**: Run `./scripts/udev_env_setup.sh` and logout/login

### Runtime Issues
```bash
# View application logs
adb logcat -s XrPassthrough

# Check OpenXR runtime status
adb logcat | grep -i openxr
```

## Key Features

### OpenXR Extensions Used
- `XR_FB_passthrough` - Camera passthrough functionality
- `XR_FB_display_refresh_rate` - Display refresh control
- `XR_META_headset_id` - Device identification

### Application Capabilities
- **Real-time passthrough** with camera feed overlay
- **6DOF spatial tracking** for head and controllers
- **Style effects** including color mapping and edge rendering
- **Selective passthrough** with custom geometry projection
- **Native C++ performance** with OpenGL ES 3.2

## Development Notes

- Project uses **C++17** standard for modern language features
- **CMake build system** with OpenXR integration
- **Android Gradle Plugin 8.2.0** with namespace support
- Optimized for **Meta Quest 3/3S hardware**

For development questions or issues, refer to the [Meta OpenXR SDK documentation](https://developers.meta.com/horizon/documentation/native/android/mobile-openxr/).