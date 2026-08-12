# Monitor compatibility and quirks

This catalog records monitor-specific evidence. It is deliberately separate
from [Apple Silicon transport notes](../apple-silicon-ddc.md), which describe
provider/back-end behavior such as IOAV framing and correlation. A quirk seen
on one monitor is not a provider-wide rule.

## Evidence levels

| Level | Meaning |
| --- | --- |
| **Hardware validated** | Executed on real hardware with direct `rss-ddc` command/output evidence. |
| **Observed** | Seen during real testing, but not systematically reproduced or generalized. |
| **Research-backed** | Recovered from prior code or research, not independently validated by current `rss-ddc` on this exact configuration. |
| **Contributor-reported** | Submitted by a user and not independently reproduced by project maintainers. |
| **Unverified** | Known or suspected, but lacking enough evidence for another level. |

Hardware validation is limited to the listed monitor, connection path, macOS
build, and provider. It does not certify other firmware versions, adapters,
providers, or displays with the same product name.

## Compatibility index

| Manufacturer | Model / reported product name | Tested provider and path | GET | SET | Set-and-Verify | Known quirk | macOS build | Details |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Samsung | Odyssey G75F | Built-in HDMI / `AppleDCPPS190`; USB-C → DisplayPort / `DCPDP13Service` | Hardware validated | Hardware validated on HDMI/PS190; see path details | Hardware validated on HDMI/PS190 only | PS190 EDID blocks 0–1 hardware validated; GET framing differs by provider path | `25F84` | [Odyssey G75F](samsung-odyssey-g75f.md) |
| Unavailable | LG HDR QHD | DisplayPort / `DCPDP13Service` | Hardware validated | Hardware validated (`0x10`) | Hardware validated (`0x10`) | Read-only DPCD `0x00000`/16 hardware validated; intermittent immediate post-SET all-zero reply | `25F84` | [LG HDR QHD](lg-hdr-qhd.md) |
| BenQ | XL2730Z | DisplayPort / `DCPDPService` / `DCPEXT2` | Validation pending | Unverified | Unverified | DPCD `0x00000`/16 hardware validated; GET harness uses inferred conventional framing | `25F84` | [XL2730Z](benq-xl2730z.md) |

`LG HDR QHD` is the exact product name currently reported by rss-ddc; no retail
model number, manufacturer string, or serial is recorded in the validated
discovery output. Please do not infer one from a similar product name.

## Future machine-readable profiles

These pages may later provide evidence for machine-readable monitor profiles:
identity match rules, known VCP support, input labels/codes, verification
policy hints, and quirks. No profile format or runtime matching exists today.
Product-name-only matching is not necessarily safe; future automatic selection
must require strong, ambiguity-safe identity evidence and fail closed when it
cannot prove a single monitor match. Until then, observations remain
documentation only and do not alter runtime timing, transport, or retry policy.

See [contribution guidance](CONTRIBUTING.md) for the evidence format.
