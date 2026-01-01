#pragma once

#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace quest3ar {

class PassthroughManager {
public:
    PassthroughManager(XrInstance instance, XrSession session);
    ~PassthroughManager();

    bool initialize();
    void start();
    void stop();
    void setOpacity(float opacity);

private:
    XrInstance instance_;
    XrSession session_;
    
    // Meta-specific passthrough handles
    XrPassthroughFB passthrough_;
    XrPassthroughLayerFB passthroughLayer_;
    
    PFN_xrCreatePassthroughFB xrCreatePassthroughFB_;
    PFN_xrDestroyPassthroughFB xrDestroyPassthroughFB_;
    PFN_xrPassthroughStartFB xrPassthroughStartFB_;
    PFN_xrPassthroughPauseFB xrPassthroughPauseFB_;
    PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB_;
    PFN_xrDestroyPassthroughLayerFB xrDestroyPassthroughLayerFB_;
    PFN_xrPassthroughLayerSetStyleFB xrPassthroughLayerSetStyleFB_;
};

} // namespace quest3ar