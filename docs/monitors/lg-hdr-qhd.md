# LG HDR QHD

## Identity

| Field | Evidence |
| --- | --- |
| Manufacturer | Unavailable in the validated rss-ddc discovery output |
| Reported product name | `LG HDR QHD` |
| Retail model / serial / firmware | Unavailable in the recorded validation output |

`LG HDR QHD` is intentionally retained as the page name because it is the
exact observed product identity. It must not be treated as a uniquely matched
retail model.

## Tested environment

User-run hardware validation occurred on macOS build `25F84`, using a
DisplayPort path classified as `DCPDP13Service`. An Odyssey G75F/PS190 HDMI
display was connected simultaneously; each selected command remained scoped to
its own display/provider binding.

## Validated capabilities

| Capability | Status | Observation |
| --- | --- | --- |
| Get VCP `0x10` brightness | Hardware validated | Maximum `100`; current `100` and `99` were strictly parsed during validation. |
| Get VCP `0x60` input source | Hardware validated | Maximum `18`, current `0`. Value `0` is not interpreted here as a known safe or settable input code. |
| Set VCP `0x10` brightness | Hardware validated | Same-state `100` and real change/restore `100 → 99 → 100`. |
| Set-and-Verify `0x10` | Hardware validated | Default policy verified same-state and real changes; the retry path was also validated. |

No broader VCP support, input-source SET semantics, or behavior on other LG
products is implied.

## Intermittent post-SET transient

This monitor produced an important **observed** post-SET behavior:

1. After one SET, an immediate GET returned eleven zero bytes.
2. The strict parser rejected that frame as invalid source/framing.
3. A GET after about one second succeeded; five more GETs about one second
   apart also succeeded.
4. During Set-and-Verify validation, another first verification GET returned
   eleven zero bytes; the configured 250 ms retry returned a valid matching
   reply.
5. A separate zero-settle, zero-retry Set-and-Verify run succeeded immediately.

The evidence shows an intermittent transient on this tested monitor. All-zero
frames remain invalid: they are never an accepted value. It does **not** show
that all LG monitors or all `DCPDP13Service` paths behave this way, that one
second is required, or that 250 ms is guaranteed. The existing default
Set-and-Verify policy handled the observed retry, but no monitor-specific
automatic policy is applied at runtime.

## VCP observations

| VCP | Observation |
| --- | --- |
| `0x10` brightness | Maximum `100`; verified current values included `100` and `99`. |
| `0x60` input source | Maximum `18`, current `0`; no input-code interpretation or SET claim is recorded. |

## EDID identity

Pending provider-specific acquisition and hardware validation. No EDID identity
is inferred from the generic `LG HDR QHD` product name.

For conventional DP transport details, see the
[Apple Silicon transport notes](../apple-silicon-ddc.md). For evidence scope,
see the [catalog index](README.md).
