#pragma once

/** @file permissions.hpp Allocation-free enum permission set. */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace vosp::security {
/**
 * @brief Compact permission set for contiguous enum values in `[0, Count)`.
 * @tparam Permission Scoped or unscoped enum used as a bit index.
 * @tparam Count Number of valid permission values; limited to one 64-bit word.
 */
template <typename Permission, std::size_t Count>
    requires std::is_enum_v<Permission> && (Count > 0) && (Count <= 64)
class PermissionSet final {
  public:
    [[nodiscard]] constexpr bool grant(Permission permission) noexcept {
        const auto bit = bit_for(permission);
        if (bit == 0) {
            return false;
        }
        mask_ |= bit;
        return true;
    }

    [[nodiscard]] constexpr bool revoke(Permission permission) noexcept {
        const auto bit = bit_for(permission);
        if (bit == 0) {
            return false;
        }
        mask_ &= ~bit;
        return true;
    }

    [[nodiscard]] constexpr bool allows(Permission permission) const noexcept {
        const auto bit = bit_for(permission);
        return bit != 0 && (mask_ & bit) != 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return mask_ == 0;
    }

    [[nodiscard]] constexpr std::uint64_t mask() const noexcept {
        return mask_;
    }

    constexpr void clear() noexcept {
        mask_ = 0;
    }

  private:
    [[nodiscard]] static constexpr std::uint64_t bit_for(Permission permission) noexcept {
        using Underlying = std::underlying_type_t<Permission>;
        const auto value = static_cast<Underlying>(permission);
        if constexpr (std::is_signed_v<Underlying>) {
            if (value < 0) {
                return 0;
            }
        }
        using Unsigned = std::make_unsigned_t<Underlying>;
        const auto index = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
        return index < Count ? (std::uint64_t{1} << index) : 0;
    }

    std::uint64_t mask_ = 0;
};
} // namespace vosp::security
