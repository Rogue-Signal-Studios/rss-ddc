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
| Set VCP | Validation hypothesis only | Same-state writes succeeded; reversible transition pending via `validate-dcpdpservice-set`. Normal `set` remains unsupported. |
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

### Same-state experiment (observed, not validated)

Earlier harness revision (write current back to itself):

- Pre-GET: VCP `0x10`, maximum 100, current **62**
- SET payload: `84 03 10 00 3e 96` (chip `0x37`, data `0x51`, two writes, 10 ms pre-delay each)
- Write #1 and #2: `IOReturn=0x00000000`
- Immediate post-SET GET (no settle): malformed reply `01 10 fd 3e 96 00 64 00 3e fe 04` → strict parser rejected (`invalid source/framing`)
- Later normal `./rss-ddc --verbose get 2 0x10` succeeded with current **62**

Classification:

- IOAV construction and conventional SET **writes** succeeded
- Immediate post-SET GET hit an **immediate post-SET malformed/transient reply window** (same class as documented LG/DCPDP13 post-SET transients; not proven stale-buffer data)
- Same-state SET did **not** prove semantic state mutation

SET is **not** hardware validated.

### Reversible validation harness (current)

Proves an adjacent reversible transition:

```text
current → adjacent target → verify target → restore original → verify original
```

Validation-only policy:

- VCP `0x10` only
- Target: `original - 1` when `original > 0`, else `original + 1` when below maximum
- One **250 ms** verification settle before each verification GET (harness policy from LG/DCPDP13 evidence; not a universal requirement)
- No retries
- Restoration mandatory after successful state-changing SET, even if target verification fails

```sh
./rss-ddc --verbose get 2 0x10
./rss-ddc --verbose validate-dcpdpservice-set 2
```

For brightness 62, expected flow: **62 → 61 → verify 61 → restore 62 → verify 62**.

Do not use `./rss-ddc set 2 ...` until SET is separately hardware validated and promoted.

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
