#pragma once

/** @file error.hpp Transport error and result model. */

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <vosp/contracts/error.hpp>

namespace vosp::transport {
enum class ErrorCode : std::uint32_t {
    invalid_argument = 1,
    address_resolution_failed,
    socket_creation_failed,
    option_failed,
    bind_failed,
    listen_failed,
    accept_failed,
    connect_failed,
    send_failed,
    receive_failed,
    peer_closed,
    not_connected,
    retry_limit_exceeded,
    datagram_too_large,
    platform_initialization_failed
};

/** @brief Owning transport failure with a portable and native error code. */
class Error final {
  public:
    Error(ErrorCode code, std::string message, int native_code = 0)
        : code_{code}, native_code_{native_code}, message_{std::move(message)} {}

    Error(std::uint32_t code, std::string message)
        : Error{static_cast<ErrorCode>(code), std::move(message)} {}

    [[nodiscard]] std::uint32_t code() const noexcept {
        return static_cast<std::uint32_t>(code_);
    }
    [[nodiscard]] ErrorCode kind() const noexcept {
        return code_;
    }
    [[nodiscard]] int native_code() const noexcept {
        return native_code_;
    }
    [[nodiscard]] std::string_view message() const noexcept {
        return message_;
    }

    friend bool operator==(const Error&, const Error&) = default;

  private:
    ErrorCode code_;
    int native_code_;
    std::string message_;
};

template <typename Type> using Result = std::expected<Type, Error>;
using OperationResult = Result<void>;

struct Model {
    using Error = transport::Error;
    template <typename Type> using Result = transport::Result<Type>;
    using OperationResult = transport::OperationResult;

    [[nodiscard]] static Error make_error(std::uint32_t code, std::string message) {
        return {code, std::move(message)};
    }
};

static_assert(vosp::contracts::ErrorModel<Model>);

namespace detail {
template <typename Value>
[[nodiscard]] Result<Value> failure(ErrorCode code, std::string message, int native_code = 0) {
    return std::unexpected{Error{code, std::move(message), native_code}};
}

[[nodiscard]] inline OperationResult failure(ErrorCode code, std::string message,
                                             int native_code = 0) {
    return std::unexpected{Error{code, std::move(message), native_code}};
}
} // namespace detail
} // namespace vosp::transport
