#include <vosp/protocol.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    const std::size_t iterations =
        argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 200000U;
    const std::size_t payload_size =
        argc > 2 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : 256U;

    vosp::protocol::FrameCodec codec;
    const vosp::protocol::Message message{vosp::protocol::Version{1, 0}, 7, 42,
                                          std::vector<std::byte>(payload_size, std::byte{0x5a})};

    std::size_t consumed = 0;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        auto frame = codec.encode(message);
        if (!frame) {
            return 1;
        }
        auto decoded = codec.decode(*frame);
        if (!decoded) {
            return 2;
        }
        consumed += decoded->payload().size();
    }
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double operations = static_cast<double>(iterations) / elapsed;
    std::cout << "iterations,payload_bytes,seconds,round_trips_per_second,"
                 "consumed_bytes\n"
              << iterations << ',' << payload_size << ',' << elapsed << ',' << operations << ','
              << consumed << '\n';
    return consumed == iterations * payload_size ? 0 : 3;
}
