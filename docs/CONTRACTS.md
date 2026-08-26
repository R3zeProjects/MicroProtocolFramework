# Stable contracts

This document defines the behavioral contracts of the `0.x` protocol core.
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

## Compatibility

- All integral wire values use fixed widths and network byte order.
- One `VersionRange` covers exactly one major version.
- Negotiation selects the highest common version or returns no value.
- Exact decode rejects trailing bytes; prefix decode reports consumed bytes.
- Unknown flags and extension IDs are transported opaquely. The framework that
  owns an extension defines and validates its semantics.

## Concurrency

- Independent codec instances and immutable messages may be used concurrently.
- `FrameCodec`, `Utf8Codec`, and `BytesCodec` have const operations and no
  mutable shared state.
- `StreamDecoder`, `BinaryReader`, and `BinaryWriter` are single-owner mutable
  objects. Sharing one instance requires synchronization outside this module.

## Resource bounds

- `Limits` bounds payload bytes, extension count, extension bytes, frame size,
  and buffered stream bytes.
- Declared wire sizes are checked before addition, slicing, copying, or reserve.
- Limits are local policy. They do not alter the VSP1 wire representation.
