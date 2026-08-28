#include <vosp/security/secure_buffer.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace vosp::security {
void secure_erase(std::span<std::byte> bytes) noexcept {
    if (!bytes.empty()) {
        ::SecureZeroMemory(bytes.data(), bytes.size());
    }
}
} // namespace vosp::security

#endif
