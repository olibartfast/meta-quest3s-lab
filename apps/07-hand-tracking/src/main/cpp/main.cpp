#include <android_native_app_glue.h>

#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_hand_tracking/xr_hand_tracker.h"
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

constexpr float kGuideDistance = 1.5F;
constexpr float kGuideSize = 0.45F;
constexpr float kPlacedSize = 0.30F;
constexpr float kGrabDistance = 0.15F;
constexpr float kPinchMarkerSize = 0.035F;
constexpr float kMinimumJointMarkerSize = 0.012F;

constexpr std::array<std::array<XrHandJointEXT, 2>, 25> kBonePairs{{
    {XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_PALM_EXT},
    {XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_THUMB_METACARPAL_EXT},
    {XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_THUMB_PROXIMAL_EXT},
    {XR_HAND_JOINT_THUMB_PROXIMAL_EXT, XR_HAND_JOINT_THUMB_DISTAL_EXT},
    {XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT},
    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_INDEX_METACARPAL_EXT},
    {XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT},
    {XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT},
    {XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT},
    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_MIDDLE_METACARPAL_EXT},
    {XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT},
    {XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT},
    {XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT},
    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_RING_METACARPAL_EXT},
    {XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT},
    {XR_HAND_JOINT_RING_PROXIMAL_EXT, XR_HAND_JOINT_RING_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT},
    {XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT},
    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_LITTLE_METACARPAL_EXT},
    {XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT},
    {XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT},
    {XR_HAND_JOINT_LITTLE_DISTAL_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT},
}};

constexpr std::array<XrHandJointEXT, 6> kAxisJoints{{
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_THUMB_TIP_EXT,
    XR_HAND_JOINT_INDEX_TIP_EXT,
    XR_HAND_JOINT_MIDDLE_TIP_EXT,
    XR_HAND_JOINT_RING_TIP_EXT,
    XR_HAND_JOINT_LITTLE_TIP_EXT,
}};

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

const char* HandName(std::size_t hand) {
    return hand == 0 ? "Left" : "Right";
}

bool HasValidPose(XrSpaceLocationFlags flags) {
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags & kValid) == kValid;
}

bool BoneModel(
    const questlab::math::Vec3& parent,
    const questlab::math::Vec3& child,
    questlab::math::Mat4* model) {
    questlab::math::Vec3 direction =
        questlab::math::Subtract(child, parent);
    const float length = questlab::math::Length(direction);
    if (model == nullptr || !questlab::math::Normalize(&direction)) {
        return false;
    }
    questlab::math::Quat rotation;
    if (!questlab::math::RotationFromTo(
            {0.0F, 0.0F, -1.0F}, direction, &rotation)) {
        return false;
    }
    *model = questlab::math::Multiply(
        questlab::math::PoseMatrix({rotation, parent}),
        ScaleMatrix(1.0F, 1.0F, length));
    return true;
}

class HandTrackingScene final :
    public questlab::XrFrameUpdater,
    public questlab::VulkanSceneProvider {
public:
    bool Initialize(
        XrInstance instance,
        XrSystemId systemId,
        XrSession session) {
        return tracker_.Initialize(instance, systemId, session);
    }

    bool UpdateFrame(
        const questlab::XrFrameUpdateInfo& frame) override {
        if (!tracker_.UpdateFrame(frame)) {
            return false;
        }

        std::array<questlab::interaction::PinchFrameResult, 2> results{};
        for (std::size_t hand = 0; hand < 2; ++hand) {
            const questlab::HandState& state = HandState(hand);
            const auto thumb = TrackedPosition(
                state, XR_HAND_JOINT_THUMB_TIP_EXT);
            const auto index = TrackedPosition(
                state, XR_HAND_JOINT_INDEX_TIP_EXT);
            if (thumb.has_value() && index.has_value()) {
                pinchCenters_[hand] = questlab::math::Scale(
                    questlab::math::Add(*thumb, *index), 0.5F);
            } else {
                pinchCenters_[hand] = std::nullopt;
            }
            results[hand] = pinches_[hand].Update(thumb, index);
            if (results[hand].started) {
                questlab::LogInfo("%s pinch started", HandName(hand));
            }
            if (results[hand].ended) {
                questlab::LogInfo("%s pinch ended", HandName(hand));
            }
        }

        if (grabbedHand_.has_value()) {
            const std::size_t hand = *grabbedHand_;
            if (results[hand].active && pinchCenters_[hand].has_value()) {
                placedInLocal_.position = *pinchCenters_[hand];
            } else {
                questlab::LogInfo(
                    "%s pinch dropped object", HandName(hand));
                grabbedHand_ = std::nullopt;
            }
            return true;
        }

        std::optional<std::size_t> startingHand;
        if (results[0].started) {
            startingHand = 0;
        }
        if (results[1].started) {
            startingHand = 1;
        }
        if (!startingHand.has_value() ||
            !pinchCenters_[*startingHand].has_value()) {
            return true;
        }

        const std::size_t hand = *startingHand;
        const questlab::math::Vec3 center = *pinchCenters_[hand];
        if (!placed_) {
            placedInLocal_ = questlab::math::IdentityPose();
            placedInLocal_.position = center;
            placed_ = true;
            questlab::LogInfo(
                "%s pinch placed object in LOCAL at (%.3f %.3f %.3f)",
                HandName(hand), center.x, center.y, center.z);
        } else if (
            questlab::math::Length(
                questlab::math::Subtract(
                    center, placedInLocal_.position)) <= kGrabDistance) {
            grabbedHand_ = hand;
            placedInLocal_.position = center;
            questlab::LogInfo("%s pinch grabbed object", HandName(hand));
        }
        return true;
    }

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override {
        if (draws == nullptr) {
            return false;
        }
        InitializeGuide(frame);

        for (std::size_t hand = 0; hand < 2; ++hand) {
            DrawHand(hand, draws);
            if (pinchCenters_[hand].has_value()) {
                draws->push_back({
                    questlab::DebugLineShape::Box,
                    questlab::math::Multiply(
                        questlab::math::TranslationMatrix(
                            *pinchCenters_[hand]),
                        ScaleMatrix(
                            kPinchMarkerSize,
                            kPinchMarkerSize,
                            kPinchMarkerSize)),
                    pinches_[hand].IsActive()
                        ? std::array<float, 4>{1.0F, 0.55F, 0.0F, 1.0F}
                        : std::array<float, 4>{0.0F, 0.85F, 1.0F, 1.0F},
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
                grabbedHand_.has_value()
                    ? std::array<float, 4>{1.0F, 0.85F, 0.0F, 1.0F}
                    : std::array<float, 4>{0.1F, 1.0F, 0.2F, 1.0F},
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
        tracker_.Shutdown();
        for (questlab::interaction::PinchState& pinch : pinches_) {
            pinch.Reset();
        }
        pinchCenters_ = {};
        grabbedHand_ = std::nullopt;
        guideInitialized_ = false;
        placed_ = false;
    }

private:
    const questlab::HandState& HandState(std::size_t hand) const {
        return tracker_.State(
            hand == 0
                ? questlab::HandSide::Left
                : questlab::HandSide::Right);
    }

    static std::optional<questlab::math::Vec3> TrackedPosition(
        const questlab::HandState& state,
        XrHandJointEXT joint) {
        if (!state.active) {
            return std::nullopt;
        }
        const questlab::HandJointState& location =
            state.joints[static_cast<std::size_t>(joint)];
        if (!location.positionValid || !location.positionTracked) {
            return std::nullopt;
        }
        return location.pose.position;
    }

    void InitializeGuide(const questlab::XrFrameRenderInfo& frame) {
        if (guideInitialized_ ||
            !HasValidPose(frame.headInLocal.locationFlags)) {
            return;
        }
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
            questlab::math::Scale(horizontalForward, kGuideDistance));
        guideInitialized_ = true;
        questlab::LogInfo(
            "Yellow placement guide fixed in LOCAL at (%.3f %.3f %.3f)",
            guideInLocal_.position.x,
            guideInLocal_.position.y,
            guideInLocal_.position.z);
    }

    void DrawHand(
        std::size_t hand,
        std::vector<questlab::DebugLineDraw>* draws) const {
        const questlab::HandState& state = HandState(hand);
        if (!state.active) {
            return;
        }
        const std::array<float, 4> trackedColor =
            hand == 0
                ? std::array<float, 4>{0.0F, 0.85F, 1.0F, 1.0F}
                : std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F};
        const std::array<float, 4> inferredColor =
            hand == 0
                ? std::array<float, 4>{0.0F, 0.35F, 0.45F, 1.0F}
                : std::array<float, 4>{0.45F, 0.45F, 0.45F, 1.0F};

        for (const questlab::HandJointState& joint : state.joints) {
            if (!joint.positionValid) {
                continue;
            }
            const float markerSize =
                2.0F * joint.radius > kMinimumJointMarkerSize
                    ? 2.0F * joint.radius
                    : kMinimumJointMarkerSize;
            draws->push_back({
                questlab::DebugLineShape::Box,
                questlab::math::Multiply(
                    questlab::math::TranslationMatrix(
                        joint.pose.position),
                    ScaleMatrix(markerSize, markerSize, markerSize)),
                joint.positionTracked ? trackedColor : inferredColor,
            });
        }

        for (const auto& pair : kBonePairs) {
            const questlab::HandJointState& parent =
                state.joints[static_cast<std::size_t>(pair[0])];
            const questlab::HandJointState& child =
                state.joints[static_cast<std::size_t>(pair[1])];
            if (!parent.positionValid || !child.positionValid) {
                continue;
            }
            questlab::math::Mat4 model;
            if (BoneModel(
                    parent.pose.position, child.pose.position, &model)) {
                draws->push_back({
                    questlab::DebugLineShape::Ray,
                    model,
                    parent.positionTracked && child.positionTracked
                        ? trackedColor
                        : inferredColor,
                });
            }
        }

        for (XrHandJointEXT joint : kAxisJoints) {
            const questlab::HandJointState& location =
                state.joints[static_cast<std::size_t>(joint)];
            if (!location.positionValid || !location.orientationValid) {
                continue;
            }
            draws->push_back({
                questlab::DebugLineShape::Axes,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(location.pose),
                    ScaleMatrix(0.025F, 0.025F, 0.025F)),
            });
        }
    }

    questlab::XrHandTracker tracker_;
    std::array<questlab::interaction::PinchState, 2> pinches_{};
    std::array<std::optional<questlab::math::Vec3>, 2> pinchCenters_{};
    std::optional<std::size_t> grabbedHand_;
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
    questlab::SetLogTag("HandTracking");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Hand tracking demo starting");
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    questlab::MetaPassthroughFB passthrough;
    HandTrackingScene scene;
    questlab::VulkanStereoRenderer renderer;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif
    const questlab::XrInstanceOptions instanceOptions{
        "Hand Tracking",
        1,
        {
            XR_FB_PASSTHROUGH_EXTENSION_NAME,
            XR_EXT_HAND_TRACKING_EXTENSION_NAME,
        },
    };
    const questlab::VulkanRendererOptions rendererOptions{true};

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
            xrInstance.SystemId(),
            xrSession.Session()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene,
            rendererOptions)) {
        questlab::LogError("Hand tracking initialization failed");
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
    questlab::LogInfo("Hand tracking demo stopped cleanly");
}
