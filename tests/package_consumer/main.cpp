#include <vosp/protocol.hpp>
#include <vosp/transport.hpp>

#include <cstddef>
#include <string>

int main() {
    vosp::transport::TcpStream stream;
    if (stream.connected()) {
        return 4;
    }
    vosp::protocol::Utf8Codec codec;
    auto payload = codec.encode(std::string{"installed package"});
    if (!payload) {
        return 1;
    }
    vsp::Protocol protocol;
    auto frame = protocol.encode(
        vsp::ProtocolMessage{vsp::ProtocolVersion{1, 0}, 7, 42, std::move(*payload)});
    if (!frame) {
        return 2;
    }
    auto decoded = protocol.decode(*frame);
    return decoded && decoded->type() == 7 && decoded->correlation_id() == 42 ? 0 : 3;
}
