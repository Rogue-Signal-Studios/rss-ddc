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

Presentation settings (`color`, `table`, `unicode`) are configurable via
`~/.config/rss-ddc/rss-ddc.conf` or CLI flags such as `--color=no`. See
[CLI output presentation](docs/cli-output.md).

```sh
make
./rss-ddc --help
./rss-ddc -h
./rss-ddc list
./rss-ddc --color=no --table=no list
./rss-ddc --table=yes list
./rss-ddc info 1
./rss-ddc characterize 1
./rss-ddc characterize 1 --mode passive
./rss-ddc characterize 1 --mode deep
./rss-ddc characterize 1 --mode deep --no-profiles
./rss-ddc profile update 1 --output /tmp/local-profiles.json
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
./rss-ddc --verbose picture-mode 2 vivid
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

`characterize` is a read-only report for one current list index. It calls
`rss_ddc_characterize_display` and does not SET, update profiles, or run
Guided Discovery / Experimental Validation. It never writes a profile store.

```sh
./rss-ddc --help
./rss-ddc -h
./rss-ddc characterize 1
./rss-ddc characterize 1 --mode passive
./rss-ddc characterize 1 --mode default
./rss-ddc characterize 1 --mode deep
./rss-ddc characterize 1 --mode deep --no-profiles
./rss-ddc characterize 1 --mode deep --no-profiles --json
./rss-ddc characterize 1 --mode default --json --output /tmp/monitor.json
```

`--json` prints canonical `monitor-knowledge/v0.1` discovery JSON to stdout
instead of the human report. `--output <file>` writes the same JSON
atomically (an existing file is replaced only after a complete write) and
implies JSON mode. Discovery JSON never includes prior PARTIAL PROFILE
augmentation. Human-readable output remains the default.

`--no-profiles` is a diagnostic true-alien path: the same characterization
pipeline with monitor-specific profile/structured prior knowledge disabled.
It remains read-only. Normal DEFAULT product behavior may still load
structured knowledge.

`profile update` is the explicit profile-store write path. It characterizes
the selected display in DEFAULT mode (read-only toward the monitor), calls
`rss_ddc_characterization_update_profile`, and saves only LOCAL overlay
records to `--output`. There is no implicit user profile path; `--output` is
required. Automatic update persists already-authoritative, schema-representable
knowledge only. Validated LG HDR QHD LG_ALT input (`lg-alt-input` at `0xf4`,
values `0x90` / `0x91` / `0xd0`) lives in builtin profile data; profile update
may copy that operational subset into a local overlay. It does not invent
LG_ALT from identity or from observed VCP `0x60`. Odyssey currently has no
safely persistable authoritative controls, so the command reports
`UNSUPPORTED` and writes no file. This does not mean every monitor can
automatically gain write support.

```sh
./rss-ddc profile update 1 --output /tmp/local-profiles.json
```

CREATED and UPDATED write the LOCAL overlay. UNCHANGED does not rewrite the
file. UNSUPPORTED does not create an empty file. CONFLICT leaves an existing
file unchanged and exits non-zero. The command never issues monitor SET,
LG_ALT SET, or picture-mode SET.

PASSIVE is identity, profile match, transport bits, and MCCS only. DEFAULT
adds Alien Probe Quick, then Extended only if sufficiency recommends it. DEEP
forces read-only Extended when GET VCP is available; it does not mean the
result is sufficient. INSUFFICIENT or CONFLICT sufficiency is still a
successful CLI run.

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

`picture-mode` is a separate, profile-gated write-only semantic operation for
the documented external `LG HDR QHD` / `DCPDP13Service` / `DCPEXT0` target.
Only historically SET-validated `vivid` and `reader` are exposed. It uses the
existing conventional DCPDP13 SetVCP framing for VCP `0x15`, not the LG F4
input command, and performs no GET, verification, retry, restore, or fallback.
See [Picture Mode](docs/picture-mode.md).

Monitor profiles are now an offline JSON store/resolver with bounded parsing,
transactional loads, atomic saves, and deterministic exact matching. They are
metadata only in this slice and introduce no display transport behavior. See
[Monitor profiles](docs/monitor-profiles.md).

Monitor knowledge is a separate, bounded offline model that retains competing
routes, values, and provenance before producing a deterministic effective view.
It does not authorize or execute a monitor operation. See [Monitor
knowledge](docs/monitor-knowledge.md).

`probe-quick` is a bounded, explicitly read-only Alien Probe observation pass:
six standard VCPs are each read twice, with optional existing MCCS retrieval.
It neither scans arbitrary VCPs nor performs a write. See [Alien Probe Quick](docs/alien-probe.md).

`probe-extended` is a paced, read-only `0x00`–`0xFF` discovery pass that reuses the
same strict observation semantics as Quick Probe. It records protocol-valid
observations separately from MCCS advertisement and never promotes unadvertised
stable replies into capability claims. See [Alien Probe Quick](docs/alien-probe.md).

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
make consumer-test  # compile/link an installed-prefix consumer; never runs it
make research       # isolated predecessor IOAV/IODP labs; not part of librss-ddc
make research-test
```

Predecessor DDC labs live under `research/` and are documented in
[Research DDC labs](docs/research-labs.md). They are opt-in and never part of
the normal library or CLI build.

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

Automatic characterization is a separate read-only entry point. It never SET
writes a monitor or mutates a profile store. Normal product use should call
`rss_ddc_characterization_inspect` first: identity and durable profile lookup
only, with no MCCS, Quick, or Extended. Onboarding may then step
`rss_ddc_characterization_begin` / `rss_ddc_characterization_run_next`, or
use blocking `rss_ddc_characterize_display` as the convenience wrapper.

```c
RSSDDCCharacterization *inspected = NULL;
RSSDDCError error = rss_ddc_characterization_inspect(1, store, NULL, &inspected);
if (error == RSS_DDC_OK) {
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    if (rss_ddc_characterization_resolve(inspected, "inputs.switching",
                                         &resolution) == RSS_DDC_OK &&
        rss_ddc_monitor_knowledge_resolution_write_authorized(resolution)) {
        /* durable authorized input knowledge; do not probe */
    }
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_characterization_destroy(inspected);
}

RSSDDCCharacterizeOptions options = rss_ddc_default_characterize_options();
RSSDDCCharacterization *result = NULL;
error = rss_ddc_characterize_display(1, NULL, &options, &result);
if (error == RSS_DDC_OK) {
    const RSSDDCMonitorKnowledge *knowledge = rss_ddc_characterization_knowledge(result);
    (void)knowledge;
    rss_ddc_characterization_destroy(result);
}
```

Authorized input switching from that characterization is a separate API,
`rss_ddc_characterization_set_input`. It resolves effective `inputs.switching`
and dispatches STANDARD or LG_ALT internally. `rss_ddc_set_input` remains the
lower-level explicit-method operation.

The public API is pre-1.0 (`0.2.0`), so source/API compatibility may evolve as
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
