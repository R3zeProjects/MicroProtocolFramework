#include <vosp/protocol.hpp>
#include <vosp/security.hpp>
#include <vosp/transport.hpp>

#include <array>
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
    const std::array secret{std::byte{1}, std::byte{2}, std::byte{3}};
    auto secure = vosp::SecureBuffer::copy_from(secret);
    if (!secure || !vosp::security::constant_time_equal(secure->bytes(), secret)) {
        return 5;
    }
    auto decoded = protocol.decode(*frame);
    return decoded && decoded->type() == 7 && decoded->correlation_id() == 42 ? 0 : 3;
}
