# rss-ddc

`rss-ddc` is an early-stage, provider-driven macOS DDC/CI library and CLI from Rogue Signal Studios.

## Status

This milestone provides real macOS display/provider discovery, strict DDC/CI parsing, and provider-specific Service-path operations. PS190 GET/SET and DCPDP13 GET/SET are hardware-validated in `rss-ddc`; read-only native DPCD is hardware-validated on their documented paths. DCPDPService GET, SET, read-only DPCD, and Set-and-Verify are hardware-validated on the documented Mac Studio XL2730Z three-display topology (capabilities `0x0b`). Unsupported providers and capabilities return explicit errors rather than falling back to a guessed transport.

The project uses Apple-private macOS interfaces inside the macOS backend only. Behavior can vary by macOS release, display provider, cable/adapter topology, and monitor firmware.

Hardware validation is topology-specific. The authoritative matrix covers the
Mac mini M4 Pro PS190/DCPDP13 mixed topology and the Mac Studio M2 Ultra
DCPDPService topology; it is maintained in [Hardware validation](docs/hardware-validation.md).
No portability beyond those documented setups is implied.

## CLI

```sh
make
./rss-ddc list
./rss-ddc info 1
./rss-ddc --verbose info 1
./rss-ddc get 1 0x10
./rss-ddc edid 1 --decode
./rss-ddc edid 1 --hex
./rss-ddc edid 1 --raw odyssey.edid
./rss-ddc --verbose dpcd 1 0x00000 16
./rss-ddc --verbose dpcd 1 0x00200 8
./rss-ddc --verbose probe-dpcd-path 2
./rss-ddc --verbose get 2 0x10
./rss-ddc --verbose set 2 0x10 61
./rss-ddc --verbose set 2 0x10 62 --verify
./rss-ddc --verbose dpcd 2 0x00000 16
./rss-ddc --verbose get 1 0x10
./rss-ddc set 1 0x60 18
./rss-ddc set 1 0x10 50 --verify
./rss-ddc --verbose set 2 0x10 100 --verify --settle-ms 100 --retries 3 --retry-delay-ms 250
```

PS190 `set` is hardware-validated only in the documented 25F84/Odyssey G75F scope. Do not assume that GET, SET, EDID, or the documented PS190 DPCD path applies to another provider, blocks 2+, or broader DPCD access.

`edid` is a read-only, independently dispatched capability. Current rss-ddc
hardware-validates PS190 Device-path EDID blocks 0 and 1 on the documented
macOS 25F84/Odyssey G75F topology. Block 0 uses offset `0x00`; the standard
E-EDID block-1 mapping uses segment `0`, offset `0x80`, and its private IOAV
Device-path mapping is now hardware validated. No segment-pointer write is
needed for block 1. Blocks 2+ require one and remain unsupported. `--decode`
is the default, `--hex` labels every acquired 128-byte block, and `--raw
<file>` creates a new exact-byte file without overwriting an existing path.
`extensions-complete` is the authoritative complete/partial state. DCPDP13,
DCPDPService, and MCDP EDID remain explicitly unsupported.

`dpcd` is a separate, read-only capability enabled for the documented PS190,
DCPDP13, and DCPDPService topologies. It performs exactly one bounded native DPCD read
through a selected, provider-specific `DCPDPDeviceProxy`,
`IODPDeviceCreateWithService`, and `IODPDeviceReadDPCD`. The maximum is 16
bytes—the largest transfer hardware validated in this project—there is no
chunking, retry, scan, or write, and the allowed address space is the 20-bit
DPCD range. PS190 uses its existing branch-correlated proxy. DCPDP13 uses the
selected Service EPIC role to require exactly one same-role
`dcpdp-device-epic` proxy; zero or multiple candidates fail closed. This
runtime path is hardware validated only on the documented PS190/Odyssey,
DCPDP13/LG, and DCPDPService/XL2730Z topologies. `probe-dpcd-path` remains a registry-only correlation
diagnostic; the one-shot validation harness was removed because normal `dpcd`
now exercises the same constrained path.

Successful non-verbose `get` prints only the current value. `--verbose` writes the selected display/provider correlation, raw request/reply bytes, IOReturns, decoded values, and checksum status to standard error for controlled validation.
For `info`, verbose mode emits a precise registry-correlation rejection reason
without constructing an IOAV Service object; it is the first diagnostic to run
when a display fails closed.

`set` remains a provider-specific write-only operation. `set --verify` is a
separate, opt-in orchestration layer: it writes once, waits for the requested
settle period, performs one GET plus the requested number of additional GET
attempts, and succeeds only when the decoded current value equals the requested
16-bit value. Its defaults are 100 ms settle, three additional attempts, and
250 ms between retries. They are caller-visible policy choices, not DDC/CI
requirements; override them with `--settle-ms`, `--retries`, and
`--retry-delay-ms`.

Before every verify GET, rss-ddc re-correlates the current display index and
requires its ColorSync/CoreGraphics display UUID, provider, product, branch, and transport to
match the binding captured before SET. If it cannot prove that identity after a
disconnect or re-enumeration, verification fails closed instead of selecting a
sibling. This means `set --verify` may report that SET completed but safe
verification is unavailable. In particular, a successful input-source (`0x60`)
SET can intentionally remove the issuing host's active transport; that outcome
does not prove that the write failed, but it is not reported as verified.

Set-and-Verify is hardware-validated only on the documented simultaneous
25F84 topology. The default policy verified PS190 brightness `50 → 49 → 50`
and DP brightness `100 → 99 → 100`. On the LG, one zero-settle/zero-retry run
verified immediately, while a separate default-policy run received a malformed
all-zero first GET, waited 250 ms, then verified successfully on attempt two.
This establishes an intermittent post-SET transient on that monitor, not a
universal delay/retry requirement. Set-and-Verify does not alter the separately
hardware-validated plain GET or plain SET provider transactions.

## Provider model

| Provider | Backend status | Capabilities |
| --- | --- | --- |
| `DCPDP13Service` | conventional Service-path GET/SET and opt-in Set-and-Verify, plus native read-only DPCD, hardware-validated on the documented LG DP setup; EDID unsupported | Get VCP, Set VCP, Read DPCD |
| `DCPDPService` | distinct registry class; conventional Service-path GET/SET/DPCD and Set-and-Verify hardware-validated on documented XL2730Z path (`0x0b`); EDID unsupported | Get VCP, Set VCP, Read DPCD |
| `AppleDCPMCDP29XX` | classified only; all runtime capabilities unsupported | none |
| `AppleDCPPS190` | raw GET, conventional SET, Device-path EDID blocks 0–1, and native DPCD reads hardware validated on the documented Odyssey topology | Get VCP, Set VCP, Read EDID, Read DPCD |
| unknown | safe unsupported result | none |

`DCPDP13Service` and `AppleDCPPS190` deliberately use different request
framing. The provider comes from the selected Service proxy's immediate EPIC
parent; a generic `IOPortTransportStateDisplayPort` node does not choose a
backend because the PS190 HDMI topology can also expose that class.

## Roadmap

1. EDID — current PS190 blocks 0–1 scope complete
2. DPCD — current PS190 + DCPDP13 + DCPDPService read-only scope complete
3. DCPDPService GET/SET/DPCD/Set-and-Verify — complete for current Mac Studio validation scope; EDID open
4. MCDP
5. More monitor catalog coverage
6. Machine-readable profiles later

Read [the architecture](docs/architecture.md), [Apple Silicon transport notes](docs/apple-silicon-ddc.md), and the [Monitor Compatibility & Quirks catalog](docs/monitors/README.md) before enabling another provider capability.

## Build, test, and library

```sh
make              # CLI plus build/librss-ddc.a
make test
make library
```

The static library is `build/librss-ddc.a`; its installed public interface is
`include/rss_ddc.h`. The project is pre-1.0 (`0.1.0`): public API source
compatibility may evolve as provider coverage matures. Private Apple ABI types
never appear in the public header.

```sh
clang -I/some/prefix/include client.c /some/prefix/lib/librss-ddc.a \
  -framework CoreDisplay -framework CoreGraphics -framework ColorSync \
  -framework IOKit -framework Foundation
```

```sh
make install PREFIX=/some/prefix
make uninstall PREFIX=/some/prefix
```

`install` writes only `bin/rss-ddc`, `include/rss_ddc.h`, and
`lib/librss-ddc.a` below the supplied prefix; `uninstall` removes only those
three exact paths. The test suite and GitHub Actions are synthetic: they do
not open display user clients or issue DDC, EDID, or DPCD requests.

## License

MIT. See [LICENSE](LICENSE).
