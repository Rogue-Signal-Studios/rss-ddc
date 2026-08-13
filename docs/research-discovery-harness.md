# Bounded monitor discovery harness

`rss-ddc-research` is a developer/research executable for collecting evidence about one selected display. It is not a consumer feature and it does not make MCCS-advertised values into product controls.

## OSD-state fingerprints and offline correlation

When a monitor OSD exposes named semantic states, a read-only fingerprint is preferred over blind mutation. Capture a known state, manually change exactly one OSD setting, capture again, and compare the resulting files offline:

```text
Known physical OSD state
        ↓
read-only fingerprint
        ↓
change exactly one OSD setting in the monitor OSD
        ↓
second read-only fingerprint
        ↓
offline comparison
        ↓
candidate VCP correlation
```

`--fingerprint` is an explicit name for the default all-advertised, bounded read sweep. It does not scan `0x00`–`0xFF`, issue SET, or add non-advertised candidates. `--label` stores a human OSD-state label in the capture.

For the documented LG HDR QHD at display index `2`, capture the current FPS baseline and then, after changing only the physical OSD Picture Mode, Custom:

```sh
./rss-ddc-research discover --display 2 --fingerprint --reads 3 --label FPS \
  --report /tmp/lg-mode-fps.json

# Manually select Custom in the monitor OSD, then run:
./rss-ddc-research discover --display 2 --fingerprint --reads 3 --label Custom \
  --report /tmp/lg-mode-custom.json
```

Repeat the same command pattern for `Vivid`, `HDR-Effect`, `Cinema`, `RTS`, `Color-Weakness`, and the currently unidentified eighth mode (use a descriptive temporary label such as `Unknown-8`). Do not issue a VCP SET as part of this workflow.

Compare two or more captures without opening a display or an IOAV client:

```sh
./rss-ddc-research compare /tmp/lg-mode-fps.json /tmp/lg-mode-custom.json

./rss-ddc-research compare /tmp/lg-mode-fps.json /tmp/lg-mode-custom.json \
  /tmp/lg-mode-vivid.json /tmp/lg-mode-hdr-effect.json /tmp/lg-mode-cinema.json \
  /tmp/lg-mode-rts.json /tmp/lg-mode-color-weakness.json /tmp/lg-mode-unknown-8.json \
  --report /tmp/lg-picture-mode-correlation.json
```

Shell glob expansion is also fine—for example, `./rss-ddc-research compare /tmp/lg-mode-*.json`—because the shell passes a normal argv file list. The comparator sorts VCP rows deterministically; pass report paths in your desired column order.

Only stable, successful reads are value-compared. The pairwise view distinguishes value changes, maximum changes, read-status changes, and missing/newly-readable controls. With three or more reports, the matrix ranks a VCP as:

- `strong-correlator`: stable and enum-advertised in every report, with at least three distinct observed values.
- `possible-correlator`: stable in every report, changes across captures, and is enum-advertised in at least one capture.
- `incidental-change`: stable numeric values change but there is no enum evidence.

These are evidence rankings, not semantic claims. Brightness, contrast, RGB gains, and other settings can change incidentally when a mode changes. The comparison warns prominently if product, manufacturer, serial, provider, branch, or transport differs between reports.

## Read-only discovery

The default `discover` command performs inventory plus a bounded GET sweep only. It gathers the selected public display identity, provider/capability bits, the bounded raw MCCS capability string when supported, parsed advertised VCPs and enum values, and repeated GET samples. No SET operation is reachable unless `--allow-set` is supplied.

Build it with `make`, then use a new report file:

```sh
./rss-ddc-research discover --display 1 --category picture --reads 3 \
  --report /tmp/lg-picture-discovery.json

./rss-ddc-research discover --display 1 --category picture --reads 3 \
  --report /tmp/g75-picture-discovery.json
```

`--category picture` starts with the monitor's advertised VCPs where available and prioritizes a small, documented standard MCCS picture/color set: brightness, contrast, color preset, RGB gain, black level, gamma, and sharpness. This is a candidate filter, not a claim that a monitor implements any control or that a vendor control has a standard meaning. Monitor-advertised-but-unknown and explicitly supplied codes remain labelled as unknown in JSON.

Use `--vcp 0x10,0x14` for a bounded explicit list, or `--range 0x10:0x1f` for an explicit range of at most 64 codes. There is intentionally no automatic 0x00–0xFF scan. `--reads` accepts only 1 through 10; repeated reads classify controls as numeric, enum-advertised, readable-unknown, unsupported, malformed, unstable, or transport-error.

## Controlled mutation (not a first-hardware milestone)

Do not use mutation mode for initial discovery. It exists only for later, explicitly authorized research:

```sh
./rss-ddc-research discover --display 1 --category picture --reads 2 \
  --allow-set --vcp 0x14 --values 0x05,0x08 --restore --settle-ms 250 \
  --report /tmp/picture-validation.json
```

All of these conditions are required before the engine can call SET:

- `--allow-set` is present.
- At least one exact `--vcp` code is present; selecting a category alone cannot mutate a control.
- At least one exact candidate `--values` value is present.
- The original value was read successfully and consistently.
- The code is not denylisted.

Restore is on by default and `--restore` documents that intent. Each candidate is GET → SET → optional bounded settle → GET → restore → GET. A failed or unverifiable restore stops further candidates for that control and is a report warning. The executable creates reports with exclusive creation, so it will not overwrite an existing report path.

Each subsequent candidate starts only after the prior candidate's restore GET has confirmed the original value; the configured settle interval is also applied after restore. `changed` is true only when the post-SET GET equals the requested candidate. A successful DDC/CI SET return is an acknowledgement of the write transaction, not proof that the monitor adopted the requested value. For example, a monitor may accept `0x08` but retain `0x05`; treat that as `changed: false`, not as a working preset.

The mutation denylist is: degauss `0x01`; factory reset `0x04`; reset luminance/contrast `0x05`; reset color `0x08`; standard input/source `0x60`; LG's write-only alternate input `0xF4`; and power mode `0xD6`. This keeps the alternate IOAV input mechanism outside the generic VCP mutation path. Unknown codes are read-only unless a researcher explicitly names that exact code and supplies values; the denylist always wins.

## JSON reports

Captured reports now use `schemaVersion: 2`; the only schema addition is top-level `label`. Existing `schemaVersion: 1` reports remain readable by `compare`, which falls back to the report filename as its label. Reports have deterministic field/order formatting for the same collected report model. The runtime timestamp is the only expected run-to-run metadata change. The capture report contains:

- `display`: index, public identity fields, provider, branch, and transport.
- `capabilities`: rss-ddc bits, MCCS retrieval status/raw text, and advertised VCP codes.
- `reads`: code/hex, evidence-labelled semantic mapping, status, current/max, advertised enum values, classification, and every sample.
- `mutations`: only when explicitly authorized, including original/candidate, SET/observed/restore outcomes.
- `warnings`: capability and restore limitations.

An optional comparison JSON report has its own `schemaVersion: 1`, report labels/paths, identity-match result, deterministic per-VCP observations and change flags, correlation rankings, and warnings.

MCCS advertisement is evidence only: it can be stale, partial, or unrelated to the monitor's real input/profile behavior. In particular, do not infer LG input labels from VCP `0x60`; its separately validated alternate input path is intentionally not probed, mutated, or restored by this harness.
