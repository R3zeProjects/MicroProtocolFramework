#pragma once

/** @file protocol.hpp Direct composition helpers for VSP1 authentication TLVs. */

#include <cstddef>
#include <span>
#include <vector>

#include <vosp/protocol/types.hpp>
#include <vosp/security/error.hpp>

namespace vosp::security {
inline constexpr std::size_t default_maximum_authentication_tag_size = 4096;

/** @brief Copies an opaque provider tag into the reserved authentication extension. */
[[nodiscard]] inline Result<protocol::Extension>
authentication_extension(std::span<const std::byte> tag,
                         std::size_t maximum_size = default_maximum_authentication_tag_size) {
    if (tag.empty() || maximum_size == 0 ||
        maximum_size > default_maximum_authentication_tag_size) {
        return std::unexpected{
            Error{ErrorCode::invalid_argument, "authentication tag limit is invalid"}};
    }
    if (tag.size() > maximum_size) {
        return std::unexpected{
            Error{ErrorCode::limit_exceeded, "authentication tag exceeds its limit"}};
    }
    return protocol::Extension{protocol::ExtensionId::authentication,
                               std::vector<std::byte>{tag.begin(), tag.end()}};
}

/**
 * @brief Returns a non-owning tag view after validating its extension identifier.
 * @return A view valid while the extension value remains alive and unchanged.
 */
[[nodiscard]] inline Result<std::span<const std::byte>>
authentication_tag(const protocol::Extension& extension) {
    if (extension.id() != static_cast<std::uint16_t>(protocol::ExtensionId::authentication) ||
        extension.value().empty()) {
        return std::unexpected{
            Error{ErrorCode::invalid_extension, "extension is not an authentication tag"}};
    }
    return extension.value();
}
} // namespace vosp::security
