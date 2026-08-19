#include "artifact_integrity/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace questlab::integrity {
namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

uint32_t RotateRight(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

uint32_t ReadBigEndian32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24U) |
           (static_cast<uint32_t>(bytes[1]) << 16U) |
           (static_cast<uint32_t>(bytes[2]) << 8U) |
           static_cast<uint32_t>(bytes[3]);
}

void WriteBigEndian32(uint32_t value, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(value >> 24U);
    bytes[1] = static_cast<uint8_t>(value >> 16U);
    bytes[2] = static_cast<uint8_t>(value >> 8U);
    bytes[3] = static_cast<uint8_t>(value);
}

}  // namespace

Sha256::Sha256()
    : state_{
          0x6a09e667U,
          0xbb67ae85U,
          0x3c6ef372U,
          0xa54ff53aU,
          0x510e527fU,
          0x9b05688cU,
          0x1f83d9abU,
          0x5be0cd19U,
      } {}

void Sha256::Update(const void* data, size_t size) {
    if (finalized_) {
        throw std::logic_error("Cannot update a finalized SHA-256 digest");
    }
    if (size == 0) {
        return;
    }
    if (data == nullptr) {
        throw std::invalid_argument("SHA-256 input is null");
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    totalBytes_ += static_cast<uint64_t>(size);
    while (size > 0) {
        const size_t copySize = std::min(size, buffer_.size() - bufferSize_);
        std::copy_n(bytes, copySize, buffer_.begin() + bufferSize_);
        bufferSize_ += copySize;
        bytes += copySize;
        size -= copySize;
        if (bufferSize_ == buffer_.size()) {
            Transform(buffer_.data());
            bufferSize_ = 0;
        }
    }
}

std::array<uint8_t, 32> Sha256::Finalize() {
    if (finalized_) {
        throw std::logic_error("SHA-256 digest was already finalized");
    }
    finalized_ = true;

    const uint64_t totalBits = totalBytes_ * 8U;
    buffer_[bufferSize_++] = 0x80U;
    if (bufferSize_ > 56U) {
        std::fill(buffer_.begin() + bufferSize_, buffer_.end(), 0U);
        Transform(buffer_.data());
        bufferSize_ = 0;
    }
    std::fill(buffer_.begin() + bufferSize_, buffer_.begin() + 56, 0U);
    for (size_t index = 0; index < 8; ++index) {
        buffer_[63U - index] =
            static_cast<uint8_t>(totalBits >> (index * 8U));
    }
    Transform(buffer_.data());

    std::array<uint8_t, 32> digest{};
    for (size_t index = 0; index < state_.size(); ++index) {
        WriteBigEndian32(state_[index], digest.data() + index * 4U);
    }
    return digest;
}

std::string Sha256::FinalizeHex() {
    const std::array<uint8_t, 32> digest = Finalize();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (uint8_t byte : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

void Sha256::Transform(const uint8_t* block) {
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16; ++index) {
        words[index] = ReadBigEndian32(block + index * 4U);
    }
    for (size_t index = 16; index < words.size(); ++index) {
        const uint32_t s0 = RotateRight(words[index - 15], 7U) ^
                            RotateRight(words[index - 15], 18U) ^
                            (words[index - 15] >> 3U);
        const uint32_t s1 = RotateRight(words[index - 2], 17U) ^
                            RotateRight(words[index - 2], 19U) ^
                            (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 +
                       words[index - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t index = 0; index < words.size(); ++index) {
        const uint32_t sum1 = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^
                              RotateRight(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + sum1 + choose +
                               kRoundConstants[index] + words[index];
        const uint32_t sum0 = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^
                              RotateRight(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::string Sha256Hex(const void* data, size_t size) {
    Sha256 digest;
    digest.Update(data, size);
    return digest.FinalizeHex();
}

bool Sha256File(
    const std::filesystem::path& path,
    std::string* digest,
    std::string* error) {
    if (digest == nullptr) {
        if (error != nullptr) {
            *error = "SHA-256 output pointer is null";
        }
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (error != nullptr) {
            *error = "Cannot open file for SHA-256: " + path.string();
        }
        return false;
    }
    Sha256 sha256;
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        if (count > 0) {
            sha256.Update(buffer.data(), static_cast<size_t>(count));
        }
    }
    if (!stream.eof()) {
        if (error != nullptr) {
            *error = "Failed while hashing file: " + path.string();
        }
        return false;
    }
    *digest = sha256.FinalizeHex();
    return true;
}

bool IsLowercaseSha256(const std::string& value) {
    if (value.size() != 64U) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

}  // namespace questlab::integrity
