#include "camera_source/yuv_converter.h"

#include <algorithm>
#include <cstddef>

namespace questlab::camera {
namespace {

uint8_t ClampToByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool PlaneContains(
    const ImagePlane& plane,
    int32_t x,
    int32_t y) {
    if (plane.rowStride <= 0 || plane.pixelStride <= 0 ||
        x < 0 || y < 0) {
        return false;
    }
    const size_t offset =
        static_cast<size_t>(y) * static_cast<size_t>(plane.rowStride) +
        static_cast<size_t>(x) * static_cast<size_t>(plane.pixelStride);
    return offset < plane.bytes.size();
}

uint8_t ReadPlane(const ImagePlane& plane, int32_t x, int32_t y) {
    const size_t offset =
        static_cast<size_t>(y) * static_cast<size_t>(plane.rowStride) +
        static_cast<size_t>(x) * static_cast<size_t>(plane.pixelStride);
    return plane.bytes[offset];
}

}  // namespace

bool ConvertYuv420ToRgba(
    const RgbCapture& capture,
    std::vector<uint8_t>* rgba) {
    if (rgba == nullptr || capture.width <= 0 || capture.height <= 0) {
        return false;
    }
    if (capture.format == PixelFormat::Rgba8888) {
        const ImagePlane& plane = capture.planes[0];
        if (plane.pixelStride < 4 || plane.rowStride <= 0 ||
            !PlaneContains(plane, capture.width - 1, capture.height - 1) ||
            static_cast<size_t>(capture.height - 1) *
                    static_cast<size_t>(plane.rowStride) +
                static_cast<size_t>(capture.width - 1) *
                    static_cast<size_t>(plane.pixelStride) + 3U >=
                plane.bytes.size()) {
            return false;
        }
        rgba->resize(
            static_cast<size_t>(capture.width) *
            static_cast<size_t>(capture.height) * 4U);
        for (int32_t y = 0; y < capture.height; ++y) {
            for (int32_t x = 0; x < capture.width; ++x) {
                const size_t source =
                    static_cast<size_t>(y) *
                        static_cast<size_t>(plane.rowStride) +
                    static_cast<size_t>(x) *
                        static_cast<size_t>(plane.pixelStride);
                const size_t destination =
                    (static_cast<size_t>(y) *
                         static_cast<size_t>(capture.width) +
                     static_cast<size_t>(x)) * 4U;
                std::copy_n(
                    plane.bytes.begin() + source,
                    4,
                    rgba->begin() + destination);
            }
        }
        return true;
    }
    if (capture.format != PixelFormat::Yuv420888) {
        return false;
    }
    const ImagePlane& yPlane = capture.planes[0];
    const ImagePlane& uPlane = capture.planes[1];
    const ImagePlane& vPlane = capture.planes[2];
    if (!PlaneContains(yPlane, capture.width - 1, capture.height - 1) ||
        !PlaneContains(
            uPlane, (capture.width - 1) / 2, (capture.height - 1) / 2) ||
        !PlaneContains(
            vPlane, (capture.width - 1) / 2, (capture.height - 1) / 2)) {
        return false;
    }

    rgba->resize(
        static_cast<size_t>(capture.width) *
        static_cast<size_t>(capture.height) * 4U);
    for (int32_t y = 0; y < capture.height; ++y) {
        for (int32_t x = 0; x < capture.width; ++x) {
            const int luminance =
                static_cast<int>(ReadPlane(yPlane, x, y)) - 16;
            const int chromaU =
                static_cast<int>(ReadPlane(uPlane, x / 2, y / 2)) - 128;
            const int chromaV =
                static_cast<int>(ReadPlane(vPlane, x / 2, y / 2)) - 128;
            const int scaledY = 298 * std::max(luminance, 0);
            const size_t output =
                (static_cast<size_t>(y) *
                    static_cast<size_t>(capture.width) +
                 static_cast<size_t>(x)) * 4U;
            (*rgba)[output] =
                ClampToByte((scaledY + 409 * chromaV + 128) >> 8);
            (*rgba)[output + 1] =
                ClampToByte(
                    (scaledY - 100 * chromaU - 208 * chromaV + 128) >> 8);
            (*rgba)[output + 2] =
                ClampToByte((scaledY + 516 * chromaU + 128) >> 8);
            (*rgba)[output + 3] = 255;
        }
    }
    return true;
}

}  // namespace questlab::camera
