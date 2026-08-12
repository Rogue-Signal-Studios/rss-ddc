# Mac Studio M2 Ultra — live display topology

Observed on macOS **26.5.2**, build **25F84**, firmware **18000.121.3**.

Host: Mac Studio (`Mac14,14`), Apple M2 Ultra, 128 GB RAM.

## Provider matrix (tested topology)

| Index | Monitor | Link | EPIC role | Registry provider | rss-ddc provider | Capabilities |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | BenQ EW3270U | DisplayPort | `DCPEXT1` | `DCPDP13Service` | `DCPDP13Service` | Get/Set/DPCD (`0x0b`) |
| 2 | BenQ XL2730Z | DisplayPort | `DCPEXT2` | `DCPDPService` | `DCPDPService` | Get/Set/DPCD (`0x0b`) |
| 3 | ASUS PG349Q | HDMI / `pHDMIg` | `DCPEXT5` | `AppleDCPPS190` | `AppleDCPPS190` | Get/Set/EDID/DPCD (`0x0f`) |

Crossbar addresses from `ConnectionMapping`: EW3270U `0.0.0`, XL2730Z `0.2.0`, PG349Q `1.4.0`.

## MCDP

No `AppleDCPMCDP29XX` provider was present in this tested live topology. This does not imply the Mac Studio can never expose MCDP on another cable, port, or monitor.

## DCPDPService (display 2)

`DCPDPService` is a registry provider class distinct from `DCPDP13Service`.
rss-ddc recognizes it as a first-class provider with runtime GET, SET, and
read-only DPCD.

See [BenQ XL2730Z](benq-xl2730z.md) for per-monitor evidence.

### Evidence status

| Capability | Status |
| --- | --- |
| GET VCP | **Hardware validated; normal runtime** |
| SET VCP | **Hardware validated; normal runtime** (62→61→62) |
| Set-and-Verify | **Hardware validated** (default policy, first attempt both directions) |
| DPCD read `0x00000`/16 | **Hardware validated; normal runtime** |
| EDID | Unsupported / unvalidated (`ReadEDID status=unsupported`) |

### Validated normal-runtime commands

```sh
./rss-ddc list
./rss-ddc --verbose get 2 0x10
./rss-ddc --verbose set 2 0x10 61
./rss-ddc --verbose get 2 0x10
./rss-ddc --verbose set 2 0x10 62
./rss-ddc --verbose set 2 0x10 61 --verify
./rss-ddc --verbose set 2 0x10 62 --verify
./rss-ddc --verbose dpcd 2 0x00000 16
```

EDID remains unsupported:

```sh
./rss-ddc --verbose edid 2
```

### Comparison to DCPDP13Service on this host (display 1)

| Property | DCPDP13Service / DCPEXT1 | DCPDPService / DCPEXT2 |
| --- | --- | --- |
| Service provider class | `DCPDP13Service` | `DCPDPService` |
| rss-ddc runtime GET/SET/DPCD | enabled | enabled |
| rss-ddc runtime EDID | unsupported | unsupported |

## Validation harness cleanup

The public `validate-dcpdpservice-set` command was removed after SET promotion
because normal `set` and `set --verify` exercise the same hardware-proven
conventional backend. Portable reversible-validation helpers remain in
`set_validation.c` for unit tests; evidence is preserved in monitor docs.

## Roadmap on this host

- DCPDPService GET/SET/DPCD complete for current validation scope
- DCPDPService EDID open
- MCDP requires different hardware/topology
