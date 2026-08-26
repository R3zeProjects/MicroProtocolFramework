#pragma once

/** @file text.hpp Strict UTF-8 and raw byte codecs. */

#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vosp/protocol/error.hpp>
#include <vosp/protocol/types.hpp>

namespace vosp::protocol {
namespace detail {
[[nodiscard]] constexpr bool continuation(std::byte value) noexcept {
    return (std::to_integer<unsigned char>(value) & 0xc0U) == 0x80U;
}

[[nodiscard]] inline bool valid_utf8(std::span<const std::byte> bytes) noexcept {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto first = std::to_integer<unsigned char>(bytes[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= bytes.size() || !continuation(bytes[index + 1U])) {
                return false;
            }
            index += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (index + 2U >= bytes.size()) {
                return false;
            }
            const auto second = std::to_integer<unsigned char>(bytes[index + 1U]);
            const bool valid_second =
                (first == 0xe0U && second >= 0xa0U && second <= 0xbfU) ||
                (first == 0xedU && second >= 0x80U && second <= 0x9fU) ||
                (((first >= 0xe1U && first <= 0xecU) || (first >= 0xeeU && first <= 0xefU)) &&
                 second >= 0x80U && second <= 0xbfU);
            if (!valid_second || !continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (index + 3U >= bytes.size()) {
                return false;
            }
            const auto second = std::to_integer<unsigned char>(bytes[index + 1U]);
            const bool valid_second =
                (first == 0xf0U && second >= 0x90U && second <= 0xbfU) ||
                (first == 0xf4U && second >= 0x80U && second <= 0x8fU) ||
                (first >= 0xf1U && first <= 0xf3U && second >= 0x80U && second <= 0xbfU);
            if (!valid_second || !continuation(bytes[index + 2U]) ||
                !continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}
} // namespace detail

template <vosp::contracts::ErrorModel ErrorModel = Model>
/** @brief Strict bounded UTF-8 codec. */
class Utf8Codec final {
  public:
    explicit Utf8Codec(std::size_t limit = std::size_t{1024} * 1024U) noexcept : limit_{limit} {}

    [[nodiscard]] typename ErrorModel::template Result<std::vector<std::byte>>
    encode(const std::string& value) const {
        if (value.size() > limit_) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::limit_exceeded, "UTF-8 value limit exceeded");
        }
        const auto* data = reinterpret_cast<const std::byte*>(value.data());
        const std::span<const std::byte> bytes{data, value.size()};
        if (!detail::valid_utf8(bytes)) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(ErrorCode::invalid_utf8,
                                                                       "text is not valid UTF-8");
        }
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    [[nodiscard]] typename ErrorModel::template Result<std::string>
    decode(std::span<const std::byte> bytes) const {
        if (bytes.size() > limit_) {
            return detail::failure<std::string, ErrorModel>(ErrorCode::limit_exceeded,
                                                            "UTF-8 value limit exceeded");
        }
        if (!detail::valid_utf8(bytes)) {
            return detail::failure<std::string, ErrorModel>(ErrorCode::invalid_utf8,
                                                            "payload is not valid UTF-8");
        }
        std::string result(bytes.size(), '\0');
        if (!bytes.empty()) {
            std::memcpy(result.data(), bytes.data(), bytes.size());
        }
        return result;
    }

  private:
    std::size_t limit_;
};

template <vosp::contracts::ErrorModel ErrorModel = Model>
/** @brief Bounded identity codec for owning byte vectors. */
class BytesCodec final {
  public:
    explicit BytesCodec(std::size_t limit = Limits::default_payload_size) noexcept
        : limit_{limit} {}

    [[nodiscard]] typename ErrorModel::template Result<std::vector<std::byte>>
    encode(const std::vector<std::byte>& value) const {
        if (value.size() > limit_) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(ErrorCode::limit_exceeded,
                                                                       "byte value limit exceeded");
        }
        return value;
    }

    [[nodiscard]] typename ErrorModel::template Result<std::vector<std::byte>>
    decode(std::span<const std::byte> bytes) const {
        if (bytes.size() > limit_) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(ErrorCode::limit_exceeded,
                                                                       "byte value limit exceeded");
        }
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

  private:
    std::size_t limit_;
};
} // namespace vosp::protocol
