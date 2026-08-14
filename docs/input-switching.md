# Validated LG alternate input switching

`rss_ddc_set_input` makes input selection explicit. `STANDARD` delegates
unchanged to the normal provider `SetVCP(0x60)` path. `LG_ALT` is a different,
write-only transport and never falls back to `STANDARD`.

## Evidence and scope

On the validated LG HDR QHD, ordinary `GetVCP(0x60)` returned maximum `18`,
current `0`; it did not establish a safe input-switch write. Slice 2's MCCS
retrieval advertises `0x60` enum values `11 12 0f 00`. Those are preserved as
monitor capability evidence, but they are not LG alternate-input values.

The historical investigation evolved as follows:

- `b692fd5` tried the alternate IOAV data address `0x50` with normal VCP `0x60`
  values `0x11`/`0x12`; its inline-address variant was deliberately rejected
  before IOAV construction.
- `bb254f5` mirrored the upstream m1ddc command: IOAV data `0x50`, control
  code `0xf4`, and a checksum that includes `0x50`.
- `3539411` promoted only that final form after hardware validation. The
  earlier conventional and inline experiments were not promoted. It retained
  two identical writes without evidence that both were necessary.

The final packet is `84 03 f4 00 value checksum`, sent to chip `0x37`, IOAV
data `0x50`. The checksum is the XOR convention `6e ^ 50 ^ 84 ^ 03 ^ f4 ^ 00
^ value`. Production sends one write after a 10 ms pre-write delay, with no
response, GET, verification, restore, retry, or fallback.

The original two-write form worked, but repository history contained no
one-write result or evidence that duplication was necessary. A controlled
hardware A/B test then established one-write success for HDMI 1 → DP 1
(`0xd0`), DP 1 → HDMI 1 (`0x90`), and a repeated one-write DP 1 transition;
all writes returned `IOReturn=0x00000000`. The existing two-write control also
succeeded, but provided no observed benefit, so production now sends one F4
transaction.

| Physical input | LG_ALT value | Complete payload |
| --- | --- | --- |
| HDMI 1 | `0x90` | `84 03 f4 00 90 dd` |
| HDMI 2 | `0x91` | `84 03 f4 00 91 dc` |
| DisplayPort 1 | `0xd0` | `84 03 f4 00 d0 9d` |

The alternate path is available only after all of these checks succeed:
`DCPDP13Service`, the ordinary DCPDP13 safety correlation, product name
`LG HDR QHD`, and service role `DCPEXT0`. The historical discovery record
has no manufacturer, retail model, serial, or EDID evidence, so this exact
product-name/service-role gate is intentionally narrow rather than a general LG
profile system.

## API and CLI

```c
RSSDDCError rss_ddc_set_input(uint32_t display_index,
                              RSSDDCInputSwitchMethod method,
                              uint16_t value);
```

The CLI equivalent is:

```sh
./rss-ddc [--verbose] input <display-index> standard <mccs-value>
./rss-ddc [--verbose] input <display-index> lg-alt <0x90|0x91|0xd0>
```

`LG_ALT` rejects every other value before display resolution or IOAV service
construction. The provider capability reports only that DCPDP13 can issue this
transport; the exact target gate is required before the first write.
