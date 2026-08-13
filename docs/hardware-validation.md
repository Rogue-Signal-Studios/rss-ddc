# Hardware validation matrix

This is the authoritative summary of direct, user-run `rss-ddc` hardware
validation. It describes documented host, macOS, provider, monitor, and link
topologies—not a portability promise for every Apple Silicon system, adapter,
or monitor with the same provider class. Synthetic CI verifies build and logic
only; it never substitutes for these results.

## Current runtime matrix

| Provider | Validated runtime capabilities | Unsupported runtime capabilities | Evidence scope |
| --- | --- | --- | --- |
| `AppleDCPPS190` | Get VCP, Set VCP, EDID blocks 0–1, read-only DPCD (`0x0f`) | EDID blocks 2+, DPCD writes | Mac mini M4 Pro, macOS 26.5.2 / `25F84`, Odyssey G75F over built-in HDMI |
| `DCPDP13Service` | Get VCP, Set VCP, Set-and-Verify, read-only DPCD, MCCS capabilities, alternate input transport (`0x3b`) | EDID, DPCD writes | Mac mini M4 Pro, macOS 26.5.2 / `25F84`, LG HDR QHD over DisplayPort |
| `DCPDPService` | Get VCP, Set VCP, Set-and-Verify, read-only DPCD (`0x0b`) | EDID, DPCD writes | Mac Studio M2 Ultra, macOS 26.5.2 / `25F84`, BenQ XL2730Z over DisplayPort / `DCPEXT2` |
| `AppleDCPMCDP29XX` | none | GET, SET, EDID, DPCD | Classified only; no validated MCDP topology |

## Documented topologies

### Mac mini M4 Pro / `25F84`

- Odyssey G75F / built-in HDMI / `AppleDCPPS190`
- LG HDR QHD / DisplayPort / `DCPDP13Service`

The simultaneous mixed-provider tests proved selected-display correlation: each
operation addressed only its chosen provider path. PS190 uses raw GET framing
with the firmware no-offset sentinel; conventional DP providers use the
separately documented conventional Service framing. See the [Apple Silicon
transport notes](apple-silicon-ddc.md) and per-monitor pages for exact bytes.

### Mac Studio M2 Ultra / `25F84`

- BenQ EW3270U / DisplayPort / `DCPDP13Service` / `DCPEXT1`
- BenQ XL2730Z / DisplayPort / `DCPDPService` / `DCPEXT2`
- ASUS PG349Q / HDMI / `AppleDCPPS190` / `DCPEXT5`

The XL2730Z normal runtime path validated conventional GET, two-write SET,
default-policy Set-and-Verify, and one 16-byte native DPCD read. EDID remains
intentionally unsupported. See [Mac Studio topology notes](monitors/mac-studio-m2-ultra.md)
and [BenQ XL2730Z](monitors/benq-xl2730z.md).

## Boundaries retained by design

- All DPCD support is read-only: one 1–16-byte read within the 20-bit address
  range, with no chunking, retries, scans, or writes.
- EDID remains independently dispatched; no DP or MCDP provider borrows the
  PS190 Device path.
- Plain GET and SET retain their validated framing and timing. Set-and-Verify
  is an explicit higher-level policy and adds no provider-specific timing rule.
- DCPDP13 MCCS retrieval uses one conventional F3/write, 50 ms delay, and
  one guarded 38-byte read per fragment. Only the strict declared E3 prefix is
  used; the observed modified read-window tail is never parsed or advanced.
- The DCPDP13 alternate input transport is available only through an explicit
  caller-selected method. Its provider capability does not identify monitors
  or infer that an LG-style mechanism applies to every DCPDP13 display.
- Unknown and unsupported providers/capabilities fail closed.

## Next research milestones

1. DCPDPService EDID research on the pinned Mac Studio topology.
2. MCDP research when a real MCDP topology is available.
3. Additional monitor coverage.
4. Machine-readable profiles only after stronger identity semantics are designed.
5. Stream Deck integration after the low-level foundation is sufficiently mature.
