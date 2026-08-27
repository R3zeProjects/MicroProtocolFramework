# Architecture

## Scope

The package owns two separate modules. `vosp::protocol` converts application
values to bounded byte frames. `vosp::transport` owns TCP/UDP handles, endpoint
resolution, blocking I/O, timeouts, and reconnect. Neither module owns
encryption keys, plugin libraries, persistence, or service lifecycle.

## Components

```text
application value
      |
      v
Utf8Codec / BytesCodec / custom MCF-compatible codec
      |
      v
Message or MessageView
      |
      v
FrameCodec <---------- opaque TLV extensions
      |
      +---- datagram transport: one complete frame
      |
      +---- stream transport: StreamDecoder -> zero or more Messages
```

`BinaryReader` and `BinaryWriter` are low-level tools for application codecs.
They use network byte order and reject truncated or excessive values.

`TcpStream`, `TcpListener`, and `UdpSocket` use public PIMPL boundaries. Native
WinSock/POSIX handles and address structures remain in platform translation
units. Protocol and transport communicate only through byte spans.

## Dependency rules

- Protocol and Transport depend on MCF contracts.
- Transport does not depend on Protocol; examples compose encoded byte frames
  directly with transport operations.
- Security may define and verify TLV values; Protocol treats them as bytes.
- Plugin may encode manifests and control messages; Protocol does not load
  libraries or define a C ABI.
- Cache may store encoded frames; Protocol does not own eviction policy.
- Testing may fuzz every public decoder; production headers do not depend on a
  testing framework.

## Invariants

- A complete frame starts with `VSP1`.
- Integer fields are big-endian.
- The fixed header is 32 bytes.
- Declared sizes are validated before allocation or copying.
- The TLV cursor must end exactly at the declared header size.
- Exact decoding rejects trailing bytes; prefix decoding reports consumption.
- Incremental decoding does not silently skip malformed input.

## Versioning

One negotiation range belongs to one major protocol line. A range crossing a
major boundary is invalid; peers select the highest common version from valid
ranges. Frame decoding preserves the declared version, while connection policy
decides whether that version was negotiated.

## Threading and ownership

`Message` and `Extension` own their byte storage. `MessageView` and spans are
non-owning and valid only for the duration of encoding. Stateless codec
instances can be kept thread-local or copied. A `StreamDecoder` represents one
ordered byte stream and is intentionally single-owner; a transport creates one
decoder per connection or protects it externally.

Transport objects are move-only single owners. They do not serialize concurrent
method calls. A service either assigns one owner per connection or provides
external synchronization. Closing a handle is idempotent.

## Extension points

MCF provides structural protocol and transport concepts. Applications can
replace concrete implementations without wrapper classes. Wire extensions use
TLV IDs so future compression, checksum, authentication, trace, and application
metadata do not change the fixed header.
