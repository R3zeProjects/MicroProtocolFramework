#pragma once

/** @file transport.hpp Public entry point for the transport module. */

#include <vosp/contracts.hpp>
#include <vosp/transport/error.hpp>
#include <vosp/transport/tcp.hpp>
#include <vosp/transport/types.hpp>
#include <vosp/transport/udp.hpp>
#include <vosp/transport/version.hpp>

namespace vosp {
using TcpEndpoint = transport::IpEndpoint;
using TcpListener = transport::TcpListener;
using TcpStream = transport::TcpStream;
using TransportError = transport::Error;
using UdpSocket = transport::UdpSocket;
} // namespace vosp
