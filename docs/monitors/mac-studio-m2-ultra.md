# Mac Studio M2 Ultra — live display topology

Observed on macOS **26.5.2**, build **25F84**, firmware **18000.121.3**.

Host: Mac Studio (`Mac14,14`), Apple M2 Ultra, 128 GB RAM.

## Provider matrix (tested topology)

| Index | Monitor | Link | EPIC role | Registry provider | rss-ddc provider | Capabilities |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | BenQ EW3270U | DisplayPort | `DCPEXT1` | `DCPDP13Service` | `DCPDP13Service` | Get/Set/DPCD (`0x0b`) |
| 2 | BenQ XL2730Z | DisplayPort | `DCPEXT2` | `DCPDPService` | `unknown` | none (`0x00`) |
| 3 | ASUS PG349Q | HDMI / `pHDMIg` | `DCPEXT5` | `AppleDCPPS190` | `AppleDCPPS190` | Get/Set/EDID/DPCD (`0x0f`) |

Crossbar addresses from `ConnectionMapping`: EW3270U `0.0.0`, XL2730Z `0.2.0`, PG349Q `1.4.0`.

## MCDP

No `AppleDCPMCDP29XX` provider was present in this tested live topology. This does not imply the Mac Studio can never expose MCDP on another cable, port, or monitor.

## DCPDPService (display 2)

`DCPDPService` is a newly observed registry provider class. It is **not** the same string as `DCPDP13Service` and must not be treated as validated standard-DP transport without hardware evidence.

### Structural correlation (read-only IORegistry)

Selected display **BenQ XL2730Z** correlates to:

- one external Unit-0 `DCPAVServiceProxy` under `dcpav-service-epic`
- immediate EPIC parent `EPICProviderClass = DCPDPService`, `role = DCPEXT2`
- `IOAVServiceUserInterfaceSupported = Yes`
- active DisplayPort transport with `BranchDeviceID = Dp1.2`
- parallel EPIC siblings on `DCPEXT2`: `dcpdp-device-epic` (`DCPDPDevice`), `dcpdp-service-epic` (`DCPDPService`), video/audio interfaces
- one external `DCPDPDeviceProxy` with `BranchDeviceID = Dp1.2` (same branch as the active transport)
- one external `DCPDPServiceProxy` on the sibling service EPIC path

The invariant remains **selected logical display → exactly one scoped physical/provider path**. Global first-match logic is invalid on this three-display host.

### Evidence status

| Capability | Status |
| --- | --- |
| GET VCP | Unknown — no project hardware validation through `DCPDPService` |
| SET VCP | Unknown |
| EDID | Unknown |
| DPCD | **Unvalidated** — structural same-role `DCPDPDeviceProxy → IODPDevice` path is plausible; use `validate-dcpdpservice-dpcd` |

Prior research (`docs/apple-silicon-ddc.md`) observed a sibling `DCPDPServiceProxy` as topology evidence only. That mention is **research-backed**, not hardware-validated transport.

### Comparison to DCPDP13Service on this host (display 1)

| Property | DCPDP13Service / DCPEXT1 | DCPDPService / DCPEXT2 |
| --- | --- | --- |
| Service EPIC name | `dcpav-service-epic` | `dcpav-service-epic` |
| Service provider class | `DCPDP13Service` | `DCPDPService` |
| Sibling service EPIC | `dcpdp-service-epic` / `DCPDP13Service` | `dcpdp-service-epic` / `DCPDPService` |
| Device EPIC | `dcpdp-device-epic` / `DCPDPDevice` | same pattern |
| BranchDeviceID on transport | absent | `Dp1.2` |
| DCPDPDeviceProxy branch field | empty | `Dp1.2` |
| rss-ddc runtime | supported | fail-closed (`unknown`) |

Parallel EPIC layout suggests shared DisplayPort plumbing with a **different Service provider class string**. Name similarity does not justify enabling DDC/DPCD capabilities.

## Manual validation (DCPDPService DPCD)

Run only on display 2 after confirming topology:

```sh
./rss-ddc list
./rss-ddc --verbose validate-dcpdpservice-dpcd 2
```

Success criteria:

- display 2 selected; registry class `DCPDPService`; service role `DCPEXT2`
- exactly one scoped same-role external `DCPDPDeviceProxy`
- `IODPDeviceCreateWithService` succeeds once
- one read at `0x00000`, exactly 16 bytes, `IOReturn = 0x00000000`
- plausible receiver-capability bytes (non-error decode)

If validation fails, **stop**. Do not retry other addresses, scan ranges, or fall back to DCPDP13/PS190 paths.
