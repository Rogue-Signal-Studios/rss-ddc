# Bounded monitor discovery harness

`rss-ddc-research` is a developer/research executable for collecting evidence about one selected display. It is not a consumer feature and it does not make MCCS-advertised values into product controls.

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

Reports use `schemaVersion: 1` and have deterministic field/order formatting for the same collected report model. The runtime timestamp is the only expected run-to-run metadata change. The report contains:

- `display`: index, public identity fields, provider, branch, and transport.
- `capabilities`: rss-ddc bits, MCCS retrieval status/raw text, and advertised VCP codes.
- `reads`: code/hex, evidence-labelled semantic mapping, status, current/max, advertised enum values, classification, and every sample.
- `mutations`: only when explicitly authorized, including original/candidate, SET/observed/restore outcomes.
- `warnings`: capability and restore limitations.

MCCS advertisement is evidence only: it can be stale, partial, or unrelated to the monitor's real input/profile behavior. In particular, do not infer LG input labels from VCP `0x60`; its separately validated alternate input path is intentionally not probed, mutated, or restored by this harness.
