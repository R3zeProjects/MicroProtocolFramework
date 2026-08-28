#pragma once

/** @file secure_buffer.hpp Bounded move-only secret storage and comparison. */

#include <cstddef>
#include <span>
#include <vector>

#include <vosp/contracts/security.hpp>
#include <vosp/security/error.hpp>

namespace vosp::security {
/** @brief Overwrites an existing byte range using the platform secure-erasure primitive. */
void secure_erase(std::span<std::byte> bytes) noexcept;

/** @brief Content-independent comparison for two equal-length byte ranges. */
[[nodiscard]] bool constant_time_equal(std::span<const std::byte> left,
                                       std::span<const std::byte> right) noexcept;

/**
 * @brief Move-only bounded secret bytes erased on clear, assignment, and destruction.
 * @details This owner does not lock pages or erase copies retained by the caller.
 */
class SecureBuffer final {
  public:
    static constexpr std::size_t default_maximum_size = std::size_t{1024} * 1024;
    static constexpr std::size_t hard_maximum_size = std::size_t{64} * 1024 * 1024;

    SecureBuffer() = default;
    ~SecureBuffer();

    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    [[nodiscard]] static Result<SecureBuffer>
    copy_from(std::span<const std::byte> source, std::size_t maximum_size = default_maximum_size);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::span<std::byte> bytes() noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    void clear() noexcept;

  private:
    explicit SecureBuffer(std::vector<std::byte> bytes) noexcept;
    std::vector<std::byte> bytes_;
};

static_assert(vosp::contracts::SecureBytes<SecureBuffer>);
} // namespace vosp::security
