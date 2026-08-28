# Stable contracts

This document defines the behavioral contracts of the `0.x` package.
Breaking these contracts requires a documented minor-version change while the
framework remains in beta.

## Ownership and lifetime

- `Message`, `Extension`, and decoded values own their storage.
- `MessageView`, `BinaryReader`, and returned byte spans never extend the
  lifetime of referenced memory. The caller keeps that storage alive.
- `FrameCodec` never stores a supplied message or input span.
- `StreamDecoder` copies accepted input into its bounded internal buffer.

## Failure and mutation

- Expected malformed input, unsupported bounds, and incomplete data are
  reported through the selected MCF error model.
- Size and structure are validated before frame-level allocations.
- A rejected `BinaryWriter` operation does not append partial protocol data.
- A rejected `StreamDecoder::push()` does not append the new byte range.
- Invalid buffered framing is fail-closed: `next()` reports the error and does
  not discard bytes. The owner chooses `reset()` or connection termination.
- Standard container allocation failures may propagate as `std::bad_alloc`.
  The error-model contract does not attempt to recover from process-wide memory
  exhaustion.

## Transport lifecycle and failure

- `TcpStream`, `TcpListener`, and `UdpSocket` are move-only RAII owners.
- `close()` is idempotent. A moved-from object may only be destroyed, assigned,
  queried, or closed; operations report `invalid_argument`/`not_connected`.
- `TcpStream::send()` may write a prefix. `send_all()` writes the whole span or
  reports a failure and closes the stream.
- A zero-byte TCP receive is reported as `peer_closed` and closes the stream.
- `connected()` and `listening()` describe local handle ownership; they do not
  probe remote liveness.
- `reconnect()` requires a previously supplied endpoint. Attempts use bounded
  exponential delay and can never exceed 1024.
- Zero I/O timeout means the platform blocking default. Positive timeouts are
  applied to send/receive operations, not DNS or connect.
- UDP payloads are capped at 65,507 bytes. The caller also supplies the receive
  allocation bound.
- Endpoint resolution can allocate and may perform operating-system name
  service. The last UDP destination is cached per socket.

## Compatibility

- All integral wire values use fixed widths and network byte order.
- One `VersionRange` covers exactly one major version.
- Negotiation selects the highest common version or returns no value.
- Exact decode rejects trailing bytes; prefix decode reports consumed bytes.
- Unknown flags and extension IDs are transported opaquely. The framework that
  owns an extension defines and validates its semantics.

## Security support

- `SecureBuffer::copy_from()` rejects empty input, caller limits above 64 MiB,
  and values above the selected limit.
- `SecureBuffer` is non-copyable. Move assignment erases the replaced secret;
  `clear()` erases every logical byte before clearing the size, and destruction
  erases it before releasing the allocation.
- `SecureBuffer` does not lock pages, prevent swapping or crash-dump capture, or
  erase source copies retained by the caller.
- `secure_erase()` delegates to `SecureZeroMemory` or `explicit_bzero` and is
  safe for an empty span.
- `constant_time_equal()` has content-independent control flow for equal public
  lengths. Length mismatch returns immediately. The C++ abstract machine does
  not guarantee wall-clock constant time; prefer a cryptographic provider's
  reviewed verification primitive when available.
- `PermissionSet<Permission, Count>` requires contiguous non-negative enum
  values in `[0, Count)` and supports at most 64 values. Invalid values do not
  mutate the mask.
- Authentication helpers copy an opaque provider tag into VSP1 and enforce a
  caller limit no greater than 4096 bytes. `authentication_tag()` returns a
  borrowed view valid only while the owning `Extension` and its value remain
  unchanged.
- Digest and MAC algorithms are not implemented by this package. Compatible
  providers satisfy the MCF structural concepts without adapters.

## Concurrency

- Independent codec instances and immutable messages may be used concurrently.
- `FrameCodec`, `Utf8Codec`, and `BytesCodec` have const operations and no
  mutable shared state.
- `StreamDecoder`, `BinaryReader`, and `BinaryWriter` are single-owner mutable
  objects. Sharing one instance requires synchronization outside this module.
- Every transport object is single-owner mutable state. Separate sockets may be
  used concurrently; sharing one object requires external synchronization.
- Sharing one `SecureBuffer` or mutable `PermissionSet` requires external
  synchronization. Separate instances have no shared mutable state.

## Resource bounds

- `Limits` bounds payload bytes, extension count, extension bytes, frame size,
  and buffered stream bytes.
- Declared wire sizes are checked before addition, slicing, copying, or reserve.
- Limits are local policy. They do not alter the VSP1 wire representation.
