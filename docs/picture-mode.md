# LG HDR QHD Picture Mode

Picture Mode is a narrow, write-only semantic operation for one historically
validated target. It is neither a generic VCP `0x15` API nor a general claim
about LG displays.

## Exact gate

The selected display must be external and match all of the following before
any IOAV client is constructed or write is issued:

| Field | Required value |
| --- | --- |
| Product name | `LG HDR QHD` |
| Provider | `DCPDP13Service` |
| Service role / transport | `DCPEXT0` |
| Existing DCPDP13 safety gate | passed |

The profile is shown as `RSS_DDC_CAP_PICTURE_MODE`; every other target fails
closed. No sibling-provider or alternate-input fallback is attempted.

## Supported semantic values

| API / CLI name | VCP `0x15` value | Direct hardware SET evidence |
| --- | --- | --- |
| `RSS_DDC_PICTURE_MODE_VIVID` / `vivid` | `0x31` | yes — historical one-shot visible validation |
| `RSS_DDC_PICTURE_MODE_READER` / `reader` | `0x01` | yes — historical one-shot visible validation |

Historical read-only OSD fingerprints also associated Custom `0x0B`, HDR
Effect `0x27`, Cinema `0x30`, FPS `0x1E`, RTS `0x1F`, and Color Weakness
`0x06` with VCP `0x15`. They are **not** exposed: their mapping was inferred
from fingerprints, not confirmed by a direct SET. The monitor's MCCS
advertisement is also not used as a mapping source.

## Wire operation

The operation uses the existing conventional DCPDP13 SetVCP backend, not the
LG alternate F4 input command. For Vivid its request is:

```text
chip=0x37 data=0x51 payload=84 03 15 00 31 9c
```

Reader replaces `31 9c` with `01 ac`. The checksum is the XOR of `0x6e`, the
out-of-band source/data address `0x51`, and the first five payload bytes.
The historically validated conventional DCPDP13 SET backend performs two
identical physical writes, each with a 10 ms pre-write delay. Picture Mode
makes one semantic SetVCP call and does not add its own duplicate loop.

No picture-mode GET, verification, retry, restore, fallback, or secondary VCP
write occurs. The old branch contained a `GetVCP(0x15)` mapping helper, but no
new readback claim is made here: the current feature intentionally remains
write-only until a dedicated readback validation is requested.

CLI usage is deliberately symbolic:

```sh
rss-ddc --verbose picture-mode 2 vivid
rss-ddc --verbose picture-mode 2 reader
```

Verbose output identifies the semantic operation, exact target, mode, raw
value, conventional payload, chip/data, two physical writes, 10 ms pre-delays,
IOReturn values, and the no-GET/no-verify policy.
