#pragma once

/** @file protocol.hpp Public entry point for MicroProtocolFramework. */

#include <vosp/contracts.hpp>
#include <vosp/protocol/binary.hpp>
#include <vosp/protocol/error.hpp>
#include <vosp/protocol/frame.hpp>
#include <vosp/protocol/stream_decoder.hpp>
#include <vosp/protocol/text.hpp>
#include <vosp/protocol/types.hpp>
#include <vosp/protocol/version.hpp>

namespace vosp {
using Protocol = protocol::FrameCodec<>;
using ProtocolError = protocol::Error;
using ProtocolLimits = protocol::Limits;
using ProtocolMessage = protocol::Message;
using ProtocolStream = protocol::StreamDecoder<>;
using ProtocolVersion = protocol::Version;
} // namespace vosp
