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
`0x50` instead of `0x51`, with all else the same. The linked m1ddc source and
PR #52 prove an additional crucial detail: its write checksum uses the selected
IOAV input address. m1ddc has no inline source-address payload form; its
alternate command keeps the source address out-of-band and changes both the
IOAV data argument and checksum input. It uses VCP `0xf4` for its own LG
alternate command, so this probe does **not** transfer that VCP mapping to this
LG; it keeps the hardware-advertised VCP `0x60` fixed.

Sources: [BetterDisplay discussion #4246](https://github.com/waydabber/BetterDisplay/discussions/4246),
[m1ddc PR #52](https://github.com/waydabber/m1ddc/pull/52), and
[m1ddc i2c.m](https://github.com/waydabber/m1ddc/blob/main/sources/i2c.m).

## Variants

`conventional` is the sole evidence-backed experiment:

```text
chip=0x37 data=0x50
payload=84 03 60 00 value checksum
checksum=0x6e xor 0x50 xor 0x84 xor 0x03 xor 0x60 xor 0x00 xor value
writes=2; pre-write delay=10 ms; no response
```

For value `0x11`, the exact payload is `84 03 60 00 11 c8`. For `0x12`, it is
`84 03 60 00 12 cb`.

`inline` is accepted only so the research question is explicit. It fails closed
before IOAV construction: upstream provides no inline source-address framing
for this operation, so rss-ddc deliberately does not invent a raw packet.

## Manual sequence

With the live HDMI 1 source visible to the LG, run one command and observe the
display before deciding whether to run the next. Do not automate or chain them:

```sh
./rss-ddc probe-input-alt 2 conventional 0x11
./rss-ddc probe-input-alt 2 inline 0x11
./rss-ddc probe-input-alt 2 conventional 0x12
./rss-ddc probe-input-alt 2 inline 0x12
```

The `inline` commands are expected to report unsupported/no write. Do not test
`0x0f` unless it is required for an intentional DP candidate/restore attempt.
