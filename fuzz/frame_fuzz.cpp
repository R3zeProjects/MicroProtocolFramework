#include <vosp/protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    const std::span<const std::byte> input{bytes, size};
    vosp::protocol::Limits limits;
    limits.max_payload_size = 1024U * 1024U;
    limits.max_frame_size =
        limits.max_payload_size + limits.max_extension_bytes + vosp::protocol::wire_header_size;
    limits.max_buffered_bytes = limits.max_frame_size;
    vosp::protocol::FrameCodec codec{limits};
    auto decoded = codec.decode_prefix(input);
    if (decoded) {
        auto encoded = codec.encode(decoded->message);
        if (encoded) {
            (void)codec.decode(*encoded);
        }
    }
    return 0;
}
