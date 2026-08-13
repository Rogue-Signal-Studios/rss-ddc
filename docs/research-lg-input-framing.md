# LG/DCPDP13 alternate-input research record

This historical research record established the production `LG_ALT` input
transport. The developer-only `probe-input-alt` CLI was removed when the
transport was promoted to the public API; see [Input switching](input-switching.md)
for the supported interface and current provider gating.

## Evidence and checksum audit

The normal DCPDP13 GET payload omits the source address because IOAV receives
`data=0x51`; its checksum is `0x6e xor 0x82 xor 0x01 xor vcp`. Normal
brightness SET uses IOAV `data=0x51` and payload `84 03 10 hi lo checksum`,
where checksum is `0x6e xor 0x51 xor 0x84 xor 0x03 xor 0x10 xor hi xor lo`.
Those production paths are unchanged.

BetterDisplay’s maintainer states that some LG input selection uses data address
`0x50` instead of `0x51`. The m1ddc source confirms that `input-alt` is also a
distinct control code: `0xf4`, not the MCCS input-source VCP `0x60`. m1ddc
creates an `input-alt` packet with input address `0x50`, and PR #52 corrects
the write checksum to use that selected IOAV address. The source address is
out-of-band (the IOAV data argument), not inline in the payload.

Sources: [BetterDisplay discussion #4246](https://github.com/waydabber/BetterDisplay/discussions/4246),
[m1ddc PR #52](https://github.com/waydabber/m1ddc/pull/52), and
[m1ddc i2c.m](https://github.com/waydabber/m1ddc/blob/main/sources/i2c.m).

## Validated framing

`lg-alt` mirrors the upstream `m1ddc set input-alt` implementation:

```text
chip=0x37 data=0x50
payload=84 03 f4 00 value checksum
checksum=0x6e xor 0x50 xor 0x84 xor 0x03 xor 0xf4 xor 0x00 xor value
writes=2; each has a 10 ms pre-write delay; no response
```

The exact documented packets are:

| Input | Value | Payload |
| --- | --- | --- |
| HDMI 1 | `0x90` | `84 03 f4 00 90 dd` |
| HDMI 2 | `0x91` | `84 03 f4 00 91 dc` |
| DisplayPort 1 | `0xd0` | `84 03 f4 00 d0 9d` |

The earlier `conventional` and `inline` experiments were intentionally not
promoted: only the validated `LG_ALT` mechanism is exposed, and only through
the caller-selected public API. No hardware command was run while implementing
the promotion.
