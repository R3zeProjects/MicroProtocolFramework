#include <vosp/transport.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<vosp::transport::TcpStream>);
static_assert(std::is_nothrow_move_constructible_v<vosp::transport::TcpStream>);
static_assert(!std::is_copy_constructible_v<vosp::transport::TcpListener>);
static_assert(std::is_nothrow_move_constructible_v<vosp::transport::TcpListener>);
static_assert(!std::is_copy_constructible_v<vosp::transport::UdpSocket>);
static_assert(std::is_nothrow_move_constructible_v<vosp::transport::UdpSocket>);
