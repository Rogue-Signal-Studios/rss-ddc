# Alien Probe™

**Alien Probe™** is Rogue's complete monitor capability-discovery and
characterization system. It creates structured monitor knowledge: identity,
capabilities, methods, values, evidence, confidence, safety risk, and observed
relationships. It is not synonymous with a VCP address scan.

Alien Probe™ is product and conceptual terminology. Low-level public C APIs
remain clear technical names such as `rss_ddc_probe_*`,
`rss_ddc_inventory_*`, `rss_ddc_characterization_*`, and
`rss_ddc_validation_*`.

## Canonical phases

1. **Quick Auto Probe** — stable identity, provider/transport, EDID, MCCS when
   available, standards-defined controls, known profiles, existing local
   validated knowledge, and safe GET observations. No mutation.
2. **Extended Auto Probe** — broader GET-only inventory, vendor/private
   readable candidates, stability/range/enum observations, and cached
   monitor-wide inventory. Zero SET operations.
3. **Guided Discovery** — the user changes a physical OSD state; rss-ddc
   captures before/after fingerprints and ranks primary, possible, secondary,
   incidental, unchanged, and unstable observations. Picture Mode is the
   first feature investigation module, not the generic design.
4. **Experimental Validation** — one explicit, narrowly scoped SET only after
   monitor/session identity, candidate, target raw value, semantic-conflict,
   risk, and denylist checks. It captures evidence and restores only when that
   is separately safe and appropriate. It never performs blind mutation.

## Product experiences

Rogue Display Control (**Configurator Full**) may show detailed progress,
inventory, evidence/confidence, raw control matrices, Guided Discovery, and
Experimental Validation. Rogue Display Control for Stream Deck
(**Configurator Light**) invokes the same engine autonomously where possible
and presents task-oriented summaries such as “3 inputs found” or “Picture
Modes need a little help.” It intentionally hides most research complexity.

## Design law: correlation is not semantic identity

A Picture Mode may alter brightness, contrast, color preset, RGB gains,
response time, or sharpness. Even a perfectly correlated standard Brightness
control remains Brightness. Alien Probe™ therefore combines mathematical
correlation, semantic and standards knowledge, model/family evidence,
provenance, confidence, and hardware validation. Known semantic controls can
be recorded as `secondary-correlator` rather than candidates for an unrelated
feature.

## Quick Auto Probe v1

Quick Auto Probe is now available through the technical `rss_ddc_probe_*` API
and `rss-ddc probe-quick <display-index>`. It accepts exactly one correlated
display and performs only two kinds of provider operation: Get VCP for the
six registry-defined standard controls and, where the selected provider
already supports it, MCCS capabilities retrieval. The implementation has no
write callback, so it cannot issue Set VCP, input switching, alternate LG
writes, power changes, or experimental validation writes.

Each successful standard read is repeated once. Equal replies receive
`stable_get` evidence; changing or failed repeats remain read observations and
are surfaced as variable in probe diagnostics. Current values are retained as
typed observations and protocol-reported maxima are separate metadata; neither
claims an empirically observed range or a safe write range. MCCS advertisement supplies
`mccs_advertised` evidence and advertised enum raw values, but never write
authority.

Quick Probe produces a canonical `monitor-knowledge/v0.1` document with the
selected display identity, provider/transport/branch metadata, observations,
and diagnostics. It does not probe unknown VCP codes, discover Picture Mode,
or compose profile knowledge; those belong to later Extended Auto Probe and
profile-resolution consumers respectively.

For a user-run inspection, first obtain the explicit display index with
`./rss-ddc list`, then run `./rss-ddc probe-quick <display-index>` or add
`--json` for canonical MonitorKnowledge output. Never use a list index from an
earlier process invocation.

## Extended Auto Probe v1

Extended Auto Probe is available through `rss_ddc_probe_extended_*` and
`rss-ddc probe-extended <display-index>`. It accepts the same single exactly
correlated target as Quick Probe, and only supports providers with an already
proven safe GET transport: AppleDCPPS190, DCPDP13Service, and DCPDPService.
AppleDCPMCDP29xx fails closed rather than receiving a speculative scan.

One run creates one fresh observation document. It scans the normal VCP byte
space once in deterministic stages: MCCS-advertised addresses, remaining
registry-defined standard controls, then all remaining addresses. Each initial
success receives one delayed repeat; the production path paces requests by
25 ms and the diagnostics record the policy. Unsupported replies are normal.
Eight consecutive transport-level failures stop the scan, retain partial
knowledge, and mark diagnostics incomplete rather than hammering an unhealthy
display.

Known registry controls retain their established semantic IDs and
`read_standard` risk. An otherwise readable address is recorded only as
`vendor.unknown.vcp.xx`, with `read_extended` risk and `writable:false`—no
meaning is inferred from its address, value, vendor, or MCCS presence. The
result’s diagnostics and MonitorKnowledge form a reusable in-memory inventory;
there is no product-specific filesystem cache. Consumers may persist a run or
explicitly merge runs, but an old run is never silently represented as new.

The CLI’s human mode declares `READ-ONLY; NO SETTINGS WILL BE CHANGED` and
updates `attempted/256` progress on stderr while it scans. With `--json`,
stdout contains canonical MonitorKnowledge only; progress and diagnostic output
remain on stderr. No Extended Probe API transport includes a write callback.
