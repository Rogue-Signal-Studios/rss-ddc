# LG/DCPDP13 input-framing research probe

`probe-input-alt` is a developer-only, state-changing hardware experiment. It
is not public API, does not alter normal `set`, and never reads, verifies,
restores, retries, or falls back between variants.

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

## Variants

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

`conventional` remains the earlier, research-only alternate-address experiment:
it uses advertised VCP `0x60` with data `0x50`. It is retained for comparison;
it is not the upstream LG-alt command. `inline` also remains research-only and
fails closed before IOAV construction.

All variants are confined to this developer-only probe. It performs no GET,
verification, restore, fallback, or retry; it is restricted to a selected
`DCPDP13Service` display and writes the two upstream-prescribed packets only.

## Manual sequence

With the live HDMI 1 source visible to the LG, run one command and observe the
display before deciding whether to run the next. Do not automate or chain them:

```sh
./rss-ddc probe-input-alt 2 lg-alt 0x90  # HDMI 1
./rss-ddc probe-input-alt 2 lg-alt 0x91  # HDMI 2
./rss-ddc probe-input-alt 2 lg-alt 0xD0  # DisplayPort 1
```

Run one command at a time and observe the display manually. No hardware command
was run while implementing this probe.
