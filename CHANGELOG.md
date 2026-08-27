# Changelog

## 0.2.0-beta

- Added the separately linkable `vosp::transport` target.
- Added move-only RAII TCP streams/listeners and bounded reconnect policy.
- Added bounded UDP datagrams with cached destination resolution.
- Added WinSock and POSIX backends behind private platform translation units.
- Added loopback integration tests, compile-fail transport contracts, package
  consumption, CI coverage, and opt-in TCP/UDP benchmarks.

## 0.1.0-beta

- Added version ranges and deterministic highest-common-version negotiation.
- Added bounds-checked network-order binary reader and writer.
- Added strict UTF-8 and raw byte codecs.
- Added the stable VSP1 frame format with opaque TLV extensions.
- Added exact, prefix, fragmented, and coalesced stream decoding.
- Added configurable payload, extension, frame, and buffering limits.
- Added compile-fail, malformed-input, package, sanitizer, fuzz, and CI checks.
