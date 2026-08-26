# MicroProtocolFramework

MicroProtocolFramework is a C++23 protocol core for versioned messages, bounded
binary/text codecs, stable framing, and incremental stream decoding. It is the
wire-format layer of the VOSP ecosystem and has no socket, reconnect,
cryptography, compression, or plugin-loading implementation.

**Current version:** `0.1.0-beta`

## Why it exists

Transport code should move bytes without owning message schemas. Security code
should authenticate bytes without owning framing. Plugins should negotiate
versions without coupling their ABI to a network backend. This framework keeps
those boundaries explicit:

```text
MicroTransportFramework   sockets / IPC / reconnect / backpressure
             |
             v
MicroProtocolFramework    messages / versions / codecs / VSP1 frames
             ^
             |
MicroSecurityFramework    checksum / authentication / encryption extensions
MicroPluginFramework      manifest and control-message codecs
```

Only MicroContractsFramework is required. The concrete error model is
replaceable through MCF concepts; a ready-to-use `std::expected` model is
provided.

## Public API

- `Version`, `VersionRange`, `negotiate_version` — version compatibility;
- `BinaryWriter`, `BinaryReader` — checked big-endian primitives and strings;
- `Utf8Codec`, `BytesCodec` — bounded text and byte payloads;
- `Message`, `MessageView`, `Extension` — owning and non-owning values;
- `FrameCodec` — exact and prefix VSP1 encoding/decoding;
- `StreamDecoder` — fragmented and coalesced stream processing;
- `Limits` — payload, extension, frame, and buffered-byte bounds.

The compact facade exposes `vsp::Protocol`, `vsp::ProtocolMessage`,
`vsp::ProtocolStream`, `vsp::ProtocolVersion`, and `vsp::ProtocolLimits`.

## Quick start

```cpp
#include <vosp/protocol.hpp>

#include <string>

vosp::protocol::Utf8Codec text;
auto payload = text.encode(std::string{"hello"});
if (!payload) {
    return 1;
}

vsp::Protocol protocol;
auto frame = protocol.encode(vsp::ProtocolMessage{
    vsp::ProtocolVersion{1, 0}, 7, 42, std::move(*payload)});
if (!frame) {
    return 2;
}

auto message = protocol.decode(*frame);
return message && message->correlation_id() == 42 ? 0 : 3;
```

For TCP-style fragmentation, pass every received byte range to
`vsp::ProtocolStream::push()` and call `next()` until it returns an empty
optional. Invalid input is fail-closed and remains buffered until `reset()`;
the transport decides whether to close or resynchronize the connection.

## Build and test

Requirements: CMake 3.25, C++23, and MicroContractsFramework 0.7.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMPROTOCOL_CONTRACTS_SOURCE_DIR=/path/to/MicroContractsFramework
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Installed package:

```cmake
find_package(mprotocol 0.1 REQUIRED CONFIG)
target_link_libraries(application PRIVATE vosp::protocol)
```

Opt-in targets:

- `-DMPROTOCOL_BUILD_BENCHMARKS=ON`;
- `-DMPROTOCOL_BUILD_FUZZERS=ON` with Clang/libFuzzer;
- `-DMPROTOCOL_ENABLE_SANITIZERS=ON`.

## Measured baseline

The bundled opt-in benchmark measures a complete owning `encode()` + `decode()`
round trip. It therefore includes frame allocation, validation, endian
conversion, payload copies, and decoded-message allocation.

Local Windows baseline (MSVC Release, AMD Ryzen 7 PRO 1700X, median of five
runs):

| Payload | Round trips/s | Payload throughput |
|---:|---:|---:|
| 0 B | 3.81 M | control frames |
| 64 B | 2.94 M | 0.188 GB/s |
| 256 B | 2.84 M | 0.728 GB/s |
| 1 KiB | 2.42 M | 2.48 GB/s |
| 4 KiB | 1.89 M | 7.72 GB/s |
| 64 KiB | 164.7 K | 10.79 GB/s |
| 1 MiB | 1.32 K | 1.38 GB/s |

These values are a reproducible development baseline, not a cross-machine
guarantee or an apples-to-oranges claim against schema compilers. Raw inputs,
iteration counts, and medians are stored in
[the benchmark result](benchmarks/results/windows-msvc-ryzen-1700x.csv).
Benchmarks are source-only development targets and are not installed with the
package.

## Safety and lifecycle

- The wire layout is encoded field-by-field; C++ padding and host endian are
  never serialized.
- Payload, TLV, frame, and stream buffers are bounded before mutation.
- `Message` owns its bytes; `MessageView` must not outlive referenced storage.
- `FrameCodec`, value codecs, and immutable messages may be used concurrently
  as separate objects.
- `StreamDecoder`, `BinaryReader`, and `BinaryWriter` are mutable single-owner
  objects and require external synchronization when shared.
- Unknown flag bits and opaque extension IDs are preserved as protocol data;
  semantic validation belongs to the owning extension framework.

See [architecture](docs/ARCHITECTURE.md), [stable contracts](docs/CONTRACTS.md),
and the [wire-format specification](docs/WIRE_FORMAT.md).

Licensed under the MIT License.
