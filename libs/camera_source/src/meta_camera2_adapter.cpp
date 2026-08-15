#include "camera_source/meta_camera2_adapter.h"

#include <android/log.h>

#include <algorithm>
#include <utility>

namespace questlab::camera {
namespace {

constexpr const char* kLogTag = "QuestCamera";

void LogError(const char* message) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

}  // namespace

MetaCamera2Adapter::MetaCamera2Adapter(JavaVM* javaVm, jobject activity)
    : javaVm_(javaVm) {
    if (javaVm_ == nullptr || activity == nullptr) {
        LogError("Camera2 adapter received an invalid JVM or activity");
        return;
    }
    JNIEnv* environment = nullptr;
    bool detach = false;
    if (javaVm_->GetEnv(
            reinterpret_cast<void**>(&environment),
            JNI_VERSION_1_6) != JNI_OK &&
        javaVm_->AttachCurrentThread(&environment, nullptr) == JNI_OK) {
        detach = true;
    }
    if (environment != nullptr) {
        activity_ = environment->NewGlobalRef(activity);
    }
    if (detach) {
        javaVm_->DetachCurrentThread();
    }
    __android_log_print(
        activity_ != nullptr ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
        kLogTag,
        "Camera2 activity reference: %s",
        activity_ != nullptr ? "ready" : "failed");
}

MetaCamera2Adapter::~MetaCamera2Adapter() {
    Stop();
    if (javaVm_ == nullptr || activity_ == nullptr) {
        return;
    }
    JNIEnv* environment = nullptr;
    bool detach = false;
    if (javaVm_->GetEnv(
            reinterpret_cast<void**>(&environment),
            JNI_VERSION_1_6) != JNI_OK &&
        javaVm_->AttachCurrentThread(&environment, nullptr) == JNI_OK) {
        detach = true;
    }
    if (environment != nullptr) {
        environment->DeleteGlobalRef(activity_);
    }
    if (detach) {
        javaVm_->DetachCurrentThread();
    }
    activity_ = nullptr;
}

bool MetaCamera2Adapter::CallActivityMethod(
    const char* name,
    const char* signature,
    const CameraStreamConfig* config) {
    if (javaVm_ == nullptr || activity_ == nullptr) {
        LogError("Camera2 JNI bridge has no activity");
        return false;
    }
    JNIEnv* environment = nullptr;
    bool detach = false;
    if (javaVm_->GetEnv(
            reinterpret_cast<void**>(&environment),
            JNI_VERSION_1_6) != JNI_OK &&
        javaVm_->AttachCurrentThread(&environment, nullptr) == JNI_OK) {
        detach = true;
    }
    if (environment == nullptr) {
        LogError("Camera2 JNI bridge cannot attach to the JVM");
        return false;
    }
    jclass activityClass = environment->GetObjectClass(activity_);
    jmethodID method =
        environment->GetMethodID(activityClass, name, signature);
    if (method == nullptr) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "Camera2 activity method not found: %s %s",
            name,
            signature);
        environment->ExceptionClear();
        environment->DeleteLocalRef(activityClass);
        if (detach) {
            javaVm_->DetachCurrentThread();
        }
        return false;
    }
    if (config != nullptr) {
        environment->CallVoidMethod(
            activity_,
            method,
            config->width,
            config->height,
            config->framesPerSecond);
    } else {
        environment->CallVoidMethod(activity_, method);
    }
    const bool succeeded = !environment->ExceptionCheck();
    if (!succeeded) {
        environment->ExceptionDescribe();
        environment->ExceptionClear();
    }
    __android_log_print(
        succeeded ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
        kLogTag,
        "Camera2 activity call %s: %s",
        name,
        succeeded ? "ok" : "failed");
    environment->DeleteLocalRef(activityClass);
    if (detach) {
        javaVm_->DetachCurrentThread();
    }
    return succeeded;
}

CameraCapabilities MetaCamera2Adapter::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

bool MetaCamera2Adapter::Start(const CameraStreamConfig& config) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.health = CameraHealth::Starting;
        stats_.lastError.clear();
    }
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Requesting Camera2 start (%dx%d @ %d)",
        config.width,
        config.height,
        config.framesPerSecond);
    if (!CallActivityMethod(
            "startCameraFromNative", "(III)V", &config)) {
        OnError("Failed to call Quest Camera2 activity bridge");
        return false;
    }
    return true;
}

bool MetaCamera2Adapter::TryConsumeLatest(RgbCapture* capture) {
    if (!queue_.TryConsumeLatest(capture)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.consumedFrames;
    stats_.currentQueueDepth = 0;
    return true;
}

CameraSourceStats MetaCamera2Adapter::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void MetaCamera2Adapter::Stop() {
    CallActivityMethod("stopCameraFromNative", "()V", nullptr);
    queue_.Clear();
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.health = CameraHealth::Stopped;
    stats_.currentQueueDepth = 0;
}

void MetaCamera2Adapter::SetPermissionState(bool granted) {
    std::lock_guard<std::mutex> lock(mutex_);
    permissionGranted_ = granted;
    if (!granted) {
        stats_.health = CameraHealth::PermissionDenied;
        stats_.lastError = "Camera permission denied";
    } else if (stats_.health == CameraHealth::PermissionDenied) {
        stats_.health = CameraHealth::Stopped;
        stats_.lastError.clear();
    }
}

void MetaCamera2Adapter::OnConfigured(
    const CameraCapabilities& capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);
    capabilities_ = capabilities;
    stats_.health = CameraHealth::Running;
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Camera source state: Running");
}

void MetaCamera2Adapter::OnFrame(RgbCapture capture) {
    const bool overwritten = queue_.Publish(std::move(capture));
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.receivedFrames;
    stats_.currentQueueDepth = 1;
    stats_.queueHighWaterMark =
        std::max<uint64_t>(stats_.queueHighWaterMark, 1);
    if (overwritten) {
        ++stats_.overwrittenFrames;
    }
}

void MetaCamera2Adapter::OnError(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.health = CameraHealth::Error;
    stats_.lastError = message;
    __android_log_print(
        ANDROID_LOG_ERROR,
        kLogTag,
        "Camera source error: %s",
        message.c_str());
}

}  // namespace questlab::camera
