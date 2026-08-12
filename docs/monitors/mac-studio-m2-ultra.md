# Mac Studio M2 Ultra — live display topology

Observed on macOS **26.5.2**, build **25F84**, firmware **18000.121.3**.

Host: Mac Studio (`Mac14,14`), Apple M2 Ultra, 128 GB RAM.

## Provider matrix (tested topology)

| Index | Monitor | Link | EPIC role | Registry provider | rss-ddc provider | Capabilities |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | BenQ EW3270U | DisplayPort | `DCPEXT1` | `DCPDP13Service` | `DCPDP13Service` | Get/Set/DPCD (`0x0b`) |
| 2 | BenQ XL2730Z | DisplayPort | `DCPEXT2` | `DCPDPService` | `DCPDPService` | Get/DPCD (`0x09`) |
| 3 | ASUS PG349Q | HDMI / `pHDMIg` | `DCPEXT5` | `AppleDCPPS190` | `AppleDCPPS190` | Get/Set/EDID/DPCD (`0x0f`) |

Crossbar addresses from `ConnectionMapping`: EW3270U `0.0.0`, XL2730Z `0.2.0`, PG349Q `1.4.0`.

## MCDP

No `AppleDCPMCDP29XX` provider was present in this tested live topology. This does not imply the Mac Studio can never expose MCDP on another cable, port, or monitor.

## DCPDPService (display 2)

`DCPDPService` is a registry provider class distinct from `DCPDP13Service`.
rss-ddc now recognizes it as a first-class provider with runtime GET and
read-only DPCD only.

See [BenQ XL2730Z](benq-xl2730z.md) for per-monitor evidence.

### Structural correlation

Selected display **BenQ XL2730Z** correlates to:

- one external Unit-0 `DCPAVServiceProxy` under `dcpav-service-epic`
- immediate EPIC parent `EPICProviderClass = DCPDPService`, `role = DCPEXT2`
- `IOAVServiceUserInterfaceSupported = Yes`
- active DisplayPort transport with `BranchDeviceID = Dp1.2`
- parallel EPIC siblings on `DCPEXT2`: `dcpdp-device-epic` (`DCPDPDevice`), `dcpdp-service-epic` (`DCPDPService`), video/audio interfaces
- one external `DCPDPDeviceProxy` with `BranchDeviceID = Dp1.2`
- one external `DCPDPServiceProxy` on the sibling service EPIC path (topology evidence; not used for GET/DPCD)

DDC GET uses the selected **`dcpav-service-epic` / `DCPAVServiceProxy`** object via `IOAVServiceCreateWithService`. DPCD uses the separate same-role **`DCPDPDeviceProxy`** path. Do not substitute one for the other.

### Evidence status

| Capability | Status |
| --- | --- |
| GET VCP | **Hardware validated; runtime supported** |
| DPCD read `0x00000`/16 | **Hardware validated; runtime supported** |
| SET VCP | Validation hypothesis only — same-state writes succeeded; reversible transition pending via `validate-dcpdpservice-set` |
| EDID | Unsupported / unvalidated |

### Post-promotion commands

```sh
./rss-ddc list
./rss-ddc --verbose get 2 0x10
./rss-ddc --verbose dpcd 2 0x00000 16
```

These remain unsupported:

```sh
./rss-ddc --verbose edid 2
./rss-ddc --verbose set 2 0x10 62
```

### Comparison to DCPDP13Service on this host (display 1)

| Property | DCPDP13Service / DCPEXT1 | DCPDPService / DCPEXT2 |
| --- | --- | --- |
| Service EPIC name | `dcpav-service-epic` | `dcpav-service-epic` |
| Service provider class | `DCPDP13Service` | `DCPDPService` |
| GET object | `DCPAVServiceProxy` → IOAVService | same structural object; **hardware validated** |
| DPCD object | same-role `DCPDPDeviceProxy` → IODPDevice | same pattern; **hardware validated** |
| rss-ddc runtime GET/SET/DPCD/EDID | GET/SET/DPCD | GET/DPCD only |
