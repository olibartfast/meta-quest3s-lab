#include "camera_source/camera_source.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/onnx_session.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/preprocessing.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace {

constexpr size_t kIdentityBytes = 64;
constexpr uint32_t kMaximumDimension = 8192;

struct Options {
    std::filesystem::path manifestPath;
    std::filesystem::path modelPath;
    uint16_t port = 48110;
};

int64_t SteadyNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool ParseOptions(int argc, char** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--manifest" || argument == "--model" ||
             argument == "--port") && index + 1 >= argc) {
            return false;
        }
        if (argument == "--manifest") {
            options->manifestPath = argv[++index];
        } else if (argument == "--model") {
            options->modelPath = argv[++index];
        } else if (argument == "--port") {
            try {
                const unsigned long parsed = std::stoul(argv[++index]);
                if (parsed == 0 || parsed > 65535) {
                    return false;
                }
                options->port = static_cast<uint16_t>(parsed);
            } catch (const std::exception&) {
                return false;
            }
        } else {
            return false;
        }
    }
    return !options->manifestPath.empty() && !options->modelPath.empty();
}

uint64_t HostToNetwork64(uint64_t value) {
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32U));
    const uint32_t low = htonl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32U) | high;
}

uint64_t NetworkToHost64(uint64_t value) {
    const uint32_t high = ntohl(static_cast<uint32_t>(value));
    const uint32_t low = ntohl(static_cast<uint32_t>(value >> 32U));
    return (static_cast<uint64_t>(high) << 32U) | low;
}

bool SendAll(int socket, const void* bytes, size_t size) {
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t sent = send(socket, cursor, size, 0);
        if (sent <= 0) return false;
        cursor += static_cast<size_t>(sent);
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool ReceiveAll(int socket, void* bytes, size_t size) {
    auto* cursor = static_cast<uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t received = recv(socket, cursor, size, 0);
        if (received <= 0) return false;
        cursor += static_cast<size_t>(received);
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool SendU32(int socket, uint32_t value) {
    value = htonl(value);
    return SendAll(socket, &value, sizeof(value));
}

bool ReceiveU32(int socket, uint32_t* value) {
    uint32_t wire = 0;
    if (!ReceiveAll(socket, &wire, sizeof(wire))) return false;
    *value = ntohl(wire);
    return true;
}

bool SendU64(int socket, uint64_t value) {
    value = HostToNetwork64(value);
    return SendAll(socket, &value, sizeof(value));
}

bool ReceiveU64(int socket, uint64_t* value) {
    uint64_t wire = 0;
    if (!ReceiveAll(socket, &wire, sizeof(wire))) return false;
    *value = NetworkToHost64(wire);
    return true;
}

bool SendFloat(int socket, float value) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return SendU32(socket, bits);
}

bool ServeClient(
    int client,
    const questlab::rfdetr::ModelContract& contract,
    questlab::rfdetr::OnnxSession* session) {
    std::array<char, 4> magic{};
    std::array<char, kIdentityBytes> clientIdentity{};
    if (!ReceiveAll(client, magic.data(), magic.size()) ||
        magic != std::array<char, 4>{'Q', 'L', 'D', '1'} ||
        !ReceiveAll(
            client, clientIdentity.data(), clientIdentity.size())) {
        return false;
    }
    std::array<char, kIdentityBytes> serverIdentity{};
    std::copy(
        contract.onnxSha256.begin(),
        contract.onnxSha256.end(),
        serverIdentity.begin());
    const bool identityMatches = clientIdentity == serverIdentity;
    const char replyMagic[4] = {
        identityMatches ? 'Q' : 'E',
        identityMatches ? 'L' : 'R',
        identityMatches ? 'D' : 'R',
        '1',
    };
    if (!SendAll(client, replyMagic, sizeof(replyMagic)) ||
        !SendAll(client, serverIdentity.data(), serverIdentity.size()) ||
        !identityMatches) {
        return false;
    }

    while (true) {
        std::array<char, 4> frameMagic{};
        uint64_t frameId = 0;
        uint64_t captureTimestamp = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t byteCount = 0;
        if (!ReceiveAll(client, frameMagic.data(), frameMagic.size())) {
            return true;
        }
        if (frameMagic != std::array<char, 4>{'F', 'R', 'M', '1'} ||
            !ReceiveU64(client, &frameId) ||
            !ReceiveU64(client, &captureTimestamp) ||
            !ReceiveU32(client, &width) ||
            !ReceiveU32(client, &height) ||
            !ReceiveU32(client, &byteCount) || width == 0 || height == 0 ||
            width > kMaximumDimension || height > kMaximumDimension ||
            static_cast<uint64_t>(width) * height * 4U != byteCount) {
            return false;
        }
        questlab::camera::RgbCapture capture;
        capture.frameId = frameId;
        capture.sensorTimestampNanoseconds =
            static_cast<int64_t>(captureTimestamp);
        capture.width = static_cast<int32_t>(width);
        capture.height = static_cast<int32_t>(height);
        capture.format = questlab::camera::PixelFormat::Rgba8888;
        capture.planes[0].bytes.resize(byteCount);
        capture.planes[0].rowStride = static_cast<int32_t>(width * 4U);
        capture.planes[0].pixelStride = 4;
        if (!ReceiveAll(
                client,
                capture.planes[0].bytes.data(),
                capture.planes[0].bytes.size())) {
            return false;
        }

        const int64_t inferenceStart = SteadyNanoseconds();
        std::vector<uint8_t> rgba;
        std::vector<float> input;
        std::vector<float> boxes;
        std::vector<float> logits;
        std::vector<questlab::rfdetr::Detection> detections;
        std::string error;
        if (!questlab::rfdetr::PreprocessCapture(
                capture, contract, &rgba, &input, &error) ||
            !session->Run(input, &boxes, &logits, &error) ||
            !questlab::rfdetr::PostprocessDetections(
                boxes,
                logits,
                capture.width,
                capture.height,
                contract,
                &detections,
                &error)) {
            std::cerr << "Frame " << frameId << " failed: " << error << '\n';
            return false;
        }
        const int64_t inferenceEnd = SteadyNanoseconds();
        const char detectionMagic[4] = {'D', 'E', 'T', '1'};
        if (!SendAll(client, detectionMagic, sizeof(detectionMagic)) ||
            !SendU64(client, frameId) ||
            !SendU64(client, static_cast<uint64_t>(inferenceStart)) ||
            !SendU64(client, static_cast<uint64_t>(inferenceEnd)) ||
            !SendU32(client, static_cast<uint32_t>(detections.size()))) {
            return false;
        }
        for (const auto& detection : detections) {
            if (!SendU32(client, static_cast<uint32_t>(detection.classId)) ||
                !SendFloat(client, detection.confidence)) {
                return false;
            }
            for (float coordinate : detection.boxXyxy) {
                if (!SendFloat(client, coordinate)) return false;
            }
            if (!SendU32(
                    client,
                    static_cast<uint32_t>(detection.className.size())) ||
                !SendAll(
                    client,
                    detection.className.data(),
                    detection.className.size())) {
                return false;
            }
        }
        std::cout << "frame=" << frameId
                  << " detections=" << detections.size()
                  << " inference_ms="
                  << static_cast<double>(inferenceEnd - inferenceStart) / 1.0e6
                  << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        std::cerr << "Usage: rfdetr_service --manifest PATH --model PATH "
                     "[--port 48110]\n";
        return 2;
    }
    questlab::rfdetr::ModelContract contract;
    std::string error;
    if (!questlab::rfdetr::LoadModelContract(
            options.manifestPath, &contract, &error)) {
        std::cerr << "Manifest rejected: " << error << '\n';
        return 1;
    }
    questlab::rfdetr::OnnxSession session;
    if (!session.Initialize(options.modelPath, contract, &error)) {
        std::cerr << "ONNX Runtime initialization failed: " << error << '\n';
        return 1;
    }
    const size_t inputCount = std::accumulate(
        contract.input.shape.begin(),
        contract.input.shape.end(),
        size_t{1},
        [](size_t product, int64_t dimension) {
            return product * static_cast<size_t>(dimension);
        });
    std::vector<float> warmupInput(inputCount, 0.0F);
    std::vector<float> warmupBoxes;
    std::vector<float> warmupLogits;
    if (!session.Run(
            warmupInput, &warmupBoxes, &warmupLogits, &error)) {
        std::cerr << "Warm-up failed: " << error << '\n';
        return 1;
    }

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "Cannot create service socket\n";
        return 1;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(options.port);
    if (bind(
            server,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0 ||
        listen(server, 1) != 0) {
        std::cerr << "Cannot bind service to 127.0.0.1:"
                  << options.port << '\n';
        close(server);
        return 1;
    }
    std::cout << "RF-DETR service ready on 127.0.0.1:" << options.port
              << " manifest=" << contract.onnxSha256 << '\n';
    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            break;
        }
        std::cout << "client connected\n";
        if (!ServeClient(client, contract, &session)) {
            std::cerr << "client session rejected or failed\n";
        }
        close(client);
    }
    close(server);
    return 0;
}
