#include <vosp/protocol.hpp>

#include <cstddef>
#include <iostream>
#include <span>
#include <string>

int main() {
    vosp::protocol::Utf8Codec text;
    auto payload = text.encode(std::string{"fragmented"});
    if (!payload) {
        return 1;
    }

    vsp::Protocol protocol;
    auto frame = protocol.encode(
        vsp::ProtocolMessage{vsp::ProtocolVersion{1, 0}, 2, 99, std::move(*payload)});
    if (!frame) {
        return 2;
    }

    vsp::ProtocolStream stream;
    for (const std::byte& byte : *frame) {
        if (!stream.push(std::span<const std::byte>{&byte, 1})) {
            return 3;
        }
    }
    auto message = stream.next();
    if (!message || !message->has_value()) {
        return 4;
    }
    std::cout << "type=" << (*message)->type() << " correlation=" << (*message)->correlation_id()
              << '\n';
    return (*message)->correlation_id() == 99 ? 0 : 5;
}
