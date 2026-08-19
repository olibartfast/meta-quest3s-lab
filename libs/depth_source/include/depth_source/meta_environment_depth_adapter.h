#pragma once

#include "depth_source/depth_source.h"
#include "xr_core/vulkan_session_binding.h"

#include <memory>
#include <mutex>

#include <openxr/openxr.h>
#include <vulkan/vulkan.h>

namespace questlab::depth {

// Wraps XR_META_environment_depth. Every OpenXR handle, swapchain index, and
// Vulkan resource stays inside this class; callers see only DepthCapture.
//
// The runtime publishes depth as a GPU texture, so producing CPU-side metric
// points requires a copy to a host-visible buffer. Acquiring and recording the
// copy happen on the OpenXR frame thread, because acquisition is only valid
// between xrBeginFrame and xrEndFrame; waiting on the fence and converting the
// texels happen in TryConsumeLatest, which the caller runs off the render loop.
class MetaEnvironmentDepthAdapter final : public IDepthSource {
public:
    MetaEnvironmentDepthAdapter();
    ~MetaEnvironmentDepthAdapter() override;

    MetaEnvironmentDepthAdapter(const MetaEnvironmentDepthAdapter&) = delete;
    MetaEnvironmentDepthAdapter& operator=(
        const MetaEnvironmentDepthAdapter&) = delete;

    // Reports whether the system supports environment depth. Safe to call
    // before Initialize, and the only correct way to gate the feature.
    static bool QuerySupport(
        XrInstance instance,
        XrSystemId systemId,
        bool* supportsDepth,
        bool* supportsHandRemoval);

    bool Initialize(
        XrInstance instance,
        XrSession session,
        XrSpace referenceSpace,
        const VulkanDeviceContext& deviceContext);

    DepthCapabilities GetCapabilities() const override;
    bool Start(const DepthStreamConfig& config) override;
    bool AcquireForFrame(int64_t predictedDisplayTime) override;
    bool TryConsumeLatest(DepthCapture* capture) override;
    DepthSourceStats GetStats() const override;
    void Stop() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Factory. Selecting a source must require configuration only, matching how
// the camera source is chosen.
std::unique_ptr<IDepthSource> CreateDepthSource(
    const DepthSourceConfig& config,
    XrInstance instance,
    XrSession session,
    XrSpace referenceSpace,
    const VulkanDeviceContext& deviceContext,
    std::string* error);

}  // namespace questlab::depth
