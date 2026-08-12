# rss-ddc

`rss-ddc` is an early-stage, provider-driven macOS DDC/CI library and CLI from Rogue Signal Studios.

## Status

This milestone provides real macOS display/provider discovery, strict DDC/CI parsing, and provider-specific Service-path operations. PS190 GET/SET and DCPDP13 GET/SET are hardware-validated in `rss-ddc`; unsupported providers and capabilities return explicit errors rather than falling back to a guessed transport.

The project uses Apple-private macOS interfaces inside the macOS backend only. Behavior can vary by macOS release, display provider, cable/adapter topology, and monitor firmware.

Validated `rss-ddc` hardware/OS scope is limited to macOS build `25F84` on a simultaneous two-display topology: an Odyssey G75F on HDMI/`AppleDCPPS190` and an LG HDR QHD on DisplayPort/`DCPDP13Service`. GET, write-only SET, and opt-in Set-and-Verify—including real brightness changes and the live retry path—were manually validated on each selected display without affecting its sibling. No portability beyond these exact setups is implied.

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
./rss-ddc --verbose get 1 0x10
./rss-ddc set 1 0x60 18
./rss-ddc set 1 0x10 50 --verify
./rss-ddc --verbose set 2 0x10 100 --verify --settle-ms 100 --retries 3 --retry-delay-ms 250
```

PS190 `set` is hardware-validated only in the documented 25F84/Odyssey G75F scope. Do not assume that GET, SET, or base-block EDID support implies DPCD support, complete E-EDID support, or support for another provider.

`edid` is a read-only, independently dispatched capability. Current rss-ddc
hardware-validates the PS190 Device-path base-block tuple on the documented
macOS 25F84/Odyssey G75F topology. When the base declares one or more
extensions, rss-ddc attempts only standard E-EDID block 1 (segment `0`, offset
`0x80`) without a write; that new mapping is standards-backed but remains
pending hardware validation. Blocks 2+ require a segment-pointer write and
remain unsupported. `--decode` is the default, `--hex` labels every acquired
128-byte block, and `--raw <file>` creates a new exact-byte file without
overwriting an existing path. `extensions-complete` is the authoritative
complete/partial state. DCPDP13 and MCDP EDID remain explicitly unsupported.

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
| `DCPDP13Service` | conventional Service-path GET/SET and opt-in Set-and-Verify hardware-validated on the documented LG DP setup; EDID acquisition unproven | Get VCP, Set VCP |
| `AppleDCPMCDP29XX` | classified; GET and SET unsupported | none |
| `AppleDCPPS190` | raw GET, conventional SET, opt-in Set-and-Verify, and Device-path base EDID hardware-validated; block-1 E-EDID acquisition is pending validation | Get VCP, Set VCP, Read EDID |
| unknown | safe unsupported result | none |

`DCPDP13Service` and `AppleDCPPS190` deliberately use different request
framing. The provider comes from the selected Service proxy's immediate EPIC
parent; a generic `IOPortTransportStateDisplayPort` node does not choose a
backend because the PS190 HDMI topology can also expose that class.

Read [the architecture](docs/architecture.md), [Apple Silicon transport notes](docs/apple-silicon-ddc.md), and the [Monitor Compatibility & Quirks catalog](docs/monitors/README.md) before enabling another provider capability.

## Development

```sh
make test
```

The test suite is synthetic and does not open display user clients or issue DDC, EDID, or DPCD requests.

## License

MIT. See [LICENSE](LICENSE).
