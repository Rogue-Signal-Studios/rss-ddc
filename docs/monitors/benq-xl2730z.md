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
| Set VCP | **Hardware validated; runtime supported** | Reversible 62→61→62 transition; conventional two-write SET. |
| Read DPCD `0x00000` / 16 | **Hardware validated; runtime supported** | Same-role scoped `DCPDPDeviceProxy → IODPDevice → IODPDeviceReadDPCD`. |
| EDID | Unsupported / unvalidated | Normal `edid` fails closed. |

rss-ddc capabilities on this topology: `0x0b` (GET + SET + DPCD).

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

## Hardware-validated SET

Command (example reversible transition):

```sh
./rss-ddc --verbose set 2 0x10 61
./rss-ddc --verbose get 2 0x10
./rss-ddc --verbose set 2 0x10 62
```

Validated during research via reversible harness (`62 → 61 → 62`):

| Step | Payload | Result |
| --- | --- | --- |
| Target SET (61) | `84 03 10 00 3d 95` | write #1/#2 `IOReturn=0`, verify current=61 after 250 ms harness settle |
| Restore SET (62) | `84 03 10 00 3e 96` | write #1/#2 `IOReturn=0`, verify current=62 after 250 ms harness settle |

Transport (both steps):

- chip `0x37`, data/subaddress `0x51`
- 10 ms pre-delay, write #1, 10 ms pre-delay, write #2
- no SET response read

Normal `./rss-ddc set 2 ...` uses the same conventional backend with no
unconditional post-SET delay. Use `./rss-ddc set 2 ... --verify` when transient
post-SET GET framing is a concern.

## Observed post-SET verification quirk

On this monitor/provider:

- Same-state SET once produced successful writes but an **immediate** post-SET GET returned malformed framing (`invalid source/framing`)
- A later normal GET recovered (current unchanged at 62)
- Reversible validation with a **250 ms** harness-only settle before verification succeeded

This establishes an **observed immediate post-SET transient verification window** on this topology. It does not prove stale-buffer data, does not require 250 ms for plain SET, and does not change global Set-and-Verify defaults (100 ms settle, up to three 250 ms retries). The generic orchestration already treats parser/read failures as retryable.

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

## Distinction from DCPDP13Service

`DCPDPService` is a distinct registry provider class on this host (display 1
uses `DCPDP13Service`). GET/SET/DPCD transport framing currently overlaps, but
provider identity, correlation, and EDID capability differ.

## Open items

- EDID remains unsupported until separately validated
- Do not generalize SET/GET/DPCD behavior to every `DCPDPService` monitor
