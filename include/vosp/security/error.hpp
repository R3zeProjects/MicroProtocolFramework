#pragma once

/** @file error.hpp Security-module error and result model. */

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <vosp/contracts/error.hpp>

namespace vosp::security {
/** @brief Portable failures reported by the security-support module. */
enum class ErrorCode : std::uint32_t {
    invalid_argument = 1,
    limit_exceeded,
    authentication_failed,
    invalid_extension,
    provider_failure
};

/** @brief Owning security-support failure with a portable code and message. */
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

/** @brief Expected value using the default security error model. */
template <typename Type> using Result = std::expected<Type, Error>;
using OperationResult = Result<void>;

/** @brief Default error model satisfying the MCF structural contract. */
struct Model {
    using Error = security::Error;
    template <typename Type> using Result = security::Result<Type>;
    using OperationResult = security::OperationResult;

    [[nodiscard]] static Error make_error(std::uint32_t code, std::string message) {
        return {code, std::move(message)};
    }
};

static_assert(vosp::contracts::ErrorModel<Model>);
} // namespace vosp::security
