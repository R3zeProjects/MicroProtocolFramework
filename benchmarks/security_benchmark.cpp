#include <vosp/security.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
enum class Permission : std::uint8_t { read, write, administer };

template <typename Operation>
[[nodiscard]] double measure(std::size_t iterations, Operation&& operation) {
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        operation(index);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    return std::chrono::duration<double, std::nano>(elapsed).count() /
           static_cast<double>(iterations);
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations =
        argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 1'000'000;
    const std::size_t bytes =
        argc > 2 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : 64;
    if (iterations == 0 || iterations > 100'000'000 || bytes == 0 || bytes > 1'048'576) {
        return 64;
    }

    std::vector<std::byte> left(bytes, std::byte{0x5a});
    const std::vector<std::byte> right = left;
    bool comparison = false;
    const double compare_ns = measure(iterations, [&](std::size_t) {
        comparison ^= vosp::security::constant_time_equal(left, right);
    });

    vosp::security::PermissionSet<Permission, 3> permissions;
    const double permission_ns = measure(iterations, [&](std::size_t index) {
        if ((index & 1U) == 0) {
            static_cast<void>(permissions.grant(Permission::read));
        } else {
            static_cast<void>(permissions.revoke(Permission::read));
        }
    });

    const double erase_ns = measure(iterations, [&](std::size_t) {
        vosp::security::secure_erase(left);
        left[0] = std::byte{0x5a};
    });
    const auto guard = static_cast<unsigned long long>(permissions.mask()) +
                       static_cast<unsigned long long>(comparison) +
                       std::to_integer<unsigned int>(left.front());
    std::cout << "payload_bytes,compare_ns,permission_update_ns,erase_ns,guard\n"
              << bytes << ',' << compare_ns << ',' << permission_ns << ',' << erase_ns << ','
              << guard << '\n';
    return 0;
}
