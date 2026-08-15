# Monitor knowledge architecture reconciliation

Decision record for the relationship between historical
`monitor-knowledge/v0.1` and the reconstructed `RSSDDCMonitorKnowledge` on
`feature/monitor-characterization`. This is not a restatement of the historical
documents. Citations use git refs; those branches were not modified.

## Determination

**B.** `monitor-knowledge/v0.1` is the intended canonical durable knowledge
contract. Current `RSSDDCMonitorKnowledge` is the reconstructed bounded native
subset of that architecture, not a competing model and not a discarded
proposal.

Evidence:

1. `83ddb44` (`docs/canonical-rogue-display-architecture:docs/monitor-knowledge-schema.md`)
   introduced v0.1 as “architecture proposal only.”
2. `c7f6834` through `38cf0b1` (`feature/monitor-knowledge-core`) implemented
   it as an offline C document with JSON parse/serialize, identity,
   capabilities, methods, values, input routes, relationships, and
   `#define RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA "monitor-knowledge/v0.1"`.
3. The same schema document at `38cf0b1` states that v0.1 “is implemented as
   an offline, heap-owned rss-ddc C model with JSON parsing, validation,
   export, semantic lookup, and bounded fixture support.”
4. `feature/alien-probe-extended:docs/alien-probe.md` (`32bfd82`) states that
   Quick/Extended Probe produce a canonical `monitor-knowledge/v0.1` document.
5. Reconstruction `3f922f9` / current `docs/monitor-knowledge.md` restored a
   smaller route-only C model and explicitly omitted serialization for that
   slice. That is a recovery-scope deferral, not a supersession of v0.1.

Not A: v0.1 was implemented, not merely illustrated. Not C: historical probe
output *was* the v0.1 document owned by `rss_ddc_monitor_knowledge_*`. Not D:
the refs above are explicit.

Intended relationship:

```text
native C representation  ↔  deterministic monitor-knowledge/v0.1 JSON
```

of the same semantic knowledge. Current C is a lossy subset. A serializer is
**explicitly deferred**; characterization must not invent a second document
type.

## Historical sources inspected

| Ref | Role |
| --- | --- |
| `docs/canonical-rogue-display-architecture` (`a097712`, `83ddb44`) | Product architecture, v0.1 proposal, semantic taxonomy, evidence/confidence, Alien Probe phases |
| `feature/monitor-knowledge-core` (`c7f6834`…`38cf0b1`) | Implemented v0.1 C API + JSON + resolution |
| `feature/alien-probe-observation` (`b1cf501`, `bfac138`) | Quick Probe producing v0.1 documents |
| `feature/alien-probe-extended` (`32bfd82`) | Extended Probe producing v0.1 + diagnostics |
| `feature/monitor-profiles` | Profile pack schema v1 (separate from v0.1) |
| Current `main` / this branch | Reconstructed route model + current probes |

### Documents found historically, absent from current main

- `docs/monitor-knowledge-schema.md`
- `docs/monitor-knowledge-resolution.md`
- `docs/semantic-controls.md`
- `docs/evidence-and-confidence.md`
- `docs/product-architecture.md`
- `docs/profile-packs.md`
- `docs/profile-resolution.md`
- `docs/alien-probe-observation.md` (extended branch only)

### Documents expected and present on this branch

- `docs/monitor-knowledge.md` (reconstruction subset)
- `docs/alien-probe.md` (reconstruction Quick/Extended)
- `docs/monitor-profiles.md`
- `docs/monitor-characterization.md`

No `docs/canonical-rogue-display-architecture/` directory exists; that name is
a branch.

## Surviving architectural decisions

These remain authoritative:

- Semantic-first knowledge; VCP/protocol is a method, not the capability
  identity (`83ddb44` schema; `docs/semantic-controls.md`).
- Retained knowledge vs effective method selection vs write authorization are
  three separate decisions (`feature/monitor-knowledge-core:docs/monitor-knowledge-resolution.md`).
- Independent resolvers for methods, values, ranges, and input routes (same
  doc; APIs `resolve_capability`, `resolve_value`, `resolve_range`,
  `resolve_input_route` at `38cf0b1:include/rss_ddc.h`).
- Evidence type, confidence, validation, and operation risk are orthogonal
  (`83ddb44:docs/evidence-and-confidence.md`).
- Write authorization requires scoped strong evidence (`set_confirmed`,
  `local_validated`, or `rogue_validated_profile`). MCCS, correlation, family
  hints, and external candidates never authorize writes (resolution doc).
- Equal-authority incompatible authorized methods fail closed (same).
- Alien Probe phases: Quick Auto Probe, Extended Auto Probe, Guided Discovery,
  Experimental Validation (`83ddb44:docs/alien-probe.md`,
  `a097712:docs/product-architecture.md`).
- Characterization APIs stay technical (`rss_ddc_probe_*`,
  `rss_ddc_characterization_*`); “Alien Probe” is product terminology (same).
- Profile packs (`schemaVersion` 1) are a narrower persistable mapping store,
  not the full knowledge document (`docs/profile-packs.md`).
- Observation / advertisement / stable GET do not become validated profile
  data. Writable profile controls require non-research
  `hardware-validated` data (`docs/profile-resolution.md`; still true in
  current `profile_store.c`).
- Canonical dotted semantic IDs: `display.brightness`, `display.contrast`,
  `display.color_preset`, `display.rgb.*`, `display.picture_mode`,
  `vendor.unknown.vcp.XX` (registry at
  `38cf0b1:src/core/monitor_knowledge.c`; probe on both historical and current
  branches).
- Implemented input semantic ID is `inputs.switching` (resolution tests and
  `monitor-knowledge-resolution.md`). Schema prose also uses `input.current`
  as the same concept.

## Superseded or reconstruction-local decisions

- Treating v0.1 as “proposal only / unused JSON vocabulary” on current main
  is **incorrect** for post-`c7f6834` history. The reconstruction docs
  described a smaller slice; they did not repeal v0.1.
- Tagging live probe facts as `RSS_DDC_PROFILE_SOURCE_RESEARCH` is a
  reconstruction artifact (`src/core/probe.c` `add_knowledge_fact`). Historical
  probe JSON used evidence types `stable_get` / `extended_discovery` /
  `mccs_advertised` with confidence `observed` and validation `read_validated`
  (`feature/alien-probe-extended:src/core/probe.c`). “Observed through a probe”
  is not “research-only evidence.”
- Current single `rss_ddc_monitor_knowledge_resolve` that selects routes (and
  therefore can prefer a PROFILE method whose value is UNKNOWN over a live
  OBSERVED value) is a reconstruction subset. Historical design already
  separated method authority from value resolution.

## Partially implemented on this branch

| Historical piece | Current subset |
| --- | --- |
| v0.1 document (identity, capabilities, methods, values, routes, relationships, evidence records) | `RSSDDCKnowledgeRoute` bag, max 128 facts, no JSON, no identity inside knowledge |
| `resolve_capability` / `resolve_value` / `resolve_range` / `resolve_input_route` | one route resolver |
| `RSSDDCEvidenceType`, `RSSDDCRisk`, `RSSDDCValidation`, `RSSDDCConfidence` | folded into `RSSDDCKnowledgeProvenance` + `RSSDDCProfileSource` / `RSSDDCProfileConfidence` |
| Probe → v0.1 document including identity | Probe → route facts + separate `RSSDDCDisplay` / diagnostics |
| Semantic registry lookup | Quick Probe hardcoded six IDs; Extended `vendor.unknown.vcp.XX` |
| Input routes as structured connector/port records | not present; input is a knowledge route or profile control |
| Relationships / condition groups | not present (historically stored, not evaluated) |

## Intentionally deferred (do not implement in characterization Slice 1)

- v0.1 JSON parser/serializer restoration
- Relationships, condition evaluation, structured input-route graph
- Guided Discovery and Experimental Validation (not production
  characterization; future interactive / write-validation workflows)
- Restoring the full pre-reconstruction C knowledge object in the first
  characterization slices
- Automatic profile generation from observations
- Public stable display fingerprint / UUID

## Four open questions

### Q1 — C model ↔ v0.1

Same semantic contract. Current C is the native subset to populate now.
Future serialization, if restored, must emit `monitor-knowledge/v0.1`, not a
`CharacterizationResult` schema. Fields the current C cannot represent:
identity-in-document, capability availability/conditions, method risk,
per-value validation, advertised/observed/validated ranges, input routes,
relationships, typed raw aliases, evidence records with timestamps/scope.

### Q2 — Method authority vs current value

Historical design **already separated** them. Preserve that design.
Characterization must not treat `preferred_read->value` as current state when
that route is a profile fact. Full `resolve_value` restoration is deferred;
orchestration must answer the two questions separately until then.

### Q3 — Semantic IDs

Authoritative vocabulary for characterization:

| Canonical ID | Notes |
| --- | --- |
| `display.brightness` | registry + Quick Probe |
| `display.contrast` | registry + Quick Probe |
| `display.color_preset` | registry + Quick Probe |
| `display.rgb.red_gain` / `green_gain` / `blue_gain` | registry + Quick Probe |
| `display.picture_mode` | schema / semantic-controls |
| `inputs.switching` | implemented resolution ID |
| `vendor.unknown.vcp.XX` | Extended unknown addresses |

Aliases / drift (do not bulk-rename code in this task):

| Drift ID | Maps to |
| --- | --- |
| profile pack `brightness` | `display.brightness` |
| profile pack `contrast` / `color-preset` / `picture-mode` / `input` | dotted / `inputs.switching` |
| schema prose `input.current` / `inputs.current` | `inputs.switching` |

### Q4 — Provenance

Historical axes: evidence **type**, evidence **source_id**, **confidence**,
**validation**, method **risk**. Production Quick/Extended observations are
`stable_get` or `extended_discovery` (type), confidence `observed`, validation
`read_validated`, risk `read_standard` or `read_extended`. They are not
research-source facts. Do not change enums in this docs task; characterization
must interpret current RESEARCH-tagged probe facts as observed production
evidence, not research-only knowledge.

## Extended Probe

Historical production characterization (`32bfd82` alien-probe.md): Extended
records protocol-valid addresses **in** the v0.1 document as
`vendor.unknown.vcp.xx` with `read_extended` and `writable:false`, plus a
separate diagnostic inventory. That is promotion into canonical knowledge, not
diagnostics-only.

Current reconstruction cannot dump 256×2 facts into a 128-route object.

Architectural rule for upcoming slices:

1. **Durable intent (historical A):** protocol-valid Extended results belong
   in canonical knowledge as unknown-vendor capabilities, never as write
   authority.
2. **Reconstruction accommodation (B):** keep the full 256-address log in
   `RSSDDCProbeExtendedDiagnostics`. Promote into current knowledge with
   priority: known semantics, advertised, profile-known, then remaining
   protocol-valid unknowns until the 128 bound. Unpromoted valids stay
   diagnostic.
3. Restoring full A requires restoring v0.1 document capacity/serialization
   (deferred).

Guided Discovery and Experimental Validation remain **out** of production
characterization.

## Profile / knowledge boundary

Monitor knowledge (v0.1 / reconstructed routes) is the characterization
result. Profile packs are a narrower validated mapping store used as an
evidence source and as an optional gated update target.

Cannot become validated profile data: MCCS advertisement, stable GET, variable
observation, unknown semantics, research facts.

Can contribute to knowledge immediately: all of the above as DECLARED/OBSERVED
facts with `write_authorized=false`.

Profile write authorization remains pack policy: writable +
`hardware-validated` + non-research. Hardcoded LG Picture Mode / LG_ALT gates
remain execution policy for those APIs.
