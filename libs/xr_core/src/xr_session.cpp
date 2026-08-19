#include "xr_core/xr_session.h"

#include "perf_telemetry/perf_telemetry.h"
#include "perf_telemetry/trace_span.h"
#include "xr_core/xr_error.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace questlab {
namespace {

const char* SessionStateName(XrSessionState state) {
    switch (state) {
        case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "INVALID";
    }
}

const char* BlendModeName(XrEnvironmentBlendMode mode) {
    switch (mode) {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
        default: return "INVALID";
    }
}

const char* ReferenceSpaceName(XrReferenceSpaceType type) {
    switch (type) {
        case XR_REFERENCE_SPACE_TYPE_VIEW: return "VIEW";
        case XR_REFERENCE_SPACE_TYPE_LOCAL: return "LOCAL";
        case XR_REFERENCE_SPACE_TYPE_STAGE: return "STAGE";
        default: return "OTHER";
    }
}

}  // namespace

class XrFrameTelemetry final {
public:
    static constexpr size_t kSampleCapacity = 256;

    enum class Phase {
        RuntimeFrameWait,
        EventProcessing,
        FrameUpdate,
        RendererSubmission,
        FrameEnd,
    };

    XrFrameTelemetry()
        : reportCadence_(std::chrono::seconds(1)),
          runStart_(perf::SteadyClock::now()),
          windowStartMonotonicNanoseconds_(perf::SteadyNowNanoseconds()) {
        std::snprintf(
            runIdentifier_,
            sizeof(runIdentifier_),
            "xr-%lld",
            static_cast<long long>(windowStartMonotonicNanoseconds_));
    }

    perf::DurationRing<kSampleCapacity>* DurationFor(Phase phase) {
        switch (phase) {
            case Phase::RuntimeFrameWait: return &runtimeFrameWait_;
            case Phase::EventProcessing: return &eventProcessing_;
            case Phase::FrameUpdate: return &frameUpdate_;
            case Phase::RendererSubmission: return &rendererSubmission_;
            case Phase::FrameEnd: return &frameEnd_;
        }
        return nullptr;
    }

    bool IsEnabled() const { return enabled_; }

    void SetEnabled(bool enabled) {
        if (enabled_ == enabled) {
            return;
        }
        enabled_ = enabled;
        ResetWindow(perf::SteadyClock::now(), true);
    }

    void RecordFrame(const XrFrameState& frameState) {
        if (!enabled_) {
            return;
        }
        ++frameCount_;
        if (frameState.shouldRender == XR_TRUE) {
            ++shouldRenderCount_;
        }
        lastPredictedDisplayTime_ = frameState.predictedDisplayTime;
    }

    void MaybeReport(XrSessionState state) {
        if (!enabled_) {
            return;
        }
        const auto now = perf::SteadyClock::now();
        if (!reportCadence_.ShouldReport(now)) {
            return;
        }
        const int64_t endNanoseconds =
            std::chrono::duration_cast<perf::Nanoseconds>(
                now.time_since_epoch()).count();
        const bool warmup = now - runStart_ < std::chrono::seconds(5);
        const auto wait = runtimeFrameWait_.Snapshot(
            "xr_frame",
            "runtime_frame_wait",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto events = eventProcessing_.Snapshot(
            "xr_frame",
            "event_processing",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto update = frameUpdate_.Snapshot(
            "xr_frame",
            "frame_update",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto render = rendererSubmission_.Snapshot(
            "xr_frame",
            "renderer_submission",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto end = frameEnd_.Snapshot(
            "xr_frame",
            "frame_end",
            windowStartMonotonicNanoseconds_,
            endNanoseconds,
            warmup);
        const auto& waitSummary = wait.Summary();
        const auto& eventSummary = events.Summary();
        const auto& updateSummary = update.Summary();
        const auto& renderSummary = render.Summary();
        const auto& endSummary = end.Summary();
        LogInfo(
            "PERF {\"schema\":\"questlab.performance.v1\","
            "\"severity\":\"info\",\"category\":\"xr_frame\","
            "\"metric_name\":\"frame_phases\","
            "\"unit\":\"milliseconds\",\"run_id\":\"%s\","
            "\"window_start_monotonic_ns\":%lld,"
            "\"window_end_monotonic_ns\":%lld,\"warmup\":%s,"
            "\"session_state\":\"%s\","
            "\"last_predicted_display_time\":%lld,"
            "\"counters\":{\"frames\":%llu,\"should_render\":%llu},"
            "\"durations\":{"
            "\"runtime_frame_wait\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu],"
            "\"event_processing\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu],"
            "\"frame_update\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu],"
            "\"renderer_submission\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu],"
            "\"frame_end\":[%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%llu]},"
            "\"duration_fields\":[\"count\",\"mean\",\"p50\","
            "\"p95\",\"p99\",\"max\",\"overflow\"]}",
            runIdentifier_,
            static_cast<long long>(windowStartMonotonicNanoseconds_),
            static_cast<long long>(endNanoseconds),
            warmup ? "true" : "false",
            SessionStateName(state),
            static_cast<long long>(lastPredictedDisplayTime_),
            static_cast<unsigned long long>(frameCount_),
            static_cast<unsigned long long>(shouldRenderCount_),
            static_cast<unsigned long long>(waitSummary.count),
            waitSummary.meanMilliseconds,
            waitSummary.p50Milliseconds,
            waitSummary.p95Milliseconds,
            waitSummary.p99Milliseconds,
            waitSummary.maximumMilliseconds,
            static_cast<unsigned long long>(waitSummary.overflowCount),
            static_cast<unsigned long long>(eventSummary.count),
            eventSummary.meanMilliseconds,
            eventSummary.p50Milliseconds,
            eventSummary.p95Milliseconds,
            eventSummary.p99Milliseconds,
            eventSummary.maximumMilliseconds,
            static_cast<unsigned long long>(eventSummary.overflowCount),
            static_cast<unsigned long long>(updateSummary.count),
            updateSummary.meanMilliseconds,
            updateSummary.p50Milliseconds,
            updateSummary.p95Milliseconds,
            updateSummary.p99Milliseconds,
            updateSummary.maximumMilliseconds,
            static_cast<unsigned long long>(updateSummary.overflowCount),
            static_cast<unsigned long long>(renderSummary.count),
            renderSummary.meanMilliseconds,
            renderSummary.p50Milliseconds,
            renderSummary.p95Milliseconds,
            renderSummary.p99Milliseconds,
            renderSummary.maximumMilliseconds,
            static_cast<unsigned long long>(renderSummary.overflowCount),
            static_cast<unsigned long long>(endSummary.count),
            endSummary.meanMilliseconds,
            endSummary.p50Milliseconds,
            endSummary.p95Milliseconds,
            endSummary.p99Milliseconds,
            endSummary.maximumMilliseconds,
            static_cast<unsigned long long>(endSummary.overflowCount));
        ResetWindow(now, false);
    }

private:
    void ResetWindow(
        perf::SteadyClock::time_point now,
        bool resetCadence) {
        runtimeFrameWait_.Clear();
        eventProcessing_.Clear();
        frameUpdate_.Clear();
        rendererSubmission_.Clear();
        frameEnd_.Clear();
        frameCount_ = 0;
        shouldRenderCount_ = 0;
        lastPredictedDisplayTime_ = 0;
        windowStartMonotonicNanoseconds_ =
            std::chrono::duration_cast<perf::Nanoseconds>(
                now.time_since_epoch()).count();
        if (resetCadence) {
            reportCadence_.Reset();
        }
    }

    perf::DurationRing<kSampleCapacity> runtimeFrameWait_;
    perf::DurationRing<kSampleCapacity> eventProcessing_;
    perf::DurationRing<kSampleCapacity> frameUpdate_;
    perf::DurationRing<kSampleCapacity> rendererSubmission_;
    perf::DurationRing<kSampleCapacity> frameEnd_;
    perf::ReportCadence reportCadence_;
    perf::SteadyClock::time_point runStart_;
    int64_t windowStartMonotonicNanoseconds_ = 0;
    XrTime lastPredictedDisplayTime_ = 0;
    uint64_t frameCount_ = 0;
    uint64_t shouldRenderCount_ = 0;
    char runIdentifier_[32]{};
    bool enabled_ = true;
};

XrSessionContext::XrSessionContext() = default;

XrSessionContext::~XrSessionContext() {
    Shutdown();
}

bool XrSessionContext::Initialize(
    XrInstance instance,
    XrSystemId systemId,
    const void* graphicsBindingChain) {
    if (session_ != XR_NULL_HANDLE) {
        return true;
    }
    instance_ = instance;
    frameTelemetry_ = std::make_unique<XrFrameTelemetry>();
    frameTelemetry_->SetEnabled(performanceTelemetryEnabled_);
    layers_.clear();
    layers_.reserve(2);

    uint32_t viewConfigurationCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurations(
                instance_, systemId, 0, &viewConfigurationCount, nullptr),
            "xrEnumerateViewConfigurations(count)")) {
        return false;
    }
    std::vector<XrViewConfigurationType> viewConfigurations(viewConfigurationCount);
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurations(
                instance_,
                systemId,
                viewConfigurationCount,
                &viewConfigurationCount,
                viewConfigurations.data()),
            "xrEnumerateViewConfigurations(list)")) {
        return false;
    }
    if (std::find(
            viewConfigurations.begin(),
            viewConfigurations.end(),
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) == viewConfigurations.end()) {
        LogError("PRIMARY_STEREO view configuration is unavailable");
        return false;
    }

    uint32_t viewCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurationViews(
                instance_,
                systemId,
                viewConfiguration_,
                0,
                &viewCount,
                nullptr),
            "xrEnumerateViewConfigurationViews(count)")) {
        return false;
    }
    viewConfigurationViews_.assign(
        viewCount,
        XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurationViews(
                instance_,
                systemId,
                viewConfiguration_,
                viewCount,
                &viewCount,
                viewConfigurationViews_.data()),
            "xrEnumerateViewConfigurationViews(list)")) {
        viewConfigurationViews_.clear();
        return false;
    }
    if (viewCount != 2) {
        LogError("PRIMARY_STEREO reported %u views instead of 2", viewCount);
        viewConfigurationViews_.clear();
        return false;
    }
    views_.assign(viewCount, XrView{XR_TYPE_VIEW});
    for (uint32_t index = 0; index < viewCount; ++index) {
        const XrViewConfigurationView& view = viewConfigurationViews_[index];
        LogInfo(
            "Stereo view %u: recommended %ux%u, sample count %u",
            index,
            view.recommendedImageRectWidth,
            view.recommendedImageRectHeight,
            view.recommendedSwapchainSampleCount);
    }

    uint32_t blendModeCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateEnvironmentBlendModes(
                instance_,
                systemId,
                viewConfiguration_,
                0,
                &blendModeCount,
                nullptr),
            "xrEnumerateEnvironmentBlendModes(count)")) {
        return false;
    }
    std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
    if (!CheckXr(
            instance_,
            xrEnumerateEnvironmentBlendModes(
                instance_,
                systemId,
                viewConfiguration_,
                blendModeCount,
                &blendModeCount,
                blendModes.data()),
            "xrEnumerateEnvironmentBlendModes(list)")) {
        return false;
    }
    constexpr std::array<XrEnvironmentBlendMode, 3> kBlendPreference = {
        XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
        XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
    };
    const auto selectedBlendMode = std::find_first_of(
        kBlendPreference.begin(),
        kBlendPreference.end(),
        blendModes.begin(),
        blendModes.end());
    if (selectedBlendMode == kBlendPreference.end()) {
        LogError("Runtime reported no supported environment blend mode");
        return false;
    }
    blendMode_ = *selectedBlendMode;
    LogInfo("Selected PRIMARY_STEREO with %s blend mode", BlendModeName(blendMode_));

    XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = graphicsBindingChain;
    sessionCreateInfo.systemId = systemId;
    if (!CheckXr(
            instance_,
            xrCreateSession(instance_, &sessionCreateInfo, &session_),
            "xrCreateSession")) {
        return false;
    }
    LogInfo("OpenXR session created");

    if (!CreateReferenceSpaces()) {
        Shutdown();
        return false;
    }
    return true;
}

bool XrSessionContext::CreateReferenceSpaces() {
    uint32_t typeCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateReferenceSpaces(
                session_, 0, &typeCount, nullptr),
            "xrEnumerateReferenceSpaces(count)")) {
        return false;
    }
    std::vector<XrReferenceSpaceType> types(typeCount);
    if (!CheckXr(
            instance_,
            xrEnumerateReferenceSpaces(
                session_, typeCount, &typeCount, types.data()),
            "xrEnumerateReferenceSpaces(list)")) {
        return false;
    }
    for (XrReferenceSpaceType type : types) {
        LogInfo("Reference space supported: %s", ReferenceSpaceName(type));
    }
    const auto supports = [&types](XrReferenceSpaceType type) {
        return std::find(types.begin(), types.end(), type) != types.end();
    };
    if (!supports(XR_REFERENCE_SPACE_TYPE_VIEW) ||
        !supports(XR_REFERENCE_SPACE_TYPE_LOCAL)) {
        LogError("Runtime must support both VIEW and LOCAL reference spaces");
        return false;
    }

    XrReferenceSpaceCreateInfo spaceCreateInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0F;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &localSpace_),
            "xrCreateReferenceSpace(LOCAL)")) {
        return false;
    }
    LogInfo("LOCAL reference space created");

    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &viewSpace_),
            "xrCreateReferenceSpace(VIEW)")) {
        return false;
    }
    LogInfo("VIEW reference space created");

    if (!supports(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        LogInfo("STAGE reference space is unavailable");
        return true;
    }
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &stageSpace_),
            "xrCreateReferenceSpace(STAGE)")) {
        return false;
    }
    LogInfo("STAGE reference space created");

    const XrResult boundsResult = xrGetReferenceSpaceBoundsRect(
        session_,
        XR_REFERENCE_SPACE_TYPE_STAGE,
        &stageBounds_);
    if (boundsResult == XR_SPACE_BOUNDS_UNAVAILABLE) {
        LogInfo("STAGE bounds are unavailable");
    } else if (!CheckXr(
            instance_,
            boundsResult,
            "xrGetReferenceSpaceBoundsRect(STAGE)")) {
        return false;
    } else {
        stageBoundsAvailable_ = true;
        LogInfo(
            "STAGE bounds: %.3f x %.3f metres",
            stageBounds_.width,
            stageBounds_.height);
    }
    return true;
}

bool XrSessionContext::PollEvents(XrEventObserver* observer) {
    QUESTLAB_ATRACE_SCOPE("questlab.xr.event_processing");
    perf::ScopedDuration<XrFrameTelemetry::kSampleCapacity> eventTimer(
        frameTelemetry_ != nullptr
            ? frameTelemetry_->DurationFor(
                XrFrameTelemetry::Phase::EventProcessing)
            : nullptr,
        performanceTelemetryEnabled_);
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (true) {
        const XrResult result = xrPollEvent(instance_, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return true;
        }
        if (!CheckXr(instance_, result, "xrPollEvent")) {
            shouldExit_ = true;
            return false;
        }

        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                if (!HandleSessionStateChanged(
                        *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event))) {
                    return false;
                }
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                LogError("OpenXR instance loss pending; exiting");
                shouldExit_ = true;
                break;
            case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
                const auto* lost =
                    reinterpret_cast<const XrEventDataEventsLost*>(&event);
                LogError("OpenXR runtime reported %u lost events", lost->lostEventCount);
                break;
            }
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                const auto* change =
                    reinterpret_cast<
                        const XrEventDataReferenceSpaceChangePending*>(&event);
                LogInfo(
                    "%s reference space change pending at time %lld",
                    ReferenceSpaceName(change->referenceSpaceType),
                    static_cast<long long>(change->changeTime));
                break;
            }
            default:
                LogInfo("OpenXR event type: %d", event.type);
                break;
        }
        if (observer != nullptr && !observer->HandleEvent(event)) {
            LogError("OpenXR event observer requested exit");
            shouldExit_ = true;
            return false;
        }
        event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool XrSessionContext::HandleSessionStateChanged(
    const XrEventDataSessionStateChanged& event) {
    if (event.session != session_) {
        LogError("Received a state change for an unknown OpenXR session");
        shouldExit_ = true;
        return false;
    }
    LogInfo(
        "OpenXR session state: %s -> %s",
        SessionStateName(state_),
        SessionStateName(event.state));
    state_ = event.state;

    if (state_ == XR_SESSION_STATE_READY && !running_) {
        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = viewConfiguration_;
        if (!CheckXr(
                instance_,
                xrBeginSession(session_, &beginInfo),
                "xrBeginSession")) {
            shouldExit_ = true;
            return false;
        }
        running_ = true;
        LogInfo("OpenXR session begun");
    } else if (state_ == XR_SESSION_STATE_STOPPING && running_) {
        if (!CheckXr(instance_, xrEndSession(session_), "xrEndSession")) {
            shouldExit_ = true;
            return false;
        }
        running_ = false;
        LogInfo("OpenXR session ended");
    } else if (
        state_ == XR_SESSION_STATE_EXITING ||
        state_ == XR_SESSION_STATE_LOSS_PENDING) {
        shouldExit_ = true;
    }
    return true;
}

bool XrSessionContext::LocateTrackedSpaces(
    XrTime time,
    XrFrameRenderInfo* renderInfo) {
    renderInfo->headInLocal = XrSpaceLocation{XR_TYPE_SPACE_LOCATION};
    if (!CheckXr(
            instance_,
            xrLocateSpace(
                viewSpace_,
                localSpace_,
                time,
                &renderInfo->headInLocal),
            "xrLocateSpace(VIEW in LOCAL)")) {
        return false;
    }
    renderInfo->stageAvailable = stageSpace_ != XR_NULL_HANDLE;
    renderInfo->stageBoundsAvailable = stageBoundsAvailable_;
    renderInfo->stageBounds = stageBounds_;
    renderInfo->stageInLocal = XrSpaceLocation{XR_TYPE_SPACE_LOCATION};
    if (stageSpace_ != XR_NULL_HANDLE &&
        !CheckXr(
            instance_,
            xrLocateSpace(
                stageSpace_,
                localSpace_,
                time,
                &renderInfo->stageInLocal),
            "xrLocateSpace(STAGE in LOCAL)")) {
        return false;
    }
    return true;
}

bool XrSessionContext::PumpFrame(
    XrFrameRenderer* renderer,
    XrFrameUpdater* updater,
    XrUnderlayProvider* underlayProvider) {
    if (!running_) {
        return true;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    XrResult waitResult = XR_SUCCESS;
    {
        QUESTLAB_ATRACE_SCOPE("questlab.xr.runtime_frame_wait");
        perf::ScopedDuration<XrFrameTelemetry::kSampleCapacity> waitTimer(
            frameTelemetry_ != nullptr
                ? frameTelemetry_->DurationFor(
                    XrFrameTelemetry::Phase::RuntimeFrameWait)
                : nullptr,
            performanceTelemetryEnabled_);
        waitResult = xrWaitFrame(session_, &waitInfo, &frameState);
    }
    if (!CheckXr(
            instance_,
            waitResult,
            "xrWaitFrame")) {
        shouldExit_ = true;
        return false;
    }
    if (frameTelemetry_ != nullptr) {
        frameTelemetry_->RecordFrame(frameState);
    }

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!CheckXr(
            instance_,
            xrBeginFrame(session_, &beginInfo),
            "xrBeginFrame")) {
        shouldExit_ = true;
        return false;
    }

    bool frameSucceeded = true;
    const XrCompositionLayerBaseHeader* layer = nullptr;
    {
        QUESTLAB_ATRACE_SCOPE("questlab.xr.frame_update");
        perf::ScopedDuration<XrFrameTelemetry::kSampleCapacity> updateTimer(
            frameTelemetry_ != nullptr
                ? frameTelemetry_->DurationFor(
                    XrFrameTelemetry::Phase::FrameUpdate)
                : nullptr,
            performanceTelemetryEnabled_);
        if (updater != nullptr &&
            !updater->UpdateFrame({
                frameState.predictedDisplayTime,
                localSpace_,
            })) {
            LogError("Frame updater failed; submitting an empty frame");
            frameSucceeded = false;
        }
    }
    {
        QUESTLAB_ATRACE_SCOPE("questlab.xr.renderer_submission");
        perf::ScopedDuration<XrFrameTelemetry::kSampleCapacity> renderTimer(
            frameTelemetry_ != nullptr
                ? frameTelemetry_->DurationFor(
                    XrFrameTelemetry::Phase::RendererSubmission)
                : nullptr,
            performanceTelemetryEnabled_);
        if (frameSucceeded &&
            frameState.shouldRender == XR_TRUE &&
            renderer != nullptr) {
            XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
            locateInfo.viewConfigurationType = viewConfiguration_;
            locateInfo.displayTime = frameState.predictedDisplayTime;
            locateInfo.space = localSpace_;
            XrViewState viewState{XR_TYPE_VIEW_STATE};
            uint32_t viewCount = 0;
            if (!CheckXr(
                    instance_,
                    xrLocateViews(
                        session_,
                        &locateInfo,
                        &viewState,
                        static_cast<uint32_t>(views_.size()),
                        &viewCount,
                        views_.data()),
                    "xrLocateViews")) {
                frameSucceeded = false;
            } else if (viewCount != views_.size()) {
                LogError(
                    "xrLocateViews returned %u views; expected %zu",
                    viewCount,
                    views_.size());
                frameSucceeded = false;
            } else {
                constexpr XrViewStateFlags kRequiredViewFlags =
                    XR_VIEW_STATE_POSITION_VALID_BIT |
                    XR_VIEW_STATE_ORIENTATION_VALID_BIT;
                if ((viewState.viewStateFlags & kRequiredViewFlags) !=
                    kRequiredViewFlags) {
                    if (!invalidViewsLogged_) {
                        LogWarning(
                            "Stereo view poses are not valid; submitting an empty frame");
                        invalidViewsLogged_ = true;
                    }
                } else {
                    invalidViewsLogged_ = false;
                    XrFrameRenderInfo renderInfo;
                    renderInfo.predictedDisplayTime = frameState.predictedDisplayTime;
                    renderInfo.space = localSpace_;
                    renderInfo.views = views_.data();
                    renderInfo.viewCount = viewCount;
                    renderInfo.viewStateFlags = viewState.viewStateFlags;
                    if (!LocateTrackedSpaces(
                            frameState.predictedDisplayTime,
                            &renderInfo)) {
                        frameSucceeded = false;
                    } else if (!renderer->RenderFrame(renderInfo, &layer)) {
                        LogError("Frame renderer failed; submitting an empty frame");
                        layer = nullptr;
                        frameSucceeded = false;
                    }
                }
            }
        }

        layers_.clear();
        if (frameSucceeded &&
            frameState.shouldRender == XR_TRUE &&
            underlayProvider != nullptr &&
            !underlayProvider->AppendUnderlayLayers(
                frameState.predictedDisplayTime,
                &layers_)) {
            LogError("Underlay provider failed; submitting an empty frame");
            frameSucceeded = false;
            layers_.clear();
        }
        if (frameSucceeded && layer != nullptr) {
            layers_.push_back(layer);
        }
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = blendMode_;
    endInfo.layerCount = static_cast<uint32_t>(layers_.size());
    endInfo.layers = layers_.empty() ? nullptr : layers_.data();
    XrResult endResult = XR_SUCCESS;
    {
        QUESTLAB_ATRACE_SCOPE("questlab.xr.frame_end");
        perf::ScopedDuration<XrFrameTelemetry::kSampleCapacity> endTimer(
            frameTelemetry_ != nullptr
                ? frameTelemetry_->DurationFor(
                    XrFrameTelemetry::Phase::FrameEnd)
                : nullptr,
            performanceTelemetryEnabled_);
        endResult = xrEndFrame(session_, &endInfo);
    }
    const bool endSucceeded = CheckXr(
            instance_,
            endResult,
            layers_.empty()
                ? "xrEndFrame(empty)"
                : layers_.size() == 1
                    ? layer == nullptr
                        ? "xrEndFrame(underlay)"
                        : "xrEndFrame(projection)"
                    : "xrEndFrame(underlay+projection)");
    if (frameTelemetry_ != nullptr) {
        frameTelemetry_->MaybeReport(state_);
    }
    if (!endSucceeded || !frameSucceeded) {
        shouldExit_ = true;
        return false;
    }
    return true;
}

bool XrSessionContext::PumpEmptyFrame() {
    return PumpFrame(nullptr, nullptr);
}

void XrSessionContext::SetPerformanceTelemetryEnabled(bool enabled) {
    performanceTelemetryEnabled_ = enabled;
    if (frameTelemetry_ != nullptr) {
        frameTelemetry_->SetEnabled(enabled);
    }
}

void XrSessionContext::RequestExit() {
    shouldExit_ = true;
}

void XrSessionContext::Shutdown() {
    if (stageSpace_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(stageSpace_), "xrDestroySpace(STAGE)");
        stageSpace_ = XR_NULL_HANDLE;
        LogInfo("STAGE reference space destroyed");
    }
    if (viewSpace_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(viewSpace_), "xrDestroySpace(VIEW)");
        viewSpace_ = XR_NULL_HANDLE;
        LogInfo("VIEW reference space destroyed");
    }
    if (localSpace_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(localSpace_), "xrDestroySpace(LOCAL)");
        localSpace_ = XR_NULL_HANDLE;
        LogInfo("LOCAL reference space destroyed");
    }
    if (session_ != XR_NULL_HANDLE) {
        // xrEndSession is only legal after the runtime enters STOPPING. When
        // Android destroys the activity earlier, destroying the session is the
        // deterministic fallback and avoids an invalid-state xrEndSession call.
        running_ = false;
        CheckXr(instance_, xrDestroySession(session_), "xrDestroySession");
        session_ = XR_NULL_HANDLE;
        LogInfo("OpenXR session destroyed");
    }
    instance_ = XR_NULL_HANDLE;
    state_ = XR_SESSION_STATE_UNKNOWN;
    viewConfigurationViews_.clear();
    views_.clear();
    layers_.clear();
    frameTelemetry_.reset();
    invalidViewsLogged_ = false;
    stageBounds_ = {};
    stageBoundsAvailable_ = false;
}

}  // namespace questlab
