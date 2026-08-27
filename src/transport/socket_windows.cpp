#include "socket_platform.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <format>
#include <string_view>

namespace vosp::transport::detail {
namespace {
static_assert(sizeof(sockaddr_storage) <= 128);

class WinsockRuntime final {
  public:
    WinsockRuntime() noexcept {
        WSADATA data{};
        status_ = ::WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockRuntime() {
        if (status_ == 0) {
            ::WSACleanup();
        }
    }

    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;

    [[nodiscard]] int status() const noexcept {
        return status_;
    }

  private:
    int status_ = WSASYSNOTREADY;
};

[[nodiscard]] WinsockRuntime& runtime() {
    static WinsockRuntime instance;
    return instance;
}

[[nodiscard]] SOCKET native(NativeSocket socket) noexcept {
    return static_cast<SOCKET>(socket.value);
}

[[nodiscard]] NativeAddress copy_address(const sockaddr* address, int size) {
    NativeAddress result;
    result.size = static_cast<std::size_t>(size);
    result.family = address->sa_family;
    std::memcpy(result.storage.data(), address, result.size);
    return result;
}

[[nodiscard]] const sockaddr* address_of(const NativeAddress& address) noexcept {
    return reinterpret_cast<const sockaddr*>(address.storage.data());
}

[[nodiscard]] std::string native_message(std::string_view operation, int code) {
    return std::format("{} failed with WinSock error {}", operation, code);
}

[[nodiscard]] OperationResult set_timeout(NativeSocket socket, int option,
                                          std::chrono::milliseconds timeout) {
    if (timeout.count() == 0) {
        return {};
    }
    const auto bounded = std::min<std::int64_t>(timeout.count(), UINT_MAX);
    const DWORD value = static_cast<DWORD>(bounded);
    if (::setsockopt(native(socket), SOL_SOCKET, option, reinterpret_cast<const char*>(&value),
                     sizeof(value)) == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure(ErrorCode::option_failed, native_message("setsockopt", code), code);
    }
    return {};
}
} // namespace

OperationResult initialize_network() {
    const int status = runtime().status();
    if (status != 0) {
        return failure(ErrorCode::platform_initialization_failed,
                       native_message("WSAStartup", status), status);
    }
    return {};
}

Result<std::vector<NativeAddress>> resolve(const IpEndpoint& endpoint, SocketType type,
                                           bool passive) {
    auto initialized = initialize_network();
    if (!initialized) {
        return std::unexpected{initialized.error()};
    }
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
            std::format("address resolution failed with WinSock error {}", status), status);
    }

    std::vector<NativeAddress> addresses;
    for (const addrinfo* entry = entries; entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_addrlen <= sizeof(NativeAddress::storage)) {
            addresses.push_back(copy_address(entry->ai_addr, static_cast<int>(entry->ai_addrlen)));
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
        ::getnameinfo(address_of(address), static_cast<int>(address.size), host.data(),
                      static_cast<DWORD>(host.size()), service.data(),
                      static_cast<DWORD>(service.size()), NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
        return failure<IpEndpoint>(
            ErrorCode::address_resolution_failed,
            std::format("endpoint conversion failed with WinSock error {}", status), status);
    }
    try {
        return IpEndpoint{host.data(), static_cast<std::uint16_t>(std::stoul(service.data()))};
    } catch (const std::exception&) {
        return failure<IpEndpoint>(ErrorCode::address_resolution_failed,
                                   "endpoint service is not a valid port");
    }
}

Result<NativeSocket> create_socket(const NativeAddress& address, SocketType type) {
    auto initialized = initialize_network();
    if (!initialized) {
        return std::unexpected{initialized.error()};
    }
    const SOCKET descriptor =
        ::socket(address.family, type == SocketType::stream ? SOCK_STREAM : SOCK_DGRAM,
                 type == SocketType::stream ? IPPROTO_TCP : IPPROTO_UDP);
    if (descriptor == INVALID_SOCKET) {
        const int code = ::WSAGetLastError();
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
    constexpr BOOL enabled = TRUE;
    if (::setsockopt(native(socket), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure(ErrorCode::option_failed, native_message("setsockopt", code), code);
    }
    return {};
}

OperationResult connect_socket(NativeSocket socket, const NativeAddress& address) {
    if (::connect(native(socket), address_of(address), static_cast<int>(address.size)) ==
        SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure(ErrorCode::connect_failed, native_message("connect", code), code);
    }
    return {};
}

OperationResult bind_socket(NativeSocket socket, const NativeAddress& address) {
    if (::bind(native(socket), address_of(address), static_cast<int>(address.size)) ==
        SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure(ErrorCode::bind_failed, native_message("bind", code), code);
    }
    return {};
}

OperationResult listen_socket(NativeSocket socket, int backlog) {
    if (::listen(native(socket), backlog) == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure(ErrorCode::listen_failed, native_message("listen", code), code);
    }
    return {};
}

Result<AcceptedSocket> accept_socket(NativeSocket socket) {
    sockaddr_storage storage{};
    int size = sizeof(storage);
    const SOCKET accepted = ::accept(native(socket), reinterpret_cast<sockaddr*>(&storage), &size);
    if (accepted == INVALID_SOCKET) {
        const int code = ::WSAGetLastError();
        return failure<AcceptedSocket>(ErrorCode::accept_failed, native_message("accept", code),
                                       code);
    }
    return AcceptedSocket{.socket = NativeSocket{static_cast<std::uintptr_t>(accepted)},
                          .remote = copy_address(reinterpret_cast<sockaddr*>(&storage), size)};
}

Result<NativeAddress> local_address(NativeSocket socket) {
    sockaddr_storage storage{};
    int size = sizeof(storage);
    if (::getsockname(native(socket), reinterpret_cast<sockaddr*>(&storage), &size) ==
        SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure<NativeAddress>(ErrorCode::address_resolution_failed,
                                      native_message("getsockname", code), code);
    }
    return copy_address(reinterpret_cast<sockaddr*>(&storage), size);
}

Result<std::size_t> send_socket(NativeSocket socket, std::span<const std::byte> bytes) {
    const int requested =
        static_cast<int>(std::min(bytes.size(), static_cast<std::size_t>(INT_MAX)));
    const int sent =
        ::send(native(socket), reinterpret_cast<const char*>(bytes.data()), requested, 0);
    if (sent == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure<std::size_t>(ErrorCode::send_failed, native_message("send", code), code);
    }
    return static_cast<std::size_t>(sent);
}

Result<std::size_t> receive_socket(NativeSocket socket, std::span<std::byte> output) {
    const int requested =
        static_cast<int>(std::min(output.size(), static_cast<std::size_t>(INT_MAX)));
    const int received =
        ::recv(native(socket), reinterpret_cast<char*>(output.data()), requested, 0);
    if (received == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure<std::size_t>(ErrorCode::receive_failed, native_message("recv", code), code);
    }
    return static_cast<std::size_t>(received);
}

Result<std::size_t> send_datagram(NativeSocket socket, const NativeAddress& address,
                                  std::span<const std::byte> payload) {
    const int sent = ::sendto(native(socket), reinterpret_cast<const char*>(payload.data()),
                              static_cast<int>(payload.size()), 0, address_of(address),
                              static_cast<int>(address.size));
    if (sent == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        return failure<std::size_t>(ErrorCode::send_failed, native_message("sendto", code), code);
    }
    return static_cast<std::size_t>(sent);
}

Result<ReceivedDatagram> receive_datagram(NativeSocket socket, std::span<std::byte> output) {
    sockaddr_storage storage{};
    int address_size = sizeof(storage);
    const int received = ::recvfrom(native(socket), reinterpret_cast<char*>(output.data()),
                                    static_cast<int>(output.size()), 0,
                                    reinterpret_cast<sockaddr*>(&storage), &address_size);
    if (received == SOCKET_ERROR) {
        const int code = ::WSAGetLastError();
        if (code == WSAEMSGSIZE) {
            return failure<ReceivedDatagram>(ErrorCode::datagram_too_large,
                                             "received UDP datagram exceeds the requested bound",
                                             code);
        }
        return failure<ReceivedDatagram>(ErrorCode::receive_failed,
                                         native_message("recvfrom", code), code);
    }
    return ReceivedDatagram{.remote =
                                copy_address(reinterpret_cast<sockaddr*>(&storage), address_size),
                            .size = static_cast<std::size_t>(received)};
}

void close_socket(NativeSocket& socket) noexcept {
    if (!socket.valid()) {
        return;
    }
    ::shutdown(native(socket), SD_BOTH);
    ::closesocket(native(socket));
    socket.value = NativeSocket::invalid_value;
}
} // namespace vosp::transport::detail

#endif
