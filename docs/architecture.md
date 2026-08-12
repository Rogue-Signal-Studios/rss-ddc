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

Capabilities are independent flags: Get VCP, Set VCP, EDID read, and DPCD read. A provider receives only the capabilities that have been separately enabled. PS190 Get VCP and Set VCP are hardware-validated in this project; DCPDP13 Get VCP is enabled from prior research-fork evidence but remains pending standalone hardware confirmation. MCDP, DCPDP13 Set VCP, EDID, and DPCD remain fail-closed.

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

The PS190 backend contains two intentionally separate transaction shapes. GET
uses the hardware-validated raw framing and `UINT32_MAX` no-offset sentinel.
SET preserves the hardware-validated conventional `data=0x51` write,
repeated twice after 10 ms pre-write delays, with no acknowledgement read.
The common protocol layer only constructs DDC/CI bytes and checksums; it never
branches on the provider. This keeps unusual IOAV mechanics contained inside
the provider backend and makes each path independently testable.

The library currently supports numeric list indices. Stable system/EDID identifiers are a planned addition after their matching semantics are designed and tested.
