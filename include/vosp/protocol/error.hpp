#pragma once

/** @file error.hpp Protocol error and replaceable result model. */

#include <concepts>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <vosp/contracts/error.hpp>

namespace vosp::protocol {
enum class ErrorCode : std::uint32_t {
    invalid_argument = 1,
    limit_exceeded,
    insufficient_data,
    invalid_magic,
    unsupported_version,
    malformed_frame,
    invalid_utf8,
    trailing_data
};

class Error final {
  public:
    Error(ErrorCode code, std::string message) : code_{code}, message_{std::move(message)} {}

    Error(std::uint32_t code, std::string message)
        : Error{static_cast<ErrorCode>(code), std::move(message)} {}

    [[nodiscard]] std::uint32_t code() const noexcept {
        return static_cast<std::uint32_t>(code_);
    }
    [[nodiscard]] ErrorCode kind() const noexcept {
        return code_;
    }
    [[nodiscard]] std::string_view message() const noexcept {
        return message_;
    }

    friend bool operator==(const Error&, const Error&) = default;

  private:
    ErrorCode code_;
    std::string message_;
};

template <typename Type> using Result = std::expected<Type, Error>;
using OperationResult = Result<void>;

struct Model {
    using Error = protocol::Error;
    template <typename Type> using Result = protocol::Result<Type>;
    using OperationResult = protocol::OperationResult;

    [[nodiscard]] static Error make_error(std::uint32_t code, std::string message) {
        return {code, std::move(message)};
    }
};

static_assert(vosp::contracts::ErrorModel<Model>);

namespace detail {
template <typename ErrorModel, typename Value>
concept ResultFor = vosp::contracts::ErrorModel<ErrorModel> &&
                    std::constructible_from<typename ErrorModel::template Result<Value>,
                                            std::unexpected<typename ErrorModel::Error>>;

template <typename Value, typename ErrorModel>
    requires ResultFor<ErrorModel, Value>
[[nodiscard]] typename ErrorModel::template Result<Value> failure(ErrorCode code,
                                                                  std::string message) {
    return typename ErrorModel::template Result<Value>{std::unexpected{
        ErrorModel::make_error(static_cast<std::uint32_t>(code), std::move(message))}};
}

template <typename ErrorModel>
    requires ResultFor<ErrorModel, void>
[[nodiscard]] typename ErrorModel::OperationResult failure(ErrorCode code, std::string message) {
    return typename ErrorModel::OperationResult{std::unexpected{
        ErrorModel::make_error(static_cast<std::uint32_t>(code), std::move(message))}};
}
} // namespace detail
} // namespace vosp::protocol
