# Monitor profiles

rss-ddc now provides an offline, value-only monitor-profile store. It owns profile schema validation, conservative identity matching, effective-profile resolution, semantic control lookup, provenance/confidence, and write authorization. It does not own HTTP, DNS, downloads, update schedules, telemetry, UI, or an application filesystem location.

The layers remain separate:

| Layer | Source | Can authorize SET? |
| --- | --- | --- |
| Rogue validated | built-in or validated pack | Only hardware-validated supported controls |
| Local characterized | consumer-loaded local pack | Same explicit hardware-validated policy |
| Research/candidate | research evidence | Never |

Source (`builtin`, `validated-pack`, `local`, `research`) and confidence (`candidate` through `hardware-validated`) are independent. A writable candidate/correlated/research control is rejected; data never becomes a production SET merely because it is parsed.

## Lifecycle and identity

Create a candidate store, load the built-in profile and a replacement pack, then let the consumer atomically swap its active pointer only after success. Every load parses into a temporary bounded representation first, so a malformed replacement leaves the existing store unchanged. `rss_ddc_profile_store_export_json` exports a caller-owned JSON buffer; the consumer chooses its local path and whether to redact serials.

Persisted identities never contain `listIndex`. Schema v1 requires exact external flag, product name, provider, and transport; optional manufacturer, serial, and branch device ID are exact additional predicates. The first bundled profile is strictly external `LG HDR QHD` / `DCPDP13Service` / `DCPEXT0`, not a general LG match.

Initial semantic IDs cover Picture Mode, input, brightness, contrast, Color Preset, response time, adaptive sync, energy saving, black stabilizer, gamma, sharpness, and audio mute. Generic profile data is not an arbitrary write channel: v1 has no reset, degauss, or power control; standard input is constrained to VCP `0x60`; and LG alternate input is constrained to its explicit method.

Picture Mode now resolves through the profile system. Existing concise APIs use the bundled profile, while `*_with_profile_store` lets a consumer use a composed validated/local store. Both issue exactly one mapped operation, never correlated brightness or vendor-control writes.

Monitor knowledge resolution is a separate offline layer for combining parsed
knowledge sources. It retains competing methods and evidence, then applies a
stricter write-authorization policy; it does not alter the existing profile
resolver or add any runtime monitor operation.
