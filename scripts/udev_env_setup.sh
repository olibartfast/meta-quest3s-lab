#!/bin/bash

#!/bin/bash

# Verify ADB connection
adb devices

# Install necessary packages
sudo apt update
sudo apt install -y adb android-sdk-platform-tools-common scrcpy

# Create udev rules if not already done
UDEV_RULE='SUBSYSTEM=="usb", ATTR{idVendor}=="2833", MODE="0666", GROUP="plugdev"'
UDEV_FILE="/etc/udev/rules.d/51-android.rules"

if [ -f "$UDEV_FILE" ] && grep -q "2833" "$UDEV_FILE"; then
    echo "udev rules already configured for Meta Quest (vendor 2833)"
else
    echo "Adding udev rules for Meta Quest..."
    echo "$UDEV_RULE" | sudo tee "$UDEV_FILE"
    sudo udevadm control --reload-rules && sudo udevadm trigger
    echo "udev rules added and reloaded"
fi

# Add yourself to plugdev group if not already a member
if groups "$USER" | grep -q plugdev; then
    echo "User $USER is already in plugdev group"
else
    echo "Adding $USER to plugdev group..."
    sudo usermod -aG plugdev "$USER"
    echo "Added to plugdev group - logout/login required for changes to take effect"
fi

echo ""
echo "Setup complete!"
echo "If you were added to plugdev group, please logout and login again."