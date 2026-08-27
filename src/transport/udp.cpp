#include <vosp/transport/udp.hpp>

#include "socket_platform.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace vosp::transport {
struct UdpSocket::Impl {
    detail::NativeSocket socket;
    IoOptions options;
    int family = 0;
    std::optional<IpEndpoint> cached_endpoint;
    std::optional<detail::NativeAddress> cached_address;
};

UdpSocket::UdpSocket(IoOptions options)
    : implementation_{std::make_unique<Impl>(Impl{.socket = {},
                                                  .options = options,
                                                  .family = 0,
                                                  .cached_endpoint = std::nullopt,
                                                  .cached_address = std::nullopt})} {}

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}

OperationResult UdpSocket::bind(const IpEndpoint& endpoint) {
    if (!implementation_) {
        return detail::failure(ErrorCode::invalid_argument, "cannot reuse a moved-from UDP socket");
    }
    if (!endpoint.valid() || !implementation_->options.valid()) {
        return detail::failure(ErrorCode::invalid_argument,
                               "UDP endpoint or socket options are invalid");
    }

    close();
    auto addresses = detail::resolve(endpoint, detail::SocketType::datagram, true);
    if (!addresses) {
        return std::unexpected{addresses.error()};
    }
    std::optional<Error> last_error;
    for (const detail::NativeAddress& address : *addresses) {
        auto socket = detail::create_socket(address, detail::SocketType::datagram);
        if (!socket) {
            last_error = socket.error();
            continue;
        }
        auto configured = detail::apply_options(*socket, implementation_->options);
        if (!configured) {
            last_error = configured.error();
            detail::close_socket(*socket);
            continue;
        }
        auto bound = detail::bind_socket(*socket, address);
        if (!bound) {
            last_error = bound.error();
            detail::close_socket(*socket);
            continue;
        }
        implementation_->socket = *socket;
        implementation_->family = address.family;
        return {};
    }
    if (last_error) {
        return std::unexpected{std::move(*last_error)};
    }
    return detail::failure(ErrorCode::bind_failed, "no usable UDP bind endpoint was resolved");
}

Result<std::size_t> UdpSocket::send_to(const IpEndpoint& endpoint,
                                       std::span<const std::byte> payload) {
    if (!implementation_) {
        return detail::failure<std::size_t>(ErrorCode::invalid_argument,
                                            "cannot reuse a moved-from UDP socket");
    }
    if (!endpoint.valid() || !implementation_->options.valid()) {
        return detail::failure<std::size_t>(ErrorCode::invalid_argument,
                                            "UDP endpoint or socket options are invalid");
    }
    if (payload.size() > maximum_payload_size) {
        return detail::failure<std::size_t>(ErrorCode::datagram_too_large,
                                            "UDP payload exceeds 65507 bytes");
    }

    const auto ensure_open = [this](const detail::NativeAddress& address) -> OperationResult {
        if (open()) {
            if (address.family == implementation_->family) {
                return {};
            }
            return detail::failure(ErrorCode::send_failed,
                                   "UDP endpoint does not match the open socket family");
        }
        auto socket = detail::create_socket(address, detail::SocketType::datagram);
        if (!socket) {
            return std::unexpected{socket.error()};
        }
        auto configured = detail::apply_options(*socket, implementation_->options);
        if (!configured) {
            detail::close_socket(*socket);
            return configured;
        }
        implementation_->socket = *socket;
        implementation_->family = address.family;
        return {};
    };

    if (implementation_->cached_endpoint == endpoint && implementation_->cached_address) {
        auto prepared = ensure_open(*implementation_->cached_address);
        if (!prepared) {
            return std::unexpected{prepared.error()};
        }
        return detail::send_datagram(implementation_->socket, *implementation_->cached_address,
                                     payload);
    }

    auto addresses = detail::resolve(endpoint, detail::SocketType::datagram, false);
    if (!addresses) {
        return std::unexpected{addresses.error()};
    }

    std::optional<Error> last_error;
    for (const detail::NativeAddress& address : *addresses) {
        auto prepared = ensure_open(address);
        if (!prepared) {
            last_error = prepared.error();
            continue;
        }
        auto sent = detail::send_datagram(implementation_->socket, address, payload);
        if (sent) {
            implementation_->cached_endpoint = endpoint;
            implementation_->cached_address = address;
            return sent;
        }
        last_error = sent.error();
    }
    if (last_error) {
        return std::unexpected{std::move(*last_error)};
    }
    return detail::failure<std::size_t>(ErrorCode::send_failed,
                                        "no UDP endpoint matches the open socket family");
}

Result<Datagram> UdpSocket::receive(std::size_t maximum_size) {
    if (!open()) {
        return detail::failure<Datagram>(ErrorCode::not_connected, "UDP socket is not open");
    }
    if (maximum_size == 0 || maximum_size > maximum_payload_size) {
        return detail::failure<Datagram>(ErrorCode::invalid_argument,
                                         "UDP receive bound must be in [1, 65507]");
    }
    std::vector<std::byte> payload(maximum_size);
    auto received = detail::receive_datagram(implementation_->socket, payload);
    if (!received) {
        return std::unexpected{received.error()};
    }
    payload.resize(received->size);
    auto endpoint = detail::to_endpoint(received->remote);
    if (!endpoint) {
        return std::unexpected{endpoint.error()};
    }
    return Datagram{std::move(*endpoint), std::move(payload)};
}

Result<IpEndpoint> UdpSocket::local_endpoint() const {
    if (!open()) {
        return detail::failure<IpEndpoint>(ErrorCode::not_connected, "UDP socket is not open");
    }
    auto address = detail::local_address(implementation_->socket);
    if (!address) {
        return std::unexpected{address.error()};
    }
    return detail::to_endpoint(*address);
}

bool UdpSocket::open() const noexcept {
    return implementation_ && implementation_->socket.valid();
}

void UdpSocket::close() noexcept {
    if (implementation_) {
        detail::close_socket(implementation_->socket);
        implementation_->family = 0;
    }
}
} // namespace vosp::transport
