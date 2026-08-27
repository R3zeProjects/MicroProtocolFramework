#pragma once

/** @file tcp.hpp Move-only RAII TCP stream and listener. */

#include <cstddef>
#include <memory>
#include <span>

#include <vosp/contracts/transport.hpp>
#include <vosp/transport/error.hpp>
#include <vosp/transport/types.hpp>

namespace vosp::transport {
class TcpListener;

/** @brief One blocking TCP connection with explicit close and bounded reconnect. */
class TcpStream final {
  public:
    explicit TcpStream(IoOptions options = {}, ReconnectPolicy reconnect = {});
    ~TcpStream();

    TcpStream(TcpStream&&) noexcept;
    TcpStream& operator=(TcpStream&&) noexcept;
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    [[nodiscard]] OperationResult connect(const IpEndpoint& endpoint);
    [[nodiscard]] OperationResult reconnect();
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] Result<std::size_t> send(std::span<const std::byte> bytes);
    [[nodiscard]] Result<std::size_t> send_all(std::span<const std::byte> bytes);
    [[nodiscard]] Result<std::size_t> receive(std::span<std::byte> output);
    void close() noexcept;

  private:
    struct Impl;
    explicit TcpStream(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
    friend class TcpListener;
};

/** @brief Blocking dual-stack TCP listener suitable for bounded service loops. */
class TcpListener final {
  public:
    explicit TcpListener(IoOptions accepted_stream_options = {});
    ~TcpListener();

    TcpListener(TcpListener&&) noexcept;
    TcpListener& operator=(TcpListener&&) noexcept;
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    [[nodiscard]] OperationResult bind(const IpEndpoint& endpoint, int backlog = 128);
    [[nodiscard]] Result<TcpStream> accept();
    [[nodiscard]] Result<IpEndpoint> local_endpoint() const;
    [[nodiscard]] bool listening() const noexcept;
    void close() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

static_assert(vosp::contracts::ByteStreamTransport<TcpStream, Model>);
static_assert(vosp::contracts::TransportConnector<TcpStream, IpEndpoint, Model>);
} // namespace vosp::transport
