#include <android_native_app_glue.h>
#include <jni.h>

#include "camera_source/meta_camera2_adapter.h"
#include "camera_source/yuv_converter.h"
#include "perf_telemetry/perf_telemetry.h"
#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_math/openxr_conversions.h"
#include "xr_meta_passthrough/meta_passthrough_fb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

std::atomic<questlab::camera::MetaCamera2Adapter*> gCameraAdapter{nullptr};
std::atomic<uint64_t> gFrameId{1};

struct AndroidState {
    bool resumed = false;
    bool destroyRequested = false;
};

questlab::math::Mat4 ScaleMatrix(float x, float y, float z) {
    questlab::math::Mat4 matrix = questlab::math::IdentityMatrix();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

int64_t MonotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<uint8_t> CopyByteArray(JNIEnv* environment, jbyteArray array) {
    if (environment == nullptr || array == nullptr) {
        return {};
    }
    const jsize length = environment->GetArrayLength(array);
    std::vector<uint8_t> result(static_cast<size_t>(length));
    environment->GetByteArrayRegion(
        array,
        0,
        length,
        reinterpret_cast<jbyte*>(result.data()));
    return result;
}

std::vector<float> CopyFloatArray(JNIEnv* environment, jfloatArray array) {
    if (environment == nullptr || array == nullptr) {
        return {};
    }
    const jsize length = environment->GetArrayLength(array);
    std::vector<float> result(static_cast<size_t>(length));
    environment->GetFloatArrayRegion(
        array, 0, length, result.data());
    return result;
}

class CameraScene final : public questlab::VulkanSceneProvider {
public:
    explicit CameraScene(questlab::camera::IRgbCameraSource* source)
        : source_(source),
          reportCadence_(std::chrono::seconds(1)),
          runStart_(questlab::perf::SteadyClock::now()),
          windowStartMonotonicNanoseconds_(
              questlab::perf::SteadyNowNanoseconds()) {
        std::snprintf(
            runIdentifier_,
            sizeof(runIdentifier_),
            "camera-%lld",
            static_cast<long long>(windowStartMonotonicNanoseconds_));
    }

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override {
        if (draws == nullptr || source_ == nullptr) {
            return false;
        }
        constexpr XrSpaceLocationFlags kRequiredPoseFlags =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((frame.headInLocal.locationFlags & kRequiredPoseFlags) !=
            kRequiredPoseFlags) {
            return true;
        }
        const questlab::math::Mat4 localFromHead =
            questlab::math::PoseMatrix(
                questlab::math::FromXr(frame.headInLocal.pose));
        const questlab::math::Mat4 panelFromHead =
            questlab::math::TranslationMatrix({0.0F, 0.0F, -1.25F});
        const questlab::math::Mat4 localFromPanel =
            questlab::math::Multiply(localFromHead, panelFromHead);

        questlab::camera::RgbCapture capture;
        if (source_->TryConsumeLatest(&capture)) {
            auto pixels = std::make_shared<std::vector<uint8_t>>();
            bool converted = false;
            {
                questlab::perf::ScopedDuration<128> conversionTimer(
                    &conversionDurations_);
                converted = questlab::camera::ConvertYuv420ToRgba(
                    capture, pixels.get());
            }
            if (converted) {
                image_.frameId = capture.frameId;
                image_.width = static_cast<uint32_t>(capture.width);
                image_.height = static_cast<uint32_t>(capture.height);
                image_.pixels = std::move(pixels);
                ++convertedFrames_;
                lastFrameId_ = capture.frameId;
                if (capture.arrivalTimestampNanoseconds > 0) {
                    captureToSceneAge_.AddSample({
                        std::chrono::nanoseconds(std::max<int64_t>(
                            MonotonicNanoseconds() -
                                capture.arrivalTimestampNanoseconds,
                            0)),
                    });
                }
            } else {
                ++conversionFailures_;
            }
        }
        ReportTelemetry();
        if (image_.pixels != nullptr) {
            const float aspect =
                static_cast<float>(image_.width) /
                static_cast<float>(image_.height);
            image_.model = questlab::math::Multiply(
                localFromPanel,
                ScaleMatrix(0.85F * aspect, 0.85F, 1.0F));
        }

        const questlab::camera::CameraSourceStats stats = source_->GetStats();
        const std::array<float, 4> statusColor =
            stats.health == questlab::camera::CameraHealth::Running
                ? std::array<float, 4>{0.1F, 1.0F, 0.2F, 1.0F}
                : std::array<float, 4>{1.0F, 0.25F, 0.1F, 1.0F};
        draws->push_back({
            questlab::DebugLineShape::ScreenRectangle,
            questlab::math::Multiply(
                questlab::math::Multiply(
                    localFromHead,
                    questlab::math::TranslationMatrix(
                        {0.0F, 0.0F, -1.24F})),
                ScaleMatrix(1.55F, 1.0F, 1.0F)),
            statusColor,
        });
        return true;
    }

    bool GetRgbaImageQuad(questlab::RgbaImageQuad* image) override {
        if (image == nullptr || image_.pixels == nullptr) {
            return false;
        }
        *image = image_;
        return true;
    }

private:
    void ReportTelemetry() {
        const auto now = questlab::perf::SteadyClock::now();
        if (!reportCadence_.ShouldReport(now)) {
            return;
        }
        const int64_t endNanoseconds =
            std::chrono::duration_cast<questlab::perf::Nanoseconds>(
                now.time_since_epoch()).count();
        const bool warmup = now - runStart_ < std::chrono::seconds(5);
        const auto conversion = conversionDurations_.Snapshot(
            "camera_pipeline",
            "yuv_to_rgba",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto age = captureToSceneAge_.Snapshot(
            "camera_pipeline",
            "capture_to_scene_publish",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto& conversionSummary = conversion.Summary();
        const auto& ageSummary = age.Summary();
        const questlab::camera::CameraSourceStats stats = source_->GetStats();
        questlab::LogInfo(
            "PERF {\"schema\":\"questlab.performance.v1\","
            "\"severity\":\"info\",\"category\":\"camera_pipeline\","
            "\"metric_name\":\"camera_pipeline\","
            "\"unit\":\"milliseconds\",\"run_id\":\"%s\","
            "\"window_start_monotonic_ns\":%lld,"
            "\"window_end_monotonic_ns\":%lld,\"warmup\":%s,"
            "\"last_frame_id\":%llu,"
            "\"counters\":{\"received\":%llu,\"consumed\":%llu,"
            "\"source_overwrite\":%llu,\"invalid\":%llu,"
            "\"converted_window\":%llu,\"conversion_failure_window\":%llu,"
            "\"queue_current\":%llu,\"queue_high_water\":%llu},"
            "\"durations\":{"
            "\"yuv_to_rgba\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu],"
            "\"capture_to_scene_publish\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu]},"
            "\"duration_fields\":[\"count\",\"mean\",\"p50\","
            "\"p95\",\"p99\",\"max\",\"overflow\"]}",
            runIdentifier_,
            static_cast<long long>(windowStartMonotonicNanoseconds_),
            static_cast<long long>(endNanoseconds),
            warmup ? "true" : "false",
            static_cast<unsigned long long>(lastFrameId_),
            static_cast<unsigned long long>(stats.receivedFrames),
            static_cast<unsigned long long>(stats.consumedFrames),
            static_cast<unsigned long long>(stats.overwrittenFrames),
            static_cast<unsigned long long>(stats.invalidFrames),
            static_cast<unsigned long long>(convertedFrames_),
            static_cast<unsigned long long>(conversionFailures_),
            static_cast<unsigned long long>(stats.currentQueueDepth),
            static_cast<unsigned long long>(stats.queueHighWaterMark),
            static_cast<unsigned long long>(conversionSummary.count),
            conversionSummary.meanMilliseconds,
            conversionSummary.p50Milliseconds,
            conversionSummary.p95Milliseconds,
            conversionSummary.p99Milliseconds,
            conversionSummary.maximumMilliseconds,
            static_cast<unsigned long long>(conversionSummary.overflowCount),
            static_cast<unsigned long long>(ageSummary.count),
            ageSummary.meanMilliseconds,
            ageSummary.p50Milliseconds,
            ageSummary.p95Milliseconds,
            ageSummary.p99Milliseconds,
            ageSummary.maximumMilliseconds,
            static_cast<unsigned long long>(ageSummary.overflowCount));
        conversionDurations_.Clear();
        captureToSceneAge_.Clear();
        convertedFrames_ = 0;
        conversionFailures_ = 0;
        windowStartMonotonicNanoseconds_ = endNanoseconds;
    }

    questlab::camera::IRgbCameraSource* source_ = nullptr;
    questlab::RgbaImageQuad image_;
    questlab::perf::DurationRing<128> conversionDurations_;
    questlab::perf::DurationRing<128> captureToSceneAge_;
    questlab::perf::ReportCadence reportCadence_;
    questlab::perf::SteadyClock::time_point runStart_;
    int64_t windowStartMonotonicNanoseconds_ = 0;
    uint64_t lastFrameId_ = 0;
    uint64_t convertedFrames_ = 0;
    uint64_t conversionFailures_ = 0;
    char runIdentifier_[32]{};
};

void HandleAppCommand(android_app* app, int32_t command) {
    auto* state = static_cast<AndroidState*>(app->userData);
    switch (command) {
        case APP_CMD_RESUME:
            state->resumed = true;
            questlab::LogInfo("Android lifecycle: RESUME");
            break;
        case APP_CMD_PAUSE:
            state->resumed = false;
            questlab::LogInfo("Android lifecycle: PAUSE");
            break;
        case APP_CMD_DESTROY:
            state->destroyRequested = true;
            questlab::LogInfo("Android lifecycle: DESTROY");
            break;
        default:
            break;
    }
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnPermissionState(
    JNIEnv*,
    jobject,
    jboolean granted) {
    if (auto* adapter = gCameraAdapter.load()) {
        adapter->SetPermissionState(granted == JNI_TRUE);
        questlab::LogInfo(
            "Camera permission: %s",
            granted == JNI_TRUE ? "granted" : "denied");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCameraConfigured(
    JNIEnv* environment,
    jobject,
    jstring cameraId,
    jint position,
    jint width,
    jint height,
    jfloatArray intrinsics,
    jfloatArray distortion,
    jfloatArray rotation,
    jfloatArray translation) {
    auto* adapter = gCameraAdapter.load();
    if (adapter == nullptr) {
        return;
    }
    questlab::camera::CameraCapabilities capabilities;
    capabilities.sourceName = "Meta Camera2 passthrough";
    const char* cameraIdChars =
        environment->GetStringUTFChars(cameraId, nullptr);
    if (cameraIdChars != nullptr) {
        capabilities.cameraId = cameraIdChars;
        environment->ReleaseStringUTFChars(cameraId, cameraIdChars);
    }
    capabilities.cameraPosition = position;
    capabilities.streams.push_back({width, height, 30});
    const std::vector<float> intrinsicValues =
        CopyFloatArray(environment, intrinsics);
    if (intrinsicValues.size() >= 5) {
        capabilities.intrinsics.fx = intrinsicValues[0];
        capabilities.intrinsics.fy = intrinsicValues[1];
        capabilities.intrinsics.cx = intrinsicValues[2];
        capabilities.intrinsics.cy = intrinsicValues[3];
        capabilities.intrinsics.skew = intrinsicValues[4];
        capabilities.intrinsics.valid = true;
    }
    const std::vector<float> distortionValues =
        CopyFloatArray(environment, distortion);
    std::copy_n(
        distortionValues.begin(),
        std::min(
            distortionValues.size(),
            capabilities.intrinsics.distortion.size()),
        capabilities.intrinsics.distortion.begin());
    const std::vector<float> rotationValues =
        CopyFloatArray(environment, rotation);
    const std::vector<float> translationValues =
        CopyFloatArray(environment, translation);
    if (rotationValues.size() >= 4 && translationValues.size() >= 3) {
        std::copy_n(
            rotationValues.begin(),
            4,
            capabilities.cameraFromHead.orientation.begin());
        std::copy_n(
            translationValues.begin(),
            3,
            capabilities.cameraFromHead.position.begin());
        capabilities.cameraFromHead.valid = true;
    }
    adapter->OnConfigured(capabilities);
    questlab::LogInfo(
        "Selected passthrough camera id=%s position=%d stream=%dx%d "
        "intrinsics=%s pose=%s",
        capabilities.cameraId.c_str(),
        capabilities.cameraPosition,
        width,
        height,
        capabilities.intrinsics.valid ? "valid" : "unavailable",
        capabilities.cameraFromHead.valid ? "valid" : "unavailable");
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCameraFrame(
    JNIEnv* environment,
    jobject,
    jlong timestampNanoseconds,
    jint width,
    jint height,
    jbyteArray y,
    jint yRowStride,
    jint yPixelStride,
    jbyteArray u,
    jint uRowStride,
    jint uPixelStride,
    jbyteArray v,
    jint vRowStride,
    jint vPixelStride) {
    auto* adapter = gCameraAdapter.load();
    if (adapter == nullptr) {
        return;
    }
    questlab::camera::RgbCapture capture;
    capture.frameId = gFrameId.fetch_add(1);
    capture.sensorTimestampNanoseconds = timestampNanoseconds;
    capture.arrivalTimestampNanoseconds = MonotonicNanoseconds();
    capture.width = width;
    capture.height = height;
    capture.format = questlab::camera::PixelFormat::Yuv420888;
    capture.planes[0] = {
        CopyByteArray(environment, y),
        yRowStride,
        yPixelStride,
    };
    capture.planes[1] = {
        CopyByteArray(environment, u),
        uRowStride,
        uPixelStride,
    };
    capture.planes[2] = {
        CopyByteArray(environment, v),
        vRowStride,
        vPixelStride,
    };
    const questlab::camera::CameraCapabilities capabilities =
        adapter->GetCapabilities();
    capture.intrinsics = capabilities.intrinsics;
    capture.cameraFromHead = capabilities.cameraFromHead;
    adapter->OnFrame(std::move(capture));
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCameraError(
    JNIEnv* environment,
    jobject,
    jstring message) {
    const char* messageChars =
        environment->GetStringUTFChars(message, nullptr);
    const std::string error =
        messageChars != nullptr ? messageChars : "Unknown Camera2 error";
    if (messageChars != nullptr) {
        environment->ReleaseStringUTFChars(message, messageChars);
    }
    if (auto* adapter = gCameraAdapter.load()) {
        adapter->OnError(error);
    }
    questlab::LogError("%s", error.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCaptureSaved(
    JNIEnv* environment,
    jobject,
    jstring path,
    jlong byteCount) {
    const char* pathChars =
        environment->GetStringUTFChars(path, nullptr);
    questlab::LogInfo(
        "Private camera capture saved: path=%s bytes=%lld",
        pathChars != nullptr ? pathChars : "unknown",
        static_cast<long long>(byteCount));
    if (pathChars != nullptr) {
        environment->ReleaseStringUTFChars(path, pathChars);
    }
}

void android_main(android_app* app) {
    questlab::SetLogTag("QuestCamera");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Quest Camera Capture starting");
    const questlab::camera::CameraSourceConfig cameraConfig{
        questlab::camera::CameraSourceKind::MetaCamera2,
        {},
    };
    const questlab::camera::CameraPlatformContext cameraPlatform{
        app->activity->vm,
        app->activity->clazz,
    };
    std::unique_ptr<questlab::camera::IRgbCameraSource> camera =
        questlab::camera::CreateCameraSource(
            cameraConfig, cameraPlatform);
    auto* metaCamera = dynamic_cast<
        questlab::camera::MetaCamera2Adapter*>(camera.get());
    if (camera == nullptr || metaCamera == nullptr) {
        questlab::LogError("Meta Camera2 source factory failed");
        ANativeActivity_finish(app->activity);
        return;
    }
    gCameraAdapter.store(metaCamera);
    CameraScene scene(camera.get());
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    questlab::MetaPassthroughFB passthrough;
    questlab::VulkanStereoRenderer renderer;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif
    const questlab::XrInstanceOptions instanceOptions{
        "Quest Camera Capture",
        1,
        {XR_FB_PASSTHROUGH_EXTENSION_NAME},
    };
    const questlab::VulkanRendererOptions rendererOptions{true};
    bool cameraRequested = false;

    if (!xrInstance.Initialize(
            app->activity->vm,
            app->activity->clazz,
            instanceOptions) ||
        !vulkanBinding.Initialize(xrInstance, bindingOptions) ||
        !xrSession.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            vulkanBinding.GraphicsBinding()) ||
        xrSession.BlendMode() != XR_ENVIRONMENT_BLEND_MODE_OPAQUE ||
        !passthrough.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            xrSession.Session()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene,
            rendererOptions)) {
        questlab::LogError("Quest Camera initialization failed");
        ANativeActivity_finish(app->activity);
    } else {
        while (!androidState.destroyRequested && !app->destroyRequested &&
               !xrSession.ShouldExit()) {
            const int timeoutMilliseconds =
                xrSession.IsRunning() ? 0 : (androidState.resumed ? 10 : -1);
            int events = 0;
            android_poll_source* source = nullptr;
            int pollResult = ALooper_pollOnce(
                timeoutMilliseconds,
                nullptr,
                &events,
                reinterpret_cast<void**>(&source));
            while (pollResult >= 0) {
                if (source != nullptr) {
                    source->process(app, source);
                }
                if (androidState.destroyRequested || app->destroyRequested) {
                    break;
                }
                source = nullptr;
                pollResult = ALooper_pollOnce(
                    0,
                    nullptr,
                    &events,
                    reinterpret_cast<void**>(&source));
            }

            if (androidState.destroyRequested || app->destroyRequested) {
                xrSession.RequestExit();
                break;
            }
            if (androidState.resumed && !cameraRequested) {
                camera->Start({0, 0, 30});
                cameraRequested = true;
            } else if (!androidState.resumed && cameraRequested) {
                camera->Stop();
                cameraRequested = false;
            }
            if (!xrSession.PollEvents(&passthrough) ||
                !passthrough.SetActive(
                    androidState.resumed && xrSession.IsRunning()) ||
                !xrSession.PumpFrame(&renderer, nullptr, &passthrough)) {
                break;
            }
        }
    }

    gCameraAdapter.store(nullptr);
    camera->Stop();
    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    passthrough.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("Quest Camera Capture stopped cleanly");
}
