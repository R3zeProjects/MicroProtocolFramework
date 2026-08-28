#include <vosp/security/secure_buffer.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vosp::security {
bool constant_time_equal(std::span<const std::byte> left,
                         std::span<const std::byte> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= std::to_integer<std::uint8_t>(left[index] ^ right[index]);
    }
    std::atomic_signal_fence(std::memory_order_seq_cst);
    return difference == 0;
}

SecureBuffer::SecureBuffer(std::vector<std::byte> bytes) noexcept : bytes_{std::move(bytes)} {}

SecureBuffer::~SecureBuffer() {
    clear();
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept : bytes_{std::move(other.bytes_)} {
    other.clear();
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        clear();
        bytes_ = std::move(other.bytes_);
        other.clear();
    }
    return *this;
}

Result<SecureBuffer> SecureBuffer::copy_from(std::span<const std::byte> source,
                                             std::size_t maximum_size) {
    if (source.empty() || maximum_size == 0 || maximum_size > hard_maximum_size) {
        return std::unexpected{
            Error{ErrorCode::invalid_argument, "secure buffer size limit is invalid"}};
    }
    if (source.size() > maximum_size) {
        return std::unexpected{
            Error{ErrorCode::limit_exceeded, "secret exceeds secure buffer limit"}};
    }
    return SecureBuffer{std::vector<std::byte>{source.begin(), source.end()}};
}

std::size_t SecureBuffer::size() const noexcept {
    return bytes_.size();
}

bool SecureBuffer::empty() const noexcept {
    return bytes_.empty();
}

std::span<std::byte> SecureBuffer::bytes() noexcept {
    return bytes_;
}

std::span<const std::byte> SecureBuffer::bytes() const noexcept {
    return bytes_;
}

void SecureBuffer::clear() noexcept {
    secure_erase(bytes_);
    bytes_.clear();
}
} // namespace vosp::security
