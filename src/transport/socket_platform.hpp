#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <vosp/transport/error.hpp>
#include <vosp/transport/types.hpp>

namespace vosp::transport::detail {
enum class SocketType : std::uint8_t { stream, datagram };

struct NativeSocket {
    static constexpr std::uintptr_t invalid_value = std::numeric_limits<std::uintptr_t>::max();
    std::uintptr_t value = invalid_value;

    [[nodiscard]] bool valid() const noexcept {
        return value != invalid_value;
    }
};

struct NativeAddress {
    alignas(std::max_align_t) std::array<std::byte, 128> storage{};
    std::size_t size = 0;
    int family = 0;
};

struct AcceptedSocket {
    NativeSocket socket;
    NativeAddress remote;
};

struct ReceivedDatagram {
    NativeAddress remote;
    std::size_t size = 0;
};

[[nodiscard]] OperationResult initialize_network();
[[nodiscard]] Result<std::vector<NativeAddress>> resolve(const IpEndpoint& endpoint,
                                                         SocketType type, bool passive);
[[nodiscard]] Result<IpEndpoint> to_endpoint(const NativeAddress& address);
[[nodiscard]] Result<NativeSocket> create_socket(const NativeAddress& address, SocketType type);
[[nodiscard]] OperationResult apply_options(NativeSocket socket, const IoOptions& options);
[[nodiscard]] OperationResult set_reuse_address(NativeSocket socket);
[[nodiscard]] OperationResult connect_socket(NativeSocket socket, const NativeAddress& address);
[[nodiscard]] OperationResult bind_socket(NativeSocket socket, const NativeAddress& address);
[[nodiscard]] OperationResult listen_socket(NativeSocket socket, int backlog);
[[nodiscard]] Result<AcceptedSocket> accept_socket(NativeSocket socket);
[[nodiscard]] Result<NativeAddress> local_address(NativeSocket socket);
[[nodiscard]] Result<std::size_t> send_socket(NativeSocket socket,
                                              std::span<const std::byte> bytes);
[[nodiscard]] Result<std::size_t> receive_socket(NativeSocket socket, std::span<std::byte> output);
[[nodiscard]] Result<std::size_t> send_datagram(NativeSocket socket, const NativeAddress& address,
                                                std::span<const std::byte> payload);
[[nodiscard]] Result<ReceivedDatagram> receive_datagram(NativeSocket socket,
                                                        std::span<std::byte> output);
void close_socket(NativeSocket& socket) noexcept;
} // namespace vosp::transport::detail
