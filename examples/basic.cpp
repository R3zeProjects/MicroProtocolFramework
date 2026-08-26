#include <vosp/protocol.hpp>

#include <iostream>
#include <string>

int main() {
    vosp::protocol::Utf8Codec text;
    auto payload = text.encode(std::string{"hello protocol"});
    if (!payload) {
        std::cerr << payload.error().message() << '\n';
        return 1;
    }

    vsp::Protocol protocol;
    auto frame = protocol.encode(
        vsp::ProtocolMessage{vsp::ProtocolVersion{1, 0}, 1, 42, std::move(*payload)});
    if (!frame) {
        std::cerr << frame.error().message() << '\n';
        return 2;
    }

    auto message = protocol.decode(*frame);
    if (!message) {
        std::cerr << message.error().message() << '\n';
        return 3;
    }
    auto decoded = text.decode(message->payload());
    if (!decoded) {
        std::cerr << decoded.error().message() << '\n';
        return 4;
    }
    std::cout << *decoded << '\n';
    return *decoded == "hello protocol" ? 0 : 5;
}
