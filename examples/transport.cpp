#include <vosp/transport.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    vosp::UdpSocket receiver{{.receive_timeout = 1s, .send_timeout = 1s}};
    if (auto bound = receiver.bind(vosp::TcpEndpoint{"127.0.0.1", 0}); !bound) {
        std::cerr << bound.error().message() << '\n';
        return 1;
    }
    auto endpoint = receiver.local_endpoint();
    if (!endpoint) {
        std::cerr << endpoint.error().message() << '\n';
        return 2;
    }

    const std::array payload{std::byte{'V'}, std::byte{'S'}, std::byte{'P'}};
    vosp::UdpSocket sender;
    if (auto sent = sender.send_to(*endpoint, payload); !sent) {
        std::cerr << sent.error().message() << '\n';
        return 3;
    }
    auto datagram = receiver.receive(64);
    if (!datagram) {
        std::cerr << datagram.error().message() << '\n';
        return 4;
    }
    std::cout << "received " << datagram->payload().size() << " bytes\n";
    return 0;
}
