# MicroProtocolFramework

MicroProtocolFramework is a C++23 package with three deliberately separate
modules: a header-only protocol core, a compiled transport runtime, and
security-support primitives. It
provides versioned messages, bounded codecs, VSP1 framing, incremental stream
decoding, RAII TCP/UDP sockets, bounded reconnect policy, secure memory
erasure, compact permissions, and authenticated-frame composition.

**Current version:** `0.3.0-beta`

## Why it exists

Transport code should move bytes without owning message schemas. Protocol code
should frame messages without owning sockets. The package keeps those modules
separate even though they share one repository and release:

```text
vosp::transport       TCP / UDP / reconnect / blocking backpressure
             |
             v
vosp::protocol        messages / versions / codecs / VSP1 frames
             ^
             |
vosp::security        secure bytes / permissions / authentication TLVs
```

Only MicroContractsFramework is required. Cryptographic algorithms are supplied
directly by an application provider satisfying the MCF digest or authenticator
concept; this package does not invent cryptography. Local IPC, compression, and
plugin loading remain outside the `0.3.0` runtime.

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
- `SecureBuffer`, `secure_erase`, `constant_time_equal` — bounded move-only
  secret ownership and platform-backed erasure;
- `PermissionSet` — allocation-free enum permissions in one 64-bit word;
- `authentication_extension`, `authentication_tag` — direct composition with
  the reserved VSP1 authentication TLV.

The compact facade exposes `vsp::Protocol`, `vsp::ProtocolMessage`,
`vsp::ProtocolStream`, `vsp::ProtocolVersion`, and `vsp::ProtocolLimits`.
Transport aliases include `vsp::TcpStream`, `vsp::TcpListener`,
`vsp::TcpEndpoint`, and `vsp::UdpSocket`. Security exposes
`vosp::SecureBuffer` plus the explicit `vosp::security` namespace.

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

Security support composes directly with protocol extensions:

```cpp
#include <vosp/security.hpp>

std::array tag{std::byte{0x10}, std::byte{0x20}}; // produced by your provider
auto extension = vosp::security::authentication_extension(tag);
auto secret = vosp::SecureBuffer::copy_from(tag);
if (!extension || !secret) {
    return 1;
}
```

The provider implements the structural `vosp::contracts::DigestProvider` or
`MessageAuthenticator` concept. No adapter or inheritance hierarchy is needed.

## Build and test

Requirements: CMake 3.25, C++23, and MicroContractsFramework 0.9.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMPROTOCOL_CONTRACTS_SOURCE_DIR=/path/to/MicroContractsFramework
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Installed package:

```cmake
find_package(mprotocol 0.3 REQUIRED CONFIG)
target_link_libraries(application
    PRIVATE vosp::protocol vosp::transport vosp::security)
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

The security-support microbenchmark measures equal-length byte comparison,
permission updates, and platform-backed erasure. Median of five local runs on
the same Windows/MSVC/Ryzen 7 PRO 1700X host:

| Payload | Equal compare | Permission update | Secure erase |
|---:|---:|---:|---:|
| 64 B | 7.99 ns | 1.17 ns | 33.18 ns |
| 4 KiB | 135.71 ns | 1.15 ns | 147.25 ns |

The comparison benchmark covers equal public lengths. C++ does not provide a
portable hard timing guarantee; `constant_time_equal` uses content-independent
control flow and must not replace a reviewed cryptographic provider when that
provider offers its own verification primitive. Raw runs are stored in
[`benchmarks/results/windows-msvc-ryzen-1700x-security.csv`](benchmarks/results/windows-msvc-ryzen-1700x-security.csv).
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
- `SecureBuffer` is move-only, bounded to 64 MiB, and erases logical bytes on
  clear, move assignment, and destruction using the operating-system primitive.
- Secure erasure does not lock pages or erase independent source copies; use an
  audited provider or operating-system facility for stronger threat models.
- Permission enums must be contiguous in `[0, Count)` and `Count <= 64`.
- Security TLV helpers validate tag identity and a configurable bound of at
  most 4096 bytes; the returned tag span borrows the extension storage.

See [architecture](docs/ARCHITECTURE.md), [stable contracts](docs/CONTRACTS.md),
and the [wire-format specification](docs/WIRE_FORMAT.md).

Licensed under the MIT License.
