#include "xr/passthrough_manager.hpp"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Passthrough", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Passthrough", __VA_ARGS__)

namespace quest3ar {

PassthroughManager::PassthroughManager(XrInstance instance, XrSession session)
    : instance_(instance)
    , session_(session)
    , passthrough_(XR_NULL_HANDLE)
    , passthroughLayer_(XR_NULL_HANDLE)
{
}

PassthroughManager::~PassthroughManager() {
    stop();
    
    if (passthroughLayer_ != XR_NULL_HANDLE) {
        xrDestroyPassthroughLayerFB_(passthroughLayer_);
    }
    if (passthrough_ != XR_NULL_HANDLE) {
        xrDestroyPassthroughFB_(passthrough_);
    }
}

bool PassthroughManager::initialize() {
    // Load extension functions
    xrGetInstanceProcAddr(instance_, "xrCreatePassthroughFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrCreatePassthroughFB_));
    xrGetInstanceProcAddr(instance_, "xrDestroyPassthroughFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyPassthroughFB_));
    xrGetInstanceProcAddr(instance_, "xrPassthroughStartFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrPassthroughStartFB_));
    xrGetInstanceProcAddr(instance_, "xrPassthroughPauseFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrPassthroughPauseFB_));
    xrGetInstanceProcAddr(instance_, "xrCreatePassthroughLayerFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrCreatePassthroughLayerFB_));
    xrGetInstanceProcAddr(instance_, "xrDestroyPassthroughLayerFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyPassthroughLayerFB_));
    xrGetInstanceProcAddr(instance_, "xrPassthroughLayerSetStyleFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrPassthroughLayerSetStyleFB_));

    // Create passthrough feature
    XrPassthroughCreateInfoFB ptci{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    ptci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    
    XrResult result = xrCreatePassthroughFB_(session_, &ptci, &passthrough_);
    if (XR_FAILED(result)) {
        LOGE("Failed to create passthrough: %d", result);
        return false;
    }

    // Create passthrough layer
    XrPassthroughLayerCreateInfoFB plci{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    plci.passthrough = passthrough_;
    plci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    plci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    
    result = xrCreatePassthroughLayerFB_(session_, &plci, &passthroughLayer_);
    if (XR_FAILED(result)) {
        LOGE("Failed to create passthrough layer: %d", result);
        return false;
    }

    LOGI("Passthrough initialized successfully");
    return true;
}

void PassthroughManager::start() {
    if (passthrough_ != XR_NULL_HANDLE) {
        xrPassthroughStartFB_(passthrough_);
        LOGI("Passthrough started");
    }
}

void PassthroughManager::stop() {
    if (passthrough_ != XR_NULL_HANDLE) {
        xrPassthroughPauseFB_(passthrough_);
        LOGI("Passthrough stopped");
    }
}

void PassthroughManager::setOpacity(float opacity) {
    XrPassthroughStyleFB style{XR_TYPE_PASSTHROUGH_STYLE_FB};
    style.textureOpacityFactor = opacity;
    style.edgeColor = {0.0f, 0.0f, 0.0f, 0.0f};
    
    xrPassthroughLayerSetStyleFB_(passthroughLayer_, &style);
}

} // namespace quest3ar