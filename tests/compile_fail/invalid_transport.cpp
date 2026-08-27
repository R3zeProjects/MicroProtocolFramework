#include <vosp/contracts/transport.hpp>
#include <vosp/transport/error.hpp>

struct InvalidTransport {
    [[nodiscard]] int connected() const noexcept {
        return 1;
    }
};

static_assert(vosp::contracts::ByteStreamTransport<InvalidTransport, vosp::transport::Model>);

int main() {}
