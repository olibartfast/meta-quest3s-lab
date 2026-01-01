#include "core/application.hpp"
#include <android/log.h>
#include <vector>
#include <cstring>

#define LOG_TAG "Quest3AR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace quest3ar {

Application::Application(ANativeActivity* activity)
    : nativeActivity_(activity)
    , instance_(XR_NULL_HANDLE)
    , session_(XR_NULL_HANDLE)
    , localSpace_(XR_NULL_HANDLE)
    , viewSpace_(XR_NULL_HANDLE)
    , systemId_(XR_NULL_SYSTEM_ID)
    , sessionState_(XR_SESSION_STATE_UNKNOWN)
    , sessionRunning_(false)
{
}

Application::~Application() {
    shutdown();
}

bool Application::initialize() {
    LOGI("Initializing Quest 3 AR Application");
    
    if (!createInstance()) {
        LOGE("Failed to create OpenXR instance");
        return false;
    }
    
    if (!createSession()) {
        LOGE("Failed to create OpenXR session");
        return false;
    }
    
    if (!createSwapchains()) {
        LOGE("Failed to create swapchains");
        return false;
    }
    
    if (!createSpaces()) {
        LOGE("Failed to create reference spaces");
        return false;
    }
    
    LOGI("Initialization complete");
    return true;
}

bool Application::createInstance() {
    // Required extensions for Quest 3
    std::vector<const char*> extensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
"XR_KHR_opengl_es_enable",
        "XR_FB_passthrough",  // Meta passthrough
        "XR_FB_spatial_entity", // Spatial anchors
        "XR_FB_scene", // Scene understanding
    };

    XrInstanceCreateInfoAndroidKHR androidInfo{
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = nativeActivity_->vm;
    androidInfo.applicationActivity = nativeActivity_->clazz;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &androidInfo;
    createInfo.enabledExtensionCount = extensions.size();
    createInfo.enabledExtensionNames = extensions.data();
    
    std::strcpy(createInfo.applicationInfo.applicationName, "Quest3 AR Native");
    createInfo.applicationInfo.applicationVersion = 1;
    std::strcpy(createInfo.applicationInfo.engineName, "Custom");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrResult result = xrCreateInstance(&createInfo, &instance_);
    if (XR_FAILED(result)) {
        LOGE("xrCreateInstance failed: %d", result);
        return false;
    }

    LOGI("OpenXR instance created successfully");
    return true;
}

bool Application::createSession() {
    // Get system ID for HMD
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    
    XrResult result = xrGetSystem(instance_, &systemInfo, &systemId_);
    if (XR_FAILED(result)) {
        LOGE("xrGetSystem failed: %d", result);
        return false;
    }

    // Create session without graphics binding for now (we'll add Vulkan later)
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = nullptr;
    sessionInfo.systemId = systemId_;
    
    result = xrCreateSession(instance_, &sessionInfo, &session_);
    if (XR_FAILED(result)) {
        LOGE("xrCreateSession failed: %d", result);
        return false;
    }

    LOGI("OpenXR session created successfully");
    return true;
}

bool Application::createSpaces() {
    // Create local space (centered at device start position)
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    
    XrResult result = xrCreateReferenceSpace(session_, &spaceInfo, &localSpace_);
    if (XR_FAILED(result)) {
        LOGE("Failed to create local space: %d", result);
        return false;
    }

    // Create view space (tracks HMD position)
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    result = xrCreateReferenceSpace(session_, &spaceInfo, &viewSpace_);
    if (XR_FAILED(result)) {
        LOGE("Failed to create view space: %d", result);
        return false;
    }

    LOGI("Reference spaces created successfully");
    return true;
}

bool Application::createSwapchains() {
    // TODO: Implement swapchain creation for Vulkan rendering
    LOGI("Swapchain creation skipped for now");
    return true;
}

void Application::update(float deltaTime) {
    // TODO: Implement application update logic
    (void)deltaTime; // Suppress unused parameter warning
}

void Application::render() {
    // TODO: Implement rendering
}

void Application::run() {
    LOGI("Starting main loop");
    
    while (sessionRunning_) {
        pollEvents();
        
        if (frameState_.shouldRender) {
            update(0.016f); // ~60 FPS for now
            render();
        }
    }
}

void Application::pollEvents() {
    XrEventDataBuffer eventData{XR_TYPE_EVENT_DATA_BUFFER};
    
    while (xrPollEvent(instance_, &eventData) == XR_SUCCESS) {
        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                sessionState_ = stateEvent->state;
                
                LOGI("Session state changed to: %d", sessionState_);
                
                switch (sessionState_) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        xrBeginSession(session_, &beginInfo);
                        sessionRunning_ = true;
                        break;
                    }
                    case XR_SESSION_STATE_STOPPING:
                        xrEndSession(session_);
                        sessionRunning_ = false;
                        break;
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }
        
        eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void Application::shutdown() {
    LOGI("Shutting down");
    
    if (localSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(localSpace_);
    }
    if (viewSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(viewSpace_);
    }
    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);
    }
    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
    }
}

} // namespace quest3ar