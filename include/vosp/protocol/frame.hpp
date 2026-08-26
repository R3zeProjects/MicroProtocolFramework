#pragma once

/** @file frame.hpp Stable bounded message framing in network byte order. */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <vosp/protocol/error.hpp>
#include <vosp/protocol/types.hpp>

namespace vosp::protocol {
inline constexpr std::uint32_t wire_magic = 0x56535031U; // "VSP1"
inline constexpr std::size_t wire_header_size = 32U;

enum class FrameState : std::uint8_t { incomplete, ready };

/** @brief Result of inspecting a possibly incomplete frame. */
struct FrameProbe {
    FrameState state = FrameState::incomplete;
    std::size_t frame_size = 0;
    std::size_t required_size = wire_header_size;
};

/** @brief One decoded frame and the number of consumed input bytes. */
struct DecodedFrame {
    Message message;
    std::size_t consumed = 0;
};

namespace detail {
inline void append_u16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>(value & 0xffU));
}

inline void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
    for (unsigned shift : {24U, 16U, 8U, 0U}) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

inline void append_u64(std::vector<std::byte>& output, std::uint64_t value) {
    for (unsigned shift : {56U, 48U, 40U, 32U, 24U, 16U, 8U, 0U}) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

[[nodiscard]] inline std::uint16_t read_u16(std::span<const std::byte> input,
                                            std::size_t offset) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(input[offset]) << 8U) |
                                      std::to_integer<std::uint16_t>(input[offset + 1U]));
}

[[nodiscard]] inline std::uint32_t read_u32(std::span<const std::byte> input,
                                            std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(input[offset + index]);
    }
    return value;
}

[[nodiscard]] inline std::uint64_t read_u64(std::span<const std::byte> input,
                                            std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8U) | std::to_integer<std::uint64_t>(input[offset + index]);
    }
    return value;
}
} // namespace detail

template <vosp::contracts::ErrorModel ErrorModel = Model>
/** @brief Stateless VSP1 frame encoder and decoder with explicit limits. */
class FrameCodec final {
  public:
    explicit FrameCodec(Limits limits = {}) noexcept : limits_{limits} {
        static_assert(detail::ResultFor<ErrorModel, std::vector<std::byte>>);
        static_assert(detail::ResultFor<ErrorModel, FrameProbe>);
        static_assert(detail::ResultFor<ErrorModel, Message>);
        static_assert(detail::ResultFor<ErrorModel, DecodedFrame>);
    }

    [[nodiscard]] const Limits& limits() const noexcept {
        return limits_;
    }

    [[nodiscard]] typename ErrorModel::template Result<std::vector<std::byte>>
    encode(const Message& message) const {
        return encode(MessageView{message});
    }

    [[nodiscard]] typename ErrorModel::template Result<std::vector<std::byte>>
    encode(const MessageView& message) const {
        if (!limits_.valid()) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::invalid_argument, "protocol limits are inconsistent");
        }
        if (message.payload().size() > limits_.max_payload_size ||
            message.payload().size() > std::numeric_limits<std::uint32_t>::max()) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::limit_exceeded, "protocol payload limit exceeded");
        }
        if (message.extensions().size() > limits_.max_extension_count ||
            message.extensions().size() > std::numeric_limits<std::uint16_t>::max()) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::limit_exceeded, "protocol extension count exceeded");
        }

        std::size_t extension_bytes = 0;
        for (const Extension& extension : message.extensions()) {
            if (extension.value().size() > std::numeric_limits<std::uint16_t>::max() ||
                extension_bytes > limits_.max_extension_bytes ||
                extension.value().size() > limits_.max_extension_bytes - extension_bytes ||
                extension.value().size() >
                    std::numeric_limits<std::size_t>::max() - extension_bytes - 4U) {
                return detail::failure<std::vector<std::byte>, ErrorModel>(
                    ErrorCode::limit_exceeded, "protocol extension bytes exceeded");
            }
            extension_bytes += 4U + extension.value().size();
        }
        if (extension_bytes > limits_.max_extension_bytes ||
            extension_bytes > std::numeric_limits<std::uint16_t>::max() - wire_header_size) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::limit_exceeded, "protocol header limit exceeded");
        }

        const std::size_t header_size = wire_header_size + extension_bytes;
        if (message.payload().size() > std::numeric_limits<std::size_t>::max() - header_size) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::limit_exceeded, "protocol frame size overflow");
        }
        const std::size_t frame_size = header_size + message.payload().size();
        if (frame_size > limits_.max_frame_size) {
            return detail::failure<std::vector<std::byte>, ErrorModel>(
                ErrorCode::limit_exceeded, "protocol frame limit exceeded");
        }

        std::vector<std::byte> output;
        output.reserve(frame_size);
        detail::append_u32(output, wire_magic);
        detail::append_u16(output, static_cast<std::uint16_t>(header_size));
        detail::append_u16(output, static_cast<std::uint16_t>(message.flags()));
        detail::append_u16(output, message.version().major());
        detail::append_u16(output, message.version().minor());
        detail::append_u32(output, message.type());
        detail::append_u64(output, message.correlation_id());
        detail::append_u32(output, static_cast<std::uint32_t>(message.payload().size()));
        detail::append_u16(output, static_cast<std::uint16_t>(message.extensions().size()));
        detail::append_u16(output, 0U);

        for (const Extension& extension : message.extensions()) {
            detail::append_u16(output, extension.id());
            detail::append_u16(output, static_cast<std::uint16_t>(extension.value().size()));
            output.insert(output.end(), extension.value().begin(), extension.value().end());
        }
        output.insert(output.end(), message.payload().begin(), message.payload().end());
        return output;
    }

    [[nodiscard]] typename ErrorModel::template Result<FrameProbe>
    probe(std::span<const std::byte> input) const {
        if (!limits_.valid()) {
            return detail::failure<FrameProbe, ErrorModel>(ErrorCode::invalid_argument,
                                                           "protocol limits are inconsistent");
        }
        if (input.size() < sizeof(std::uint32_t)) {
            return FrameProbe{};
        }
        if (detail::read_u32(input, 0) != wire_magic) {
            return detail::failure<FrameProbe, ErrorModel>(ErrorCode::invalid_magic,
                                                           "protocol frame magic mismatch");
        }
        if (input.size() < wire_header_size) {
            return FrameProbe{};
        }

        const auto header_size = static_cast<std::size_t>(detail::read_u16(input, 4));
        const auto payload_size = static_cast<std::size_t>(detail::read_u32(input, 24));
        const auto extension_count = static_cast<std::size_t>(detail::read_u16(input, 28));
        const auto reserved = detail::read_u16(input, 30);

        if (reserved != 0U || header_size < wire_header_size) {
            return detail::failure<FrameProbe, ErrorModel>(ErrorCode::malformed_frame,
                                                           "protocol frame header is malformed");
        }
        if (extension_count > limits_.max_extension_count ||
            header_size - wire_header_size > limits_.max_extension_bytes ||
            payload_size > limits_.max_payload_size) {
            return detail::failure<FrameProbe, ErrorModel>(
                ErrorCode::limit_exceeded, "protocol frame declares excessive sizes");
        }
        if (payload_size > std::numeric_limits<std::size_t>::max() - header_size) {
            return detail::failure<FrameProbe, ErrorModel>(ErrorCode::limit_exceeded,
                                                           "protocol frame size overflow");
        }
        const std::size_t frame_size = header_size + payload_size;
        if (frame_size > limits_.max_frame_size) {
            return detail::failure<FrameProbe, ErrorModel>(ErrorCode::limit_exceeded,
                                                           "protocol frame limit exceeded");
        }
        if (input.size() < header_size) {
            return FrameProbe{.state = FrameState::incomplete,
                              .frame_size = frame_size,
                              .required_size = frame_size};
        }

        auto validation = validate_extensions(input.first(header_size), extension_count);
        if (!validation) {
            return typename ErrorModel::template Result<FrameProbe>{
                std::unexpected{validation.error()}};
        }
        if (input.size() < frame_size) {
            return FrameProbe{.state = FrameState::incomplete,
                              .frame_size = frame_size,
                              .required_size = frame_size};
        }
        return FrameProbe{
            .state = FrameState::ready, .frame_size = frame_size, .required_size = frame_size};
    }

    [[nodiscard]] typename ErrorModel::template Result<Message>
    decode(std::span<const std::byte> input) const {
        auto decoded = decode_prefix(input);
        if (!decoded) {
            return typename ErrorModel::template Result<Message>{std::unexpected{decoded.error()}};
        }
        if (decoded->consumed != input.size()) {
            return detail::failure<Message, ErrorModel>(ErrorCode::trailing_data,
                                                        "protocol frame has trailing bytes");
        }
        return std::move(decoded->message);
    }

    [[nodiscard]] typename ErrorModel::template Result<DecodedFrame>
    decode_prefix(std::span<const std::byte> input) const {
        auto inspected = probe(input);
        if (!inspected) {
            return typename ErrorModel::template Result<DecodedFrame>{
                std::unexpected{inspected.error()}};
        }
        if (inspected->state != FrameState::ready) {
            return detail::failure<DecodedFrame, ErrorModel>(ErrorCode::insufficient_data,
                                                             "protocol frame is incomplete");
        }

        const auto header_size = static_cast<std::size_t>(detail::read_u16(input, 4));
        const auto extension_count = static_cast<std::size_t>(detail::read_u16(input, 28));
        std::vector<Extension> extensions;
        extensions.reserve(extension_count);
        std::size_t cursor = wire_header_size;
        for (std::size_t index = 0; index < extension_count; ++index) {
            const auto id = detail::read_u16(input, cursor);
            const auto size = static_cast<std::size_t>(detail::read_u16(input, cursor + 2U));
            cursor += 4U;
            extensions.emplace_back(
                id,
                std::vector<std::byte>{input.begin() + static_cast<std::ptrdiff_t>(cursor),
                                       input.begin() + static_cast<std::ptrdiff_t>(cursor + size)});
            cursor += size;
        }

        const auto payload = input.subspan(header_size, inspected->frame_size - header_size);
        Message message{Version{detail::read_u16(input, 8), detail::read_u16(input, 10)},
                        detail::read_u32(input, 12),
                        detail::read_u64(input, 16),
                        std::vector<std::byte>{payload.begin(), payload.end()},
                        static_cast<FrameFlags>(detail::read_u16(input, 6)),
                        std::move(extensions)};
        return DecodedFrame{.message = std::move(message), .consumed = inspected->frame_size};
    }

  private:
    [[nodiscard]] typename ErrorModel::OperationResult
    validate_extensions(std::span<const std::byte> header, std::size_t extension_count) const {
        std::size_t cursor = wire_header_size;
        for (std::size_t index = 0; index < extension_count; ++index) {
            if (header.size() - cursor < 4U) {
                return detail::failure<ErrorModel>(ErrorCode::malformed_frame,
                                                   "protocol extension header is truncated");
            }
            const auto value_size = static_cast<std::size_t>(detail::read_u16(header, cursor + 2U));
            cursor += 4U;
            if (value_size > header.size() - cursor) {
                return detail::failure<ErrorModel>(ErrorCode::malformed_frame,
                                                   "protocol extension value is truncated");
            }
            cursor += value_size;
        }
        if (cursor != header.size()) {
            return detail::failure<ErrorModel>(ErrorCode::malformed_frame,
                                               "protocol extension count is inconsistent");
        }
        return {};
    }

    Limits limits_;
};
} // namespace vosp::protocol
