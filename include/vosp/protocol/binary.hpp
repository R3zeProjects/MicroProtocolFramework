#pragma once

/** @file binary.hpp Bounds-checked network-order binary reader and writer. */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vosp/protocol/error.hpp>

namespace vosp::protocol {
/** @brief Bounded binary writer using big-endian network byte order. */
template <vosp::contracts::ErrorModel ErrorModel = Model> class BinaryWriter final {
  public:
    explicit BinaryWriter(std::size_t limit) : limit_{limit} {}

    template <std::unsigned_integral Value>
    [[nodiscard]] typename ErrorModel::OperationResult write(Value value) {
        if (!can_append(sizeof(Value))) {
            return detail::failure<ErrorModel>(ErrorCode::limit_exceeded,
                                               "binary writer limit exceeded");
        }
        for (std::size_t shift = sizeof(Value); shift > 0; --shift) {
            const auto bits = static_cast<unsigned>((shift - 1U) * 8U);
            bytes_.push_back(static_cast<std::byte>((value >> bits) & static_cast<Value>(0xffU)));
        }
        return {};
    }

    [[nodiscard]] typename ErrorModel::OperationResult
    write_bytes(std::span<const std::byte> bytes) {
        if (!can_append(bytes.size())) {
            return detail::failure<ErrorModel>(ErrorCode::limit_exceeded,
                                               "binary writer limit exceeded");
        }
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        return {};
    }

    [[nodiscard]] typename ErrorModel::OperationResult write_string(std::string_view value) {
        constexpr std::size_t prefix_size = sizeof(std::uint32_t);
        if (value.size() > std::numeric_limits<std::uint32_t>::max() ||
            value.size() > std::numeric_limits<std::size_t>::max() - prefix_size ||
            !can_append(prefix_size + value.size())) {
            return detail::failure<ErrorModel>(ErrorCode::limit_exceeded,
                                               "binary string limit exceeded");
        }
        const auto length = static_cast<std::uint32_t>(value.size());
        for (std::size_t shift = sizeof(length); shift > 0; --shift) {
            const auto bits = static_cast<unsigned>((shift - 1U) * 8U);
            bytes_.push_back(static_cast<std::byte>((length >> bits) & 0xffU));
        }
        const auto* first = reinterpret_cast<const std::byte*>(value.data());
        bytes_.insert(bytes_.end(), first, first + value.size());
        return {};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return bytes_.size();
    }
    [[nodiscard]] std::vector<std::byte> take() && noexcept {
        return std::move(bytes_);
    }

  private:
    [[nodiscard]] bool can_append(std::size_t count) const noexcept {
        return count <= limit_ - std::min(limit_, bytes_.size());
    }

    std::size_t limit_;
    std::vector<std::byte> bytes_;
};

/** @brief Non-owning bounds-checked big-endian binary reader. */
template <vosp::contracts::ErrorModel ErrorModel = Model> class BinaryReader final {
  public:
    explicit BinaryReader(std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    template <std::unsigned_integral Value>
    [[nodiscard]] typename ErrorModel::template Result<Value> read() {
        if (remaining() < sizeof(Value)) {
            return detail::failure<Value, ErrorModel>(ErrorCode::insufficient_data,
                                                      "binary value is truncated");
        }
        Value value = 0;
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            value = static_cast<Value>((value << 8U) |
                                       std::to_integer<unsigned char>(bytes_[position_ + index]));
        }
        position_ += sizeof(Value);
        return value;
    }

    [[nodiscard]] typename ErrorModel::template Result<std::span<const std::byte>>
    read_bytes(std::size_t count) {
        if (count > remaining()) {
            return detail::failure<std::span<const std::byte>, ErrorModel>(
                ErrorCode::insufficient_data, "binary byte range is truncated");
        }
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    [[nodiscard]] typename ErrorModel::template Result<std::string>
    read_string(std::size_t maximum = std::numeric_limits<std::uint32_t>::max()) {
        constexpr std::size_t prefix_size = sizeof(std::uint32_t);
        if (remaining() < prefix_size) {
            return detail::failure<std::string, ErrorModel>(ErrorCode::insufficient_data,
                                                            "binary string length is truncated");
        }
        std::uint32_t length = 0;
        for (std::size_t index = 0; index < prefix_size; ++index) {
            length = static_cast<std::uint32_t>(
                (length << 8U) | std::to_integer<unsigned char>(bytes_[position_ + index]));
        }
        const auto size = static_cast<std::size_t>(length);
        if (size > maximum) {
            return detail::failure<std::string, ErrorModel>(ErrorCode::limit_exceeded,
                                                            "binary string limit exceeded");
        }
        if (size > remaining() - prefix_size) {
            return detail::failure<std::string, ErrorModel>(ErrorCode::insufficient_data,
                                                            "binary string payload is truncated");
        }
        position_ += prefix_size;
        std::string result(size, '\0');
        if (size != 0) {
            std::memcpy(result.data(), bytes_.data() + position_, size);
        }
        position_ += size;
        return result;
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return remaining() == 0;
    }

  private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};
} // namespace vosp::protocol
