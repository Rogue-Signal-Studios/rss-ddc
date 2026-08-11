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

Capabilities are independent flags: Get VCP, Set VCP, EDID read, and DPCD read. A provider receives only the capabilities that have been separately validated. In this milestone, PS190 has Get VCP only.

For PS190, the binding resolver requires an external display, a correlated active DisplayPort transport, a `BranchDeviceID` with exactly one external `DCPDPDeviceProxy`, and an external Unit-0 `DCPAVServiceProxy`. Its immediate parent must identify `dcpav-service-epic`, `DCPEXT0`, and `AppleDCPPS190`; `IOAVServiceUserInterfaceSupported` must be true. Only then does the backend construct the private Service interface. Registry IDs are transient evidence, not persistent identifiers.

The library currently supports numeric list indices. Stable system/EDID identifiers are a planned addition after their matching semantics are designed and tested.
