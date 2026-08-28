#pragma once

/** @file security.hpp Public entry point for the security-support module. */

#include <vosp/contracts.hpp>
#include <vosp/security/error.hpp>
#include <vosp/security/permissions.hpp>
#include <vosp/security/protocol.hpp>
#include <vosp/security/secure_buffer.hpp>
#include <vosp/security/version.hpp>

namespace vosp {
using SecureBuffer = security::SecureBuffer;
using SecurityError = security::Error;
} // namespace vosp
