#include <vosp/security.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<vosp::security::SecureBuffer>);
static_assert(std::is_nothrow_move_constructible_v<vosp::security::SecureBuffer>);
static_assert(vosp::contracts::SecureBytes<vosp::security::SecureBuffer>);
