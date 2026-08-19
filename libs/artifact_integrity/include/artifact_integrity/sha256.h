#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace questlab::integrity {

class Sha256 {
public:
    Sha256();

    void Update(const void* data, size_t size);
    std::array<uint8_t, 32> Finalize();
    std::string FinalizeHex();

private:
    void Transform(const uint8_t* block);

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t totalBytes_ = 0;
    size_t bufferSize_ = 0;
    bool finalized_ = false;
};

std::string Sha256Hex(const void* data, size_t size);

bool Sha256File(
    const std::filesystem::path& path,
    std::string* digest,
    std::string* error);

bool IsLowercaseSha256(const std::string& value);

}  // namespace questlab::integrity
