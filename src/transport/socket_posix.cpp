#include "socket_platform.hpp"

#ifndef _WIN32

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <format>
#include <string_view>

namespace vosp::transport::detail {
namespace {
static_assert(sizeof(sockaddr_storage) <= sizeof(NativeAddress::storage));

[[nodiscard]] int native(NativeSocket socket) noexcept {
    return static_cast<int>(socket.value);
}

[[nodiscard]] NativeAddress copy_address(const sockaddr* address, socklen_t size) {
    NativeAddress result;
    result.size = size;
    result.family = address->sa_family;
    std::memcpy(result.storage.data(), address, size);
    return result;
}

[[nodiscard]] const sockaddr* address_of(const NativeAddress& address) noexcept {
    return reinterpret_cast<const sockaddr*>(address.storage.data());
}

[[nodiscard]] std::string native_message(std::string_view operation, int code) {
    return std::format("{} failed: {}", operation, std::strerror(code));
}

[[nodiscard]] OperationResult set_timeout(NativeSocket socket, int option,
                                          std::chrono::milliseconds timeout) {
    if (timeout.count() == 0) {
        return {};
    }
    const timeval value{.tv_sec = static_cast<time_t>(timeout.count() / 1000),
                        .tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
    if (::setsockopt(native(socket), SOL_SOCKET, option, &value, sizeof(value)) != 0) {
        const int code = errno;
        return failure(ErrorCode::option_failed, native_message("setsockopt", code), code);
    }
    return {};
}
} // namespace

OperationResult initialize_network() {
    return {};
}

Result<std::vector<NativeAddress>> resolve(const IpEndpoint& endpoint, SocketType type,
                                           bool passive) {
    if (!endpoint.valid()) {
        return failure<std::vector<NativeAddress>>(ErrorCode::invalid_argument,
                                                   "transport endpoint host is empty");
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type == SocketType::stream ? SOCK_STREAM : SOCK_DGRAM;
    hints.ai_protocol = type == SocketType::stream ? IPPROTO_TCP : IPPROTO_UDP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    const std::string service = std::to_string(endpoint.port());
    const char* host = passive && endpoint.host() == "*" ? nullptr : endpoint.host().c_str();

    addrinfo* entries = nullptr;
    const int status = ::getaddrinfo(host, service.c_str(), &hints, &entries);
    if (status != 0) {
        return failure<std::vector<NativeAddress>>(
            ErrorCode::address_resolution_failed,
            std::format("address resolution failed: {}", ::gai_strerror(status)), status);
    }

    std::vector<NativeAddress> addresses;
    for (const addrinfo* entry = entries; entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_addrlen <= sizeof(NativeAddress::storage)) {
            addresses.push_back(
                copy_address(entry->ai_addr, static_cast<socklen_t>(entry->ai_addrlen)));
        }
    }
    ::freeaddrinfo(entries);
    if (addresses.empty()) {
        return failure<std::vector<NativeAddress>>(ErrorCode::address_resolution_failed,
                                                   "address resolution returned no endpoints");
    }
    return addresses;
}

Result<IpEndpoint> to_endpoint(const NativeAddress& address) {
    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    const int status =
        ::getnameinfo(address_of(address), static_cast<socklen_t>(address.size), host.data(),
                      host.size(), service.data(), service.size(), NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
        return failure<IpEndpoint>(
            ErrorCode::address_resolution_failed,
            std::format("endpoint conversion failed: {}", ::gai_strerror(status)), status);
    }
    try {
        return IpEndpoint{host.data(), static_cast<std::uint16_t>(std::stoul(service.data()))};
    } catch (const std::exception&) {
        return failure<IpEndpoint>(ErrorCode::address_resolution_failed,
                                   "endpoint service is not a valid port");
    }
}

Result<NativeSocket> create_socket(const NativeAddress& address, SocketType type) {
    const int descriptor =
        ::socket(address.family, type == SocketType::stream ? SOCK_STREAM : SOCK_DGRAM,
                 type == SocketType::stream ? IPPROTO_TCP : IPPROTO_UDP);
    if (descriptor < 0) {
        const int code = errno;
        return failure<NativeSocket>(ErrorCode::socket_creation_failed,
                                     native_message("socket", code), code);
    }
    return NativeSocket{static_cast<std::uintptr_t>(descriptor)};
}

OperationResult apply_options(NativeSocket socket, const IoOptions& options) {
    if (!options.valid()) {
        return failure(ErrorCode::invalid_argument, "transport I/O options are invalid");
    }
    auto receive = set_timeout(socket, SO_RCVTIMEO, options.receive_timeout);
    if (!receive) {
        return receive;
    }
    return set_timeout(socket, SO_SNDTIMEO, options.send_timeout);
}

OperationResult set_reuse_address(NativeSocket socket) {
    constexpr int enabled = 1;
    if (::setsockopt(native(socket), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
        const int code = errno;
        return failure(ErrorCode::option_failed, native_message("setsockopt", code), code);
    }
    return {};
}

OperationResult connect_socket(NativeSocket socket, const NativeAddress& address) {
    if (::connect(native(socket), address_of(address), static_cast<socklen_t>(address.size)) != 0) {
        const int code = errno;
        return failure(ErrorCode::connect_failed, native_message("connect", code), code);
    }
    return {};
}

OperationResult bind_socket(NativeSocket socket, const NativeAddress& address) {
    if (::bind(native(socket), address_of(address), static_cast<socklen_t>(address.size)) != 0) {
        const int code = errno;
        return failure(ErrorCode::bind_failed, native_message("bind", code), code);
    }
    return {};
}

OperationResult listen_socket(NativeSocket socket, int backlog) {
    if (::listen(native(socket), backlog) != 0) {
        const int code = errno;
        return failure(ErrorCode::listen_failed, native_message("listen", code), code);
    }
    return {};
}

Result<AcceptedSocket> accept_socket(NativeSocket socket) {
    sockaddr_storage storage{};
    socklen_t size = sizeof(storage);
    const int accepted = ::accept(native(socket), reinterpret_cast<sockaddr*>(&storage), &size);
    if (accepted < 0) {
        const int code = errno;
        return failure<AcceptedSocket>(ErrorCode::accept_failed, native_message("accept", code),
                                       code);
    }
    return AcceptedSocket{.socket = NativeSocket{static_cast<std::uintptr_t>(accepted)},
                          .remote = copy_address(reinterpret_cast<sockaddr*>(&storage), size)};
}

Result<NativeAddress> local_address(NativeSocket socket) {
    sockaddr_storage storage{};
    socklen_t size = sizeof(storage);
    if (::getsockname(native(socket), reinterpret_cast<sockaddr*>(&storage), &size) != 0) {
        const int code = errno;
        return failure<NativeAddress>(ErrorCode::address_resolution_failed,
                                      native_message("getsockname", code), code);
    }
    return copy_address(reinterpret_cast<sockaddr*>(&storage), size);
}

Result<std::size_t> send_socket(NativeSocket socket, std::span<const std::byte> bytes) {
    const std::size_t requested = std::min(bytes.size(), static_cast<std::size_t>(INT_MAX));
    const ssize_t sent = ::send(native(socket), bytes.data(), requested, MSG_NOSIGNAL);
    if (sent < 0) {
        const int code = errno;
        return failure<std::size_t>(ErrorCode::send_failed, native_message("send", code), code);
    }
    return static_cast<std::size_t>(sent);
}

Result<std::size_t> receive_socket(NativeSocket socket, std::span<std::byte> output) {
    const std::size_t requested = std::min(output.size(), static_cast<std::size_t>(INT_MAX));
    const ssize_t received = ::recv(native(socket), output.data(), requested, 0);
    if (received < 0) {
        const int code = errno;
        return failure<std::size_t>(ErrorCode::receive_failed, native_message("recv", code), code);
    }
    return static_cast<std::size_t>(received);
}

Result<std::size_t> send_datagram(NativeSocket socket, const NativeAddress& address,
                                  std::span<const std::byte> payload) {
    const ssize_t sent = ::sendto(native(socket), payload.data(), payload.size(), MSG_NOSIGNAL,
                                  address_of(address), static_cast<socklen_t>(address.size));
    if (sent < 0) {
        const int code = errno;
        return failure<std::size_t>(ErrorCode::send_failed, native_message("sendto", code), code);
    }
    return static_cast<std::size_t>(sent);
}

Result<ReceivedDatagram> receive_datagram(NativeSocket socket, std::span<std::byte> output) {
    sockaddr_storage storage{};
    socklen_t address_size = sizeof(storage);
    const ssize_t received = ::recvfrom(native(socket), output.data(), output.size(), MSG_TRUNC,
                                        reinterpret_cast<sockaddr*>(&storage), &address_size);
    if (received < 0) {
        const int code = errno;
        return failure<ReceivedDatagram>(ErrorCode::receive_failed,
                                         native_message("recvfrom", code), code);
    }
    if (static_cast<std::size_t>(received) > output.size()) {
        return failure<ReceivedDatagram>(ErrorCode::datagram_too_large,
                                         "received UDP datagram exceeds the requested bound");
    }
    return ReceivedDatagram{.remote =
                                copy_address(reinterpret_cast<sockaddr*>(&storage), address_size),
                            .size = static_cast<std::size_t>(received)};
}

void close_socket(NativeSocket& socket) noexcept {
    if (!socket.valid()) {
        return;
    }
    ::shutdown(native(socket), SHUT_RDWR);
    ::close(native(socket));
    socket.value = NativeSocket::invalid_value;
}
} // namespace vosp::transport::detail

#endif
