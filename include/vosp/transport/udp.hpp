#pragma once

/** @file udp.hpp Move-only RAII UDP socket. */

#include <cstddef>
#include <memory>
#include <span>

#include <vosp/contracts/transport.hpp>
#include <vosp/transport/error.hpp>
#include <vosp/transport/types.hpp>

namespace vosp::transport {
/** @brief Blocking UDP socket with owning receives and an explicit payload bound. */
class UdpSocket final {
  public:
    static constexpr std::size_t maximum_payload_size = 65'507;

    explicit UdpSocket(IoOptions options = {});
    ~UdpSocket();

    UdpSocket(UdpSocket&&) noexcept;
    UdpSocket& operator=(UdpSocket&&) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    [[nodiscard]] OperationResult bind(const IpEndpoint& endpoint);
    [[nodiscard]] Result<std::size_t> send_to(const IpEndpoint& endpoint,
                                              std::span<const std::byte> payload);
    [[nodiscard]] Result<Datagram> receive(std::size_t maximum_size = maximum_payload_size);
    [[nodiscard]] Result<IpEndpoint> local_endpoint() const;
    [[nodiscard]] bool open() const noexcept;
    void close() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

static_assert(vosp::contracts::TransportDatagram<Datagram>);
static_assert(vosp::contracts::DatagramTransport<UdpSocket, IpEndpoint, Datagram, Model>);
} // namespace vosp::transport
