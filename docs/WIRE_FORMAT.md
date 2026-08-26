# VSP1 Wire Format

All integer fields use big-endian network byte order. Offsets are measured from
the beginning of a frame.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic: ASCII `VSP1` |
| 4 | 2 | Total header size, including TLVs |
| 6 | 2 | Frame flags |
| 8 | 2 | Protocol major version |
| 10 | 2 | Protocol minor version |
| 12 | 4 | Application message type |
| 16 | 8 | Correlation identifier |
| 24 | 4 | Payload size |
| 28 | 2 | Extension count |
| 30 | 2 | Reserved, must be zero |
| 32 | variable | TLV extensions |
| header size | payload size | Payload |

Each TLV consists of a 16-bit extension ID, a 16-bit value size, and exactly
that many value bytes.

Reserved extension IDs:

- `1`: content type;
- `2`: compression metadata;
- `3`: checksum;
- `4`: authentication metadata;
- `5`: trace context;
- `1024` and above: application-defined.

MicroProtocolFramework does not interpret extension values. The framework that
owns an extension defines its encoding, validation order, and failure policy.

## Compatibility

Major versions identify incompatible protocol families. Version negotiation
selects the highest value in the intersection of two explicit `VersionRange`
objects. Unknown flags and extension IDs remain opaque so a proxy or newer
consumer can preserve forward-compatible metadata.

Before `1.0.0`, the VSP1 format is beta and may change through a documented
minor release. Production users should pin an exact package version.
