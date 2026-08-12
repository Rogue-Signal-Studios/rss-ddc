# Apple Silicon DDC transport notes

## Evidence scope

**Hardware-validated `rss-ddc` behavior** is limited to the Service-path
transactions below, manually run from iTerm2 on macOS build `25F84` with an
Odyssey G75F. The tested HDMI provider was `AppleDCPPS190`; the separately
tested USB-C → DisplayPort provider was `DCPDP13Service`. **Static-analysis
conclusions** cover the PS190 no-offset transport sentinel. Provider
classification and safety correlation are implementation architecture, not
portability evidence.

## Standard DP Get VCP (`DCPDP13Service`)

`DCPDP13Service` is a distinct provider backend. `rss-ddc` Get VCP was
hardware-validated on macOS `25F84` with the Odyssey G75F connected by USB-C
to DisplayPort. It used the conventional Service-level IOAV form:

```text
write chip=0x37, data=0x51, payload=82 01 <VCP> <checksum>
delay 50 ms
read  chip=0x37, data=0x51, length=11
```

For that representation, `0x51` is the IOAV data/subaddress argument and is
not included in the four-byte payload. The request checksum remains seeded by
the DDC destination address `0x6e`.

| VCP | Request | Reply | Decoded result |
| --- | --- | --- | --- |
| `0x10` | `82 01 10 fd` | `6e 88 02 00 10 00 00 32 00 32 a4` | max 50, current 50 |
| `0x60` | `82 01 60 8d` | `6e 88 02 00 60 00 00 12 00 0f c9` | max 18, current 15 (DisplayPort / GigaChad path) |

For both requests, `chip=0x37`, `data/subaddress=0x51`, and payload length
was four bytes; after a 50 ms delay, the conventional read used the same
`data/subaddress=0x51` and length 11. Both IOAV writes and reads returned
`IOReturn = 0x00000000`, and the strict parser accepted the source, framing,
response length, command, status, requested VCP, and checksum. Normal CLI
output was `50` and `15`, respectively.

`rss-ddc` enables this backend only after correlating the selected external
display to exactly one external `DCPAVServiceProxy`, then verifies that that
proxy's immediate EPIC parent has `EPICProviderClass = DCPDP13Service` and
that `IOAVServiceUserInterfaceSupported` is true. `DCPAVServiceProxy` is the
registry object used to construct the private Service interface; a
`DCPDPDeviceProxy` is not substituted for it.

This distinction matters because the PS190 resolver's active-branch
`BranchDeviceID` → unique `DCPDPDeviceProxy` relationship is a separately
observed PS190 safety rule, not a standard-DP requirement. A live read-only
inspection on macOS `25F84` after moving the Odyssey G75F to USB-C → DP found
one active `IOPortTransportStateDisplayPort` for the monitor
(`Port-USB-C@2/DisplayPort`) but no `BranchDeviceID`. Requiring that PS190
field caused the first standalone DP attempt to fail closed before any IOAV
operation. The corrected DP gate does not use the field.

Earlier research also observed a `DCPDPServiceProxy` under a sibling EPIC
interface to the display-correlated AV service. That is useful topology
evidence, but the research did not establish it as a prerequisite for
constructing `IOAVService`; rss-ddc therefore does not turn it into a new
untested requirement. A generic `IOPortTransportStateDisplayPort` observation
still cannot select this backend: PS190 HDMI has presented the same
transport-state class. This validation does not establish behavior for other
DP adapters, Apple Silicon systems, macOS releases, or monitors.

Unlike this conventional DP form, PS190 GET includes `0x51` in a five-byte raw
payload and uses `UINT32_MAX` for both IOAV calls. Both use a 50 ms delay and
the same strict 11-byte reply parser; framing is selected only by the proven
provider identity.

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
by 10 ms, and did not read or validate a Set VCP acknowledgement. The same
sequence is now hardware-validated in `rss-ddc`: the user manually executed
the VCP `0x60` state-changing SET from `18` to `17`, and the Odyssey visibly
switched to the requested Hook input. The same-state `18` transaction also
returned `IOReturn = 0x00000000` for both writes. No GET result for value `17`
was captured, so this validation establishes the successful write and visible
state change only. A successful return still means only that both IOAV writes
succeeded; acceptance of other values or monitor configurations is not
implied. This behavior derives from `m1ddc-rss` commit `a561e56` and its
current `sources/i2c.m`/`sources/m1ddc.m` Service-write path.

No out-of-bounds or canary corruption was observed in the predecessor research lab's guarded request/reply buffers. This does not establish behavior on different providers, monitors, cables/adapters, firmware revisions, or macOS releases. DCPDP13 Set VCP, MCDP GET/SET, EDID/DPCD operations, and broader provider/hardware coverage remain unsupported or unvalidated.
