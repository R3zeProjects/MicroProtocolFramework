#include <vosp/protocol.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
[[nodiscard]] std::vector<std::byte> as_bytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char value : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return bytes;
}

class Checks {
  public:
    void expect(bool condition, std::string_view message) {
        if (!condition) {
            ++failures_;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    [[nodiscard]] int result() const noexcept {
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int failures_ = 0;
};

void test_contracts(Checks& checks) {
    static_assert(vosp::contracts::ProtocolVersion<vosp::protocol::Version>);
    static_assert(vosp::contracts::ProtocolMessage<vosp::protocol::Message>);
    static_assert(vosp::contracts::ProtocolMessage<vosp::protocol::MessageView>);
    static_assert(vosp::contracts::ProtocolCodec<vosp::protocol::Utf8Codec<>, std::string,
                                                 vosp::protocol::Model>);
    static_assert(vosp::contracts::ProtocolFramer<vosp::protocol::FrameCodec<>,
                                                  vosp::protocol::Message, vosp::protocol::Model>);
    static_assert(
        vosp::contracts::ProtocolStreamDecoder<vosp::protocol::StreamDecoder<>,
                                               vosp::protocol::Message, vosp::protocol::Model>);
    checks.expect(vosp::protocol::version::api == "0.1.0-beta",
                  "public version must match package version");
}

void test_versioning(Checks& checks) {
    using vosp::protocol::Version;
    using vosp::protocol::VersionRange;
    checks.expect(Version{1, 5}.compatible_with(Version{1, 1}),
                  "versions with the same major must be compatible");
    checks.expect(!Version{1, 5}.compatible_with(Version{2, 0}),
                  "different major versions must be incompatible");
    const auto selected = vosp::protocol::negotiate_version(
        VersionRange{Version{1, 0}, Version{1, 5}}, VersionRange{Version{1, 2}, Version{1, 3}});
    checks.expect(selected == Version{1, 3}, "negotiation must select the highest common version");
    checks.expect(!vosp::protocol::negotiate_version(VersionRange{Version{1, 0}, Version{1, 5}},
                                                     VersionRange{Version{2, 0}, Version{2, 1}}),
                  "disjoint ranges must not negotiate");
    checks.expect(!VersionRange{Version{1, 9}, Version{2, 0}}.valid(),
                  "one negotiation range must not cross major versions");
}

void test_binary_codec(Checks& checks) {
    vosp::protocol::BinaryWriter writer{128};
    checks.expect(static_cast<bool>(writer.write<std::uint16_t>(0x1234U)),
                  "u16 write must succeed");
    checks.expect(static_cast<bool>(writer.write<std::uint32_t>(0x89abcdefU)),
                  "u32 write must succeed");
    checks.expect(static_cast<bool>(writer.write_string("VOSP")), "string write must succeed");
    auto bytes = std::move(writer).take();
    checks.expect(bytes.size() == 14, "binary layout size must be stable");
    checks.expect(bytes[0] == std::byte{0x12} && bytes[1] == std::byte{0x34},
                  "binary integers must use network byte order");

    vosp::protocol::BinaryReader reader{bytes};
    const auto first = reader.read<std::uint16_t>();
    const auto second = reader.read<std::uint32_t>();
    const auto text = reader.read_string();
    checks.expect(first && *first == 0x1234U, "u16 round trip must succeed");
    checks.expect(second && *second == 0x89abcdefU, "u32 round trip must succeed");
    checks.expect(text && *text == "VOSP", "string round trip must succeed");
    checks.expect(reader.empty(), "reader must consume the complete input");

    vosp::protocol::BinaryReader truncated{std::span<const std::byte>{bytes}.first(1)};
    checks.expect(!truncated.read<std::uint16_t>(), "truncated integer must be rejected");

    vosp::protocol::BinaryWriter limited{2};
    checks.expect(!limited.write<std::uint32_t>(7),
                  "writer limit must be enforced before mutation");
    checks.expect(limited.size() == 0, "failed write must be atomic");

    vosp::protocol::BinaryWriter string_limited{5};
    checks.expect(!string_limited.write_string("xx"),
                  "string prefix and payload must share one bound");
    checks.expect(string_limited.size() == 0, "failed string write must be atomic");

    const std::vector<std::byte> bounded_string{std::byte{0}, std::byte{0},   std::byte{0},
                                                std::byte{2}, std::byte{'o'}, std::byte{'k'}};
    vosp::protocol::BinaryReader bounded_reader{bounded_string};
    checks.expect(!bounded_reader.read_string(1), "reader string limit must be enforced");
    checks.expect(bounded_reader.position() == 0,
                  "failed bounded string read must preserve the cursor");
}

void test_text_codec(Checks& checks) {
    vosp::protocol::Utf8Codec codec{64};
    const std::string source =
        "VOSP \xd0\xbf\xd1\x80\xd0\xbe\xd1\x82\xd0\xbe\xd0\xba\xd0\xbe\xd0\xbb";
    const auto encoded = codec.encode(source);
    checks.expect(static_cast<bool>(encoded), "valid UTF-8 must encode");
    const auto decoded =
        encoded ? codec.decode(*encoded)
                : vosp::protocol::Result<std::string>{std::unexpected{vosp::protocol::Error{
                      vosp::protocol::ErrorCode::invalid_utf8, "test setup failed"}}};
    checks.expect(decoded && *decoded == source, "UTF-8 must round trip");

    const std::vector invalid{std::byte{0xc0}, std::byte{0xaf}};
    checks.expect(!codec.decode(invalid), "overlong UTF-8 must be rejected");
    const std::vector truncated{std::byte{0xe2}, std::byte{0x82}};
    checks.expect(!codec.decode(truncated), "truncated UTF-8 must be rejected");
    const std::vector surrogate{std::byte{0xed}, std::byte{0xa0}, std::byte{0x80}};
    checks.expect(!codec.decode(surrogate), "UTF-16 surrogate code points must be rejected");
    const std::vector out_of_range{std::byte{0xf4}, std::byte{0x90}, std::byte{0x80},
                                   std::byte{0x80}};
    checks.expect(!codec.decode(out_of_range), "code points above U+10FFFF must be rejected");
    checks.expect(!vosp::protocol::Utf8Codec{2}.encode(std::string{"long"}),
                  "text limit must be enforced");
}

[[nodiscard]] vosp::protocol::Message sample_message(std::uint64_t correlation) {
    return {vosp::protocol::Version{1, 2},
            17,
            correlation,
            as_bytes("payload"),
            vosp::protocol::FrameFlags::authenticated | vosp::protocol::FrameFlags::checksum,
            {vosp::protocol::Extension{vosp::protocol::ExtensionId::content_type,
                                       as_bytes("text/plain")}}};
}

void test_frame_round_trip(Checks& checks) {
    vosp::protocol::FrameCodec codec;
    const auto message = sample_message(42);
    const auto encoded = codec.encode(message);
    checks.expect(static_cast<bool>(encoded), "frame encode must succeed");
    if (!encoded) {
        return;
    }
    checks.expect(encoded->size() == 32U + 4U + 10U + 7U,
                  "frame size must equal header, TLV and payload");
    checks.expect((*encoded)[0] == std::byte{'V'} && (*encoded)[1] == std::byte{'S'} &&
                      (*encoded)[2] == std::byte{'P'} && (*encoded)[3] == std::byte{'1'},
                  "frame magic must be VSP1");

    const auto probe = codec.probe(*encoded);
    checks.expect(probe && probe->state == vosp::protocol::FrameState::ready &&
                      probe->frame_size == encoded->size(),
                  "complete frame must probe as ready");
    const auto decoded = codec.decode(*encoded);
    checks.expect(decoded && *decoded == message, "frame must round trip exactly");

    const vosp::protocol::Message application_message{
        message.version(),        message.type(),
        message.correlation_id(), message.owning_payload(),
        message.flags(),          {vosp::protocol::Extension{2048, as_bytes("opaque")}}};
    const auto application_frame = codec.encode(application_message);
    checks.expect(static_cast<bool>(application_frame),
                  "application-defined extension must encode");
    if (application_frame) {
        const auto application_decoded = codec.decode(*application_frame);
        checks.expect(application_decoded && *application_decoded == application_message,
                      "application-defined extensions must remain opaque and round trip");
    }

    auto with_trailing = *encoded;
    with_trailing.push_back(std::byte{0});
    checks.expect(!codec.decode(with_trailing), "exact decode must reject trailing bytes");
    const auto prefix = codec.decode_prefix(with_trailing);
    checks.expect(prefix && prefix->consumed == encoded->size(),
                  "prefix decode must report consumed bytes");
}

void test_stream_decoder(Checks& checks) {
    vosp::protocol::FrameCodec codec;
    const auto first_frame = codec.encode(sample_message(100));
    const auto second_frame = codec.encode(sample_message(101));
    checks.expect(first_frame && second_frame, "stream test frames must encode");
    if (!first_frame || !second_frame) {
        return;
    }

    vosp::protocol::StreamDecoder fragmented;
    for (std::size_t index = 0; index < first_frame->size(); ++index) {
        const std::span<const std::byte> byte{first_frame->data() + index, 1};
        checks.expect(static_cast<bool>(fragmented.push(byte)),
                      "one-byte stream push must succeed");
        const auto next = fragmented.next();
        checks.expect(static_cast<bool>(next), "fragmented probe must not fail");
        const bool should_be_ready = index + 1U == first_frame->size();
        checks.expect(next && next->has_value() == should_be_ready,
                      "frame must appear only after the last byte");
        if (should_be_ready && next && next->has_value()) {
            checks.expect((*next)->correlation_id() == 100,
                          "fragmented frame must preserve correlation");
        }
    }
    checks.expect(fragmented.buffered_size() == 0,
                  "consumed fragmented frame must release its buffer");

    std::vector<std::byte> coalesced = *first_frame;
    coalesced.insert(coalesced.end(), second_frame->begin(), second_frame->end());
    vosp::protocol::StreamDecoder stream;
    checks.expect(static_cast<bool>(stream.push(coalesced)), "coalesced stream push must succeed");
    const auto first = stream.next();
    const auto second = stream.next();
    const auto empty = stream.next();
    checks.expect(first && first->has_value() && (*first)->correlation_id() == 100,
                  "first coalesced frame must decode");
    checks.expect(second && second->has_value() && (*second)->correlation_id() == 101,
                  "second coalesced frame must decode");
    checks.expect(empty && !empty->has_value(), "empty stream must return no message");
}

void test_invalid_frames_and_limits(Checks& checks) {
    vosp::protocol::FrameCodec codec;
    const auto encoded = codec.encode(sample_message(7));
    checks.expect(static_cast<bool>(encoded), "invalid-frame fixture must encode");
    if (!encoded) {
        return;
    }

    auto invalid_magic = *encoded;
    invalid_magic[0] = std::byte{0};
    const auto magic_result = codec.probe(invalid_magic);
    checks.expect(!magic_result &&
                      magic_result.error().kind() == vosp::protocol::ErrorCode::invalid_magic,
                  "invalid magic must fail closed");
    checks.expect(!codec.decode(std::span<const std::byte>{*encoded}.first(12)),
                  "truncated frame must not decode");

    auto invalid_extensions = *encoded;
    invalid_extensions[28] = std::byte{0};
    invalid_extensions[29] = std::byte{2};
    checks.expect(!codec.probe(invalid_extensions),
                  "inconsistent extension count must be rejected");

    auto reserved = *encoded;
    reserved[31] = std::byte{1};
    checks.expect(!codec.probe(reserved), "non-zero reserved header bits must be rejected");

    auto short_header = *encoded;
    short_header[4] = std::byte{0};
    short_header[5] = std::byte{31};
    checks.expect(!codec.probe(short_header), "undersized fixed header must be rejected");

    auto truncated_extension = *encoded;
    truncated_extension[34] = std::byte{0xff};
    truncated_extension[35] = std::byte{0xff};
    checks.expect(!codec.probe(truncated_extension),
                  "TLV value extending beyond the header must be rejected");

    auto excessive_payload = *encoded;
    excessive_payload[24] = std::byte{0xff};
    excessive_payload[25] = std::byte{0xff};
    excessive_payload[26] = std::byte{0xff};
    excessive_payload[27] = std::byte{0xff};
    checks.expect(!codec.probe(excessive_payload),
                  "declared payload above configured bounds must be rejected");

    vosp::protocol::Limits limits{.max_payload_size = 8,
                                  .max_extension_count = 0,
                                  .max_extension_bytes = 0,
                                  .max_frame_size = 40,
                                  .max_buffered_bytes = 40};
    checks.expect(limits.valid(), "small test limits must be internally valid");
    vosp::protocol::FrameCodec limited{limits};
    checks.expect(!limited.encode(sample_message(9)), "extension limit must be enforced");

    vosp::protocol::StreamDecoder bounded{limits};
    std::vector<std::byte> excessive(41, std::byte{0});
    checks.expect(!bounded.push(excessive), "stream backpressure limit must be enforced");
    checks.expect(bounded.buffered_size() == 0, "rejected stream push must not retain bytes");

    vosp::protocol::Limits invalid_limits{};
    invalid_limits.max_frame_size = 31;
    vosp::protocol::FrameCodec invalid_codec{invalid_limits};
    const auto invalid_configuration = invalid_codec.encode(sample_message(10));
    checks.expect(!invalid_configuration && invalid_configuration.error().kind() ==
                                                vosp::protocol::ErrorCode::invalid_argument,
                  "inconsistent limits must fail as invalid arguments");

    vosp::protocol::StreamDecoder fail_closed;
    checks.expect(static_cast<bool>(fail_closed.push(invalid_magic)),
                  "bounded malformed bytes may enter the stream buffer");
    checks.expect(!fail_closed.next(), "malformed stream input must fail closed");
    checks.expect(fail_closed.buffered_size() == invalid_magic.size(),
                  "fail-closed stream input must remain available for policy decisions");
    fail_closed.reset();
    checks.expect(fail_closed.buffered_size() == 0, "stream reset must release malformed input");
}
} // namespace

int main() {
    Checks checks;
    test_contracts(checks);
    test_versioning(checks);
    test_binary_codec(checks);
    test_text_codec(checks);
    test_frame_round_trip(checks);
    test_stream_decoder(checks);
    test_invalid_frames_and_limits(checks);
    return checks.result();
}
