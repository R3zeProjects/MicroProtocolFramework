#include <vosp/protocol.hpp>
#include <vosp/transport.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {
class Checks {
  public:
    void expect(bool condition, std::string_view message) {
        if (!condition) {
            ++failures_;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    [[nodiscard]] int result() const noexcept {
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int failures_ = 0;
};

[[nodiscard]] bool receive_exact(vosp::transport::TcpStream& stream, std::span<std::byte> output) {
    std::size_t offset = 0;
    while (offset < output.size()) {
        auto received = stream.receive(output.subspan(offset));
        if (!received) {
            return false;
        }
        offset += *received;
    }
    return true;
}

void test_invalid_arguments(Checks& checks) {
    vosp::transport::TcpStream stream;
    checks.expect(!stream.connect(vosp::transport::IpEndpoint{}),
                  "empty TCP endpoint must be rejected");
    checks.expect(!stream.reconnect(), "reconnect without an endpoint must be rejected");

    vosp::transport::TcpListener listener;
    checks.expect(!listener.bind(vosp::transport::IpEndpoint{"127.0.0.1", 0}, 0),
                  "zero TCP backlog must be rejected");

    vosp::transport::UdpSocket socket;
    std::vector<std::byte> oversized(vosp::transport::UdpSocket::maximum_payload_size + 1U);
    checks.expect(!socket.send_to(vosp::transport::IpEndpoint{"127.0.0.1", 9}, oversized),
                  "oversized UDP payload must be rejected before opening a socket");
}

void test_tcp_protocol_round_trip(Checks& checks) {
    using namespace std::chrono_literals;
    const vosp::transport::IoOptions options{.receive_timeout = 2s, .send_timeout = 2s};
    vosp::transport::TcpListener listener{options};
    auto bound = listener.bind(vosp::transport::IpEndpoint{"127.0.0.1", 0});
    checks.expect(static_cast<bool>(bound), "loopback TCP listener must bind");
    if (!bound) {
        return;
    }
    auto endpoint = listener.local_endpoint();
    checks.expect(endpoint && endpoint->port() != 0, "TCP listener must expose assigned port");
    if (!endpoint) {
        return;
    }

    const std::vector<std::byte> payload{std::byte{'V'}, std::byte{'O'}, std::byte{'S'},
                                         std::byte{'P'}};
    vosp::protocol::FrameCodec codec;
    auto frame =
        codec.encode(vosp::protocol::Message{vosp::protocol::Version{1, 0}, 7, 42, payload});
    checks.expect(static_cast<bool>(frame), "protocol frame must encode");
    if (!frame) {
        return;
    }

    std::atomic_bool server_ok{false};
    std::jthread server{[&] {
        auto accepted = listener.accept();
        if (!accepted) {
            return;
        }
        std::vector<std::byte> input(frame->size());
        if (!receive_exact(*accepted, input)) {
            return;
        }
        auto message = codec.decode(input);
        if (!message || message->type() != 7 || message->correlation_id() != 42) {
            return;
        }
        auto sent = accepted->send_all(input);
        server_ok.store(sent && *sent == input.size(), std::memory_order_release);
    }};

    vosp::transport::TcpStream client{options};
    auto connected = client.connect(*endpoint);
    checks.expect(static_cast<bool>(connected), "loopback TCP client must connect");
    if (connected) {
        auto sent = client.send_all(*frame);
        checks.expect(sent && *sent == frame->size(), "TCP send_all must write complete frame");
        std::vector<std::byte> echoed(frame->size());
        checks.expect(receive_exact(client, echoed), "TCP client must receive echoed frame");
        checks.expect(codec.decode(echoed).has_value(), "echoed TCP frame must decode");
    }
    server.join();
    checks.expect(server_ok.load(std::memory_order_acquire),
                  "TCP server must decode and echo the frame");
}

void test_tcp_reconnect(Checks& checks) {
    using namespace std::chrono_literals;
    const vosp::transport::IoOptions options{.receive_timeout = 2s, .send_timeout = 2s};
    vosp::transport::TcpListener listener{options};
    auto bound = listener.bind(vosp::transport::IpEndpoint{"127.0.0.1", 0});
    if (!bound) {
        checks.expect(false, "reconnect listener must bind");
        return;
    }
    auto endpoint = listener.local_endpoint();
    if (!endpoint) {
        checks.expect(false, "reconnect listener must expose assigned port");
        return;
    }

    std::atomic_bool server_ok{false};
    std::jthread server{[&] {
        auto first = listener.accept();
        if (!first) {
            return;
        }
        first->close();
        auto second = listener.accept();
        if (!second) {
            return;
        }
        std::array<std::byte, 1> value{};
        server_ok.store(receive_exact(*second, value) && value[0] == std::byte{0x2a},
                        std::memory_order_release);
    }};

    vosp::transport::TcpStream client{
        options, {.max_attempts = 3, .initial_delay = 1ms, .maximum_delay = 4ms}};
    auto connected = client.connect(*endpoint);
    checks.expect(static_cast<bool>(connected), "initial TCP connection must succeed");
    if (!connected) {
        listener.close();
        server.join();
        return;
    }
    std::array<std::byte, 1> input{};
    auto closed = client.receive(input);
    checks.expect(!closed && closed.error().kind() == vosp::transport::ErrorCode::peer_closed,
                  "remote close must be explicit before reconnect");
    auto reconnected = client.reconnect();
    checks.expect(static_cast<bool>(reconnected), "bounded TCP reconnect must succeed");
    if (reconnected) {
        const std::array output{std::byte{0x2a}};
        checks.expect(client.send_all(output).has_value(),
                      "reconnected TCP stream must transfer bytes");
    } else {
        listener.close();
    }
    server.join();
    checks.expect(server_ok.load(std::memory_order_acquire),
                  "server must receive data after reconnect");
}

void test_udp_round_trip(Checks& checks) {
    using namespace std::chrono_literals;
    const vosp::transport::IoOptions options{.receive_timeout = 2s, .send_timeout = 2s};
    vosp::transport::UdpSocket receiver{options};
    auto bound = receiver.bind(vosp::transport::IpEndpoint{"127.0.0.1", 0});
    checks.expect(static_cast<bool>(bound), "loopback UDP receiver must bind");
    if (!bound) {
        return;
    }
    auto endpoint = receiver.local_endpoint();
    checks.expect(endpoint && endpoint->port() != 0, "UDP socket must expose assigned port");
    if (!endpoint) {
        return;
    }

    const std::array payload{std::byte{'u'}, std::byte{'d'}, std::byte{'p'}};
    vosp::transport::UdpSocket sender{options};
    auto sent = sender.send_to(*endpoint, payload);
    checks.expect(sent && *sent == payload.size(), "UDP datagram must be sent completely");
    auto received = receiver.receive(64);
    checks.expect(received && received->payload().size() == payload.size(),
                  "UDP datagram must preserve payload size");
    checks.expect(received && std::ranges::equal(received->payload(), payload),
                  "UDP datagram must preserve payload bytes");

    checks.expect(sender.send_to(*endpoint, payload).has_value(),
                  "second UDP datagram must use the cached endpoint");
    auto truncated = receiver.receive(2);
    checks.expect(!truncated &&
                      truncated.error().kind() == vosp::transport::ErrorCode::datagram_too_large,
                  "UDP receive must reject truncation instead of returning partial data");
}
} // namespace

int main() {
    Checks checks;
    test_invalid_arguments(checks);
    test_tcp_protocol_round_trip(checks);
    test_tcp_reconnect(checks);
    test_udp_round_trip(checks);
    return checks.result();
}
