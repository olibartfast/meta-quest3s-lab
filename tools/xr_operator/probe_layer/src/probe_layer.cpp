// A minimal pass-through OpenXR API layer.
//
// It exists to prove one thing that is otherwise invisible: that an implicit
// API layer packaged inside an APK is actually discovered and loaded by the
// OpenXR loader on Horizon OS. The loader enumerates layer manifests from the
// APK asset directory openxr/1/api_layers/implicit.d, then resolves the
// manifest's relative library_path against ApplicationInfo.nativeLibraryDir and
// stats it. Both halves fail silently when they are wrong, so the only reliable
// signal is a layer that announces itself in logcat.
//
// This layer changes no behaviour. It forwards every call to the next layer or
// the runtime.

#include <android/log.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <cstdarg>
#include <cstring>

namespace {

constexpr const char* kLogTag = "QuestLabProbeLayer";
constexpr const char* kLayerName = "XR_APILAYER_QUESTLAB_probe";

PFN_xrGetInstanceProcAddr g_nextGetInstanceProcAddr = nullptr;

void LogInfo(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    __android_log_vprint(ANDROID_LOG_INFO, kLogTag, format, arguments);
    va_end(arguments);
}

XRAPI_ATTR XrResult XRAPI_CALL ProbeGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function) {
    if (g_nextGetInstanceProcAddr == nullptr) {
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
    return g_nextGetInstanceProcAddr(instance, name, function);
}

XRAPI_ATTR XrResult XRAPI_CALL ProbeCreateApiLayerInstance(
    const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance) {
    if (apiLayerInfo == nullptr || apiLayerInfo->nextInfo == nullptr) {
        LogInfo("Layer create info is missing next-layer information");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    const XrApiLayerNextInfo* nextInfo = apiLayerInfo->nextInfo;
    g_nextGetInstanceProcAddr = nextInfo->nextGetInstanceProcAddr;

    XrApiLayerCreateInfo forwardedInfo = *apiLayerInfo;
    forwardedInfo.nextInfo = nextInfo->next;

    LogInfo(
        "%s is loaded and creating the OpenXR instance (application: %s)",
        kLayerName,
        info != nullptr ? info->applicationInfo.applicationName : "unknown");

    return nextInfo->nextCreateApiLayerInstance(
        info,
        &forwardedInfo,
        instance);
}

}  // namespace

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest) {
    // Reaching this function at all is the result the probe exists to produce:
    // it means the manifest asset was found and the shared object was resolved
    // from the native library directory.
    LogInfo("Loader negotiated with %s", layerName != nullptr ? layerName : "");

    if (loaderInfo == nullptr ||
        loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->minInterfaceVersion >
            XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion <
            XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->minApiVersion > XR_CURRENT_API_VERSION ||
        loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (apiLayerRequest == nullptr ||
        apiLayerRequest->structType !=
            XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (layerName != nullptr && std::strcmp(layerName, kLayerName) != 0) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    apiLayerRequest->layerInterfaceVersion =
        XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr = ProbeGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = ProbeCreateApiLayerInstance;
    return XR_SUCCESS;
}
