# Samsung Odyssey G75F

## Identity

| Field | Evidence |
| --- | --- |
| Manufacturer | Samsung (model identity supplied by the validated test environment) |
| Reported product name | `Odyssey G75F` |
| Retail/model identifier beyond reported name | Unavailable in this catalog |
| Serial / firmware | Unavailable in the recorded validation output |

The absence of a serial, firmware version, and stronger product identifiers
means these findings apply only to the documented monitor/configuration, not
to every Odyssey variant.

## Tested environment

Hardware validation was user-run on macOS build `25F84`. The HDMI PS190 path
was also tested while an LG HDR QHD/DCPDP13Service display was connected, which
confirmed single-target operation in a mixed-provider topology. Provider
transport details belong in the [Apple Silicon transport notes](../apple-silicon-ddc.md).

## Tested provider paths

### Built-in HDMI / `AppleDCPPS190`

| Capability | Status | Evidence |
| --- | --- | --- |
| Get VCP `0x10` brightness | Hardware validated | Reply max/current `50/50`; raw PS190 GET framing is a provider-path fact, not a monitor rule. |
| Get VCP `0x60` input source | Hardware validated | Reply max/current `18/18`. |
| Set VCP `0x60` input source | Hardware validated | State-changing `18 → 17` visibly selected Hook; same-state `18` writes also succeeded. |
| Set VCP `0x10` brightness | Hardware validated | Same-state `50`; real change/restore `50 → 49 → 50`. |
| Set-and-Verify `0x10` | Hardware validated | Default policy verified the same-state and real brightness change/restore on attempt one. |

The HDMI provider uses a raw, inline-`0x51` GET request and `UINT32_MAX`
no-offset IOAV argument. That is documented as `AppleDCPPS190` transport
behavior; do not use it as a claim about every Odyssey connection path.

### USB-C → DisplayPort / `DCPDP13Service`

| Capability | Status | Evidence |
| --- | --- | --- |
| Get VCP `0x10` brightness | Hardware validated | Observed max/current `50/50`. |
| Get VCP `0x60` input source | Hardware validated | Observed max/current `18/15` on the DisplayPort/GigaChad path. |
| Set VCP `0x60` | Research-backed | Historical conventional-DP request values `15`, `17`, and `18` are documented, but this catalog has no current rss-ddc hardware-validation record for this exact G75F USB-C → DP path. |
| Set VCP `0x10` | Unverified | No current rss-ddc hardware-validation record for this exact path. |
| Set-and-Verify | Unverified | No current hardware-validation record for this exact path. |

This is specifically USB-C → DisplayPort evidence. It is not evidence for a
native DP → DP connection or another adapter/cable path.

## Observed input-source codes

The following `0x60` values were observed on this monitor/configuration:

| Value | Observed label |
| --- | --- |
| `15` | DisplayPort / GigaChad |
| `17` | Hook |
| `18` | Rogue |

These are monitor/configuration-specific labels, not universal MCCS input
codes. Only `17` and `18` have documented PS190 SET evidence in this catalog.

## VCP observations

| VCP | Observation |
| --- | --- |
| `0x10` brightness | PS190 path reported maximum/current `50/50`; Set-and-Verify demonstrated `50 → 49 → 50`. |
| `0x60` input source | PS190 path reported maximum/current `18/18`; USB-C → DP path reported `18/15`. |

## EDID identity

The following identity was read by the rss-ddc PS190 **Device** path on macOS
`25F84`. It describes EDID data, not the ordinary CoreGraphics display
discovery fields above.

| Field | Hardware-validated EDID value |
| --- | --- |
| Manufacturer ID | `SAM` |
| Product code | `0x7967` |
| Numeric serial | `825246545` |
| Text serial | `HNTL501790` |
| Monitor name | `Odyssey G75F` |
| EDID version | `1.3` |
| Physical size | `93 × 40 cm` |
| Declared extensions | `1` |
| Received extensions | `1` |
| Complete | Yes |
| Base-block checksum | Valid |
| Extension 1 type | CTA-861 |
| Extension 1 revision | `3` |
| Extension 1 checksum | Valid |
| Total EDID size | `256` bytes |

The acquisition was `IOAVDeviceReadI2C(device, 0x50, 0x00, buffer, 128)` after
the PS190 branch/device safety correlation, followed by the hardware-validated
block-1 call `IOAVDeviceReadI2C(device, 0x50, 0x80, buffer + 128, 128)`. The
standard E-EDID mapping is segment 0/offset `0x80`; no DDC/CI or segment-pointer
write was used. This complete two-block result was targeted only at the
selected PS190 display in the live mixed PS190 + DCPDP13 topology. It does not
establish blocks 2+, segment-pointer semantics, or DCPDP13 EDID support. No
EDID identity is recorded for the LG sibling display.

## Roadmap

1. EDID — current PS190 scope complete
2. DPCD
3. MCDP
4. More monitor catalog coverage
5. Machine-readable profiles later

## DPCD

Prior guarded PS190 research hardware-validated the selected-display native
path `DCPDPDeviceProxy → IODPDeviceCreateWithService → IODPDeviceReadDPCD`.
Reads of `0x00000`/16 and `0x00200`/8 succeeded with intact canaries; the raw
bytes are recorded in the [Apple Silicon transport notes](../apple-silicon-ddc.md).
rss-ddc implements only those evidence-backed constraints: one read, maximum
16 bytes, no chunking, and no writes. Its runtime reproduction remains pending
manual validation. This is PS190-path evidence, not an Odyssey-wide or
cross-provider DPCD claim.

## Notes

Connection provider, not product name, chooses GET transport framing. See the
[provider notes](../apple-silicon-ddc.md) and the [catalog index](README.md)
for scope and evidence terminology.
