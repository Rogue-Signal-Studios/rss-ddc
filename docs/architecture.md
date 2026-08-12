# Architecture

The public C API in `include/rss_ddc.h` exposes display discovery, inspection, Get VCP, and Set VCP without leaking CoreFoundation or IOKit handles. A display is selected by the current numeric list index; registry IDs are never exposed as stable user identifiers.

The macOS backend owns all CoreGraphics, IORegistry, and private IOAV details. It discovers online displays, resolves the corresponding external `DCPAVServiceProxy`, reads the immediate EPIC provider class, and dispatches by provider:

```text
public API / CLI
        |
macOS discovery and safety correlation
        |
provider dispatcher
   |        |        |
 DP      MCDP      PS190
```

Capabilities are independent flags: Get VCP, Set VCP, EDID read, and DPCD read. A provider receives only the capabilities that have been separately enabled. PS190 Get VCP and Set VCP, plus DCPDP13 Get VCP and Set VCP, are hardware-validated in this project. PS190 Device-path EDID and read-only DPCD, plus DCPDP13 read-only DPCD, are hardware-validated on their documented topologies. DCPDP13/MCDP EDID and MCDP DPCD remain fail-closed.

## EDID

EDID parsing is portable C: it validates the 128-byte header/checksum, decodes
base-block identity, validates every extension block that is present, and
reports declared versus received extension count plus tag, recognized type, and
revision for every acquired extension. Raw bytes remain caller-owned in
`RSSDDCEDID`; no CoreFoundation or IOKit ownership leaks through the public
API. A declared extension absent from acquisition is reported as incomplete
rather than fabricated or treated as checksum-valid. A successful partial
acquisition has a valid base block and `extensions_complete=false`; callers
must use that state rather than assuming the declared count was read.

rss-ddc hardware validation established PS190's Device-path EDID reads: the
branch-correlated `DCPAVDeviceProxy` creates `IOAVDevice`, then performs
`IOAVDeviceReadI2C(device, 0x50, 0x00, buffer, 128)` for block 0 and
`IOAVDeviceReadI2C(device, 0x50, 0x80, buffer + 128, 128)` for block 1. Both
checksums passed on macOS 25F84 with the documented Odyssey G75F, producing a
complete 256-byte EDID. Standard E-EDID maps that second block to segment 0,
offset `0x80`; the private IOAV Device-path mapping is hardware validated for
this provider/monitor/topology. Blocks 2+ require an E-EDID segment-pointer
write; no PS190 IOAV mapping for that write is enabled, so they remain partial.
DCPDP13 and MCDP EDID remain unsupported. EDID uses the same selected-display
correlation and never scans global displays or borrows a sibling binding.

The portable storage bound is eight 128-byte blocks (1024 bytes). It is a
memory/validation bound, not a promise that a provider can acquire all eight.
The standard E-EDID addressing model is: block 0 = segment 0/offset `0x00`,
block 1 = segment 0/offset `0x80`, block 2 = segment 1/offset `0x00`, and
block 3 = segment 1/offset `0x80`. The PS190 backend uses only the first two
locations; it does not write the E-EDID segment pointer at I²C address `0x30`.
This follows the [VESA E-EDID addressing table](https://glenwing.github.io/docs/VESA-EEDID-A2.pdf),
while the private-IOAV mapping of block 1 is hardware validated only for the
documented PS190/Odyssey topology. It does not establish the required segment
pointer write or access to later blocks.

EDID data may later help profile matching through manufacturer/product, name,
serial, fingerprint, and provider/path context. No fingerprint is currently
added: a portable SHA-256 implementation would be a new dependency without a
current runtime consumer. EDID alone can be duplicated or altered by adapters,
so any future automatic profile selection must combine strong identity evidence
and fail closed on ambiguity.

Provider dispatch is a pure C mapping from the immediate EPIC provider class,
which keeps classification testable without opening a display user client.
The macOS binding then applies the provider's safety gate before it creates
the private `IOAVService` object. A generic DisplayPort transport-state class
is correlation evidence only—not a provider selector—because PS190 HDMI can
present that same state.

For PS190, the binding resolver requires an external display, a correlated active DisplayPort transport, a `BranchDeviceID` with exactly one external `DCPDPDeviceProxy`, and an external Unit-0 `DCPAVServiceProxy`. Its immediate parent must identify `dcpav-service-epic`, the matching selected `DCPEXT` role, and `AppleDCPPS190`; `IOAVServiceUserInterfaceSupported` must be true. Only then does the backend construct the private Service interface. Registry IDs are transient evidence, not persistent identifiers.

For conventional DP, the resolver deliberately uses a different, smaller
gate: the selected display must map to exactly one external
`DCPAVServiceProxy`; that service's immediate EPIC parent must identify
`DCPDP13Service`; and `IOAVServiceUserInterfaceSupported` must be true. This
is enough to identify the exact Service object that the backend constructs,
while rejecting missing, ambiguous, non-external, provider-mismatched, or
UI-disabled candidates. It does not require PS190's `BranchDeviceID` or
`DCPDPDeviceProxy`: the live USB-C DP topology on the documented machine has
no `BranchDeviceID`. Neither gate selects a provider from a generic
DisplayPort-named registry node.

Correlation failures retain an internal predicate and can be surfaced by the
diagnostic public API or `rss-ddc --verbose info`. This gives operators a
specific fail-closed reason without exposing transient IOKit handles or
opening a user client.

## DPCD

The public DPCD API uses caller-owned bytes and an explicit 20-bit DPCD
register address. It accepts only a single 1–16 byte read: 16 bytes is the
largest guarded transfer validated in this project, so the backend does not
split, retry, scan, or write. Each enabled backend constructs an `IODPDevice`
and invokes the private `IODPDeviceReadDPCD(device, uint32_t address, void
*buffer, uint32_t length)` ABI exactly once. IODP creation follows Create
ownership and is released with `CFRelease`; it is never exposed from the
portable API.

This is an independent capability from EDID and DDC/CI. The PS190 runtime
reproduced its two prior research reads exactly on the documented topology.
For DCPDP13, normal runtime reads derive the Service EPIC role from the
selected display and accept only one external `DCPDPDeviceProxy` whose
immediate parent is `dcpdp-device-epic` with that same role. The validated LG
path was `DCPDP13Service → DCPEXT0 → one scoped DCPDPDeviceProxy →
IODPDeviceCreateWithService → IODPDeviceReadDPCD(0x00000, 16)`, with
`IOReturn=0x00000000` and bytes `12 14 c4 01 01 00 01 80 02 00 06 00 00 00 83
00`. Zero or multiple scoped candidates fail closed; the backend never picks a
global first match and never falls back to PS190. `probe-dpcd-path` remains a
registry-only way to diagnose that correlation. The special construction/read
harness was removed once the constrained normal runtime path was validated.

## Multi-monitor targeting

Single-target operations are a core safety constraint. A logical display
index must first resolve to its own CoreGraphics adapter and then to exactly
one external provider/Service path within that display's registry scope.
`rss-ddc set 2 0x60 18`, for example, may affect only display 2. It must never
broadcast to every provider or Service of a matching class.

Global multiplicity is valid: one PS190 display plus two `DCPDP13Service`
displays—or a PS190, DP, and MCDP display together—is not an error merely
because classes, proxies, transports, or external displays repeat. The
resolver therefore does not require global uniqueness of provider classes,
`DCPAVServiceProxy`, `DCPDPDeviceProxy`, or transport nodes. It requires
uniqueness only after candidates are scoped to the selected display. Two
equally plausible paths for that one display are an ambiguity and fail closed;
two paths belonging to two different selected displays are valid.

Future multi-target API or CLI behavior must require explicit intent (for
example, an explicit list of display indices or `--all`). It is not implied by
the presence of multiple displays. The current public API and CLI remain
single-target by default.

During development, Sumac exposed an `AppleDCPPS190` HDMI Odyssey alongside a
`DCPDP13Service` DisplayPort LG. The original PS190 gate incorrectly required
the literal role `DCPEXT0`, which was true only in the earlier one-display
topology; with both displays present, the selected Odyssey and its
`BranchDeviceID=pHDMIg` DCPDP device both used `DCPEXT1`. The gate now compares
the selected AV Service's role with the selected branch-matched device's role.
User-run same-state GET/SET validation then confirmed that each selected
binding dispatched only to its own provider path. This is evidence for the
documented two-display topology, not a certification for arbitrary monitor
counts or providers.

The PS190 backend contains two intentionally separate transaction shapes. GET
uses the hardware-validated raw framing and `UINT32_MAX` no-offset sentinel.
SET preserves the hardware-validated conventional `data=0x51` write,
repeated twice after 10 ms pre-write delays, with no acknowledgement read.
The common protocol layer only constructs DDC/CI bytes and checksums; it never
branches on the provider. This keeps unusual IOAV mechanics contained inside
the provider backend and makes each path independently testable.

DCPDP13 GET is conventional (`data=0x51`, four-byte request, 50 ms, then an
11-byte read). Its Set VCP backend reuses the common
six-byte conventional request builder and the same historical two-write,
10-ms-prewrite, no-acknowledgement sequence as PS190 SET. This similarity does
not make the providers interchangeable: the DP backend is selected only after
the DCPDP13 per-display safety gate. The transaction was hardware-validated
only as a same-state brightness SET on the documented LG DP path.

SET remains write-only and GET remains an independent operation. On the tested
LG, an immediate GET after SET returned an all-zero 11-byte frame; the strict
parser rejected it as malformed. A GET after approximately one second, and
five additional GETs spaced roughly one second apart, all returned valid
frames. rss-ddc deliberately adds no global post-SET delay, retry, or implicit
verification because this is a monitor-specific observation, not a universal
timing rule. The separate, explicit Set-and-Verify API below owns a
caller-controlled settling/retry policy.

The library currently supports numeric list indices. Stable system/EDID identifiers are a planned addition after their matching semantics are designed and tested.

Provider transport behavior and monitor observations are documented separately.
The [monitor compatibility and quirks catalog](monitors/README.md) records
tested models, VCPs, input labels, and timing observations without turning any
one monitor's behavior into a provider-wide rule or runtime profile.

## Opt-in Set-and-Verify

Plain operations intentionally retain their narrow transport meaning:

```text
Get VCP = one independent provider GET
Set VCP = one provider-specific, write-only SET
```

`rss_ddc_set_vcp_and_verify` and the CLI's `set ... --verify` add a distinct
orchestration layer, not backend timing. It performs one ordinary provider SET,
an optional caller-selected settling delay, an initial independent GET, then
`retry_count` additional GET attempts separated by an optional caller-selected
delay. It succeeds only if a strict GET reply reports exactly the requested
full-width `uint16_t` current value. `retry_count` does **not** include the
initial GET. The default policy is 100 ms settle, three additional attempts,
and 250 ms between attempts. Bounds of 60,000 ms per delay and ten additional
attempts keep the optional operation finite; callers may use zero delay or zero
retries.

The defaults are a modest recovery window informed by the documented LG
observation, where an immediate post-SET GET was malformed and a GET at about
one second recovered. They are not presented as a protocol requirement or a
claim about other monitors. No post-SET sleep, retry, or automatic GET was
added to either provider backend, `rss_ddc_set_vcp`, or `rss_ddc_get_vcp`.

After SET, a numeric display index alone is unsafe: the monitor can disconnect,
indices can reorder, or the same index can denote a sibling. The macOS adapter
therefore captures the initial ColorSync/CoreGraphics display UUID, CoreGraphics
display ID, provider, product name, `BranchDeviceID`, and provider-role/transport. Each
verification GET performs a fresh safety correlation at the original index and
requires all retained identity evidence to match. It never searches for a
replacement or falls back to another display. A missing/changed identity or a
failed fresh correlation yields `RSS_DDC_ERROR_VERIFY_UNAVAILABLE`; the GET is
not issued. This intentionally conservative behavior can produce “SET
completed but verification unavailable” after a successful write.

Malformed replies (including the observed all-zero LG frame), read failures,
temporary service construction failures, and valid-but-mismatched values are
retryable during the explicit policy window. Invalid policy/input, unsupported
provider/capability, SET failure, and identity unavailability are not retried.
Exhausted mismatches return `RSS_DDC_ERROR_VERIFY_MISMATCH`; exhausted
retryable GET failures return `RSS_DDC_ERROR_VERIFY_RETRY_EXHAUSTED`, while
per-attempt diagnostics retain the underlying parser/read error.

The feature is hardware-validated only on macOS `25F84` with the simultaneous
Odyssey G75F/`AppleDCPPS190` and LG HDR QHD/`DCPDP13Service` topology. Default
policy verification succeeded for PS190 brightness `50 → 49 → 50` and DP
brightness `100 → 99 → 100`; each command remained scoped to its selected
display/provider binding. The strongest DP retry evidence was a successful
SET to `100`, an all-zero first verification reply rejected by the strict
parser as source/framing failure, a retry after the configured 250 ms, and a
valid matching reply on attempt two. Conversely, one LG zero-settle/zero-retry
run verified immediately. Together these observations show an intermittent
post-SET transient on this monitor, not that every DP SET needs retry or that
250 ms is sufficient elsewhere.

Input VCP `0x60` receives no protocol special case. At the orchestration level,
an input change may intentionally remove the issuing host's active transport.
If the original display then cannot be safely re-correlated, rss-ddc reports
verification unavailable rather than claiming SET failed or verified. The
operation remains single-target: one selected display, one binding, one
backend; no `--all` or broadcast behavior is introduced.
