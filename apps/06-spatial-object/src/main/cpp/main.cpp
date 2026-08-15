#include <android_native_app_glue.h>

#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_controller_actions.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_interaction/xr_interaction.h"
#include "xr_math/openxr_conversions.h"
#include "xr_math/xr_math.h"
#include "xr_meta_passthrough/meta_passthrough_fb.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

constexpr float kAimRayLength = 2.0F;
constexpr float kGuideDistance = 1.5F;
constexpr float kGuideSize = 0.45F;
constexpr float kPreviewSize = 0.20F;
constexpr float kPlacedSize = 0.30F;
constexpr float kTriggerPressThreshold = 0.75F;
constexpr float kTriggerReleaseThreshold = 0.55F;
constexpr float kHapticAmplitude = 0.5F;
constexpr XrDuration kHapticDuration = 50'000'000;

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

questlab::XrHand XrHandForIndex(std::size_t hand) {
    return hand == 0 ? questlab::XrHand::Left : questlab::XrHand::Right;
}

const char* HandName(std::size_t hand) {
    return hand == 0 ? "Left" : "Right";
}

bool HasValidPose(XrSpaceLocationFlags flags) {
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags & kValid) == kValid;
}

class SpatialObjectScene final :
    public questlab::XrFrameUpdater,
    public questlab::VulkanSceneProvider {
public:
    bool Initialize(XrInstance instance, XrSession session) {
        return actions_.Initialize(instance, session);
    }

    bool UpdateFrame(
        const questlab::XrFrameUpdateInfo& frame) override {
        if (!actions_.UpdateFrame(frame)) {
            return false;
        }

        std::optional<std::size_t> placementHand;
        bool clearRequested = false;
        for (std::size_t hand = 0; hand < 2; ++hand) {
            const questlab::XrControllerState& state =
                actions_.State(XrHandForIndex(hand));
            previewPoses_[hand] = PreviewPose(state);
            const bool aimVisible = previewPoses_[hand].has_value();
            if (aimVisible != aimVisible_[hand]) {
                questlab::LogInfo(
                    "%s aim pose %s",
                    HandName(hand),
                    aimVisible ? "valid" : "inactive");
                aimVisible_[hand] = aimVisible;
            }
            if (state.stateChanged) {
                LogState(hand, state);
            }

            const float trigger =
                state.triggerActive ? state.trigger : 0.0F;
            bool triggerRising = false;
            if (!triggerPressed_[hand] &&
                trigger >= kTriggerPressThreshold) {
                triggerPressed_[hand] = true;
                triggerRising = true;
            } else if (
                triggerPressed_[hand] &&
                trigger <= kTriggerReleaseThreshold) {
                triggerPressed_[hand] = false;
            }
            if (triggerRising && previewPoses_[hand].has_value()) {
                placementHand = hand;
            }

            const bool primary =
                state.primaryActive && state.primary;
            if (primary && !primaryPressed_[hand]) {
                clearRequested = true;
            }
            primaryPressed_[hand] = primary;
        }

        if (clearRequested) {
            if (placed_) {
                questlab::LogInfo("Placed object cleared");
            }
            placed_ = false;
            return true;
        }

        if (placementHand.has_value()) {
            const std::size_t hand = *placementHand;
            placedInLocal_ = *previewPoses_[hand];
            placed_ = true;
            questlab::LogInfo(
                "%s controller placed object in LOCAL at "
                "(%.3f %.3f %.3f)",
                HandName(hand),
                placedInLocal_.position.x,
                placedInLocal_.position.y,
                placedInLocal_.position.z);
            if (!actions_.ApplyHaptic(
                    XrHandForIndex(hand),
                    kHapticAmplitude,
                    kHapticDuration)) {
                return false;
            }
        }
        return true;
    }

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override {
        if (draws == nullptr) {
            return false;
        }
        if (!guideInitialized_ &&
            HasValidPose(frame.headInLocal.locationFlags)) {
            const questlab::math::Pose localFromHead =
                questlab::math::FromXr(frame.headInLocal.pose);
            questlab::math::Vec3 horizontalForward =
                questlab::math::TransformDirection(
                    localFromHead, {0.0F, 0.0F, -1.0F});
            horizontalForward.y = 0.0F;
            if (!questlab::math::Normalize(&horizontalForward)) {
                horizontalForward = {0.0F, 0.0F, -1.0F};
            }
            guideInLocal_.position = questlab::math::Add(
                localFromHead.position,
                questlab::math::Scale(
                    horizontalForward,
                    kGuideDistance));
            guideInitialized_ = true;
            questlab::LogInfo(
                "Yellow placement guide fixed in LOCAL at "
                "(%.3f %.3f %.3f)",
                guideInLocal_.position.x,
                guideInLocal_.position.y,
                guideInLocal_.position.z);
        }

        for (std::size_t hand = 0; hand < 2; ++hand) {
            const questlab::XrControllerState& state =
                actions_.State(XrHandForIndex(hand));
            if (state.grip.active && state.grip.valid) {
                draws->push_back({
                    questlab::DebugLineShape::Axes,
                    questlab::math::Multiply(
                        questlab::math::PoseMatrix(state.grip.pose),
                        ScaleMatrix(0.08F, 0.08F, 0.08F)),
                });
            }
            if (state.aim.active && state.aim.valid) {
                draws->push_back({
                    questlab::DebugLineShape::Ray,
                    questlab::math::Multiply(
                        questlab::math::PoseMatrix(state.aim.pose),
                        ScaleMatrix(1.0F, 1.0F, kAimRayLength)),
                    {1.0F, 1.0F, 1.0F, 1.0F},
                });
            }
            if (previewPoses_[hand].has_value()) {
                draws->push_back({
                    questlab::DebugLineShape::Box,
                    questlab::math::Multiply(
                        questlab::math::PoseMatrix(*previewPoses_[hand]),
                        ScaleMatrix(
                            kPreviewSize,
                            kPreviewSize,
                            kPreviewSize)),
                    {0.0F, 0.85F, 1.0F, 1.0F},
                });
            }
        }

        if (!placed_ && guideInitialized_) {
            draws->push_back({
                questlab::DebugLineShape::Box,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(guideInLocal_),
                    ScaleMatrix(kGuideSize, kGuideSize, kGuideSize)),
                {1.0F, 0.85F, 0.0F, 1.0F},
            });
            draws->push_back({
                questlab::DebugLineShape::Axes,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(guideInLocal_),
                    ScaleMatrix(0.25F, 0.25F, 0.25F)),
            });
        }
        if (placed_) {
            draws->push_back({
                questlab::DebugLineShape::Box,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(placedInLocal_),
                    ScaleMatrix(kPlacedSize, kPlacedSize, kPlacedSize)),
                {0.1F, 1.0F, 0.2F, 1.0F},
            });
            draws->push_back({
                questlab::DebugLineShape::Axes,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(placedInLocal_),
                    ScaleMatrix(0.20F, 0.20F, 0.20F)),
            });
        }
        return true;
    }

    void Shutdown() {
        actions_.Shutdown();
        previewPoses_ = {};
        aimVisible_ = {};
        triggerPressed_ = {};
        primaryPressed_ = {};
        guideInitialized_ = false;
        placed_ = false;
    }

private:
    static void LogState(
        std::size_t hand,
        const questlab::XrControllerState& state) {
        questlab::LogInfo(
            "%s input: aim=%s grip=%s trigger=%.2f primary=%s",
            HandName(hand),
            state.aim.active && state.aim.valid
                ? "valid"
                : "inactive",
            state.grip.active && state.grip.valid
                ? "valid"
                : "inactive",
            state.trigger,
            state.primary ? "down" : "up");
    }

    static std::optional<questlab::math::Pose> PreviewPose(
        const questlab::XrControllerState& state) {
        if (!state.aim.active || !state.aim.valid) {
            return std::nullopt;
        }
        questlab::interaction::Ray ray{
            state.aim.pose.position,
            questlab::math::Rotate(
                state.aim.pose.orientation,
                {0.0F, 0.0F, -1.0F}),
        };
        if (!questlab::math::Normalize(&ray.direction)) {
            return std::nullopt;
        }
        return questlab::math::Pose{
            state.aim.pose.orientation,
            questlab::math::Add(
                ray.origin,
                questlab::math::Scale(
                    ray.direction,
                    kAimRayLength)),
        };
    }

    questlab::XrControllerActions actions_;
    std::array<std::optional<questlab::math::Pose>, 2> previewPoses_{};
    std::array<bool, 2> aimVisible_{};
    std::array<bool, 2> triggerPressed_{};
    std::array<bool, 2> primaryPressed_{};
    questlab::math::Pose guideInLocal_{};
    questlab::math::Pose placedInLocal_{};
    bool guideInitialized_ = false;
    bool placed_ = false;
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

void android_main(android_app* app) {
    questlab::SetLogTag("SpatialObject");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Spatial object demo starting");
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    questlab::MetaPassthroughFB passthrough;
    SpatialObjectScene scene;
    questlab::VulkanStereoRenderer renderer;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif
    const questlab::XrInstanceOptions instanceOptions{
        "Spatial Object",
        1,
        {XR_FB_PASSTHROUGH_EXTENSION_NAME},
    };
    const questlab::VulkanRendererOptions rendererOptions{
        true,
    };

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
        !scene.Initialize(
            xrInstance.Instance(),
            xrSession.Session()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene,
            rendererOptions)) {
        questlab::LogError("Spatial object initialization failed");
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
            if (!xrSession.PollEvents(&passthrough) ||
                !passthrough.SetActive(
                    androidState.resumed && xrSession.IsRunning())) {
                break;
            }

            if (!xrSession.PumpFrame(
                    &renderer, &scene, &passthrough)) {
                break;
            }
        }
    }

    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    scene.Shutdown();
    passthrough.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("Spatial object demo stopped cleanly");
}
