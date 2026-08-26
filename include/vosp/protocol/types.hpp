#pragma once

/** @file types.hpp Owning protocol values and wire limits. */

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace vosp::protocol {
/** @brief Semantic protocol version used in frames and negotiation. */
class Version final {
  public:
    constexpr Version() = default;
    constexpr Version(std::uint16_t major, std::uint16_t minor) noexcept
        : major_{major}, minor_{minor} {}

    [[nodiscard]] constexpr std::uint16_t major() const noexcept {
        return major_;
    }
    [[nodiscard]] constexpr std::uint16_t minor() const noexcept {
        return minor_;
    }
    [[nodiscard]] constexpr bool compatible_with(const Version& peer) const noexcept {
        return major_ == peer.major_;
    }

    auto operator<=>(const Version&) const = default;

  private:
    std::uint16_t major_ = 1;
    std::uint16_t minor_ = 0;
};

/** @brief Inclusive range of supported protocol versions. */
struct VersionRange {
    Version minimum{};
    Version maximum{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return minimum <= maximum && minimum.major() == maximum.major();
    }
    [[nodiscard]] constexpr bool contains(Version version) const noexcept {
        return valid() && version >= minimum && version <= maximum;
    }
};

/** @brief Selects the highest protocol version supported by both peers. */
[[nodiscard]] constexpr std::optional<Version> negotiate_version(VersionRange local,
                                                                 VersionRange remote) noexcept {
    if (!local.valid() || !remote.valid()) {
        return std::nullopt;
    }
    const Version lower = std::max(local.minimum, remote.minimum);
    const Version upper = std::min(local.maximum, remote.maximum);
    if (lower > upper) {
        return std::nullopt;
    }
    return upper;
}

/** @brief Wire flags whose semantics are owned by extension frameworks. */
enum class FrameFlags : std::uint16_t {
    none = 0,
    compressed = 1U << 0U,
    encrypted = 1U << 1U,
    authenticated = 1U << 2U,
    checksum = 1U << 3U,
    all = compressed | encrypted | authenticated | checksum
};

[[nodiscard]] constexpr FrameFlags operator|(FrameFlags left, FrameFlags right) noexcept {
    return static_cast<FrameFlags>(static_cast<std::uint16_t>(left) |
                                   static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr FrameFlags operator&(FrameFlags left, FrameFlags right) noexcept {
    return static_cast<FrameFlags>(static_cast<std::uint16_t>(left) &
                                   static_cast<std::uint16_t>(right));
}

constexpr FrameFlags& operator|=(FrameFlags& left, FrameFlags right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_flag(FrameFlags value, FrameFlags flag) noexcept {
    return (value & flag) == flag;
}

/** @brief Reserved TLV identifiers; IDs from 1024 are application-defined. */
enum class ExtensionId : std::uint16_t {
    content_type = 1,
    compression = 2,
    checksum = 3,
    authentication = 4,
    trace_context = 5,
    application_first = 1024
};

/** @brief Owning opaque TLV extension. */
class Extension final {
  public:
    Extension() = default;
    Extension(std::uint16_t id, std::vector<std::byte> value) : id_{id}, value_{std::move(value)} {}
    Extension(ExtensionId id, std::vector<std::byte> value)
        : Extension{static_cast<std::uint16_t>(id), std::move(value)} {}

    [[nodiscard]] std::uint16_t id() const noexcept {
        return id_;
    }
    [[nodiscard]] std::span<const std::byte> value() const noexcept {
        return value_;
    }
    [[nodiscard]] const std::vector<std::byte>& owning_value() const noexcept {
        return value_;
    }

    friend bool operator==(const Extension&, const Extension&) = default;

  private:
    std::uint16_t id_ = 0;
    std::vector<std::byte> value_;
};

/** @brief Owning versioned protocol message. */
class Message final {
  public:
    using VersionType = protocol::Version;

    Message() = default;
    Message(Version version, std::uint32_t type, std::uint64_t correlation_id,
            std::vector<std::byte> payload, FrameFlags flags = FrameFlags::none,
            std::vector<Extension> extensions = {})
        : version_{version}, type_{type}, correlation_id_{correlation_id}, flags_{flags},
          extensions_{std::move(extensions)}, payload_{std::move(payload)} {}

    [[nodiscard]] Version version() const noexcept {
        return version_;
    }
    [[nodiscard]] std::uint32_t type() const noexcept {
        return type_;
    }
    [[nodiscard]] std::uint64_t correlation_id() const noexcept {
        return correlation_id_;
    }
    [[nodiscard]] FrameFlags flags() const noexcept {
        return flags_;
    }
    [[nodiscard]] std::span<const Extension> extensions() const noexcept {
        return extensions_;
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept {
        return payload_;
    }
    [[nodiscard]] const std::vector<std::byte>& owning_payload() const noexcept {
        return payload_;
    }

    friend bool operator==(const Message&, const Message&) = default;

  private:
    Version version_{};
    std::uint32_t type_ = 0;
    std::uint64_t correlation_id_ = 0;
    FrameFlags flags_ = FrameFlags::none;
    std::vector<Extension> extensions_;
    std::vector<std::byte> payload_;
};

/** @brief Non-owning message view valid for the duration of encoding. */
class MessageView final {
  public:
    using VersionType = protocol::Version;

    MessageView(Version version, std::uint32_t type, std::uint64_t correlation_id,
                std::span<const std::byte> payload, FrameFlags flags = FrameFlags::none,
                std::span<const Extension> extensions = {}) noexcept
        : version_{version}, type_{type}, correlation_id_{correlation_id}, flags_{flags},
          extensions_{extensions}, payload_{payload} {}

    MessageView(const Message& message) noexcept
        : MessageView{message.version(), message.type(),  message.correlation_id(),
                      message.payload(), message.flags(), message.extensions()} {}

    [[nodiscard]] Version version() const noexcept {
        return version_;
    }
    [[nodiscard]] std::uint32_t type() const noexcept {
        return type_;
    }
    [[nodiscard]] std::uint64_t correlation_id() const noexcept {
        return correlation_id_;
    }
    [[nodiscard]] FrameFlags flags() const noexcept {
        return flags_;
    }
    [[nodiscard]] std::span<const Extension> extensions() const noexcept {
        return extensions_;
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept {
        return payload_;
    }

  private:
    Version version_;
    std::uint32_t type_;
    std::uint64_t correlation_id_;
    FrameFlags flags_;
    std::span<const Extension> extensions_;
    std::span<const std::byte> payload_;
};

/** @brief Hard bounds applied before protocol allocation and copying. */
struct Limits {
    static constexpr std::size_t default_payload_size = std::size_t{16} * 1024U * 1024U;
    static constexpr std::size_t default_extension_bytes =
        std::numeric_limits<std::uint16_t>::max() - 32U;
    static constexpr std::size_t default_frame_size =
        default_payload_size + default_extension_bytes + 32U;
    static constexpr std::size_t default_buffered_bytes = 2U * default_frame_size;

    std::size_t max_payload_size = default_payload_size;
    std::size_t max_extension_count = 64;
    std::size_t max_extension_bytes = default_extension_bytes;
    std::size_t max_frame_size = default_frame_size;
    std::size_t max_buffered_bytes = default_buffered_bytes;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return max_extension_count <= std::numeric_limits<std::uint16_t>::max() &&
               max_extension_bytes <= std::numeric_limits<std::uint16_t>::max() &&
               max_payload_size <= std::numeric_limits<std::uint32_t>::max() &&
               max_frame_size >= 32U && max_payload_size <= max_frame_size - 32U &&
               max_buffered_bytes >= max_frame_size;
    }
};
} // namespace vosp::protocol
