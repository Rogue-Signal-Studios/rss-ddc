# Apple Silicon DDC transport notes

## Evidence scope

**Hardware-validated `rss-ddc` behavior** is limited to the Service-path
transactions and PS190 Device-path EDID reads below, manually run from iTerm2
on macOS Tahoe `26.5.2` build `25F84` with an Odyssey G75F on a Mac mini M4
Pro. The tested HDMI provider was `AppleDCPPS190`; the separately tested USB-C
→ DisplayPort provider was `DCPDP13Service`. **Static-analysis conclusions**
cover the PS190 no-offset transport sentinel. Provider classification and
safety correlation are implementation architecture, not portability evidence.

## EDID acquisition evidence

rss-ddc has hardware-validated PS190 **Device** path reads of both available
EDID blocks on macOS Tahoe `26.5.2` build `25F84` with the Odyssey G75F. The
structurally paired `DCPAVDeviceProxy` created `IOAVDevice`; block 0 used
`IOAVDeviceReadI2C(device, 0x50, 0x00, buffer, 128)` and block 1 used
`IOAVDeviceReadI2C(device, 0x50, 0x80, buffer + 128, 128)`. This produced the
valid 256-byte image below. No write or segment-pointer operation was needed.
This Device path is distinct from the Service APIs used by PS190 GET/SET.

```text
00 ff ff ff ff ff ff 00 4c 2d 67 79 51 43 30 31
16 24 01 03 80 5d 28 78 2a 50 25 ad 51 48 a8 27
09 50 54 21 08 00 81 c0 81 00 81 80 95 00 a9 c0
b3 00 d1 c0 01 01 e7 7c 70 a0 d0 a0 29 50 30 20
3a 00 a2 90 31 00 00 1a 00 00 00 fd 00 30 b4 1e
ff eb 00 0a 20 20 20 20 20 20 00 00 00 fc 00 4f
64 79 73 73 65 79 20 47 37 35 46 0a 00 00 00 ff
00 48 4e 54 4c 35 30 31 37 39 30 0a 20 20 01 91

02 03 4c f0 e2 78 03 4b 61 5f 10 3f 04 03 76 5a
5c 7e c1 23 09 07 07 83 01 00 00 e2 00 4f e3 05
c0 00 6b 03 0c 00 20 00 b8 44 28 00 20 01 6d d8
5d c4 01 78 80 6b 00 30 b4 c3 64 3f e6 06 05 01
73 5a 00 e2 0f 41 e5 01 8b 84 90 59 6f c2 00 a0
a0 a0 55 50 30 20 35 00 a2 90 31 00 00 1a 56 5e
00 a0 a0 a0 29 50 30 20 35 00 a2 90 31 00 00 1a
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 76
```

It decodes as `SAM`, product `0x7967`, numeric serial `825246545`, text serial
`HNTL501790`, `Odyssey G75F`, EDID `1.3`, `93 × 40 cm`, with one declared
extension and a valid base checksum. The hardware-read block 1 is CTA-861,
revision 3, with a valid checksum; it completed the declared one-extension
image at 256 bytes and also succeeded through the guarded raw export path.
The E-EDID block-number-to-segment/offset model is standards-backed. Its
segment-0/offset-`0x80` block-1 IOAV Device mapping is now hardware validated
for this exact PS190/Odyssey topology. Blocks 2+ need a segment-pointer write,
which remains unimplemented and unvalidated. DCPDP13/MCDP EDID acquisition,
extension timing beyond block 1, and any provider-wide EDID rule remain
unproven.

## Native DPCD evidence and current scope

Prior guarded hardware research on the selected PS190 HDMI path established a
different private object path from DDC/CI and EDID: the exact
branch-correlated `DCPDPDeviceProxy` was passed to
`IODPDeviceCreateWithService`, then `IODPDeviceReadDPCD` was called with a
20-bit DPCD register address, a caller destination, and a 32-bit byte count.
Both reads returned `IOReturn = 0x00000000` with intact canaries:

| Address / length | Bytes |
| --- | --- |
| `0x00000` / 16 | `11 0a c4 83 01 1d 01 c1 2a 4b 04 00 4f 00 84 00` |
| `0x00200` / 8 | `41 00 77 77 01 07 00 00` |

rss-ddc reproduced both reads exactly on the Mac mini M4 Pro / macOS Tahoe
26.5.2 build 25F84 Odyssey G75F PS190 path: each returned
`IOReturn = 0x00000000`, the base-capability decode was revision `0x11`, HBR
raw code `0x0a`, four lanes, enhanced framing, and downstream-port-present.
The selected display remained isolated in the simultaneous PS190 + DCPDP13
topology. PS190 continues to use its existing branch-correlated proxy path.

rss-ddc permits one PS190 or DCPDP13 read of at most 16 bytes and performs no
chunking or DPCD writes. The evidence does not establish larger transfers,
boundary crossing, or retries. DCPDP13 runtime DPCD is hardware validated only
on the LG HDR QHD / `DCPEXT0` topology: the selected Service role resolved one
same-role `dcpdp-device-epic` `DCPDPDeviceProxy`, construction succeeded, and
one `0x00000`/16 read returned `IOReturn = 0x00000000` with:

```text
12 14 c4 01 01 00 01 80 02 00 06 00 00 00 83 00
```

The portable decoder identifies revision `0x12`, raw link rate `0x14` (HBR2),
four lanes, enhanced framing, and no downstream-port-present bit. This does
not generalize to another DCPDP13 display or to MCDP. Zero or multiple scoped
candidates fail closed, and DCPDP13 never borrows PS190's proxy.

## Standard DP Get VCP (`DCPDP13Service`)

`DCPDP13Service` is a distinct provider backend. `rss-ddc` Get VCP was
hardware-validated on macOS `25F84` using conventional Service-level IOAV
framing. The initial single-display validation used an Odyssey G75F over USB-C
to DisplayPort; the later simultaneous mixed-provider validation below used
an LG HDR QHD on the DisplayPort path.

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

The PS190 safety gate is intentionally more topology-specific. It scopes an
active `BranchDeviceID` and its unique external `DCPDPDeviceProxy` to the
selected display, then requires the selected AV Service EPIC role to equal
that device EPIC role. It does not assume a fixed `DCPEXT` ordinal: a
development topology with simultaneous PS190 HDMI and DP service paths placed
the selected PS190 display on `DCPEXT1` and the DP display on `DCPEXT0`.
Provider identity, EPIC name, external Unit 0, UI support, and scoped
ambiguity rejection remain required. This is a correlation fix, not yet a
mixed-topology hardware GET/SET validation claim.

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

On a Mac Studio M2 Ultra (macOS `25F84`, three external displays), display
index 2 (BenQ XL2730Z) reported registry class `DCPDPService` on `DCPEXT2`.
No `AppleDCPMCDP29XX` provider was present in that tested live topology.
`DCPDPService` remains runtime-unsupported. DPCD at `0x00000`/16 is hardware
validated on that path via `validate-dcpdpservice-dpcd`. GET uses
`validate-dcpdpservice-get` with conventional framing **inferred** from
DCPDP13; it is not yet hardware validated. See
[Mac Studio topology notes](monitors/mac-studio-m2-ultra.md) and
[BenQ XL2730Z](monitors/benq-xl2730z.md).

DCPDP13 GET uses `IOAVServiceCreateWithService` on the selected external
`DCPAVServiceProxy` under `dcpav-service-epic`. The MCCS payload is
protocol-standard; the IOAV chip/data/delay contract is DCPDP13-validated ABI
evidence. For `DCPDPService`, the same `dcpav-service-epic` object shape exists,
but GET success is still **unknown**. `DCPDPServiceProxy` is a sibling on
`dcpdp-service-epic` and is not used. `DCPDPDeviceProxy` is validated for DPCD
only, not DDC GET.

Unlike this conventional DP form, PS190 GET includes `0x51` in a five-byte raw
payload and uses `UINT32_MAX` for both IOAV calls. Both use a 50 ms delay and
the same strict 11-byte reply parser; framing is selected only by the proven
provider identity.

## Standard DP Set VCP (`DCPDP13Service`)

`rss-ddc` implements the conventional DP Set VCP transaction recovered from
the original USB-C/DisplayPort-only m1ddc path (the parent of research commit
`a561e56`). The common portable builder
forms `84 03 <VCP> <value-high> <value-low> <checksum>`, where the checksum is
`0x6e ^ 0x51 ^ 0x84 ^ 0x03 ^ VCP ^ value-high ^ value-low`. The DP backend uses
the display-correlated `IOAVService` to issue exactly two writes:

```text
chip=0x37, data/subaddress=0x51, payload length=6
pre-write delay=10 ms
write count=2
response/ack read=none
```

For input VCP `0x60`, values 15, 17, and 18 produce `84 03 60 00 0f d7`,
`84 03 60 00 11 c9`, and `84 03 60 00 12 ca`. This transaction shape matches
PS190 SET but not PS190 GET: PS190 GET is raw-framed with `UINT32_MAX`, while
both SET paths use the conventional subaddress form. DP SET was
hardware-validated as a same-state `0x10 = 100` transaction on the documented
LG HDR QHD path: both writes returned `IOReturn = 0x00000000`, and the sibling
PS190 display did not change. A successful write return still does not
establish acceptance of other values or monitor configurations.

## Simultaneous mixed-provider validation

On macOS `25F84`, the following two external displays were attached at once:

| Display | Product / transport | Provider / role |
| --- | --- | --- |
| 1 | Odyssey G75F / HDMI | `AppleDCPPS190` / `DCPEXT1`, branch `pHDMIg` |
| 2 | LG HDR QHD / DisplayPort | `DCPDP13Service` / `DCPEXT0` |

Discovery and provider correlation resolved both displays independently.
Selected-display GET stayed isolated: Odyssey returned the PS190 fixtures
`51 82 01 10 ac` → `6e 88 02 00 10 00 00 32 00 32 a4` (max/current 50) and
`51 82 01 60 dc` → `6e 88 02 00 60 00 00 12 00 12 d4` (max/current 18). LG
returned conventional DP fixtures `82 01 10 fd` →
`6e 88 02 00 10 00 00 64 00 64 a4` (max/current 100) and `82 01 60 8d` →
`6e 88 02 00 60 00 00 12 00 00 c6` (max 18, current 0). Every listed reply
passed strict framing and checksum validation.

Same-state SETs were also isolated: Odyssey `0x60 = 18` sent
`84 03 60 00 12 ca` twice, and LG `0x10 = 100` sent `84 03 10 00 64 cc`
twice. Each write used chip `0x37`, data `0x51`, length 6, and a 10 ms
pre-write delay; all four calls returned `IOReturn = 0x00000000`. Neither
operation changed its sibling display. Two writes within one backend SET are
one selected-display transaction, not a broadcast.

On the LG only, an immediate independent GET after the same-state SET returned
`00 00 00 00 00 00 00 00 00 00 00`. The strict parser rejected it as invalid
source/framing. A GET after approximately one second was valid, and five more
GETs approximately one second apart were all valid. This is an observed
monitor-specific settling behavior, not a required one-second delay or a rule
for DP monitors. rss-ddc keeps SET write-only and GET independent; it adds no
automatic sleep, retry, or verification after SET.

## Opt-in verification policy (hardware-validated scope)

The public `set --verify` facility deliberately sits above the documented
PS190 and conventional-DP backends. It does not change raw PS190 GET framing,
conventional DP framing, either provider's two-write SET sequence, or ordinary
GET/SET timing. After a successful write-only SET it can apply a caller-chosen
settle/retry policy and issue independent GETs. The initial default is 100 ms
settle plus three additional attempts at 250 ms intervals; this is a modest
policy choice, not a claim that DDC/CI or the LG requires those values.

For safety in a mixed topology, verification captures the original
ColorSync/CoreGraphics display UUID/ID and provider/transport correlation and refuses a later
GET unless the original numeric index still proves that same display. It never
searches or selects a sibling after re-enumeration. If an input-source SET
switches the monitor away from the issuing host, the write may have succeeded
while verification is unavailable. That outcome is explicitly not “verified”
and not proof that the backend SET failed.

User-run validation on macOS `25F84` covered both displays attached at once:

| Selected display / provider | Set VCP `0x10` request | Strict verification reply | Result |
| --- | --- | --- | --- |
| Odyssey G75F / `AppleDCPPS190` | same state `50`: `84 03 10 00 32 9a`; real change `49`: `84 03 10 00 31 99` | `6e 88 02 00 10 00 00 32 00 32 a4` for 50; `6e 88 02 00 10 00 00 32 00 31 a7` for 49 | `50 → 49 → 50` verified |
| LG HDR QHD / `DCPDP13Service` | same state `100`: `84 03 10 00 64 cc`; real change `99`: `84 03 10 00 63 cb` | `6e 88 02 00 10 00 00 64 00 64 a4` for 100; `6e 88 02 00 10 00 00 64 00 63 a3` for 99 | `100 → 99 → 100` verified |

All listed replies passed the strict source/framing, length, command, status,
VCP, value, and checksum checks. The default policy was `settle_ms=100`,
`retry_count=3`, and `retry_delay_ms=250`; same-state verification succeeded
on attempt one for both providers.

The LG also provided live validation of the retry path. A zero-settle,
zero-retry `99` verification succeeded on attempt one, so the transient is not
deterministic. In a different default-policy restore to `100`, SET succeeded
but verification GET #1 returned `00 00 00 00 00 00 00 00 00 00 00`. The
strict parser rejected it as invalid source/framing, the orchestration marked
the error retryable, waited 250 ms, and GET #2 returned
`6e 88 02 00 10 00 00 64 00 64 a4`; the expected/returned value matched and
the CLI reported `verified 100`. This proves retry recovery on this observed
LG path only. It does not establish that every LG/DP SET needs a retry, that
zero settling is generally sufficient, or that 250 ms is a portable delay.

The validated commands remained single-target while both displays were live:
one selected display, one retained identity, one provider binding, and either
verification against that same display or a fail-closed result. Plain GET,
plain SET, and provider backend transaction shapes remain independent of this
higher-level policy.

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

No out-of-bounds or canary corruption was observed in the predecessor research lab's guarded request/reply buffers. This does not establish behavior on different providers, monitors, cables/adapters, firmware revisions, or macOS releases. DCPDP13 Set VCP and read-only DPCD are hardware validated only on their documented paths; DCPDP13/MCDP EDID, MCDP DPCD, MCDP GET/SET, and broader provider/hardware coverage remain unsupported or unvalidated.
