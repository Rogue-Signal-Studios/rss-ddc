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

Capabilities are independent flags: Get VCP, Set VCP, EDID read, and DPCD read. A provider receives only the capabilities that have been separately enabled. PS190 Get VCP and Set VCP, plus DCPDP13 Get VCP, are hardware-validated in this project. DCPDP13 Set VCP is enabled from prior USB-C/DP research but remains pending standalone validation; MCDP, EDID, and DPCD remain fail-closed.

Provider dispatch is a pure C mapping from the immediate EPIC provider class,
which keeps classification testable without opening a display user client.
The macOS binding then applies the provider's safety gate before it creates
the private `IOAVService` object. A generic DisplayPort transport-state class
is correlation evidence only—not a provider selector—because PS190 HDMI can
present that same state.

For PS190, the binding resolver requires an external display, a correlated active DisplayPort transport, a `BranchDeviceID` with exactly one external `DCPDPDeviceProxy`, and an external Unit-0 `DCPAVServiceProxy`. Its immediate parent must identify `dcpav-service-epic`, `DCPEXT0`, and `AppleDCPPS190`; `IOAVServiceUserInterfaceSupported` must be true. Only then does the backend construct the private Service interface. Registry IDs are transient evidence, not persistent identifiers.

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
It does not make the mixed topology a hardware-validation claim: GET/SET must
still be rerun by the user before multi-monitor runtime behavior is validated.

The PS190 backend contains two intentionally separate transaction shapes. GET
uses the hardware-validated raw framing and `UINT32_MAX` no-offset sentinel.
SET preserves the hardware-validated conventional `data=0x51` write,
repeated twice after 10 ms pre-write delays, with no acknowledgement read.
The common protocol layer only constructs DDC/CI bytes and checksums; it never
branches on the provider. This keeps unusual IOAV mechanics contained inside
the provider backend and makes each path independently testable.

DCPDP13 GET is conventional (`data=0x51`, four-byte request, 50 ms, then an
11-byte read). Its implemented Set VCP backend instead reuses the common
six-byte conventional request builder and the same historical two-write,
10-ms-prewrite, no-acknowledgement sequence as PS190 SET. This similarity does
not make the providers interchangeable: the DP backend is selected only after
the DCPDP13 per-display safety gate, and its rss-ddc Set behavior remains
pending controlled hardware validation.

The library currently supports numeric list indices. Stable system/EDID identifiers are a planned addition after their matching semantics are designed and tested.
