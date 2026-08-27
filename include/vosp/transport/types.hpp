#pragma once

/** @file types.hpp Transport endpoints, limits, and owning datagrams. */

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vosp::transport {
/** @brief Host or numeric address paired with a TCP/UDP port. */
class IpEndpoint final {
  public:
    IpEndpoint() = default;
    IpEndpoint(std::string host, std::uint16_t port) : host_{std::move(host)}, port_{port} {}

    [[nodiscard]] const std::string& host() const noexcept {
        return host_;
    }
    [[nodiscard]] std::uint16_t port() const noexcept {
        return port_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return !host_.empty();
    }

    friend bool operator==(const IpEndpoint&, const IpEndpoint&) = default;

  private:
    std::string host_;
    std::uint16_t port_ = 0;
};

/** @brief Per-socket blocking I/O timeouts; zero keeps the platform default. */
struct IoOptions {
    std::chrono::milliseconds receive_timeout{0};
    std::chrono::milliseconds send_timeout{0};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return receive_timeout.count() >= 0 && send_timeout.count() >= 0;
    }
};

/** @brief Bounded retry policy used by TcpStream::reconnect(). */
struct ReconnectPolicy {
    static constexpr std::size_t hard_attempt_limit = 1024;

    std::size_t max_attempts = 3;
    std::chrono::milliseconds initial_delay{10};
    std::chrono::milliseconds maximum_delay{1000};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return max_attempts > 0 && max_attempts <= hard_attempt_limit &&
               initial_delay.count() >= 0 && maximum_delay >= initial_delay;
    }
};

/** @brief Owning UDP packet and its remote endpoint. */
class Datagram final {
  public:
    using EndpointType = IpEndpoint;

    Datagram() = default;
    Datagram(IpEndpoint endpoint, std::vector<std::byte> payload)
        : endpoint_{std::move(endpoint)}, payload_{std::move(payload)} {}

    [[nodiscard]] const IpEndpoint& endpoint() const noexcept {
        return endpoint_;
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept {
        return payload_;
    }
    [[nodiscard]] const std::vector<std::byte>& owning_payload() const noexcept {
        return payload_;
    }

    friend bool operator==(const Datagram&, const Datagram&) = default;

  private:
    IpEndpoint endpoint_;
    std::vector<std::byte> payload_;
};
} // namespace vosp::transport
