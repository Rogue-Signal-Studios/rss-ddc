# rss-ddc

`rss-ddc` is an early-stage, provider-driven macOS DDC/CI library and CLI from Rogue Signal Studios.

## Status

This first milestone provides real macOS display/provider discovery, a strict DDC/CI Get VCP parser, and a hardware-validated PS190 Service-path GET implementation. It is intentionally conservative: unsupported providers and capabilities return explicit errors rather than falling back to a guessed transport.

The project uses Apple-private macOS interfaces inside the macOS backend only. Behavior can vary by macOS release, display provider, cable/adapter topology, and monitor firmware.

Validated `rss-ddc` hardware/OS scope is limited to macOS build `25F84`, an `AppleDCPPS190` path, and an Odyssey G75F. Get VCP `0x10` and `0x60` were validated end-to-end with the CLI; no portability beyond that setup is implied.

## CLI

```sh
make
./rss-ddc list
./rss-ddc info 1
./rss-ddc get 1 0x10
./rss-ddc --verbose get 1 0x10
./rss-ddc set 1 0x60 18
```

`set` is present for API/CLI completeness but intentionally returns an unsupported-capability error in this milestone. Do not assume that GET support implies SET, EDID, or DPCD support.

Successful non-verbose `get` prints only the current value. `--verbose` writes the selected display/provider correlation, raw request/reply bytes, IOReturns, decoded values, and checksum status to standard error for controlled validation.

## Provider model

| Provider | Backend status | Capabilities |
| --- | --- | --- |
| `DCPDP13Service` | classified; GET not yet enabled | none |
| `AppleDCPMCDP29XX` | classified; GET not yet enabled | none |
| `AppleDCPPS190` | Service-path GET hardware-validated on the documented 25F84 setup | Get VCP |
| unknown | safe unsupported result | none |

Read [the architecture](docs/architecture.md) and [Apple Silicon transport notes](docs/apple-silicon-ddc.md) before enabling another provider capability.

## Development

```sh
make test
```

The test suite is synthetic and does not open display user clients or issue DDC, EDID, or DPCD requests.

## License

MIT. See [LICENSE](LICENSE).
