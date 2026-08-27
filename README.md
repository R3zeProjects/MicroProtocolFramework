# MicroProtocolFramework

MicroProtocolFramework is a C++23 package with two deliberately separate
modules: a header-only protocol core and a compiled transport runtime. It
provides versioned messages, bounded codecs, VSP1 framing, incremental stream
decoding, RAII TCP/UDP sockets, and bounded reconnect policy.

**Current version:** `0.2.0-beta`

## Why it exists

Transport code should move bytes without owning message schemas. Protocol code
should frame messages without owning sockets. The package keeps those modules
separate even though they share one repository and release:

```text
vosp::transport            TCP / UDP / reconnect / blocking backpressure
             |
             v
vosp::protocol             messages / versions / codecs / VSP1 frames
             ^
             |
MicroSecurityFramework    checksum / authentication / encryption extensions
MicroPluginFramework      manifest and control-message codecs
```

Only MicroContractsFramework is required. Local IPC, cryptography, compression,
and plugin loading are intentionally outside the `0.2.0` runtime.

## Public API

- `Version`, `VersionRange`, `negotiate_version` — version compatibility;
- `BinaryWriter`, `BinaryReader` — checked big-endian primitives and strings;
- `Utf8Codec`, `BytesCodec` — bounded text and byte payloads;
- `Message`, `MessageView`, `Extension` — owning and non-owning values;
- `FrameCodec` — exact and prefix VSP1 encoding/decoding;
- `StreamDecoder` — fragmented and coalesced stream processing;
- `Limits` — payload, extension, frame, and buffered-byte bounds.
- `TcpStream`, `TcpListener` — move-only TCP ownership, complete writes, and
  bounded reconnect;
- `UdpSocket`, `Datagram` — bounded message-oriented I/O;
- `IpEndpoint`, `IoOptions`, `ReconnectPolicy` — explicit network policy.

The compact facade exposes `vsp::Protocol`, `vsp::ProtocolMessage`,
`vsp::ProtocolStream`, `vsp::ProtocolVersion`, and `vsp::ProtocolLimits`.
Transport aliases include `vsp::TcpStream`, `vsp::TcpListener`,
`vsp::TcpEndpoint`, and `vsp::UdpSocket`.

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

Transport is used directly, without a protocol adapter:

```cpp
#include <vosp/protocol.hpp>
#include <vosp/transport.hpp>

vsp::Protocol codec;
vsp::TcpStream connection;
auto connected = connection.connect(vsp::TcpEndpoint{"127.0.0.1", 9000});
auto frame = codec.encode(vsp::ProtocolMessage{
    vsp::ProtocolVersion{1, 0}, 7, 42, {std::byte{0x2a}}});
if (connected && frame) {
    auto sent = connection.send_all(*frame);
}
```

## Build and test

Requirements: CMake 3.25, C++23, and MicroContractsFramework 0.8.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMPROTOCOL_CONTRACTS_SOURCE_DIR=/path/to/MicroContractsFramework
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Installed package:

```cmake
find_package(mprotocol 0.2 REQUIRED CONFIG)
target_link_libraries(application PRIVATE vosp::protocol vosp::transport)
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
The transport benchmark measures one-client loopback request/echo round trips.
Median of five local runs on the same Windows/MSVC/Ryzen 7 PRO 1700X host:

| Transport | Payload | Round trips/s | Bidirectional wire throughput |
|---|---:|---:|---:|
| TCP | 64 B | 13.44 K | 1.72 MB/s |
| UDP | 64 B | 20.36 K | 2.61 MB/s |
| TCP | 1 KiB | 12.29 K | 25.17 MB/s |
| UDP | 1 KiB | 20.08 K | 41.13 MB/s |
| TCP | 65,507 B | 5.75 K | 753.14 MB/s |
| UDP | 65,507 B | 4.61 K | 603.81 MB/s |

This is a latency-sensitive loopback baseline, not internet throughput or an
external-library comparison. Raw medians are stored in
[`benchmarks/results/windows-msvc-ryzen-1700x-transport.csv`](benchmarks/results/windows-msvc-ryzen-1700x-transport.csv).
Benchmarks are source-only development targets and are not installed.

## Safety and lifecycle

- The wire layout is encoded field-by-field; C++ padding and host endian are
  never serialized.
- Payload, TLV, frame, and stream buffers are bounded before mutation.
- `Message` owns its bytes; `MessageView` must not outlive referenced storage.
- `FrameCodec`, value codecs, and immutable messages may be used concurrently
  as separate objects.
- `StreamDecoder`, `BinaryReader`, and `BinaryWriter` are mutable single-owner
  objects and require external synchronization when shared.
- Socket classes are move-only RAII owners; `close()` is idempotent and also
  runs during destruction.
- Reconnect attempts are bounded by policy and by a hard limit of 1024.
- UDP sends and receives reject payload bounds above 65,507 bytes.
- One socket object is single-owner and requires external synchronization when
  shared between threads.
- Unknown flag bits and opaque extension IDs are preserved as protocol data;
  semantic validation belongs to the owning extension framework.

See [architecture](docs/ARCHITECTURE.md), [stable contracts](docs/CONTRACTS.md),
and the [wire-format specification](docs/WIRE_FORMAT.md).

Licensed under the MIT License.
