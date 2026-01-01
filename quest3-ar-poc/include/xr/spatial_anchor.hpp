#pragma once

#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <unordered_map>

namespace quest3ar {

struct Anchor {
    XrSpace space;
    XrUuidEXT uuid;
    glm::vec3 position;
    glm::quat orientation;
    uint64_t timestamp;
    std::string label;
};

class SpatialAnchorManager {
public:
    SpatialAnchorManager(XrInstance instance, XrSession session);
    ~SpatialAnchorManager();

    bool initialize();
    
    // Anchor CRUD operations
    bool createAnchor(const glm::vec3& position, const glm::quat& orientation, 
                     const std::string& label = "");
    bool removeAnchor(const XrUuidEXT& uuid);
    bool updateAnchor(const XrUuidEXT& uuid, XrSpace baseSpace, XrTime time);
    
    // Persistence
    bool saveAnchors();
    bool loadAnchors();
    
    // Query
    const Anchor* getAnchor(const XrUuidEXT& uuid) const;
    std::vector<const Anchor*> getAnchorsInRadius(const glm::vec3& center, float radius) const;
    
    const std::unordered_map<uint64_t, Anchor>& getAllAnchors() const { return anchors_; }

private:
    XrInstance instance_;
    XrSession session_;
    
    std::unordered_map<uint64_t, Anchor> anchors_;
    
    // Extension function pointers
    PFN_xrCreateSpatialAnchorFB xrCreateSpatialAnchorFB_;
    PFN_xrDestroySpatialAnchorFB xrDestroySpatialAnchorFB_;
    PFN_xrSaveSpaceFB xrSaveSpaceFB_;
    PFN_xrEnumerateSpaceSupportedComponentsFB xrEnumerateSpaceSupportedComponentsFB_;
};

} // namespace quest3ar