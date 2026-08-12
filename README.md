# rss-ddc

`rss-ddc` is an early-stage, provider-driven macOS DDC/CI library and CLI from Rogue Signal Studios.

## Status

This milestone provides real macOS display/provider discovery, strict DDC/CI parsing, and provider-specific Service-path operations. PS190 GET/SET and DCPDP13 GET/SET are hardware-validated in `rss-ddc`; unsupported providers and capabilities return explicit errors rather than falling back to a guessed transport.

The project uses Apple-private macOS interfaces inside the macOS backend only. Behavior can vary by macOS release, display provider, cable/adapter topology, and monitor firmware.

Validated `rss-ddc` hardware/OS scope is limited to macOS build `25F84` on a simultaneous two-display topology: an Odyssey G75F on HDMI/`AppleDCPPS190` and an LG HDR QHD on DisplayPort/`DCPDP13Service`. GET and same-state SET were manually validated on each selected display without affecting its sibling. No portability beyond these exact setups is implied.

## CLI

```sh
make
./rss-ddc list
./rss-ddc info 1
./rss-ddc --verbose info 1
./rss-ddc get 1 0x10
./rss-ddc --verbose get 1 0x10
./rss-ddc set 1 0x60 18
```

PS190 `set` is hardware-validated only in the documented 25F84/Odyssey G75F scope. Do not assume that GET or SET support implies EDID or DPCD support, or that either operation applies to another provider.

Successful non-verbose `get` prints only the current value. `--verbose` writes the selected display/provider correlation, raw request/reply bytes, IOReturns, decoded values, and checksum status to standard error for controlled validation.
For `info`, verbose mode emits a precise registry-correlation rejection reason
without constructing an IOAV Service object; it is the first diagnostic to run
when a display fails closed.

## Provider model

| Provider | Backend status | Capabilities |
| --- | --- | --- |
| `DCPDP13Service` | conventional Service-path GET and SET hardware-validated on the documented LG DP setup | Get VCP, Set VCP |
| `AppleDCPMCDP29XX` | classified; GET and SET unsupported | none |
| `AppleDCPPS190` | raw GET and conventional SET hardware-validated on the documented 25F84 setup | Get VCP, Set VCP |
| unknown | safe unsupported result | none |

`DCPDP13Service` and `AppleDCPPS190` deliberately use different request
framing. The provider comes from the selected Service proxy's immediate EPIC
parent; a generic `IOPortTransportStateDisplayPort` node does not choose a
backend because the PS190 HDMI topology can also expose that class.

Read [the architecture](docs/architecture.md) and [Apple Silicon transport notes](docs/apple-silicon-ddc.md) before enabling another provider capability.

## Development

```sh
make test
```

The test suite is synthetic and does not open display user clients or issue DDC, EDID, or DPCD requests.

## License

MIT. See [LICENSE](LICENSE).
