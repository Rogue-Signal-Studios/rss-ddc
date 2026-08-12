# BenQ XL2730Z (Mac Studio / DCPDPService)

| Field | Value |
| --- | --- |
| Manufacturer | Unavailable in rss-ddc discovery output |
| Product name | BenQ XL2730Z |
| Connection | DisplayPort |
| Provider | `DCPDPService` |
| EPIC role | `DCPEXT2` |
| macOS build | `25F84` |
| Host | Mac Studio M2 Ultra |

Validated only on the simultaneous three-display topology documented in
[Mac Studio M2 Ultra](mac-studio-m2-ultra.md). Do not generalize to other hosts,
ports, cables, or firmware.

## Capability evidence

| Capability | Status | Notes |
| --- | --- | --- |
| Get VCP | **Hardware validated; runtime supported** | Conventional Service-path IOAV GET. |
| Read DPCD `0x00000` / 16 | **Hardware validated; runtime supported** | Same-role scoped `DCPDPDeviceProxy → IODPDevice → IODPDeviceReadDPCD`. |
| Set VCP | Validation hypothesis only | Use `validate-dcpdpservice-set`; standard-DP two-write sequence **inferred** from DCPDP13/PS190. Normal `set` remains unsupported. |
| EDID | Unsupported / unvalidated | Normal `edid` fails closed. |

rss-ddc capabilities on this topology: `0x09` (GET + DPCD).

## Hardware-validated GET

Command:

```sh
./rss-ddc --verbose get 2 0x10
```

Path:

```text
selected BenQ XL2730Z
→ registry provider DCPDPService / role DCPEXT2
→ selected DCPAVServiceProxy
→ IOAVServiceCreateWithService
→ conventional MCCS Get VCP
```

Validated request (VCP `0x10`):

| Field | Value |
| --- | --- |
| chip | `0x37` |
| data/subaddress | `0x51` |
| payload | `82 01 10 fd` |
| delay | 50 ms |
| read length | 11 |

Validated reply:

```text
6e 88 02 00 10 00 00 64 00 3e fe
```

Decode: VCP `0x10`, maximum 100, current 62, checksum valid.

## Hardware-validated DPCD

Command:

```sh
./rss-ddc --verbose dpcd 2 0x00000 16
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

## SET validation (pending hardware proof)

Hypothesis (**inferred** from validated DCPDP13/PS190 standard-DP SET):

- payload `84 03 10 <hi> <lo> <checksum>`
- chip `0x37`, data/subaddress `0x51`
- two identical writes, 10 ms before each, no response read

The harness performs one GET of VCP `0x10`, writes the captured current value
back to itself, then optionally verifies with a post-GET:

```sh
./rss-ddc --verbose validate-dcpdpservice-set 2
```

Do not use `./rss-ddc set 2 ...` until SET is separately hardware validated and
promoted.

## Distinction from DCPDP13Service

`DCPDPService` is a distinct registry provider class on this host (display 1
uses `DCPDP13Service`). Transport framing for GET/DPCD currently overlaps, but
capability states differ: DCPDPService has no runtime SET or EDID until
independently validated.

## Next steps after successful SET validation

1. Mark DCPDPService SET hardware validated on this topology.
2. Enable normal SET capability in a separate checkpoint.
3. Validate a reversible brightness transition if needed.
4. Keep EDID separate.
