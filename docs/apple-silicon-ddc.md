# Apple Silicon DDC transport notes

## Evidence scope

**Hardware-validated `rss-ddc` behavior** is limited to the PS190 Service-path Get VCP transactions below, manually run from iTerm2 on macOS build `25F84`, an Odyssey G75F, and provider `AppleDCPPS190`. **Prior research-fork hardware evidence** supports the conventional DCPDP13 GET and the PS190 SET sequence described below, but neither has yet been repeated by this standalone project. **Static-analysis conclusions** cover the PS190 no-offset transport sentinel. Provider classification and safety correlation are implementation architecture, not portability evidence.

## Standard DP Get VCP (`DCPDP13Service`)

`DCPDP13Service` is a distinct provider backend. Earlier hardware work on an
Odyssey connected through USB-C to DisplayPort used the conventional
Service-level IOAV form:

```text
write chip=0x37, data=0x51, payload=82 01 <VCP> <checksum>
delay 50 ms
read  chip=0x37, data=0x51, length=11
```

For that representation, `0x51` is the IOAV data/subaddress argument and is
not included in the four-byte payload. The request checksum remains seeded by
the DDC destination address `0x6e`: VCP `0x10` uses `82 01 10 fd`; VCP `0x60`
uses `82 01 60 8d`.

`rss-ddc` enables this backend only after correlating the selected external
display to an actual `DCPDP13Service` Service proxy, the active branch, and a
unique external `DCPDPDeviceProxy`. A generic
`IOPortTransportStateDisplayPort` observation cannot select this backend:
PS190 HDMI has presented the same transport-state class. The standalone
backend is pending its first hardware validation, so these details are an
implementation based on prior evidence, not a portability claim.

## PS190 Get VCP

For `AppleDCPPS190`, a normal `IOAVServiceWriteI2C(..., data=0x51, ...)` uses register/subaddress preparation and is not the validated DDC/CI GET framing. The working request is raw-framed and uses `UINT32_MAX` for both calls:

```text
write chip=0x37, data=UINT32_MAX, payload=51 82 01 <VCP> <checksum>
delay 50 ms
read  chip=0x37, data=UINT32_MAX, length=11
```

The request checksum is `0x6e ^ 0x51 ^ 0x82 ^ 0x01 ^ VCP`.

| VCP | Request | Reply | Decoded value |
| --- | --- | --- | --- |
| `0x10` | `51 82 01 10 ac` | `6e 88 02 00 10 00 00 32 00 32 a4` | max 50, current 50 |
| `0x60` | `51 82 01 60 dc` | `6e 88 02 00 60 00 00 12 00 12 d4` | max 18, current 18 |

For both rows, `rss-ddc` selected the `AppleDCPPS190` backend; the raw write and raw read each returned `IOReturn = 0x00000000`; the strict parser accepted the frame and checksum. Normal CLI output was `50` for VCP `0x10` and `18` for VCP `0x60`.

The parser validates source, framing, response length, command, status, requested VCP, decoded maximum/current values, and checksum. BetterDisplay independently reported the same current values; it was used only as a behavior oracle.

## PS190 Set VCP

PS190 Set VCP uses a different, historically successful representation from
PS190 GET. The prior `m1ddc-rss` implementation sent a conventional
Service-level write with `data=0x51`, rather than a raw `UINT32_MAX` write.
It constructed the six-byte payload:

```text
84 03 <VCP> <value-high> <value-low> <checksum>
```

The checksum is `0x6e ^ 0x51 ^ 0x84 ^ 0x03 ^ VCP ^ value-high ^ value-low`.
For input VCP `0x60`, values `17` and `18` produce `84 03 60 00 11 c9` and
`84 03 60 00 12 ca`, respectively.

The historical write helper performed **two** identical writes, each preceded
by 10 ms, and did not read or validate a Set VCP acknowledgement. `rss-ddc`
preserves that exact evidence-backed sequence pending its first standalone
hardware validation. A write failure is reported; a successful return means
only that both IOAV writes succeeded, not that the monitor accepted the
semantic value. This behavior derives from
`m1ddc-rss` commit `a561e56` and its current
`sources/i2c.m`/`sources/m1ddc.m` path, which applied the same Service write
to the built-in HDMI transport.

No out-of-bounds or canary corruption was observed in the predecessor research lab's guarded request/reply buffers. This does not establish behavior on different providers, monitors, cables/adapters, firmware revisions, or macOS releases. DCPDP13 standalone validation, PS190 SET standalone validation, MCDP GET, EDID/DPCD operations, and broader provider/hardware coverage remain unsupported or unvalidated.
