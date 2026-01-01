#!/bin/bash
# build_deploy.sh

set -e

PROJECT_DIR="$HOME/repos/meta-quest3s-lab"
BUILD_DIR="$PROJECT_DIR/build"
APK_NAME="quest3ar"

cd "$PROJECT_DIR"

# Clean build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/home/oli/android-ndk-r29/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -G Ninja

# Build
ninja -j$(nproc)

# Create lib directory structure and copy library (correct Android APK structure)
mkdir -p lib/arm64-v8a
cp libmeta_quest3s_lab.so lib/arm64-v8a/

# Package APK
$ANDROID_SDK_ROOT/build-tools/35.0.0/aapt package -f -M ../AndroidManifest.xml \
    -I $ANDROID_SDK_ROOT/platforms/android-34/android.jar \
    -F ${APK_NAME}_unsigned.apk \
    lib/arm64-v8a

# Align APK first
$ANDROID_SDK_ROOT/build-tools/35.0.0/zipalign -f 4 ${APK_NAME}_unsigned.apk ${APK_NAME}_aligned.apk

# Sign APK with apksigner (modern Android signing)
if [ ! -f ~/.android/debug.keystore ]; then
    keytool -genkey -v -keystore ~/.android/debug.keystore \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass android -keypass android \
        -dname "CN=Android Debug,O=Android,C=US"
fi

$ANDROID_SDK_ROOT/build-tools/35.0.0/apksigner sign \
    --ks ~/.android/debug.keystore \
    --ks-key-alias androiddebugkey \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out ${APK_NAME}.apk \
    ${APK_NAME}_aligned.apk

# Deploy
echo "Installing on device..."
adb install -r ${APK_NAME}.apk

echo "Launching application..."
adb shell am start -n com.app.quest3ar/android.app.NativeActivity

echo "Monitoring logs..."
adb logcat -s Quest3AR:V OpenXR:V *:E