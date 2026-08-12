# BenQ XL2730Z (Mac Studio / DCPDPService)

| Field | Value |
| --- | --- |
| Manufacturer | Unavailable in rss-ddc discovery output |
| Product name | BenQ XL2730Z |
| Connection | DisplayPort |
| Provider | `DCPDPService` (registry); rss-ddc reports `unknown` |
| EPIC role | `DCPEXT2` |
| macOS build | `25F84` |
| Host | Mac Studio M2 Ultra |

Validated only on the simultaneous three-display topology documented in
[Mac Studio M2 Ultra](mac-studio-m2-ultra.md). Do not generalize to other hosts,
ports, cables, or firmware.

## Capability evidence

| Capability | Status | Notes |
| --- | --- | --- |
| Read DPCD `0x00000` / 16 | **Hardware validated** | Same-role scoped `DCPDPDeviceProxy → IODPDevice → IODPDeviceReadDPCD`. |
| Get VCP `0x10` | Validation hypothesis pending | Use `validate-dcpdpservice-get`; conventional framing inferred from DCPDP13. |
| Set VCP | Unknown | Not validated. |
| EDID | Unknown | Not validated. |

## Hardware-validated DPCD

Command:

```sh
./rss-ddc --verbose validate-dcpdpservice-dpcd 2
```

Path:

```text
selected BenQ XL2730Z
→ registry provider DCPDPService / role DCPEXT2
→ exactly one same-role scoped DCPDPDeviceProxy
→ IODPDeviceCreateWithService
→ IODPDeviceReadDPCD(device, 0x00000, buffer, 16)
```

Result (macOS 26.5.2 / 25F84):

```text
12 14 c4 01 01 00 01 c0 02 00 06 00 00 00 01 00
IOReturn = 0x00000000
```

Decode:

| Field | Value |
| --- | --- |
| DPCD revision | `0x12` |
| Max link rate (raw) | `0x14` (HBR2 / 5.40 Gbps per lane) |
| Max lane count | 4 |
| Enhanced framing | yes |
| Downstream port present | no |

Runtime `dpcd` remains disabled for `DCPDPService` until separately promoted.

## GET validation (pending)

Hypothesis: conventional Service-path IOAV GET identical to validated DCPDP13
framing (`0x37`/`0x51`, payload `82 01 10 fd`, 50 ms delay, 11-byte strict parse).

```sh
./rss-ddc list
./rss-ddc --verbose validate-dcpdpservice-get 2
```

Do not use `./rss-ddc get 2 0x10` until GET is hardware validated and the
provider is promoted.

## Next steps after successful GET validation

1. Mark DCPDPService GET hardware validated on this topology.
2. Decide runtime promotion (GET + validated DPCD) separately from SET.
3. Validate SET independently; do not infer SET from GET.
4. EDID remains unknown until separately tested.
