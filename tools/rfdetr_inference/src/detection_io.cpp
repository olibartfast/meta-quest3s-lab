#include "rfdetr_inference/detection_io.h"

#include "artifact_integrity/sha256.h"
#include "json_value.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace questlab::rfdetr {
namespace {

std::string EscapeJson(const std::string& input) {
    std::ostringstream output;
    for (unsigned char character : input) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0')
                           << static_cast<unsigned int>(character)
                           << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

void WriteTiming(std::ostream& output, const TimingSummary& timing) {
    output << "{\"count\":" << timing.count
           << ",\"mean_ms\":" << timing.meanMilliseconds
           << ",\"p50_ms\":" << timing.p50Milliseconds
           << ",\"p95_ms\":" << timing.p95Milliseconds
           << ",\"p99_ms\":" << timing.p99Milliseconds
           << ",\"maximum_ms\":" << timing.maximumMilliseconds
           << '}';
}

std::array<uint8_t, 7> Glyph(char character) {
    switch (character) {
        case 'A': return {14, 17, 17, 31, 17, 17, 17};
        case 'B': return {30, 17, 17, 30, 17, 17, 30};
        case 'C': return {14, 17, 16, 16, 16, 17, 14};
        case 'D': return {30, 17, 17, 17, 17, 17, 30};
        case 'E': return {31, 16, 16, 30, 16, 16, 31};
        case 'F': return {31, 16, 16, 30, 16, 16, 16};
        case 'G': return {14, 17, 16, 23, 17, 17, 15};
        case 'H': return {17, 17, 17, 31, 17, 17, 17};
        case 'I': return {14, 4, 4, 4, 4, 4, 14};
        case 'J': return {1, 1, 1, 1, 17, 17, 14};
        case 'K': return {17, 18, 20, 24, 20, 18, 17};
        case 'L': return {16, 16, 16, 16, 16, 16, 31};
        case 'M': return {17, 27, 21, 21, 17, 17, 17};
        case 'N': return {17, 25, 21, 19, 17, 17, 17};
        case 'O': return {14, 17, 17, 17, 17, 17, 14};
        case 'P': return {30, 17, 17, 30, 16, 16, 16};
        case 'Q': return {14, 17, 17, 17, 21, 18, 13};
        case 'R': return {30, 17, 17, 30, 20, 18, 17};
        case 'S': return {15, 16, 16, 14, 1, 1, 30};
        case 'T': return {31, 4, 4, 4, 4, 4, 4};
        case 'U': return {17, 17, 17, 17, 17, 17, 14};
        case 'V': return {17, 17, 17, 17, 17, 10, 4};
        case 'W': return {17, 17, 17, 21, 21, 21, 10};
        case 'X': return {17, 17, 10, 4, 10, 17, 17};
        case 'Y': return {17, 17, 10, 4, 4, 4, 4};
        case 'Z': return {31, 1, 2, 4, 8, 16, 31};
        case '0': return {14, 17, 19, 21, 25, 17, 14};
        case '1': return {4, 12, 4, 4, 4, 4, 14};
        case '2': return {14, 17, 1, 2, 4, 8, 31};
        case '3': return {30, 1, 1, 14, 1, 1, 30};
        case '4': return {2, 6, 10, 18, 31, 2, 2};
        case '5': return {31, 16, 16, 30, 1, 1, 30};
        case '6': return {14, 16, 16, 30, 17, 17, 14};
        case '7': return {31, 1, 2, 4, 8, 8, 8};
        case '8': return {14, 17, 17, 14, 17, 17, 14};
        case '9': return {14, 17, 17, 15, 1, 1, 14};
        case '-': return {0, 0, 0, 31, 0, 0, 0};
        default: return {};
    }
}

void SetPixel(
    std::vector<uint8_t>* pixels,
    int32_t width,
    int32_t height,
    int32_t x,
    int32_t y,
    size_t channelCount,
    const std::array<uint8_t, 3>& color) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    const size_t offset =
        (static_cast<size_t>(y) * static_cast<size_t>(width) +
         static_cast<size_t>(x)) * 3U;
    const size_t correctedOffset =
        offset / 3U * channelCount;
    std::copy(
        color.begin(), color.end(), pixels->begin() + correctedOffset);
}

void DrawText(
    std::vector<uint8_t>* pixels,
    int32_t width,
    int32_t height,
    int32_t originX,
    int32_t originY,
    const std::string& text,
    size_t channelCount,
    const std::array<uint8_t, 3>& color) {
    int32_t x = originX;
    for (unsigned char rawCharacter : text) {
        const char character = rawCharacter >= 'a' && rawCharacter <= 'z'
            ? static_cast<char>(rawCharacter - 'a' + 'A')
            : static_cast<char>(rawCharacter);
        const std::array<uint8_t, 7> glyph = Glyph(character);
        for (int32_t row = 0; row < 7; ++row) {
            for (int32_t column = 0; column < 5; ++column) {
                if ((glyph[static_cast<size_t>(row)] &
                     (1U << static_cast<unsigned int>(4 - column))) != 0U) {
                    SetPixel(
                        pixels,
                        width,
                        height,
                        x + column,
                        originY + row,
                        channelCount,
                        color);
                }
            }
        }
        x += 6;
        if (x >= width - 5) {
            break;
        }
    }
}

void DrawDetection(
    std::vector<uint8_t>* pixels,
    int32_t width,
    int32_t height,
    const Detection& detection,
    size_t channelCount,
    const std::array<uint8_t, 3>& color) {
    const int32_t x0 = std::clamp(
        static_cast<int32_t>(std::lround(detection.boxXyxy[0])), 0, width - 1);
    const int32_t y0 = std::clamp(
        static_cast<int32_t>(std::lround(detection.boxXyxy[1])), 0, height - 1);
    const int32_t x1 = std::clamp(
        static_cast<int32_t>(std::lround(detection.boxXyxy[2])), 0, width - 1);
    const int32_t y1 = std::clamp(
        static_cast<int32_t>(std::lround(detection.boxXyxy[3])), 0, height - 1);
    for (int32_t thickness = 0; thickness < 2; ++thickness) {
        for (int32_t x = x0; x <= x1; ++x) {
            SetPixel(
                pixels, width, height, x, y0 + thickness, channelCount, color);
            SetPixel(
                pixels, width, height, x, y1 - thickness, channelCount, color);
        }
        for (int32_t y = y0; y <= y1; ++y) {
            SetPixel(
                pixels, width, height, x0 + thickness, y, channelCount, color);
            SetPixel(
                pixels, width, height, x1 - thickness, y, channelCount, color);
        }
    }
    std::ostringstream label;
    label << detection.className << ' '
          << static_cast<int>(std::lround(detection.confidence * 100.0F));
    const int32_t labelY = y0 >= 9 ? y0 - 9 : std::min(y0 + 3, height - 7);
    DrawText(
        pixels,
        width,
        height,
        x0,
        labelY,
        label.str(),
        channelCount,
        color);
}

constexpr std::array<std::array<uint8_t, 3>, 6> kDetectionColors = {{
    {255, 64, 64},
    {64, 255, 64},
    {64, 160, 255},
    {255, 220, 64},
    {255, 64, 220},
    {64, 255, 220},
}};

}  // namespace

bool AnnotateRgba(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<Detection>& detections,
    std::string* error) {
    if (rgba == nullptr || width <= 0 || height <= 0 ||
        rgba->size() != static_cast<size_t>(width) *
                            static_cast<size_t>(height) * 4U) {
        if (error != nullptr) {
            *error = "Annotated RGBA dimensions do not match pixel bytes";
        }
        return false;
    }
    for (size_t index = 0; index < detections.size(); ++index) {
        DrawDetection(
            rgba,
            width,
            height,
            detections[index],
            4U,
            kDetectionColors[index % kDetectionColors.size()]);
    }
    return true;
}

bool AnnotateRgbaStatus(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<std::string>& statusLines,
    std::string* error) {
    if (rgba == nullptr || width <= 0 || height <= 0 ||
        rgba->size() != static_cast<size_t>(width) *
                            static_cast<size_t>(height) * 4U) {
        if (error != nullptr) {
            *error = "Status RGBA dimensions do not match pixel bytes";
        }
        return false;
    }
    constexpr std::array<uint8_t, 3> kStatusColor = {255, 220, 64};
    int32_t y = 4;
    for (const std::string& line : statusLines) {
        DrawText(rgba, width, height, 4, y, line, 4U, kStatusColor);
        y += 9;
        if (y + 7 >= height) {
            break;
        }
    }
    return true;
}

bool WriteRawRgba(
    const std::filesystem::path& path,
    const std::vector<uint8_t>& rgba,
    std::string* error) {
    std::ofstream output(path, std::ios::binary);
    if (!output ||
        (!rgba.empty() &&
         !output.write(
             reinterpret_cast<const char*>(rgba.data()),
             static_cast<std::streamsize>(rgba.size())))) {
        if (error != nullptr) {
            *error = "Cannot write raw RGBA output: " + path.string();
        }
        return false;
    }
    return true;
}

bool WriteAnnotatedPpm(
    const std::filesystem::path& path,
    int32_t width,
    int32_t height,
    const std::vector<uint8_t>& rgba,
    const std::vector<Detection>& detections,
    std::string* error) {
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<size_t>(width) *
                           static_cast<size_t>(height) * 4U) {
        if (error != nullptr) {
            *error = "Annotated preview dimensions do not match RGBA bytes";
        }
        return false;
    }
    std::vector<uint8_t> rgb(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 3U);
    for (size_t pixel = 0; pixel < rgb.size() / 3U; ++pixel) {
        rgb[pixel * 3U] = rgba[pixel * 4U];
        rgb[pixel * 3U + 1U] = rgba[pixel * 4U + 1U];
        rgb[pixel * 3U + 2U] = rgba[pixel * 4U + 2U];
    }
    for (size_t index = 0; index < detections.size(); ++index) {
        DrawDetection(
            &rgb,
            width,
            height,
            detections[index],
            3U,
            kDetectionColors[index % kDetectionColors.size()]);
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        if (error != nullptr) {
            *error = "Cannot open annotated PPM output: " + path.string();
        }
        return false;
    }
    output << "P6\n" << width << ' ' << height << "\n255\n";
    output.write(
        reinterpret_cast<const char*>(rgb.data()),
        static_cast<std::streamsize>(rgb.size()));
    if (!output) {
        if (error != nullptr) {
            *error = "Cannot write annotated PPM output: " + path.string();
        }
        return false;
    }
    return true;
}

bool WriteDetectionJson(
    const std::filesystem::path& path,
    int32_t width,
    int32_t height,
    const std::string& fixturePixelSha256,
    const std::vector<Detection>& detections,
    const TimingSummary& preprocessing,
    const TimingSummary& inference,
    const TimingSummary& postprocessing,
    const ModelContract& contract,
    std::string* error) {
    if (!questlab::integrity::IsLowercaseSha256(fixturePixelSha256)) {
        if (error != nullptr) {
            *error = "Detection output requires a fixture pixel SHA-256";
        }
        return false;
    }
    std::ofstream output(path);
    if (!output) {
        if (error != nullptr) {
            *error = "Cannot open detection JSON output: " + path.string();
        }
        return false;
    }
    output << std::setprecision(9)
           << "{\n  \"schema\": \"QUESTLAB_RFDETR_DETECTIONS_V1\",\n"
           << "  \"frame\": {\"width\": " << width
           << ", \"height\": " << height
           << ", \"pixel_sha256\": \"" << fixturePixelSha256
           << "\"},\n"
           << "  \"model\": {\"onnx_sha256\": \""
           << contract.onnxSha256 << "\"},\n"
           << "  \"detections\": [";
    for (size_t index = 0; index < detections.size(); ++index) {
        const Detection& detection = detections[index];
        output << (index == 0 ? "\n" : ",\n")
               << "    {\"class_id\": " << detection.classId
               << ", \"class_name\": \""
               << EscapeJson(detection.className)
               << "\", \"confidence\": " << detection.confidence
               << ", \"box_xyxy\": ["
               << detection.boxXyxy[0] << ", "
               << detection.boxXyxy[1] << ", "
               << detection.boxXyxy[2] << ", "
               << detection.boxXyxy[3] << "]}";
    }
    if (!detections.empty()) {
        output << '\n';
    }
    output << "  ],\n  \"timing\": {\n    \"preprocessing\": ";
    WriteTiming(output, preprocessing);
    output << ",\n    \"inference\": ";
    WriteTiming(output, inference);
    output << ",\n    \"postprocessing\": ";
    WriteTiming(output, postprocessing);
    output << "\n  },\n  \"runtime\": {\"execution_provider\": \""
           << EscapeJson(contract.executionProvider)
           << "\", \"intra_op_threads\": " << contract.intraOpThreads
           << ", \"inter_op_threads\": " << contract.interOpThreads
           << "}\n}\n";
    if (!output) {
        if (error != nullptr) {
            *error = "Failed while writing detection JSON: " + path.string();
        }
        return false;
    }
    return true;
}

bool LoadDetectionJson(
    const std::filesystem::path& path,
    std::vector<Detection>* detections,
    std::string* error) {
    return LoadDetectionJson(path, detections, nullptr, error);
}

bool LoadDetectionJson(
    const std::filesystem::path& path,
    std::vector<Detection>* detections,
    std::string* manifestIdentity,
    std::string* error) {
    if (detections == nullptr) {
        if (error != nullptr) {
            *error = "Detection JSON output pointer is null";
        }
        return false;
    }
    std::ifstream input(path);
    if (!input) {
        if (error != nullptr) {
            *error = "Cannot open detection JSON: " + path.string();
        }
        return false;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    internal::JsonValue root;
    if (!internal::ParseJson(contents.str(), &root, error)) {
        return false;
    }
    try {
        if (root.At("schema").AsString("schema") !=
            "QUESTLAB_RFDETR_DETECTIONS_V1") {
            throw std::runtime_error("Unsupported detection JSON schema");
        }
        const std::string loadedManifestIdentity =
            root.At("model").At("onnx_sha256").AsString(
                "model.onnx_sha256");
        if (!questlab::integrity::IsLowercaseSha256(
                loadedManifestIdentity)) {
            throw std::runtime_error(
                "Detection JSON model identity is not a SHA-256");
        }
        std::vector<Detection> loaded;
        for (const internal::JsonValue& value :
             root.At("detections").AsArray("detections")) {
            Detection detection;
            const double classId = value.At("class_id").AsNumber("class_id");
            if (std::floor(classId) != classId || classId < 0.0 ||
                classId > 90.0) {
                throw std::runtime_error("Invalid detection class ID");
            }
            detection.classId = static_cast<int32_t>(classId);
            detection.className =
                value.At("class_name").AsString("class_name");
            detection.confidence = static_cast<float>(
                value.At("confidence").AsNumber("confidence"));
            const std::vector<internal::JsonValue>& box =
                value.At("box_xyxy").AsArray("box_xyxy");
            if (box.size() != 4U) {
                throw std::runtime_error("Detection box must have four values");
            }
            for (size_t index = 0; index < box.size(); ++index) {
                detection.boxXyxy[index] = static_cast<float>(
                    box[index].AsNumber("box_xyxy"));
            }
            loaded.push_back(std::move(detection));
        }
        *detections = std::move(loaded);
        if (manifestIdentity != nullptr) {
            *manifestIdentity = loadedManifestIdentity;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

}  // namespace questlab::rfdetr
