#include "depth_source/meta_environment_depth_adapter.h"

#include "xr_core/xr_error.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace questlab::depth {
namespace {

// Meta publishes the environment depth swapchain as a two-layer array, one
// layer per eye. Fusion consumes the left eye.
constexpr uint32_t kLeftEyeLayer = 0;

// The extension exposes no way to query the swapchain format, so the texel
// type is an assumption rather than a negotiated fact. Meta documents
// D16_UNORM. If that is ever wrong the readback will produce plausible but
// meaningless depth, so the assumption is asserted at the one place it is
// acted on, in TryConsumeLatest, and stated here.
using DepthTexel = uint16_t;
constexpr float kDepthTexelMaximum = 65535.0F;

// Layout the runtime leaves the acquired depth image in. Transitioning from
// VK_IMAGE_LAYOUT_UNDEFINED would discard the contents, which is exactly the
// bug that yields an all-zero depth buffer, so the source layout is named
// explicitly instead.
constexpr VkImageLayout kAcquiredLayout =
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

math::Pose PoseFromXr(const XrPosef& pose) {
    math::Pose result;
    result.orientation = {
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z,
        pose.orientation.w,
    };
    result.position = {pose.position.x, pose.position.y, pose.position.z};
    return result;
}

bool FindHostVisibleMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeBits,
    uint32_t* typeIndex) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    const VkMemoryPropertyFlags required =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeBits & (1U << index)) == 0U) {
            continue;
        }
        if ((properties.memoryTypes[index].propertyFlags & required) ==
            required) {
            *typeIndex = index;
            return true;
        }
    }
    return false;
}

}  // namespace

struct MetaEnvironmentDepthAdapter::Impl {
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    VulkanDeviceContext device{};

    PFN_xrCreateEnvironmentDepthProviderMETA createProvider = nullptr;
    PFN_xrDestroyEnvironmentDepthProviderMETA destroyProvider = nullptr;
    PFN_xrStartEnvironmentDepthProviderMETA startProvider = nullptr;
    PFN_xrStopEnvironmentDepthProviderMETA stopProvider = nullptr;
    PFN_xrCreateEnvironmentDepthSwapchainMETA createSwapchain = nullptr;
    PFN_xrDestroyEnvironmentDepthSwapchainMETA destroySwapchain = nullptr;
    PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA enumerateImages =
        nullptr;
    PFN_xrGetEnvironmentDepthSwapchainStateMETA getSwapchainState = nullptr;
    PFN_xrAcquireEnvironmentDepthImageMETA acquireImage = nullptr;
    PFN_xrSetEnvironmentDepthHandRemovalMETA setHandRemoval = nullptr;

    XrEnvironmentDepthProviderMETA provider = XR_NULL_HANDLE;
    XrEnvironmentDepthSwapchainMETA swapchain = XR_NULL_HANDLE;
    std::vector<VkImage> images;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize stagingSize = 0;

    DepthCapabilities capabilities{};
    DepthStreamConfig streamConfig{};
    DepthSourceStats stats{};
    mutable std::mutex mutex;

    bool started = false;
    bool copyPending = false;
    uint64_t nextFrameId = 1;
    // Metadata for the copy currently in flight. Held here because the fence
    // is waited on later, on a different call, than the acquire.
    DepthView pendingView{};
    int64_t pendingDisplayTime = 0;
    int64_t pendingArrival = 0;

    void SetError(const std::string& message, DepthHealth health) {
        std::lock_guard<std::mutex> lock(mutex);
        stats.lastError = message;
        stats.health = health;
    }
};

MetaEnvironmentDepthAdapter::MetaEnvironmentDepthAdapter()
    : impl_(std::make_unique<Impl>()) {}

MetaEnvironmentDepthAdapter::~MetaEnvironmentDepthAdapter() {
    Stop();
}

bool MetaEnvironmentDepthAdapter::QuerySupport(
    XrInstance instance,
    XrSystemId systemId,
    bool* supportsDepth,
    bool* supportsHandRemoval) {
    XrSystemEnvironmentDepthPropertiesMETA depthProperties{
        XR_TYPE_SYSTEM_ENVIRONMENT_DEPTH_PROPERTIES_META};
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    systemProperties.next = &depthProperties;
    if (!CheckXr(
            instance,
            xrGetSystemProperties(instance, systemId, &systemProperties),
            "xrGetSystemProperties(environment depth)")) {
        return false;
    }
    if (supportsDepth != nullptr) {
        *supportsDepth = depthProperties.supportsEnvironmentDepth == XR_TRUE;
    }
    if (supportsHandRemoval != nullptr) {
        *supportsHandRemoval = depthProperties.supportsHandRemoval == XR_TRUE;
    }
    return true;
}

bool MetaEnvironmentDepthAdapter::Initialize(
    XrInstance instance,
    XrSession session,
    XrSpace referenceSpace,
    const VulkanDeviceContext& deviceContext) {
    impl_->instance = instance;
    impl_->session = session;
    impl_->space = referenceSpace;
    impl_->device = deviceContext;
    impl_->capabilities.sourceName = "XR_META_environment_depth";

    const auto resolve = [&](const char* name, PFN_xrVoidFunction* function) {
        return CheckXr(
            instance,
            xrGetInstanceProcAddr(instance, name, function),
            name);
    };
    const bool resolved =
        resolve(
            "xrCreateEnvironmentDepthProviderMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->createProvider)) &&
        resolve(
            "xrDestroyEnvironmentDepthProviderMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->destroyProvider)) &&
        resolve(
            "xrStartEnvironmentDepthProviderMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->startProvider)) &&
        resolve(
            "xrStopEnvironmentDepthProviderMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->stopProvider)) &&
        resolve(
            "xrCreateEnvironmentDepthSwapchainMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->createSwapchain)) &&
        resolve(
            "xrDestroyEnvironmentDepthSwapchainMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->destroySwapchain)) &&
        resolve(
            "xrEnumerateEnvironmentDepthSwapchainImagesMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->enumerateImages)) &&
        resolve(
            "xrGetEnvironmentDepthSwapchainStateMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(
                &impl_->getSwapchainState)) &&
        resolve(
            "xrAcquireEnvironmentDepthImageMETA",
            reinterpret_cast<PFN_xrVoidFunction*>(&impl_->acquireImage));
    if (!resolved) {
        impl_->SetError(
            "Environment depth entry points are unavailable",
            DepthHealth::Unsupported);
        return false;
    }
    // Hand removal is optional; a missing entry point is not fatal.
    resolve(
        "xrSetEnvironmentDepthHandRemovalMETA",
        reinterpret_cast<PFN_xrVoidFunction*>(&impl_->setHandRemoval));

    XrEnvironmentDepthProviderCreateInfoMETA providerInfo{
        XR_TYPE_ENVIRONMENT_DEPTH_PROVIDER_CREATE_INFO_META};
    if (!CheckXr(
            instance,
            impl_->createProvider(session, &providerInfo, &impl_->provider),
            "xrCreateEnvironmentDepthProviderMETA")) {
        impl_->SetError("Cannot create the depth provider", DepthHealth::Error);
        return false;
    }

    XrEnvironmentDepthSwapchainCreateInfoMETA swapchainInfo{
        XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_CREATE_INFO_META};
    if (!CheckXr(
            instance,
            impl_->createSwapchain(
                impl_->provider, &swapchainInfo, &impl_->swapchain),
            "xrCreateEnvironmentDepthSwapchainMETA")) {
        impl_->SetError(
            "Cannot create the depth swapchain", DepthHealth::Error);
        Stop();
        return false;
    }

    XrEnvironmentDepthSwapchainStateMETA state{
        XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META};
    if (!CheckXr(
            instance,
            impl_->getSwapchainState(impl_->swapchain, &state),
            "xrGetEnvironmentDepthSwapchainStateMETA")) {
        Stop();
        return false;
    }
    impl_->capabilities.width = static_cast<int32_t>(state.width);
    impl_->capabilities.height = static_cast<int32_t>(state.height);
    if (state.width == 0U || state.height == 0U) {
        impl_->SetError(
            "Depth swapchain reported zero dimensions", DepthHealth::Error);
        Stop();
        return false;
    }

    uint32_t imageCount = 0;
    if (!CheckXr(
            instance,
            impl_->enumerateImages(impl_->swapchain, 0, &imageCount, nullptr),
            "xrEnumerateEnvironmentDepthSwapchainImagesMETA(count)")) {
        Stop();
        return false;
    }
    std::vector<XrSwapchainImageVulkan2KHR> vulkanImages(
        imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
    if (!CheckXr(
            instance,
            impl_->enumerateImages(
                impl_->swapchain,
                imageCount,
                &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(
                    vulkanImages.data())),
            "xrEnumerateEnvironmentDepthSwapchainImagesMETA")) {
        Stop();
        return false;
    }
    impl_->images.clear();
    impl_->images.reserve(imageCount);
    for (const XrSwapchainImageVulkan2KHR& image : vulkanImages) {
        impl_->images.push_back(image.image);
    }

    // Staging buffer sized for one eye's worth of D16 texels.
    impl_->stagingSize = static_cast<VkDeviceSize>(state.width) *
                         static_cast<VkDeviceSize>(state.height) *
                         sizeof(DepthTexel);
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = impl_->stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(
            impl_->device.device, &bufferInfo, nullptr, &impl_->staging) !=
        VK_SUCCESS) {
        impl_->SetError("Cannot create the depth staging buffer",
                        DepthHealth::Error);
        Stop();
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
        impl_->device.device, impl_->staging, &requirements);
    uint32_t memoryType = 0;
    if (!FindHostVisibleMemoryType(
            impl_->device.physicalDevice,
            requirements.memoryTypeBits,
            &memoryType)) {
        impl_->SetError("No host-visible memory for depth readback",
                        DepthHealth::Error);
        Stop();
        return false;
    }
    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(
            impl_->device.device,
            &allocateInfo,
            nullptr,
            &impl_->stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(
            impl_->device.device,
            impl_->staging,
            impl_->stagingMemory,
            0) != VK_SUCCESS) {
        impl_->SetError("Cannot bind depth staging memory", DepthHealth::Error);
        Stop();
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = impl_->device.queueFamilyIndex;
    if (vkCreateCommandPool(
            impl_->device.device, &poolInfo, nullptr, &impl_->commandPool) !=
        VK_SUCCESS) {
        impl_->SetError("Cannot create the depth command pool",
                        DepthHealth::Error);
        Stop();
        return false;
    }
    VkCommandBufferAllocateInfo commandInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = impl_->commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(
            impl_->device.device, &commandInfo, &impl_->commandBuffer) !=
        VK_SUCCESS) {
        impl_->SetError("Cannot allocate the depth command buffer",
                        DepthHealth::Error);
        Stop();
        return false;
    }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(
            impl_->device.device, &fenceInfo, nullptr, &impl_->fence) !=
        VK_SUCCESS) {
        impl_->SetError("Cannot create the depth fence", DepthHealth::Error);
        Stop();
        return false;
    }

    impl_->capabilities.supportsEnvironmentDepth = true;
    impl_->SetError("", DepthHealth::Stopped);
    return true;
}

DepthCapabilities MetaEnvironmentDepthAdapter::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->capabilities;
}

bool MetaEnvironmentDepthAdapter::Start(const DepthStreamConfig& config) {
    if (impl_->provider == XR_NULL_HANDLE) {
        impl_->SetError("Depth provider is not initialized",
                        DepthHealth::Error);
        return false;
    }
    if (impl_->started) {
        return true;
    }
    impl_->streamConfig = config;
    impl_->SetError("", DepthHealth::Starting);

    if (!CheckXr(
            impl_->instance,
            impl_->startProvider(impl_->provider),
            "xrStartEnvironmentDepthProviderMETA")) {
        impl_->SetError("Cannot start the depth provider", DepthHealth::Error);
        return false;
    }
    if (impl_->setHandRemoval != nullptr) {
        XrEnvironmentDepthHandRemovalSetInfoMETA handRemoval{
            XR_TYPE_ENVIRONMENT_DEPTH_HAND_REMOVAL_SET_INFO_META};
        handRemoval.enabled = config.removeHands ? XR_TRUE : XR_FALSE;
        // Not fatal: an unsupported request leaves hands in the depth map.
        CheckXr(
            impl_->instance,
            impl_->setHandRemoval(impl_->provider, &handRemoval),
            "xrSetEnvironmentDepthHandRemovalMETA");
    }
    impl_->started = true;
    impl_->SetError("", DepthHealth::Running);
    return true;
}

bool MetaEnvironmentDepthAdapter::AcquireForFrame(
    int64_t predictedDisplayTime) {
    if (!impl_->started || impl_->copyPending) {
        // A copy still in flight means the consumer has not kept up. Skipping
        // is correct: the newest frame wins and the old one is counted.
        if (impl_->copyPending) {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            ++impl_->stats.overwrittenFrames;
        }
        return false;
    }

    XrEnvironmentDepthImageAcquireInfoMETA acquireInfo{
        XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META};
    acquireInfo.space = impl_->space;
    acquireInfo.displayTime = static_cast<XrTime>(predictedDisplayTime);
    // The runtime writes into views[], but the application still owns their
    // type tags. The spec requires every accessed array element to carry the
    // expected type, and a zero tag yields XR_ERROR_VALIDATION_FAILURE on
    // every acquire with no other symptom: the provider starts, reports a
    // resolution, and simply never hands over an image.
    XrEnvironmentDepthImageMETA image{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_META};
    for (XrEnvironmentDepthImageViewMETA& view : image.views) {
        view.type = XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META;
        view.next = nullptr;
    }
    const XrResult result =
        impl_->acquireImage(impl_->provider, &acquireInfo, &image);
    if (result != XR_SUCCESS) {
        // Not every frame has depth available, so this is not fatal. It is
        // still reported: a provider that starts but never yields an image is
        // otherwise indistinguishable from one that is simply idle.
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->stats.rejectedFrames;
        impl_->stats.lastError =
            "xrAcquireEnvironmentDepthImageMETA: " +
            XrResultName(impl_->instance, result);
        return false;
    }
    if (image.swapchainIndex >= impl_->images.size()) {
        impl_->SetError("Depth swapchain index is out of range",
                        DepthHealth::Error);
        return false;
    }

    static_assert(
        kLeftEyeLayer <
            sizeof(XrEnvironmentDepthImageMETA::views) /
                sizeof(XrEnvironmentDepthImageMETA::views[0]),
        "The selected eye layer must exist in the acquired image");
    const XrEnvironmentDepthImageViewMETA& view = image.views[kLeftEyeLayer];
    impl_->pendingView.projection.nearZ = image.nearZ;
    impl_->pendingView.projection.farZ = image.farZ;
    impl_->pendingView.projection.angleLeft = view.fov.angleLeft;
    impl_->pendingView.projection.angleRight = view.fov.angleRight;
    impl_->pendingView.projection.angleUp = view.fov.angleUp;
    impl_->pendingView.projection.angleDown = view.fov.angleDown;
    impl_->pendingView.poseInSpace = PoseFromXr(view.pose);
    impl_->pendingDisplayTime = predictedDisplayTime;

    // Record the copy. The wait happens in TryConsumeLatest so the frame loop
    // never blocks on the GPU.
    vkResetFences(impl_->device.device, 1, &impl_->fence);
    vkResetCommandBuffer(impl_->commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(impl_->commandBuffer, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = kAcquiredLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = impl_->images[image.swapchainIndex];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = kLeftEyeLayer;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(
        impl_->commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = kLeftEyeLayer;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {
        static_cast<uint32_t>(impl_->capabilities.width),
        static_cast<uint32_t>(impl_->capabilities.height),
        1,
    };
    vkCmdCopyImageToBuffer(
        impl_->commandBuffer,
        impl_->images[image.swapchainIndex],
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        impl_->staging,
        1,
        &region);

    VkImageMemoryBarrier toRead = toTransfer;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toRead.newLayout = kAcquiredLayout;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        impl_->commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toRead);

    if (vkEndCommandBuffer(impl_->commandBuffer) != VK_SUCCESS) {
        return false;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->commandBuffer;
    if (vkQueueSubmit(impl_->device.queue, 1, &submit, impl_->fence) !=
        VK_SUCCESS) {
        impl_->SetError("Depth readback submit failed", DepthHealth::Error);
        return false;
    }

    impl_->copyPending = true;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->stats.acquiredFrames;
        impl_->stats.currentQueueDepth = 1;
        impl_->stats.queueHighWaterMark =
            std::max<uint64_t>(impl_->stats.queueHighWaterMark, 1U);
    }
    return true;
}

bool MetaEnvironmentDepthAdapter::TryConsumeLatest(DepthCapture* capture) {
    if (capture == nullptr || !impl_->copyPending) {
        return false;
    }
    // Poll rather than block: if the copy is not finished the caller simply
    // tries again next frame.
    const VkResult status =
        vkWaitForFences(impl_->device.device, 1, &impl_->fence, VK_TRUE, 0);
    if (status == VK_TIMEOUT) {
        return false;
    }
    if (status != VK_SUCCESS) {
        impl_->copyPending = false;
        impl_->SetError("Depth readback fence failed", DepthHealth::Error);
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(
            impl_->device.device,
            impl_->stagingMemory,
            0,
            impl_->stagingSize,
            0,
            &mapped) != VK_SUCCESS) {
        impl_->copyPending = false;
        impl_->SetError("Cannot map the depth staging buffer",
                        DepthHealth::Error);
        return false;
    }

    const size_t texelCount =
        static_cast<size_t>(impl_->capabilities.width) *
        static_cast<size_t>(impl_->capabilities.height);
    capture->normalizedDepth.resize(texelCount);
    // Assumes D16_UNORM, per the note at the top of this file. A wrong
    // assumption here yields plausible-looking depth, so the first device run
    // must sanity-check a known distance before any result is trusted.
    const auto* raw = static_cast<const DepthTexel*>(mapped);
    constexpr float kInverseMax = 1.0F / kDepthTexelMaximum;
    for (size_t index = 0; index < texelCount; ++index) {
        capture->normalizedDepth[index] =
            static_cast<float>(raw[index]) * kInverseMax;
    }
    vkUnmapMemory(impl_->device.device, impl_->stagingMemory);

    capture->frameId = impl_->nextFrameId++;
    capture->displayTimeNanoseconds = impl_->pendingDisplayTime;
    capture->width = impl_->capabilities.width;
    capture->height = impl_->capabilities.height;
    capture->view = impl_->pendingView;
    impl_->copyPending = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->stats.consumedFrames;
        impl_->stats.currentQueueDepth = 0;
    }
    return true;
}

DepthSourceStats MetaEnvironmentDepthAdapter::GetStats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

void MetaEnvironmentDepthAdapter::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    // Reverse dependency order, and never destroy while the GPU may still be
    // reading the staging buffer.
    if (impl_->device.device != VK_NULL_HANDLE && impl_->copyPending &&
        impl_->fence != VK_NULL_HANDLE) {
        vkWaitForFences(
            impl_->device.device, 1, &impl_->fence, VK_TRUE, 1'000'000'000ULL);
        impl_->copyPending = false;
    }
    if (impl_->started && impl_->stopProvider != nullptr &&
        impl_->provider != XR_NULL_HANDLE) {
        impl_->stopProvider(impl_->provider);
        impl_->started = false;
    }
    if (impl_->device.device != VK_NULL_HANDLE) {
        if (impl_->fence != VK_NULL_HANDLE) {
            vkDestroyFence(impl_->device.device, impl_->fence, nullptr);
            impl_->fence = VK_NULL_HANDLE;
        }
        if (impl_->commandPool != VK_NULL_HANDLE) {
            // Frees the command buffer with the pool.
            vkDestroyCommandPool(
                impl_->device.device, impl_->commandPool, nullptr);
            impl_->commandPool = VK_NULL_HANDLE;
            impl_->commandBuffer = VK_NULL_HANDLE;
        }
        if (impl_->staging != VK_NULL_HANDLE) {
            vkDestroyBuffer(impl_->device.device, impl_->staging, nullptr);
            impl_->staging = VK_NULL_HANDLE;
        }
        if (impl_->stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(impl_->device.device, impl_->stagingMemory, nullptr);
            impl_->stagingMemory = VK_NULL_HANDLE;
        }
    }
    if (impl_->swapchain != XR_NULL_HANDLE &&
        impl_->destroySwapchain != nullptr) {
        impl_->destroySwapchain(impl_->swapchain);
        impl_->swapchain = XR_NULL_HANDLE;
    }
    if (impl_->provider != XR_NULL_HANDLE &&
        impl_->destroyProvider != nullptr) {
        impl_->destroyProvider(impl_->provider);
        impl_->provider = XR_NULL_HANDLE;
    }
    impl_->images.clear();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stats.health = DepthHealth::Stopped;
        impl_->stats.currentQueueDepth = 0;
    }
}

std::unique_ptr<IDepthSource> CreateDepthSource(
    const DepthSourceConfig& config,
    XrInstance instance,
    XrSession session,
    XrSpace referenceSpace,
    const VulkanDeviceContext& deviceContext,
    std::string* error) {
    switch (config.kind) {
        case DepthSourceKind::MetaEnvironmentDepth: {
            auto adapter = std::make_unique<MetaEnvironmentDepthAdapter>();
            if (!adapter->Initialize(
                    instance, session, referenceSpace, deviceContext)) {
                if (error != nullptr) {
                    *error = adapter->GetStats().lastError;
                }
                return nullptr;
            }
            return adapter;
        }
        case DepthSourceKind::Replay:
            if (error != nullptr) {
                *error = "Replay depth source is not implemented yet";
            }
            return nullptr;
    }
    if (error != nullptr) {
        *error = "Unknown depth source kind";
    }
    return nullptr;
}

}  // namespace questlab::depth
