#include <vosp/security.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

enum class Permission : std::uint8_t { read, write, administer };

int main() {
    const std::array source{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    auto secret = vosp::SecureBuffer::copy_from(source);
    if (!secret) {
        std::cerr << secret.error().message() << '\n';
        return 1;
    }

    vosp::security::PermissionSet<Permission, 3> permissions;
    if (!permissions.grant(Permission::read) || !permissions.allows(Permission::read)) {
        return 2;
    }

    auto extension = vosp::security::authentication_extension(source);
    if (!extension) {
        return 3;
    }
    auto tag = vosp::security::authentication_tag(*extension);
    if (!tag || !vosp::security::constant_time_equal(*tag, source)) {
        return 4;
    }
    secret->clear();
    return secret->empty() ? 0 : 5;
}
