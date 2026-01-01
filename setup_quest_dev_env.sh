#!/bin/bash
# setup_quest_dev_env.sh


set -e

sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    wget \
    unzip \
    openjdk-21-jdk \
    adb \
    android-sdk-platform-tools-common

NDK_VERSION="r29"
NDK_DIR="$HOME/android-ndk-$NDK_VERSION"
SDK_DIR="$HOME/android-sdk"

# Download NDK
if [ ! -d "$NDK_DIR" ]; then
    echo "Downloading Android NDK $NDK_VERSION..."
    NDK_URL="https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip"
    wget "$NDK_URL" -O ndk.zip
    unzip -q ndk.zip -d "$HOME"
    rm ndk.zip
fi

# Download SDK Command Line Tools
mkdir -p "$SDK_DIR"

if [ ! -d "$SDK_DIR/cmdline-tools/latest" ]; then
    echo "Downloading Android SDK Command Line Tools..."
    SDK_URL="https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
    wget "$SDK_URL" -O cmdline-tools.zip
    unzip -q cmdline-tools.zip -d "$SDK_DIR"
    
    mkdir -p "$SDK_DIR/cmdline-tools/latest"
    mv "$SDK_DIR/cmdline-tools/bin" "$SDK_DIR/cmdline-tools/latest/" 2>/dev/null || true
    mv "$SDK_DIR/cmdline-tools/lib" "$SDK_DIR/cmdline-tools/latest/" 2>/dev/null || true
    mv "$SDK_DIR/cmdline-tools/source.properties" "$SDK_DIR/cmdline-tools/latest/" 2>/dev/null || true
    rm cmdline-tools.zip
fi

# Set environment variables
if ! grep -q "ANDROID_HOME" ~/.bashrc; then
    cat >> ~/.bashrc << EOF

# Android Development
export ANDROID_HOME="$SDK_DIR"
export ANDROID_SDK_ROOT="$SDK_DIR"
export ANDROID_NDK_HOME="$NDK_DIR"
export ANDROID_NDK_ROOT="$NDK_DIR"
export PATH=\$PATH:\$ANDROID_SDK_ROOT/cmdline-tools/latest/bin
export PATH=\$PATH:\$ANDROID_SDK_ROOT/platform-tools
export PATH=\$PATH:\$ANDROID_NDK_ROOT
EOF
fi

# Export for current session
export ANDROID_SDK_ROOT="$SDK_DIR"
export PATH="$PATH:$SDK_DIR/cmdline-tools/latest/bin:$SDK_DIR/platform-tools"

# Install SDK packages
yes | sdkmanager --sdk_root="$SDK_DIR" --licenses
sdkmanager --sdk_root="$SDK_DIR" "platform-tools" "platforms;android-34" "build-tools;35.0.0"

echo "Setup complete!"
echo "NDK: $NDK_DIR"
echo "SDK: $SDK_DIR"