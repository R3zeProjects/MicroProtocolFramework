#include <vosp/transport.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

namespace {
[[nodiscard]] bool receive_exact(vosp::TcpStream& stream, std::span<std::byte> output) {
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

[[nodiscard]] int benchmark_tcp(std::size_t iterations, std::span<const std::byte> payload) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    const vosp::transport::IoOptions options{.receive_timeout = 5s, .send_timeout = 5s};
    vosp::TcpListener listener{options};
    if (!listener.bind(vosp::TcpEndpoint{"127.0.0.1", 0})) {
        return 1;
    }
    auto endpoint = listener.local_endpoint();
    if (!endpoint) {
        return 2;
    }

    std::jthread server{[&] {
        auto stream = listener.accept();
        if (!stream) {
            return;
        }
        std::vector<std::byte> input(payload.size());
        for (std::size_t index = 0; index < iterations; ++index) {
            if (!receive_exact(*stream, input) || !stream->send_all(input)) {
                return;
            }
        }
    }};

    vosp::TcpStream client{options};
    if (!client.connect(*endpoint)) {
        return 3;
    }
    std::vector<std::byte> echoed(payload.size());
    const auto started = Clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        if (!client.send_all(payload) || !receive_exact(client, echoed)) {
            return 4;
        }
    }
    const auto elapsed = Clock::now() - started;
    server.join();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double operations = static_cast<double>(iterations) / seconds;
    const double bytes_per_second =
        2.0 * static_cast<double>(iterations) * static_cast<double>(payload.size()) / seconds;
    std::cout << "tcp," << iterations << ',' << payload.size() << ',' << operations << ','
              << bytes_per_second << '\n';
    return 0;
}

[[nodiscard]] int benchmark_udp(std::size_t iterations, std::span<const std::byte> payload) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    const vosp::transport::IoOptions options{.receive_timeout = 5s, .send_timeout = 5s};
    vosp::UdpSocket server{options};
    if (!server.bind(vosp::TcpEndpoint{"127.0.0.1", 0})) {
        return 5;
    }
    auto endpoint = server.local_endpoint();
    if (!endpoint) {
        return 6;
    }

    vosp::UdpSocket client{options};
    const auto started = Clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        if (!client.send_to(*endpoint, payload)) {
            return 7;
        }
        auto received = server.receive(payload.size());
        if (!received || !server.send_to(received->endpoint(), received->payload())) {
            return 8;
        }
        if (!client.receive(payload.size())) {
            return 9;
        }
    }
    const auto elapsed = Clock::now() - started;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double operations = static_cast<double>(iterations) / seconds;
    const double bytes_per_second =
        2.0 * static_cast<double>(iterations) * static_cast<double>(payload.size()) / seconds;
    std::cout << "udp," << iterations << ',' << payload.size() << ',' << operations << ','
              << bytes_per_second << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations =
        argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 10'000;
    const std::size_t payload_size =
        argc > 2 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : 256;
    if (iterations == 0 || iterations > 10'000'000 || payload_size == 0 ||
        payload_size > vosp::UdpSocket::maximum_payload_size) {
        std::cerr << "usage: transport_benchmark [iterations>0] [payload=1..65507]\n";
        return 64;
    }
    std::vector<std::byte> payload(payload_size, std::byte{0x5a});
    std::cout
        << "transport,iterations,payload_bytes,round_trips_per_second,wire_bytes_per_second\n";
    if (const int status = benchmark_tcp(iterations, payload); status != 0) {
        return status;
    }
    return benchmark_udp(iterations, payload);
}
