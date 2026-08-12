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

`DCPDPService` is a registry provider class distinct from `DCPDP13Service`.
rss-ddc runtime capabilities remain **disabled** (`unknown`, `0x00`) until
evidence and promotion are kept separate from validation harnesses.

See [BenQ XL2730Z](benq-xl2730z.md) for per-monitor evidence.

### Structural correlation (read-only IORegistry)

Selected display **BenQ XL2730Z** correlates to:

- one external Unit-0 `DCPAVServiceProxy` under `dcpav-service-epic`
- immediate EPIC parent `EPICProviderClass = DCPDPService`, `role = DCPEXT2`
- `IOAVServiceUserInterfaceSupported = Yes`
- active DisplayPort transport with `BranchDeviceID = Dp1.2`
- parallel EPIC siblings on `DCPEXT2`: `dcpdp-device-epic` (`DCPDPDevice`), `dcpdp-service-epic` (`DCPDPService`), video/audio interfaces
- one external `DCPDPDeviceProxy` with `BranchDeviceID = Dp1.2`
- one external `DCPDPServiceProxy` on the sibling service EPIC path (topology evidence; not used by GET/DPCD harnesses)

DDC GET uses the selected **`dcpav-service-epic` / `DCPAVServiceProxy`** object via `IOAVServiceCreateWithService`. DPCD uses the separate same-role **`DCPDPDeviceProxy`** path. Do not substitute one for the other.

### Evidence status

| Capability | Status |
| --- | --- |
| DPCD read `0x00000`/16 | **Hardware validated** on this topology |
| GET VCP `0x10` | **Validation hypothesis pending** — conventional framing inferred from DCPDP13; use `validate-dcpdpservice-get` |
| SET VCP | Unknown |
| EDID | Unknown |

### Hardware-validated DPCD

```sh
./rss-ddc --verbose validate-dcpdpservice-dpcd 2
```

Returned bytes:

```text
12 14 c4 01 01 00 01 c0 02 00 06 00 00 00 01 00
```

`IOReturn = 0x00000000`. Decode: revision `0x12`, HBR2 (`0x14`), 4 lanes, enhanced framing yes, downstream port no.

Normal `./rss-ddc dpcd 2 ...` remains unsupported for `DCPDPService`.

### GET validation (pending hardware proof)

Hypothesis (**inferred** from DCPDP13 standard-DP behavior):

- `IOAVServiceCreateWithService(selected DCPAVServiceProxy)`
- write `0x37` / data `0x51` / payload `82 01 10 fd`
- delay 50 ms
- read 11 bytes from `0x37` / `0x51`
- strict MCCS parser

```sh
./rss-ddc --verbose validate-dcpdpservice-get 2
```

Normal `./rss-ddc get 2 0x10` remains unsupported.

### Comparison to DCPDP13Service on this host (display 1)

| Property | DCPDP13Service / DCPEXT1 | DCPDPService / DCPEXT2 |
| --- | --- | --- |
| Service EPIC name | `dcpav-service-epic` | `dcpav-service-epic` |
| Service provider class | `DCPDP13Service` | `DCPDPService` |
| GET object | `DCPAVServiceProxy` → IOAVService | same structural object; **unvalidated for DDC** |
| DPCD object | same-role `DCPDPDeviceProxy` → IODPDevice | same pattern; **hardware validated** |
| rss-ddc runtime | enabled | fail-closed |

## Promotion policy (not implemented)

After GET hardware validation, a separate decision is required before enabling runtime GET/DPCD for `DCPDPService`. SET and EDID remain independently unknown.
