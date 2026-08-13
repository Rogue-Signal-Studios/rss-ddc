# rss-ddc

`rss-ddc` is an early-stage, provider-driven macOS DDC/CI library and CLI from Rogue Signal Studios.

[![Repository quality](https://github.com/Rogue-Signal-Studios/rss-ddc/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Rogue-Signal-Studios/rss-ddc/actions/workflows/ci.yml)
[![Coverage](https://img.shields.io/badge/coverage-LLVM%20report-5f8dd3)](https://rogue-signal-studios.github.io/rss-ddc/quality/)
[![Security](https://img.shields.io/badge/security-CodeQL-2b6cb0)](https://github.com/Rogue-Signal-Studios/rss-ddc/security/code-scanning)
[![Dependency review](https://img.shields.io/badge/dependencies-review%20on%20PR-6c7a89)](https://github.com/Rogue-Signal-Studios/rss-ddc/actions/workflows/dependency-review.yml)
[![License](https://img.shields.io/github/license/Rogue-Signal-Studios/rss-ddc)](LICENSE)
[![rss-ddc API](https://img.shields.io/badge/rss--ddc%20API-0.3.0-58d69c)](include/rss_ddc.h)

The [Quality Dashboard](https://rogue-signal-studios.github.io/rss-ddc/quality/) shows the current main-build evidence, generated from CI artifacts rather than hand-maintained values. See [Quality and CI](docs/quality.md) for scope, local commands, and the GitHub Pages setup requirement.

## Status

This milestone provides real macOS display/provider discovery, strict DDC/CI parsing, and provider-specific Service-path operations. PS190 GET/SET and DCPDP13 GET/SET are hardware-validated in `rss-ddc`; read-only native DPCD is hardware-validated on their documented paths. DCPDPService GET, SET, read-only DPCD, and Set-and-Verify are hardware-validated on the documented Mac Studio XL2730Z three-display topology (capabilities `0x0b`). Unsupported providers and capabilities return explicit errors rather than falling back to a guessed transport.

MCCS capability retrieval is a public, caller-owned API for
`DCPDP13Service` only. It was hardware-validated by a complete 35-request,
336-byte LG HDR QHD retrieval with explicit completion and strict parsing.
PS190, DCPDPService, and MCDP capability retrieval remain explicitly
unsupported. Values are raw monitor-advertised candidates, not input labels or
authorization for disruptive SET operations. See
[MCCS capability discovery](docs/mccs-capability-discovery.md).

Picture Mode is now a narrow, profile-gated semantic library capability for
the documented LG HDR QHD / `DCPDP13Service` / `DCPEXT0` identity. Consumers
select one of eight friendly validated modes without using raw VCP `0x15`
values; all other displays fail closed. It is distinct from Color Preset
(`0x14`), brightness/contrast, and advanced raw VCP access. See
[Picture Mode](docs/picture-mode.md).

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

Input switching has an explicit public API with standard VCP `0x60` and the
separately validated alternate transport. See
[Input switching](docs/input-switching.md) for provider gating and
monitor-profile requirements; the research record remains in
[research-lg-input-framing.md](docs/research-lg-input-framing.md).

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
| `DCPDP13Service` | conventional Service-path GET/SET and opt-in Set-and-Verify, native read-only DPCD, and bounded MCCS capability retrieval, hardware-validated on the documented LG DP setup; EDID unsupported | Get VCP, Set VCP, Read DPCD, MCCS Capabilities; exact LG profile also adds Picture Mode |
| `DCPDPService` | distinct registry class; conventional Service-path GET/SET/DPCD and Set-and-Verify hardware-validated on documented XL2730Z path (`0x0b`); EDID unsupported | Get VCP, Set VCP, Read DPCD |
| `AppleDCPMCDP29XX` | classified only; all runtime capabilities unsupported | none |
| `AppleDCPPS190` | raw GET, conventional SET, Device-path EDID blocks 0–1, and native DPCD reads hardware validated on the documented Odyssey topology | Get VCP, Set VCP, Read EDID, Read DPCD |
| unknown | safe unsupported result | none |

`DCPDP13Service` and `AppleDCPPS190` deliberately use different request
framing. The provider comes from the selected Service proxy's immediate EPIC
parent; a generic `IOPortTransportStateDisplayPort` node does not choose a
backend because the PS190 HDMI topology can also expose that class.

## MCCS capability consumer example

```c
RSSDDCMCCSCapabilities capabilities = {};
if (rss_ddc_get_mccs_capabilities(display_index, &capabilities) == RSS_DDC_OK &&
    rss_ddc_mccs_capabilities_has_vcp(&capabilities, 0x60)) {
    const uint8_t *values = NULL;
    size_t count = 0;
    if (rss_ddc_mccs_capabilities_enum_values(&capabilities, 0x60, &values, &count) == RSS_DDC_OK) {
        /* Consume only raw monitor-advertised values; do not infer connector labels. */
    }
}
```

`capabilities` is caller-owned, needs no cleanup function, and owns the raw
string plus all parsed storage. On the validated LG, the raw VCP `0x60` values
are `0x11`, `0x12`, `0x0f`, and `0x00`.

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
make consumer-test  # compile/link an installed-prefix consumer; never runs it
```

## Using rss-ddc as a library

`rss-ddc` can be consumed as a static C library without the CLI, test suite,
or any source-tree headers. The public API is [include/rss_ddc.h](include/rss_ddc.h).
It intentionally contains only C standard-library types; it exposes no
CoreFoundation, IOKit, CoreDisplay, or reconstructed Apple-private handles.
It is safe to include from C++ as well as C.

Build and stage the library into a private prefix:

```sh
make library
make install-library PREFIX=/some/prefix
```

The staged consumer contract is exactly:

```text
/some/prefix/include/rss_ddc.h
/some/prefix/lib/librss-ddc.a
```

For example, an unrelated application can compile and link with no `src/`
include paths and no loose rss-ddc objects or source files:

```sh
clang -std=c11 -I/some/prefix/include client.c /some/prefix/lib/librss-ddc.a \
  -framework CoreDisplay -o client
```

`CoreDisplay` is the minimum explicit framework link requirement proven by the
current macOS build. It re-exports the CoreGraphics, ColorSync, IOKit,
CoreFoundation, and Objective-C dependencies used by the backend, so consumers
need not link them separately. The static archive includes all rss-ddc
implementation objects needed by the public API: portable core/protocol/EDID/
DPCD code, provider dispatch and backends, and macOS discovery. It deliberately
excludes `cli/main.m`, every `tests/` source, and the historical GET/SET
validation runners. Static linking does not remove the macOS-private-interface
or runtime-compatibility risk described above. An application must still select
only providers and capabilities validated for its own macOS, topology, and
monitor firmware; consult the provider model and [hardware matrix](docs/hardware-validation.md).

```c
#include <rss_ddc.h>

int main(void) {
    size_t count = 0;
    RSSDDCError error = rss_ddc_list_displays(NULL, 0, &count);

    return error == RSS_DDC_OK ? 0 : 1;
}
```

The zero-capacity call is the first half of the documented two-call display
snapshot pattern. It returns the observed count but does not open an IOAV
client. GET, SET, EDID, and DPCD requests are explicit separate API calls.

The public API is pre-1.0 (`0.3.0`), so source/API compatibility may evolve as
provider coverage matures. Consumers should pin an exact release or commit;
the planned external consumer will pin rss-ddc rather than track `main`. No
stable ABI promise is made before 1.0.

```sh
make install PREFIX=/some/prefix
make uninstall PREFIX=/some/prefix
```

`make install PREFIX=/some/prefix` adds the separate CLI at
`/some/prefix/bin/rss-ddc` in addition to the library artifacts. `make
uninstall PREFIX=/some/prefix` removes only those three exact project-owned
files. `DESTDIR` is supported for staged packaging, e.g. `make install
DESTDIR=/package-root PREFIX=/usr/local` writes below
`/package-root/usr/local`; no internal/private headers are installed.

For a pinned git submodule, build and stage a private prefix, then compile
against only that prefix's `include/` and `lib/` directories. This is preferred
over direct source-tree paths because it exercises the same install boundary
as an unrelated application. The test suite and GitHub Actions are synthetic:
they do not open display user clients or issue DDC, EDID, or DPCD requests.

## License

rss-ddc is MIT licensed; see [LICENSE](LICENSE). Applications that statically
link and redistribute rss-ddc should preserve the MIT copyright and permission
notice in their redistribution materials.
