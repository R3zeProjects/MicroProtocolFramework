#include <vosp/contracts/security.hpp>
#include <vosp/security/error.hpp>

#include <cstddef>
#include <span>

struct InvalidAuthenticator {
    using TagType = int;
    [[nodiscard]] int authenticate(std::span<const std::byte>) const {
        return 0;
    }
};

static_assert(vosp::contracts::MessageAuthenticator<InvalidAuthenticator, vosp::security::Model>);

int main() {}
