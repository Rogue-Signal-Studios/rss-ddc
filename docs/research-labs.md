# Research DDC labs

These labs were migrated from the predecessor `m1ddc-rss` research fork. They are
intentionally isolated from production `librss-ddc` and the `rss-ddc` CLI.

Provenance:

- `9992bf9255189d8d57a09273d5b3646778de20f5`
  `research: generalize guarded PS190 raw GetVCP probe`
- `c0e695c4c82f482ec647374ee0ed5f68d71485a1`
  `fix: bind PS190 lab gate to selected display topology`

They use Apple-private IOAV/IOKit interfaces. Behavior can vary by macOS
release, provider, cable/adapter topology, and monitor firmware. They are
research instruments, not a supported public API.

## Promotion workflow

```text
research
    ↓
hardware validation
    ↓
production implementation
```

A lab result is evidence for a later production change. It is not itself a
production capability.

## Layout

```text
research/
  common/                 guarded buffers, sentinels, PS190 selected-display gate,
                          diagnostic Get VCP parser
  ioav-ddc-lab/           Service-path IOAV DDC/CI experiments
  ioav-device-lab/        Device-path IOAV experiments
  iodp-ddc-lab/           read-only IODP DPCD experiments
  tests/                  synthetic research tests (no hardware)
```

## Build

Normal `make`, `make test`, and `make consumer-test` do not build or link these
labs. `librss-ddc.a` does not contain research objects.

```sh
make research
make research-test
```

Binaries are written to `build/research/`.

## Labs

### `ioav-ddc-lab`

Service-path IOAV experiments. Starts from `rss_ddc_list_displays` /
`rss_ddc_get_display`, then independently inspects live registry facts before
any I2C.

| Mode | Hardware effect |
|---|---|
| `topology`, `topology-detail`, `--list-displays` | registry/CoreGraphics only |
| `read-only` | IOAV reads only |
| `edid` | IOAV EDID read |
| `one`, `stream`, `repeat`, `mcdp`, `sentinel-vcp` | Get VCP write then read |
| `service-ddc-vcp-raw` | conventional Get VCP write + `UINT32_MAX` read |
| `service-ddc-vcp-raw-framed` | raw-framed Get VCP; `--vcp` / `--reads` |
| `service-ddc-input-raw-framed` | fixed VCP `0x60` alias of the raw-framed probe |

Example:

```sh
./build/research/ioav-ddc-lab --mode service-ddc-vcp-raw-framed --display 1 --vcp 0x10 --reads 1
```

Guarded PS190 raw-framed mode (`service-ddc-vcp-raw-framed`):

- `--vcp HEX`, `--reads N` with `N` in `1..100`
- fresh guarded request/reply buffers each iteration
- chip `0x37`, write data `UINT32_MAX`, read data `UINT32_MAX`
- request bytes `51 82 01 <VCP> <checksum>`
- reply length 11, default post-write delay 50 ms
- request/reply canary checks, `0xcc` sentinel residue, diagnostic parser dump
- the fixed `0x60` alias (`service-ddc-input-raw-framed`) is unchanged

Fail-closed selected-display gate (not “any AppleDCPPS190”):

- product Odyssey G75F
- provider AppleDCPPS190
- selected-display `DCPAVServiceProxy`, Location External, unit 0
- EPIC name `dcpav-service-epic`
- UI supported
- BranchDeviceID `pHDMIg`
- path contains `Port-HDMI@1`
- role resolved dynamically from the selected proxy path (observed `DCPEXT1`)
- exactly one selected-display matching transport and service
- an unrelated active LG transport does not invalidate the selected display
- no global unique-active-DP requirement and no hardcoded `DCPEXT0`
- ambiguous selected-display mapping fails closed

### `ioav-device-lab`

Device-path IOAV. Registry/safety diagnostics are read-only. `edid` performs one
`0x50/0x00` read. `ddc-vcp-raw` performs one Get VCP write/read pair.

```sh
./build/research/ioav-device-lab --mode safety-diagnostic --display 1
```

### `iodp-ddc-lab`

Read-only IODP DPCD. Topology/matrix modes make no DPCD call. `dpcd` performs
one `IODPDeviceReadDPCD`.

```sh
./build/research/iodp-ddc-lab --mode topology --display 1
```

## Shared vs research-private

Shared with production:

- DDC request construction (`rss_ddc_build_raw_get_vcp`,
  `rss_ddc_build_conventional_get_vcp`)
- canonical display identity (`rss_ddc_list_displays`, `rss_ddc_get_display`)
- reconstructed IOAV/IODP/CoreDisplay declarations under
  `src/platform/macos/private/`

Research-private:

- guarded pages, `0xa5`/`0x5a` canaries, `0xcc` fill, sentinel residue
- selected-display PS190 safety gate (independent live registry predicates)
- diagnostic Get VCP parser that populates fields on failure for lab dumps
- raw IOAV/IODP experimental transports, including alternate/invalid tuples
- IODP page-middle guard allocation (different from the 32-byte IOAV canaries)
- `IOAVDeviceWriteI2C` and IODPService constructors not in the production
  private headers

The diagnostic parser is retained because production
`rss_ddc_parse_get_vcp_reply` leaves the result untouched on failure and does
not expose `vcpType` or checksum diagnostics. That duplication is deferred for
later cleanup after hardware parity.
