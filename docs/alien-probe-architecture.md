# Canonical Alien Probe™ architecture

This document is **normative**.

If code and this architecture disagree, the implementation must be corrected
or the architectural conflict must be explicitly reviewed before proceeding.
Passing tests do not authorize drift.

Related process history remains in
[monitor characterization](monitor-characterization.md). This file locks the
execution and knowledge boundaries.

## Canonical pipeline

```text
CONNECTED / UNKNOWN MONITOR
        │
        ▼
1. DISCOVER IDENTITY
   manufacturer, product/model, EDID, serial/stable ids, provider,
   transport, branch/path, external/internal, other stable identity fields
        │
        ▼
2. LOOK UP STRUCTURED MONITOR KNOWLEDGE
        │
        ├── exact + COMPLETE validated match
        │       → LOAD STRUCTURED KNOWLEDGE → DONE
        │         (PASSIVE and DEFAULT only; see modes)
        │
        └── none, PARTIAL, or CONFLICT
                │
                ▼
3. DISCOVER PASSIVELY
   MCCS, EDID-derived facts, transport/provider capabilities,
   other generic read-only metadata
        │
        ▼
4. ALIEN PROBE™ QUICK AUTO PROBE
   generic, read-only, no monitor-specific assumptions
        │
        ▼
5. NEED MORE KNOWLEDGE?
        ├── no
        └── yes → ALIEN PROBE™ EXTENDED AUTO PROBE
                  generic 0x00–0xFF observation, read-only,
                  no monitor-specific assumptions
                │
                ▼
6. BUILD CANONICAL MONITOR KNOWLEDGE
   from what was discovered, advertised, observed, stable/variable,
   semantically understood, and validated
        │
        ▼
7. EMIT CANONICAL MACHINE-READABLE JSON
   Historical contract: monitor-knowledge/v0.1
   (serialization is a later slice; this is the durable artifact)
        │
        ▼
8. OPTIONAL PRIOR-KNOWLEDGE AUGMENTATION
   Partial or additional previously validated monitor-specific knowledge
   may be merged while preserving provenance and authority.
```

A PARTIAL profile match is augmentation, not a cache hit. It must not
suppress discovery. PARTIAL prior structured knowledge must not influence
discovery sufficiency or Alien Probe depth.

## Knowledge views

Characterization keeps three distinguishable views:

```text
discovery knowledge
    identity, transport capability, passive/MCCS, Quick observations,
    Extended observations, generic semantic knowledge
    This is what Alien Probe actually discovered.

prior structured knowledge
    exact matched PROFILE/pack facts retained at lookup
    Quarantined from discovery sufficiency until the pipeline completes.

effective / augmented knowledge
    discovery knowledge plus prior structured knowledge after discovery
    Product consumers see this view.
```

Only an exact COMPLETE validated structured match may suppress discovery.
For NONE / PARTIAL / CONFLICT, discovery depth is decided from discovery
knowledge alone.

`monitor-knowledge/v0.1` is the discovery artifact. A future
augmented/effective view may contain prior PROFILE facts, but those must
remain distinguishable from what Alien Probe actually discovered.

Public `rss_ddc_characterization_knowledge()` returns effective/augmented
knowledge. `rss_ddc_characterization_discovered_knowledge()` returns
discovery-only knowledge. `--no-profiles` / `IGNORE_KNOWN` makes these the
same object because there is no prior augmentation.

## Two artifacts

```text
characterization runtime model
        ↓ deterministic mapping
monitor-knowledge/v0.1 JSON     ← discovery / evidence artifact

validated operational subset
        ↓
profile schemaVersion 1         ← durable reusable validated control knowledge
```

These are not the same artifact. Profile update persists only the operational
subset the current profile schema can represent. It is not the canonical
discovery document. `rss-ddc characterize` remains read-only and never mutates
profiles.

JSON restoration of `monitor-knowledge/v0.1` is the next slice. Do not invent
a competing characterization JSON schema.

## Protocol code vs monitor-specific knowledge

**Protocol / transport knowledge IS allowed in executable code**, including:

- DDC/CI framing
- VCP Get/Set mechanics
- generic semantics of VCP `0x10`, `0x12`, `0x60`
- MCCS parsing, EDID parsing
- AppleDCPPS190 / DCPDP13 transport implementation
- LG_ALT protocol/framing/address mechanics as a generic supported mechanism

**Monitor-specific behavioral decisions in generic discovery are forbidden.**

Not allowed in characterization/probe decision logic:

```text
"LG HDR QHD uses LG_ALT with values X/Y/Z"
```

That fact belongs in structured profile/knowledge data. Generic code may
*execute* LG_ALT when a validated profile/data record selects it. The SET
path may still fail closed against transport/topology safety. That is write
safety, not discovery applicability.

Identity strings such as `LG HDR QHD` or `Odyssey G75F` are legitimate
hardware-derived data. Using those strings to conclude undocumented
capabilities inside generic discovery is forbidden.

## COMPLETE vs PARTIAL

After identity is assembled and structured knowledge is matched, completeness
is the existing durable sufficiency question asked **before** passive/probe
discovery:

- **NONE** — no exact profile match
- **PARTIAL** — exact match exists, but required durable methods are not all
  represented (example: identity matches but only Picture Mode is known)
- **COMPLETE** — exact match exists and current structured knowledge already
  satisfies characterization sufficiency for in-scope durable controls
- **CONFLICT** — structured match is ambiguous

Do not invent a fuzzy “looks close enough” rule. A complete match may
short-circuit PASSIVE/DEFAULT discovery. A partial match must not.
PARTIAL prior methods must not satisfy pre-Extended discovery sufficiency
or change what Alien Probe probes.

## Mode semantics after early lookup

**PASSIVE:** identity, structured lookup. Complete exact validated match
returns loaded knowledge. Otherwise perform passive discovery only (no Quick,
no Extended).

**DEFAULT:** identity, structured lookup. Complete match returns loaded
knowledge (no Quick/Extended). Otherwise passive, Quick, discovery-only
sufficiency, optional Extended, then prior-knowledge augmentation.

**DEEP:** identity, structured lookup. Retain known knowledge as prior
augmentation when present, but **always** perform the requested deep
discovery. A complete profile must not skip a rescan the caller asked for.
Prior knowledge does not determine DEEP depth because Extended is forced.

**`--no-profiles` / `IGNORE_KNOWN`:** same pipeline with monitor-specific
profile/structured prior knowledge disabled. Diagnostic/discovery option.
Read-only. Proves a true alien encounter.

## Invariants

1. Alien Probe™ must work on a connected monitor for which rss-ddc contains
   zero monitor-specific knowledge.
2. Monitor make/model/product strings must not control what Quick or Extended
   probes, what capability Probe claims was discovered, whether a GET is
   write-authorized, or what monitor-specific write method core
   characterization injects.
3. Protocol/transport knowledge is allowed in executable code. Monitor-specific
   applicability is not.
4. Monitor-specific facts belong in data (profiles/knowledge packs).
5. Discovery evidence and prior knowledge remain distinguishable (identity,
   EDID, MCCS/DECLARED, Quick OBSERVED, Extended OBSERVED, PROFILE,
   hardware-validated prior knowledge). Do not collapse these into “supported”.
6. Successful GET never creates SET authority. Advertised, observed, stable
   GET, Extended discovery, and current value are not writable.
7. Profiles augment discovery unless they qualify for the explicit complete
   match short-circuit. A partial known profile must not replace actual
   characterization and must not influence discovery sufficiency or Alien
   Probe depth.
8. Alien Probe™ discovery must be independently testable with monitor profiles
   and monitor-specific structured knowledge disabled.
9. Characterization must ultimately produce `monitor-knowledge/v0.1`. Runtime
   population happens now; JSON restore is the next slice.
10. Core characterization must not contain monitor-specific capability
    branches such as `if product == "LG HDR QHD"`.
11. Identity itself is discovered generically. Product strings are data, not
    capability switches in discovery.
12. Implement → validate → commit → push → verify remote → report. Completed
    slices must be remotely preserved before the next slice.

## Provenance limitation (deferred)

Probe observations may still be tagged `RESEARCH` in the reconstructed
knowledge model. That historical tagging is deferred cleanup. It must not be
used to treat live GET as write authority, and profile-free execution must
still contain no monitor-specific PROFILE facts.
