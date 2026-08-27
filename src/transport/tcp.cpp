#include <vosp/transport/tcp.hpp>

#include "socket_platform.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

namespace vosp::transport {
struct TcpStream::Impl {
    detail::NativeSocket socket;
    IoOptions options;
    ReconnectPolicy reconnect_policy;
    std::optional<IpEndpoint> endpoint;
};

struct TcpListener::Impl {
    detail::NativeSocket socket;
    IoOptions accepted_stream_options;
};

TcpStream::TcpStream(IoOptions options, ReconnectPolicy reconnect)
    : implementation_{std::make_unique<Impl>(Impl{.socket = {},
                                                  .options = options,
                                                  .reconnect_policy = reconnect,
                                                  .endpoint = std::nullopt})} {}

TcpStream::TcpStream(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

TcpStream::~TcpStream() {
    close();
}

TcpStream::TcpStream(TcpStream&&) noexcept = default;

TcpStream& TcpStream::operator=(TcpStream&& other) noexcept {
    if (this != &other) {
        close();
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}

OperationResult TcpStream::connect(const IpEndpoint& endpoint) {
    if (!implementation_) {
        return detail::failure(ErrorCode::invalid_argument, "cannot reuse a moved-from TCP stream");
    }
    if (!endpoint.valid() || !implementation_->options.valid() ||
        !implementation_->reconnect_policy.valid()) {
        return detail::failure(ErrorCode::invalid_argument,
                               "TCP endpoint or stream options are invalid");
    }

    close();
    implementation_->endpoint = endpoint;
    auto addresses = detail::resolve(endpoint, detail::SocketType::stream, false);
    if (!addresses) {
        return std::unexpected{addresses.error()};
    }

    std::optional<Error> last_error;
    for (const detail::NativeAddress& address : *addresses) {
        auto socket = detail::create_socket(address, detail::SocketType::stream);
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
        auto connected = detail::connect_socket(*socket, address);
        if (!connected) {
            last_error = connected.error();
            detail::close_socket(*socket);
            continue;
        }
        implementation_->socket = *socket;
        return {};
    }

    if (last_error) {
        return std::unexpected{std::move(*last_error)};
    }
    return detail::failure(ErrorCode::connect_failed, "no usable TCP endpoint was resolved");
}

OperationResult TcpStream::reconnect() {
    if (!implementation_ || !implementation_->endpoint) {
        return detail::failure(ErrorCode::not_connected,
                               "TCP reconnect requires a previous endpoint");
    }
    if (!implementation_->reconnect_policy.valid()) {
        return detail::failure(ErrorCode::invalid_argument, "TCP reconnect policy is invalid");
    }

    const IpEndpoint endpoint = *implementation_->endpoint;
    auto delay = implementation_->reconnect_policy.initial_delay;
    std::optional<Error> last_error;
    for (std::size_t attempt = 0; attempt < implementation_->reconnect_policy.max_attempts;
         ++attempt) {
        auto result = connect(endpoint);
        if (result) {
            return {};
        }
        last_error = result.error();
        if (attempt + 1U < implementation_->reconnect_policy.max_attempts && delay.count() > 0) {
            std::this_thread::sleep_for(delay);
            delay = std::min(delay * 2, implementation_->reconnect_policy.maximum_delay);
        }
    }
    const int native_code = last_error ? last_error->native_code() : 0;
    return detail::failure(ErrorCode::retry_limit_exceeded, "TCP reconnect retry limit exceeded",
                           native_code);
}

bool TcpStream::connected() const noexcept {
    return implementation_ && implementation_->socket.valid();
}

Result<std::size_t> TcpStream::send(std::span<const std::byte> bytes) {
    if (!connected()) {
        return detail::failure<std::size_t>(ErrorCode::not_connected,
                                            "TCP stream is not connected");
    }
    if (bytes.empty()) {
        return std::size_t{0};
    }
    auto result = detail::send_socket(implementation_->socket, bytes);
    if (!result) {
        close();
    }
    return result;
}

Result<std::size_t> TcpStream::send_all(std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto sent = send(bytes.subspan(offset));
        if (!sent) {
            return std::unexpected{sent.error()};
        }
        if (*sent == 0) {
            close();
            return detail::failure<std::size_t>(ErrorCode::peer_closed,
                                                "TCP peer closed while sending");
        }
        offset += *sent;
    }
    return offset;
}

Result<std::size_t> TcpStream::receive(std::span<std::byte> output) {
    if (!connected()) {
        return detail::failure<std::size_t>(ErrorCode::not_connected,
                                            "TCP stream is not connected");
    }
    if (output.empty()) {
        return std::size_t{0};
    }
    auto received = detail::receive_socket(implementation_->socket, output);
    if (!received) {
        close();
        return received;
    }
    if (*received == 0) {
        close();
        return detail::failure<std::size_t>(ErrorCode::peer_closed, "TCP peer closed the stream");
    }
    return received;
}

void TcpStream::close() noexcept {
    if (implementation_) {
        detail::close_socket(implementation_->socket);
    }
}

TcpListener::TcpListener(IoOptions accepted_stream_options)
    : implementation_{std::make_unique<Impl>(
          Impl{.socket = {}, .accepted_stream_options = accepted_stream_options})} {}

TcpListener::~TcpListener() {
    close();
}

TcpListener::TcpListener(TcpListener&&) noexcept = default;

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
    if (this != &other) {
        close();
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}

OperationResult TcpListener::bind(const IpEndpoint& endpoint, int backlog) {
    if (!implementation_) {
        return detail::failure(ErrorCode::invalid_argument,
                               "cannot reuse a moved-from TCP listener");
    }
    if (!endpoint.valid() || backlog <= 0 || backlog > 65'535 ||
        !implementation_->accepted_stream_options.valid()) {
        return detail::failure(ErrorCode::invalid_argument,
                               "TCP listener endpoint, backlog, or options are invalid");
    }

    close();
    auto addresses = detail::resolve(endpoint, detail::SocketType::stream, true);
    if (!addresses) {
        return std::unexpected{addresses.error()};
    }
    std::optional<Error> last_error;
    for (const detail::NativeAddress& address : *addresses) {
        auto socket = detail::create_socket(address, detail::SocketType::stream);
        if (!socket) {
            last_error = socket.error();
            continue;
        }
        auto reused = detail::set_reuse_address(*socket);
        if (!reused) {
            last_error = reused.error();
            detail::close_socket(*socket);
            continue;
        }
        auto bound = detail::bind_socket(*socket, address);
        if (!bound) {
            last_error = bound.error();
            detail::close_socket(*socket);
            continue;
        }
        auto listening = detail::listen_socket(*socket, backlog);
        if (!listening) {
            last_error = listening.error();
            detail::close_socket(*socket);
            continue;
        }
        implementation_->socket = *socket;
        return {};
    }
    if (last_error) {
        return std::unexpected{std::move(*last_error)};
    }
    return detail::failure(ErrorCode::bind_failed, "no usable TCP bind endpoint was resolved");
}

Result<TcpStream> TcpListener::accept() {
    if (!listening()) {
        return detail::failure<TcpStream>(ErrorCode::not_connected, "TCP listener is not active");
    }
    auto accepted = detail::accept_socket(implementation_->socket);
    if (!accepted) {
        return std::unexpected{accepted.error()};
    }
    auto configured =
        detail::apply_options(accepted->socket, implementation_->accepted_stream_options);
    if (!configured) {
        detail::close_socket(accepted->socket);
        return std::unexpected{configured.error()};
    }
    auto stream = std::make_unique<TcpStream::Impl>();
    stream->socket = accepted->socket;
    stream->options = implementation_->accepted_stream_options;
    return TcpStream{std::move(stream)};
}

Result<IpEndpoint> TcpListener::local_endpoint() const {
    if (!listening()) {
        return detail::failure<IpEndpoint>(ErrorCode::not_connected, "TCP listener is not active");
    }
    auto address = detail::local_address(implementation_->socket);
    if (!address) {
        return std::unexpected{address.error()};
    }
    return detail::to_endpoint(*address);
}

bool TcpListener::listening() const noexcept {
    return implementation_ && implementation_->socket.valid();
}

void TcpListener::close() noexcept {
    if (implementation_) {
        detail::close_socket(implementation_->socket);
    }
}
} // namespace vosp::transport
