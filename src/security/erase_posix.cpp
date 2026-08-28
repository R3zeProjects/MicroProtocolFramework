#include <vosp/security/secure_buffer.hpp>

#ifndef _WIN32

#include <string.h>

namespace vosp::security {
void secure_erase(std::span<std::byte> bytes) noexcept {
    if (!bytes.empty()) {
        ::explicit_bzero(bytes.data(), bytes.size());
    }
}
} // namespace vosp::security

#endif
