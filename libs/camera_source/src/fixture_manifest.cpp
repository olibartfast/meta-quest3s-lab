#include "camera_source/fixture_manifest.h"

#include "artifact_integrity/sha256.h"

#include <array>
#include <fstream>
#include <sstream>
#include <utility>

namespace questlab::camera {
namespace {

bool ReadBinaryFile(
    const std::filesystem::path& path,
    std::vector<uint8_t>* bytes) {
    if (bytes == nullptr) {
        return false;
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return false;
    }
    const std::streamsize length = stream.tellg();
    if (length < 0) {
        return false;
    }
    bytes->resize(static_cast<size_t>(length));
    stream.seekg(0);
    if (length == 0) {
        return true;
    }
    return stream.read(
        reinterpret_cast<char*>(bytes->data()), length).good();
}

bool IsSafeRelativeFile(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_parent_path()) {
        return false;
    }
    return path.filename() == path;
}

bool ValidatePlaneGeometry(const RgbCapture& capture, std::string* error) {
    if (capture.width <= 0 || capture.height <= 0) {
        if (error != nullptr) {
            *error = "Fixture dimensions must be positive";
        }
        return false;
    }
    for (size_t index = 0; index < capture.planes.size(); ++index) {
        const ImagePlane& plane = capture.planes[index];
        const int32_t planeWidth = index == 0
            ? capture.width
            : (capture.width + 1) / 2;
        const int32_t planeHeight = index == 0
            ? capture.height
            : (capture.height + 1) / 2;
        if (plane.rowStride <= 0 || plane.pixelStride <= 0 ||
            planeWidth <= 0 || planeHeight <= 0) {
            if (error != nullptr) {
                *error = "Fixture plane strides are invalid";
            }
            return false;
        }
        const size_t lastByte =
            static_cast<size_t>(planeHeight - 1) *
                static_cast<size_t>(plane.rowStride) +
            static_cast<size_t>(planeWidth - 1) *
                static_cast<size_t>(plane.pixelStride);
        if (lastByte >= plane.bytes.size()) {
            if (error != nullptr) {
                *error = "Fixture plane is shorter than its declared geometry";
            }
            return false;
        }
    }
    return true;
}

void UpdateText(
    questlab::integrity::Sha256* digest,
    const std::string& value) {
    digest->Update(value.data(), value.size());
}

}  // namespace

std::string ComputeQuestCameraPixelSha256(const RgbCapture& capture) {
    questlab::integrity::Sha256 digest;
    UpdateText(&digest, "QUEST_CAMERA_PIXEL_SHA256_V2\n");
    std::ostringstream geometry;
    geometry << capture.width << ' ' << capture.height;
    for (const ImagePlane& plane : capture.planes) {
        geometry << ' ' << plane.rowStride << ' ' << plane.pixelStride;
    }
    geometry << '\n';
    UpdateText(&digest, geometry.str());
    for (const ImagePlane& plane : capture.planes) {
        UpdateText(&digest, std::to_string(plane.bytes.size()) + "\n");
        if (!plane.bytes.empty()) {
            digest.Update(plane.bytes.data(), plane.bytes.size());
        }
        UpdateText(&digest, "\n");
    }
    return digest.FinalizeHex();
}

bool LoadQuestCameraFixture(
    const std::filesystem::path& manifestPath,
    RgbCapture* capture,
    std::string* error) {
    if (capture == nullptr) {
        if (error != nullptr) {
            *error = "Fixture output pointer is null";
        }
        return false;
    }

    std::ifstream manifest(manifestPath);
    if (!manifest) {
        if (error != nullptr) {
            *error = "Cannot open replay manifest: " + manifestPath.string();
        }
        return false;
    }

    RgbCapture loaded;
    std::array<std::filesystem::path, 3> planeFiles;
    std::string headerLine;
    if (!std::getline(manifest, headerLine)) {
        if (error != nullptr) {
            *error = "Replay manifest is empty";
        }
        return false;
    }
    std::istringstream header(headerLine);
    std::string magic;
    header >> magic >> loaded.width >> loaded.height
           >> loaded.planes[0].rowStride
           >> loaded.planes[0].pixelStride
           >> loaded.planes[1].rowStride
           >> loaded.planes[1].pixelStride
           >> loaded.planes[2].rowStride
           >> loaded.planes[2].pixelStride
           >> planeFiles[0] >> planeFiles[1] >> planeFiles[2];
    std::string unexpectedHeaderToken;
    if (!header || magic != kQuestCameraFixtureVersion ||
        (header >> unexpectedHeaderToken)) {
        if (error != nullptr) {
            *error = "Replay manifest is invalid or unsupported; expected " +
                     std::string(kQuestCameraFixtureVersion);
        }
        return false;
    }
    for (const std::filesystem::path& path : planeFiles) {
        if (!IsSafeRelativeFile(path)) {
            if (error != nullptr) {
                *error = "Fixture plane paths must be plain relative filenames";
            }
            return false;
        }
    }

    std::string expectedPixelSha256;
    std::string metadataLine;
    while (std::getline(manifest, metadataLine)) {
        if (metadataLine.empty()) {
            continue;
        }
        std::istringstream metadata(metadataLine);
        std::string key;
        metadata >> key;
        if (key == "pixel_sha256") {
            if (!expectedPixelSha256.empty()) {
                if (error != nullptr) {
                    *error = "Fixture contains duplicate pixel_sha256 metadata";
                }
                return false;
            }
            metadata >> expectedPixelSha256;
        } else if (key == "sensor_timestamp_ns") {
            metadata >> loaded.sensorTimestampNanoseconds;
        } else if (key == "intrinsics") {
            metadata >> loaded.intrinsics.fx
                     >> loaded.intrinsics.fy
                     >> loaded.intrinsics.cx
                     >> loaded.intrinsics.cy
                     >> loaded.intrinsics.skew;
            loaded.intrinsics.valid = metadata.good() || metadata.eof();
        } else if (key == "distortion") {
            for (float& value : loaded.intrinsics.distortion) {
                metadata >> value;
            }
        } else if (key == "camera_from_head") {
            for (float& value : loaded.cameraFromHead.orientation) {
                metadata >> value;
            }
            for (float& value : loaded.cameraFromHead.position) {
                metadata >> value;
            }
            loaded.cameraFromHead.valid = metadata.good() || metadata.eof();
        } else {
            if (error != nullptr) {
                *error = "Unknown fixture metadata key: " + key;
            }
            return false;
        }
        std::string extraToken;
        if (!metadata || (metadata >> extraToken)) {
            if (error != nullptr) {
                *error = "Malformed fixture metadata line: " + metadataLine;
            }
            return false;
        }
    }
    if (!questlab::integrity::IsLowercaseSha256(expectedPixelSha256)) {
        if (error != nullptr) {
            *error = "Fixture requires one lowercase pixel_sha256 value";
        }
        return false;
    }

    const std::filesystem::path directory = manifestPath.parent_path();
    for (size_t index = 0; index < planeFiles.size(); ++index) {
        if (!ReadBinaryFile(
                directory / planeFiles[index],
                &loaded.planes[index].bytes)) {
            if (error != nullptr) {
                *error = "Replay plane file is missing or unreadable: " +
                         planeFiles[index].string();
            }
            return false;
        }
    }
    loaded.format = PixelFormat::Yuv420888;
    if (!ValidatePlaneGeometry(loaded, error)) {
        return false;
    }
    const std::string actualPixelSha256 =
        ComputeQuestCameraPixelSha256(loaded);
    if (actualPixelSha256 != expectedPixelSha256) {
        if (error != nullptr) {
            *error = "Fixture pixel checksum mismatch: expected " +
                     expectedPixelSha256 + ", actual " + actualPixelSha256;
        }
        return false;
    }
    *capture = std::move(loaded);
    return true;
}

}  // namespace questlab::camera
