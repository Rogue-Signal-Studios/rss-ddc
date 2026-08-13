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

## Scope status

This document establishes terminology and design direction. It does not claim
that Quick/Extended Auto Probe or generic characterization APIs exist today.
