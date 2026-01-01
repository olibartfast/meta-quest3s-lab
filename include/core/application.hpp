#pragma once

#include <memory>
#include <vector>
#include <string>
#include <jni.h>
#include <android/native_activity.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace quest3ar {

class Application {
public:
    Application(ANativeActivity* activity);
    ~Application();

    bool initialize();
    void run();
    void shutdown();

    // XR lifecycle
    bool createInstance();
    bool createSession();
    bool createSwapchains();
    bool createSpaces();

    // Main loop
    void pollEvents();
    void update(float deltaTime);
    void render();

private:
    struct FrameState {
        XrTime predictedDisplayTime;
        bool shouldRender;
    };

    ANativeActivity* nativeActivity_;
    
    // OpenXR handles
    XrInstance instance_;
    XrSession session_;
    XrSpace localSpace_;
    XrSpace viewSpace_;
    XrSystemId systemId_;
    
    // Swapchains
    struct SwapchainInfo {
        XrSwapchain handle;
        int32_t width;
        int32_t height;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
    };
    std::vector<SwapchainInfo> swapchains_;
    
    // Session state
    XrSessionState sessionState_;
    bool sessionRunning_;
    
    FrameState frameState_;
};

} // namespace quest3ar