# Monitor characterization architecture

This document defines characterization as the **process** that collects evidence,
enriches the existing monitor-knowledge model, merges competing facts, and
resolves effective read/write methods. It does not introduce a second knowledge
schema and does not implement the engine.

Git/source on `feature/monitor-characterization` is authoritative for *what
is implemented now*. Historical branches remain authoritative for *what was
designed and previously implemented*. See
[monitor knowledge architecture reconciliation](monitor-knowledge-architecture-reconciliation.md).

**Canonical durable contract:** `monitor-knowledge/v0.1` (implemented on
`feature/monitor-knowledge-core` as a C document with JSON parse/serialize;
`#define RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA "monitor-knowledge/v0.1"`).

**Current native subset:** reconstructed `RSSDDCMonitorKnowledge` route facts
in `include/rss_ddc.h`, plus `RSSDDCDisplay` / optional EDID snapshots. This
is a recovery-scope subset (`3f922f9`), not a competing schema and not a
repeal of v0.1.

Characterization populates the current C subset now. A future serializer, if
restored, must emit v0.1, not a new `CharacterizationResult` type.

## 1. Purpose

Characterization answers, for one selected display:

- what display is this?
- how is it connected?
- which provider/transport paths are available?
- what is advertised vs observed?
- what known profile knowledge exists?
- which controls resolve to known semantics?
- how confident are those conclusions?
- which operations are readable, writable, validated/authorized, unsafe,
  unresolved, or experimental?
- what evidence supports every conclusion?

It is the shared monitor-understanding layer for:

1. rss-ddc itself
2. Rogue Display Control
3. the commercial Windows/macOS Stream Deck plugin on macOS

Those products consume the same resolved knowledge. They must not each invent
monitor-capability discovery.

## 2. Relationship to monitor-knowledge/v0.1

Determination **B** (see reconciliation doc): v0.1 is the durable contract;
current `RSSDDCMonitorKnowledge` is the bounded native implementation of a
subset.

```text
native C representation  ↔  deterministic monitor-knowledge/v0.1 JSON
```

| Layer | What it is | Status on this branch |
| --- | --- | --- |
| Characterization | Process / orchestrator (historical Alien Probe Quick+Extended subset) | Slice 1 orchestration core implemented internally; pipeline stages deferred |
| `monitor-knowledge/v0.1` | Canonical durable document: identity, capabilities, methods, values, input routes, relationships, evidence | Implemented historically (`38cf0b1`); **not present** on current main; serializer deferred |
| Current `RSSDDCMonitorKnowledge` | Reconstructed subset: copied `RSSDDCKnowledgeRoute` facts, max 128 | Implemented (`src/core/monitor_knowledge.c`) |
| `RSSDDCDisplay` / `RSSDDCEDIDInfo` / `RSSDDCProfileIdentity` | Identity and connection evidence (historically also inside the v0.1 document) | Implemented beside knowledge |
| Profile packs `schemaVersion` 1 | Narrower persistable mappings; evidence source / gated update target | Implemented; distinct from v0.1 |

Characterization **populates, merges, and resolves** the current C subset.
It does not invent a parallel result schema. Restoring v0.1 JSON is an
explicit later knowledge-model task, not a characterization Slice 1 task.

## 3. Terminology

| Term | Meaning in this repository |
| --- | --- |
| Fact / route | One `RSSDDCKnowledgeRoute`: a semantic ID plus one access path and provenance |
| Observation | A live probe result (`RSSDDCProbeObservation`); not automatically a knowledge fact |
| Declared | MCCS-advertised fact (`RSS_DDC_KNOWLEDGE_FACT_DECLARED`) |
| Profile fact | Copied `RSSDDCProfileControl` (`RSS_DDC_KNOWLEDGE_FACT_PROFILE`) |
| Observed fact | Live GET evidence (`RSS_DDC_KNOWLEDGE_FACT_OBSERVED`) |
| Resolution | Heap-owned view of candidates for **one** semantic ID; preferred read and write are independent |
| Write authorized | Metadata on a selected writable route; never permission to issue a SET |
| Local profile | Narrow persistable `RSSDDCProfileStore` record (`schemaVersion` 1 JSON packs) |
| Characterization | The pipeline that produces merged knowledge for one live display |
| Runtime pipeline state | Transient orchestrator object; not persisted; not a schema |
| Product-relevant control | A semantic control a production consumer actually exposes (see §15) |

Source terminology is used as-is: `write_authorized`, `RSSDDCProfileConfidence`,
`RSSDDCProbeResultCategory`, `RSS_DDC_CAP_*`. Do not invent aliases such as
`MonitorInputProfile` — that type does not exist. Input persistence is
`RSSDDCProfileControl` with `id = RSS_DDC_PROFILE_CONTROL_INPUT`.

## 4. Existing building blocks

Classification: **A** reuse unchanged, **B** extend, **C** wrap/orchestrate,
**D** keep separate, **E** replace later.

| File | Symbol | Purpose | Inputs | Outputs | Class |
| --- | --- | --- | --- | --- | --- |
| `include/rss_ddc.h`, `src/core/rss_ddc.c`, `src/platform/macos/discovery.m` | `rss_ddc_list_displays`, `rss_ddc_get_display[_with_diagnostics]` | Enumerate / snapshot one display | list index | `RSSDDCDisplay` | A / C |
| `include/rss_ddc.h` | `RSSDDCDisplay` | Process-local snapshot: index, CG id, online, external, product, manufacturer, serial, branch, transport, provider, capability bits | discovery | value struct | A |
| `src/ddc/edid.c` | `rss_ddc_read_edid`, `rss_ddc_parse_edid` | Provider EDID acquisition and portable identity decode | list index / bytes | `RSSDDCEDID`, `RSSDDCEDIDInfo` | A / C |
| `src/core/provider.c` | `rss_ddc_provider_capabilities`, `rss_ddc_provider_backend` | Independently validated provider capability bits | `RSSDDCProvider` | `uint32_t` flags | A |
| `src/core/picture_mode.c` | `rss_ddc_picture_mode_profile_capabilities` | Exact-gate Picture Mode bit OR'd at discovery | `RSSDDCDisplay` | `RSS_DDC_CAP_PICTURE_MODE` | D (hardcoded gate; do not replace) |
| `src/ddc/input_switch.c` | `rss_ddc_validate_lg_alt_input_target` | Exact-gate LG alternate input | provider, product, transport | error | D |
| `src/core/mccs_capabilities.c` | `rss_ddc_parse_mccs_capabilities`, `rss_ddc_mccs_capabilities_has_vcp`, `rss_ddc_mccs_capabilities_enum_values` | Pure MCCS parse | bytes | `RSSDDCMCCSCapabilities` | A |
| `src/core/mccs_retrieval.c` | `rss_ddc_get_mccs_capabilities` | DCPDP13-only F3/E3 retrieval | list index | parsed model or error | A / C |
| `src/core/rss_ddc.c` | `rss_ddc_get_vcp`, `rss_ddc_set_vcp` | Strict GET / write-only SET | index, VCP | `RSSDDCVCPResult` / error | A (GET used by probe; SET not used by characterization) |
| `src/core/probe.c` | `rss_ddc_probe_quick`, `rss_ddc_probe_extended` | Bounded read-only observation | `RSSDDCProbeTarget` + GET transport | observations + probe-owned knowledge | C |
| `src/core/probe.c` | `rss_ddc_probe_knowledge` | Probe-owned knowledge (observed + declared only) | probe | borrowed `RSSDDCMonitorKnowledge` | C |
| `src/core/profile_store.c` | `rss_ddc_profile_store_*`, `rss_ddc_profile_store_resolve` | Offline JSON packs and deterministic match | identity | `RSSDDCEffectiveProfile` | A / C |
| `src/core/profile_store.c` | `rss_ddc_profile_identity_from_display` | Copy snapshot → persistable identity | `RSSDDCDisplay` | `RSSDDCProfileIdentity` | A |
| `src/core/monitor_knowledge.c` | `rss_ddc_monitor_knowledge_add_route`, `_add_profile_control`, `_merge`, `_resolve` | Fact store, lossless merge, independent read/write resolution | routes / sources / semantic ID | knowledge / resolution | A |
| `src/core/verify.c` | Set-and-Verify | Write orchestration | SET+GET | verified result | D (not characterization) |
| `cli/presentation/*` | probe/list renderers | CLI presentation of snapshots and probe diagnostics | diagnostics | text/table | B later (characterization view) |
| `research/*` | IOAV/IODP labs | Research instruments | hardware | lab output | D never in production characterization |

### Duplicated concepts (keep both; do not collapse)

- **Identity gates:** hardcoded LG Picture Mode / LG_ALT product+provider+transport checks vs JSON `RSSDDCProfileStore` matching. Characterization treats both as evidence sources. It does not delete the hardcoded production gates.
- **Semantic IDs:** probe uses `display.brightness`; profile packs use `brightness`; knowledge tests use `inputs.switching`; the v0.1 proposal uses `input.current`. Characterization defines one canonical mapping table (§5) without renaming existing APIs.
- **Capability bits vs knowledge routes:** `RSS_DDC_CAP_*` describe **transport** availability. Knowledge routes describe **control** facts. Do not encode GET/SET availability as fake VCP routes.
- **Current value vs preferred read method:** resolution selects methods by authority. Live current values live on `OBSERVED` facts. Do not treat `preferred_read->value` as current state when that route is a profile fact with `UNKNOWN` value.
- **Probe knowledge vs merged characterization knowledge:** `rss_ddc_probe_quick` / `_extended` build a **fresh** knowledge object from this run's observations and MCCS declarations. They do not merge profile facts. Characterization must merge.

## 5. Schema coverage map

Map required characterization concepts onto **existing** types. The v0.1 JSON
names in parentheses are conceptual only.

### IDENTITY

| Concept | Existing home |
| --- | --- |
| Manufacturer | `RSSDDCDisplay.manufacturer` (currently often empty; see gap G1); `RSSDDCEDIDInfo.manufacturer_id` |
| Model / product | `RSSDDCDisplay.product_name`; `RSSDDCEDIDInfo.monitor_name`, `product_code` |
| Serial | `RSSDDCDisplay.serial` (currently often empty); `RSSDDCEDIDInfo.serial_number`, `serial_text` |
| EDID-derived identity | `RSSDDCEDID` + `RSSDDCEDIDInfo` |
| Stable display identifier | **None public.** `list_index` is ephemeral. ColorSync UUID exists only inside private `RSSMacOSDisplayIdentity` for Set-and-Verify. Do not promote registry IDs. |

Identity is **not** stored inside `RSSDDCMonitorKnowledge`. Runtime pipeline
state holds the snapshot and optional EDID beside knowledge.

### CONNECTION / PLATFORM

| Concept | Existing home |
| --- | --- |
| Provider | `RSSDDCDisplay.provider` / `rss_ddc_provider_string` |
| Transport / service role | `RSSDDCDisplay.transport` (EPIC `role`, e.g. `DCPEXT0`) |
| Branch | `RSSDDCDisplay.branch_device_id` |
| Online / external | `RSSDDCDisplay.online`, `.external` |
| Backend policy | `rss_ddc_provider_backend` |

### TRANSPORT CAPABILITY

Represented as `RSSDDCDisplay.capabilities` bits from
`rss_ddc_provider_capabilities` plus the Picture Mode exact-gate OR:

| Need | Bit / rule |
| --- | --- |
| Get VCP | `RSS_DDC_CAP_GET_VCP` |
| Set VCP | `RSS_DDC_CAP_SET_VCP` |
| MCCS retrieval | `RSS_DDC_CAP_MCCS_CAPABILITIES` (DCPDP13 only) |
| DPCD | `RSS_DDC_CAP_READ_DPCD` |
| EDID | `RSS_DDC_CAP_READ_EDID` (PS190 only today) |
| LG alternate input mechanism | `RSS_DDC_CAP_ALTERNATE_INPUT` (provider can issue it; exact target gate is separate) |
| Picture Mode semantic write | `RSS_DDC_CAP_PICTURE_MODE` |
| Unsupported | bit clear for that provider |
| Unknown | `RSS_DDC_PROVIDER_UNKNOWN` / `RSS_DDC_CAP_NONE` |
| Unverified | no separate enum; MCDP is fail-closed `CAP_NONE` |

There is no “unverified” flag to add. Absent bit = unsupported for production.

### ADVERTISED CAPABILITY

| Concept | Existing home |
| --- | --- |
| MCCS-advertised VCPs | `RSSDDCMCCSCapabilities.features` |
| Enumerated values | `enum_values` slice per feature |
| Advertised ranges | **Not modeled.** MCCS parser stores raw enum bytes, not min/max ranges. GET `maximum_value` is protocol-reported, not an advertised range. |

Advertisements become `DECLARED` knowledge routes (`write_authorized=false`,
`value.state=UNKNOWN`) as probe already does.

### OBSERVED CAPABILITY

| Probe category | Knowledge effect |
| --- | --- |
| `stable` | `OBSERVED` fact, `evidence_id=stable-get`, readable, not write-authorized |
| `variable` | `OBSERVED` fact, `evidence_id=variable-get`, readable, not write-authorized |
| `protocol-reported` (`REPLY_STATUS`) | retained in probe diagnostics; **not** currently a knowledge route |
| `malformed` | diagnostics only |
| `semantic-mismatch` | diagnostics only |
| `transport-error` | diagnostics only |
| `unattempted` | diagnostics only |
| `current > maximum` | preserved on the OBSERVED route via `reported_maximum` |

Protocol-reported unsupported is **not** inferred from a single GET failure
(Alien Probe rule). Characterization must not invent `UNSUPPORTED` knowledge
from one error.

### KNOWN PROFILE KNOWLEDGE

| Concept | Existing home |
| --- | --- |
| Profile matched | `rss_ddc_profile_store_resolve` success vs `NOT_FOUND` / `PROFILE_CONFLICT` |
| Match strength | internal specificity score (required fields + optional manufacturer/serial/branch) |
| Profile-derived semantics / methods | `RSSDDCEffectiveProfile.controls` copied via `rss_ddc_monitor_knowledge_add_profile_control` |
| Provider/transport-specific knowledge | profile identity predicates + hardcoded LG gates |

### RESOLVED CONTROL

`rss_ddc_monitor_knowledge_resolve(sources, n, semantic_id, &resolution)`:

| Need | Existing field |
| --- | --- |
| Semantic capability id | `RSSDDCKnowledgeRoute.semantic_id` |
| Mechanism / method | `kind`, `address`, `transport_family`, `command_semantics` |
| Current observed state | OBSERVED route `value` (see §6 G3 — do not use profile UNKNOWN value) |
| Allowed values / range | profile `enum_values`; MCCS enum bytes; GET `reported_maximum` is **not** a write range |
| Read mechanism | `preferred_read` |
| Write mechanism | `preferred_write` |
| Evidence | `provenance` on every candidate |
| Confidence | `provenance.confidence` |
| Write authorization | `write_authorized` on selected write route |
| Competing methods | `candidate_at`; `CONFLICT` clears preferred routes |

Canonical semantic IDs for characterization orchestration (caller-supplied
strings to `add_profile_control` / `resolve`; existing APIs unchanged).
Authority is the historical semantic registry and resolution tests
(`38cf0b1:src/core/monitor_knowledge.c`,
`feature/monitor-knowledge-core:docs/semantic-controls.md`):

| Profile control id | Canonical semantic ID | Typical method |
| --- | --- | --- |
| `brightness` | `display.brightness` | VCP `0x10` |
| `contrast` | `display.contrast` | VCP `0x12` |
| `color-preset` | `display.color_preset` | VCP `0x14` |
| `picture-mode` | `display.picture_mode` | VCP `0x15` |
| `input` | `inputs.switching` | VCP `0x60` and/or `LG_ALT_INPUT` |
| `gamma` / `sharpness` / others | `display.<profile-name>` | profile address |
| unknown Extended VCP | `vendor.unknown.vcp.XX` | observed only |

Schema prose also uses `input.current` / `inputs.current` for the same input
capability. Characterization uses the **implemented** ID `inputs.switching`.
Do not bulk-rename existing tests or profile pack JSON in characterization
slices; map at the orchestration boundary.

### EVIDENCE / PROVENANCE

`RSSDDCKnowledgeProvenance`: `source_id`, `source` (`builtin` / `validated-pack`
/ `local` / `research`), `confidence`, `fact_kind`, `evidence_id`.

| Evidence class | How it is represented today |
| --- | --- |
| Identity / connection | `RSSDDCDisplay` (not a knowledge provenance record) |
| EDID | `RSSDDCEDIDInfo` beside knowledge |
| Provider / transport | display snapshot + capability bits |
| MCCS advertisement | `DECLARED` + `source_id=mccs-capabilities` (probe currently tags `source=RESEARCH`; historically evidence type `mccs_advertised`) |
| Known profile | `PROFILE` + profile source class |
| Quick / Extended Probe | `OBSERVED` + `source_id=alien-probe-live-read` (probe currently tags `source=RESEARCH`; historically `stable_get` / `extended_discovery`, confidence `observed`, validation `read_validated`) |
| Hardware-validated known behavior | profile `HARDWARE_VALIDATED` and/or hardcoded production gates |
| Local/manual profile | `RSS_DDC_PROFILE_SOURCE_LOCAL` |

Probe diagnostics remain the lossless observation log. Knowledge facts are the
normalized subset.

### WRITE AUTHORITY / SAFETY

Existing flags, not a new enum:

| Product language | Existing representation |
| --- | --- |
| Unknown | no route, or `confidence=UNKNOWN` |
| Observable / readable | OBSERVED `readable=true` |
| Writable candidate | `writable=true` and `write_authorized=false` |
| Validated writable | selected write route with `write_authorized=true` |
| Do-not-write / unsafe | `write_authorized=false`; `RSS_DDC_ERROR_PROFILE_UNSAFE` at pack load; research source cannot authorize |
| Unresolved | `RSS_DDC_KNOWLEDGE_RESOLUTION_CONFLICT` |
| Experimental | `source=RESEARCH` |

Profile load already rejects writable controls unless
`confidence==HARDWARE_VALIDATED` and `source!=RESEARCH`. Resolver
`write_authorized` only reports the selected route's metadata.

## 6. Schema gaps

No profile-pack `schemaVersion` bump. Current C knowledge has no schema
version field because reconstruction omitted the v0.1 document. Restoring
`RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA "monitor-knowledge/v0.1"` is deferred
knowledge-model work, not a characterization convenience extension.

Fields the current C subset cannot represent (historically present at
`38cf0b1`): identity-in-document, capability availability/conditions, method
risk, per-value validation, advertised/observed/validated ranges, input
routes, relationships, typed raw aliases, timestamped evidence records. Do
not reintroduce them in Slice 1.

### G1. Display manufacturer/serial are usually empty

`RSSDDCDisplay` has the fields, and diagnostics print them, but discovery
currently fills `product_name` from `DisplayAttributes.ProductAttributes.ProductName`
and does **not** copy manufacturer/serial. EDID parse holds those facts when
EDID is available (PS190 only).

**Smallest fix:** keep both sources in runtime state. Do not mutate
`RSSDDCDisplay` as a side effect of EDID. Profile match continues to use
`rss_ddc_profile_identity_from_display` (builtin LG profile does not require
manufacturer/serial). Optional later: copy ProductAttributes manufacturer/serial
into the snapshot if those keys exist — that is identity assembly, not a
knowledge-schema change.

### G2. No public stable display identifier

`list_index` is process-local. Architecture.md already defers stable IDs.
Characterization keys a run by the current list index plus the copied snapshot.
It does not invent a fingerprint (architecture.md rejected SHA-256 for now).

### G3. Resolution selects methods, not current values

This is a **reconstruction gap**, not missing historical policy.
`feature/monitor-knowledge-core:docs/monitor-knowledge-resolution.md` already
separated retained knowledge, effective **method** selection, and independent
value / range / input-route resolvers (`rss_ddc_monitor_knowledge_resolve_value`,
`_resolve_range`). Historical Quick Probe stored current as value id
`observed` with `validation: read_validated`, separate from methods
(`feature/alien-probe-extended:src/core/probe.c`).

Until that resolver is restored, a hardware-validated PROFILE route can
outrank a live OBSERVED GET for `preferred_read` even when the profile value
is `UNKNOWN`. Characterization must still publish:

1. effective read **method** from the current route resolver
2. current **value** from the highest-quality OBSERVED fact (else UNKNOWN)

Do not restore `resolve_value` in Slice 1. Orchestration answers the two
questions separately.

### G4. Knowledge bound vs Extended Probe cardinality

`RSSDDCMonitorKnowledge` retains at most **128** facts.
Extended Probe can theoretically emit up to 256 OBSERVED + 256 DECLARED facts.

**Historical intent (`32bfd82` alien-probe.md):** protocol-valid Extended
addresses belong **in** canonical knowledge as `vendor.unknown.vcp.xx` with
`read_extended` and `writable:false`, plus a separate diagnostic inventory.

**Reconstruction accommodation:** keep the full 256-address log in
`RSSDDCProbeExtendedDiagnostics`. Promote into current knowledge with
priority: known semantics, advertised, profile-known, then remaining
protocol-valid unknowns until the 128 bound. Unpromoted valids stay
diagnostic. Restoring full historical promotion requires v0.1 document
capacity (deferred).

### G5. Relationships / structured input routes / availability conditions

These were **implemented** in historical v0.1 C (`38cf0b1`), not merely
proposed. Condition groups were stored but not evaluated. They are **not**
required to ship characterization of brightness, contrast, input, and picture
mode on the reconstructed subset.

**Do not reintroduce them in characterization slices.** They remain deferred
knowledge-model restoration, not a new invention.

### G6. Semantic ID drift

Documented in §5. Historical registry + `inputs.switching` are authoritative.
Map profile pack short names at the orchestration boundary. No bulk rename.
No schema bump.

### G7. Probe tags live reads as `RESEARCH`

Reconstruction artifact. Historical probe JSON used evidence types
`stable_get` / `extended_discovery` / `mccs_advertised`, not research source
(`feature/alien-probe-extended:src/core/probe.c`). “Observed through a probe”
must not be conflated with “research-only evidence.”

Until probe enums are corrected (later knowledge/probe slice, not
characterization Slice 1), orchestration treats those RESEARCH-tagged OBSERVED
facts as production observations: they never authorize writes, and they remain
the source of current value (G3).

### Gaps that are not schema gaps

- MCCS ranges: parser is evidence-only by design.
- `reported_maximum` is not a write range: already documented.
- No knowledge JSON serialization on this branch: reconstruction Slice 6
  (`docs/monitor-knowledge.md`) deferred it; restoring v0.1 JSON remains
  deferred.

## 7. Locked pipeline

```text
IDENTITY
   ↓
PROFILE MATCH + TRANSPORT CAPABILITIES
   ↓
PASSIVE RETRIEVAL
   ↓
PRELIMINARY MERGE
   ↓
QUICK PROBE
   ↓
MERGE + RESOLVE
   ↓
need more evidence?
   ├─ no
   └─ yes → EXTENDED PROBE → MERGE + RESOLVE
   ↓
CHARACTERIZATION
   ↓
optional profile update
```

Dependency rule: **no stage may require information that is only generated by
a later mandatory stage.** Later evidence may re-resolve; it must not rewrite
or delete earlier raw facts.

This is a DAG, not a script. See §9.

## 8. Stage table

Legend: I/O = hardware I/O; RO = read-only; Mut = can change monitor OSD/state.

| Stage | Mandatory | I/O | RO | Mut | Adds / resolves |
| --- | --- | --- | --- | --- | --- |
| A Identity | yes | registry/CG snapshot; EDID only if `CAP_READ_EDID` | yes | no | evidence |
| B Profile match | yes (store may be empty) | no | — | no | evidence |
| C Transport capabilities | yes | no (uses snapshot bits) | — | no | evidence |
| D Passive retrieval | no (skip if unsupported) | MCCS GET if capable | yes | no | evidence |
| E Preliminary merge | yes | no | — | no | merge |
| F Quick Probe | default/deep yes; passive-only no | GET ×12 (+ optional MCCS if not already loaded) | yes | no | evidence |
| G Merge + resolve | yes after F | no | — | no | merge + resolve |
| H Extended decision | yes | no | — | no | policy |
| I Extended Probe | optional | paced GET 0x00–0xFF | yes | no | evidence |
| J Final merge + resolve | yes if I ran; else G is final | no | — | no | merge + resolve |
| K Finalization | yes | no | — | no | resolve / snapshot |
| L Optional profile update | no | filesystem only | — | no monitor | persist **validated** subset |

Characterization never calls `rss_ddc_set_vcp`, `rss_ddc_set_input`,
`rss_ddc_set_picture_mode`, or Set-and-Verify.

## 9. Dependency DAG

```text
identity
   ├──────────────► profile match
   │
   └──────────────► transport capability resolution
                          │
                          ▼
                   passive retrieval
                          │
          profile ────────┤
                          ▼
                 preliminary merge
                          │
                          ▼
                     quick probe
                          │
                          ▼
                   merge + resolve
                          │
                          ▼
                characterization sufficient?
                   │                  │
                  yes                 no
                   │                  │
                   │            extended probe
                   │                  │
                   │            merge + resolve
                   │                  │
                   └──────────┬───────┘
                              ▼
                    characterization finalization
                              │
                              ▼
                     optional profile update
```

**Parallel once identity exists:** profile match and transport capability
assembly. Transport bits do not need the profile store. Picture Mode's
capability bit is already OR'd at discovery from the hardcoded gate; profile
store picture-mode controls are additional facts, not a prerequisite for that
bit.

**Passive retrieval** requires transport resolution (`CAP_MCCS_CAPABILITIES`)
and a selected display. It must not require Quick or Extended Probe.

**Quick Probe** requires identity + a GET-capable transport. Profile knowledge
is optional borrowed context (`RSSDDCProbeTarget.profile_knowledge`) used only
to label `profile_known`. Quick Probe must not require Extended Probe.

**Extended Probe** requires the same GET-capable validated providers as today
(PS190, DCPDP13, DCPDPService). It remains optional.

### Ordering properties

- Identity before profile matching.
- Profile matching must not require probe output.
- Transport capability determination must not require Extended Probe.
- Passive MCCS may require provider/transport resolution.
- Profile knowledge may influence interpretation labels and later resolution.
- Profile knowledge must not suppress independent observation unless an
  explicit safety rule requires skipping a write — characterization issues
  **no writes**, so observation always proceeds when GET is available.
- Later evidence re-resolves; merge retains earlier facts.
- Profile update consumes resolved/validated knowledge only.

### Hazards vs current code

| Hazard | What the code does today | Smallest clean fix |
| --- | --- | --- |
| H1. Quick/Extended Probe each call `load_mccs` | Duplicate MCCS I/O if characterization already retrieved MCCS | Early slices accept duplicate I/O. Later: inject already-parsed MCCS or skip `load_mccs` when present. |
| H2. Probe convenience APIs ignore profiles | `rss_ddc_probe_quick_for_display` does not load a store or set `profile_knowledge` | Orchestrator uses `rss_ddc_probe_create` with borrowed profile knowledge. |
| H3. Probe knowledge overwrites, does not merge | `rss_ddc_probe_quick` destroys prior probe knowledge and builds observation-only facts | Orchestrator merges probe knowledge with profile knowledge via `rss_ddc_monitor_knowledge_merge`. |
| H4. 128-fact overflow on Extended | `add_route` returns `PROFILE_CONFLICT` at 128 | G4: diagnostics keep all 256; promote with priority until bound |
| H5. Semantic ID mismatch | profile `brightness` vs probe `display.brightness` | Canonical mapping table at add/resolve time. |
| H6. Manufacturer/serial empty | profile optional predicates never match live snapshots | G1; do not block characterization. |
| H7. Hidden EDID prerequisite | identity “completeness” must not require EDID (unsupported on DCPDP13) | EDID is optional enrichment. |
| H8. Circular dependency risk | none found if Extended stays optional and profile match does not wait for probe | Keep that split. |
| H9. Hardcoded LG gates vs profile store | two identity systems | Compose: both may contribute facts; production write APIs keep their gates. |

No circular dependencies in the DAG above.

## 10. Stage prerequisites and outputs

### A. IDENTITY

- **In:** current `list_index`.
- **Out:** `RSSDDCDisplay`; optional `RSSDDCEDID`/`RSSDDCEDIDInfo` if
  `CAP_READ_EDID`; correlation diagnostics if requested.
- **Knowledge fields:** none (identity lives beside knowledge).
- **Existing:** `rss_ddc_get_display[_with_diagnostics]`, `rss_ddc_read_edid`,
  `rss_ddc_parse_edid`.
- **Recoverable:** EDID unsupported/malformed → continue without EDID.
- **Fatal:** display not found, discovery failure, safety-gate failure that
  prevents even a snapshot.
- **Re-resolve:** snapshot is for this process invocation; a later unplug is a
  new run, not an in-place rewrite.

### B. PROFILE MATCH

- **In:** `RSSDDCProfileIdentity` from the snapshot; borrowed `RSSDDCProfileStore`
  (may be empty or builtin-only).
- **Out:** `RSSDDCEffectiveProfile` or `NOT_FOUND` / `PROFILE_CONFLICT`; match
  present/absent recorded in runtime state.
- **Knowledge:** profile controls copied as `PROFILE` facts using the canonical
  semantic IDs.
- **Existing:** `rss_ddc_profile_identity_from_display`,
  `rss_ddc_profile_store_resolve`, `rss_ddc_monitor_knowledge_add_profile_control`.
- **Recoverable:** missing store, `NOT_FOUND`, malformed file (warn, continue).
- **Blocks:** `PROFILE_CONFLICT` at equal-authority controls fails closed for
  **that control** (do not copy the conflicting control). Other controls may
  still copy. Ambiguous whole-store conflict: skip profile facts, keep live
  observation.
- **Must not** require probe output.

### C. TRANSPORT CAPABILITIES

- **In:** `RSSDDCDisplay.provider` and `.capabilities`.
- **Out:** interpreted GET/SET/MCCS/DPCD/EDID/ALT-INPUT/PICTURE-MODE
  availability for this run.
- **Knowledge:** none required (bits stay on the snapshot). Optional later:
  diagnostic-only facts; not needed now.
- **Existing:** `rss_ddc_provider_capabilities` (already applied at discovery).
- **Fatal only if** GET is required by the selected mode and `CAP_GET_VCP` is
  absent: default/deep characterization is incomplete-but-usable identity +
  profile; skip probe stages.

### D. PASSIVE RETRIEVAL

- **In:** GET-capable DCPDP13 with `CAP_MCCS_CAPABILITIES`.
- **Out:** `RSSDDCMCCSCapabilities` or a stage-local error.
- **Knowledge:** one `DECLARED` route per advertised VCP that maps to a known
  semantic ID **or** `vendor.unknown.vcp.XX` for advertised-but-unknown codes
  **subject to the 128 bound / G4**.
- **Existing:** `rss_ddc_get_mccs_capabilities`.
- **Recoverable:** unsupported provider, malformed, incomplete, too large →
  record error, continue with advertised=unknown.
- **Does not** authorize writes.

### E. PRELIMINARY MERGE

- **In:** profile knowledge + declared MCCS knowledge.
- **Out:** merged `RSSDDCMonitorKnowledge`.
- **Existing:** `rss_ddc_monitor_knowledge_merge`.
- **No I/O.** Failure to merge at the 128 bound is fatal for this run's
  knowledge object (do not publish a partial merge). G4 exists to keep this
  from happening in normal product-relevant sets.

### F. QUICK PROBE

- **In:** exact correlation, GET transport, optional borrowed profile knowledge.
- **Out:** six observations; probe knowledge (OBSERVED + DECLARED for those
  six); diagnostics.
- **Existing:** `rss_ddc_probe_create`, `rss_ddc_probe_quick`.
- **Read set (unchanged):** `0x10,0x12,0x14,0x16,0x18,0x1a` only. No `0x60`
  sweep.
- **Recoverable:** per-control transport/protocol failures remain observations.
- **Fatal:** allocation failure; non-exact correlation.
- **Does not** depend on Extended Probe.

### G. MERGE + RESOLVE

- **In:** preliminary knowledge + Quick Probe knowledge.
- **Out:** merged knowledge; per-semantic resolutions for the product-relevant
  set.
- **Existing:** merge + `rss_ddc_monitor_knowledge_resolve`.
- **Re-resolve:** yes, when later Extended facts arrive.

### H. EXTENDED PROBE DECISION

See §15. Pure policy on current knowledge, diagnostics, mode, and
product-relevant unresolved set.

### I. EXTENDED PROBE

- **In:** same as Quick, plus validated provider (not MCDP/unknown).
- **Out:** 256 observations; abort after 8 consecutive transport failures.
- **Knowledge:** G4 subset only.
- **Existing:** `rss_ddc_probe_extended`.
- **Cancellable:** not in v1 API (synchronous). Document UI progress as a later
  product need (~tens of seconds), not an async library contract now.

### J. FINAL MERGE + RESOLVE

Same as G with Extended facts included. Earlier OBSERVED/DECLARED/PROFILE facts
remain.

### K. CHARACTERIZATION FINALIZATION

Freeze runtime accessors: display snapshot, optional EDID, merged knowledge,
stage errors/warnings, sufficiency flag, whether Extended ran/aborted.
No additional I/O.

### L. OPTIONAL PROFILE UPDATE

Default **off**. See §14. Filesystem only. Never automatic in the default API.

## 11. Evidence model

Characterization is evidence transformation:

```text
collect evidence
    ↓
normalize evidence
    ↓
populate / enrich RSSDDCMonitorKnowledge
    ↓
merge competing facts
    ↓
resolve semantics and effective methods
    ↓
evaluate confidence and write authority
    ↓
produce characterized knowledge + identity snapshot
```

**Runtime pipeline state** (transient, not a schema) may hold pointers to
MCCS bytes, probe diagnostics, stage status, and the profile store. Downstream
consumers are given:

- `const RSSDDCDisplay *`
- `const RSSDDCMonitorKnowledge *`
- optional EDID info
- optional probe diagnostics for developer views
- resolution accessors for a semantic ID

They do not receive a second document type.

Raw probe observations stay in diagnostics even when not promoted to knowledge
(G4). Failed stages retain whatever facts were already merged.

## 12. Merge / precedence rules

Reuse `rss_ddc_monitor_knowledge_merge` and `_resolve` unchanged.

Merge: identical route+provenance coalesces; everything else is retained.
Resolve: rank `confidence` then source class (`local` > validated pack >
`builtin` > `research`). Read and write selection are independent.
Equal-authority non-equivalent routes → `CONFLICT`, no preferred route.
`write_authorized` is never inferred by resolve; it is copied metadata.

Worked cases:

1. **MCCS advertises 0x10 and Quick Probe confirms it.** DECLARED + OBSERVED
   both retained. Read method can be the OBSERVED VCP `0x10` route. Write is
   **not** authorized by either fact.
2. **MCCS does not advertise 0x10; Quick Probe returns stable valid.** OBSERVED
   fact only. Still not write-authorized. Not labeled “supported.”
3. **Profile says 0x60 writable; live probe reports protocol failure.** Profile
   PROFILE fact remains. Probe diagnostics record `protocol-reported` or
   transport error; no OBSERVED fact. Write authorization still only the
   profile metadata — production write APIs keep their own gates. Live failure
   is a warning, not automatic revocation, until an explicit policy slice says
   otherwise. Prefer: do not silently clear `write_authorized` on the profile
   fact (that would mutate evidence). Surface live failure in diagnostics so
   consumers can refuse the write at use time.
4. **Profile says unsupported; live observation succeeds.** Both retained.
   OBSERVED readable fact exists. Profile UNSUPPORTED remains a candidate.
   Resolution: higher-authority profile UNSUPPORTED is **not** selected as a
   write (`value.state==UNSUPPORTED` is excluded from write selection). Read
   may still select a readable OBSERVED route if it has lower authority —
   `select_route` only considers `readable` routes. A high-authority
   unsupported profile route is not readable, so it does not steal read
   selection. Good: observation is not suppressed.
5. **Extended finds stable valid with no known semantics.** Diagnostic +
   `vendor.unknown.vcp.XX` OBSERVED fact only if G4 allows. No product control.
   No write authority.
6. **`current > max` (PS190 odd values).** Stored on OBSERVED
   `reported_maximum`; unusual flag in diagnostics. Not malformed. Not a write
   range.
7. **Multiple protocol-valid values across repeats.** `variable` OBSERVED;
   first valid current retained (Alien Probe rule). Not write-authorized.
8. **Transport error vs protocol-reported unsupported.** Distinct probe
   categories. Neither becomes `UNSUPPORTED` knowledge from a single error.
9. **Hardware-validated vendor path vs generic MCCS.** Example: LG_ALT vs VCP
   `0x60`. Independent routes (`LG_ALT_INPUT` vs `STANDARD_VCP`). Higher
   authority wins; equal-authority different methods → CONFLICT (fail closed).
10. **Multiple candidate read methods.** All retained; preferred read is
    highest authority readable route.
11. **Multiple candidate write methods.** Same, independently.
12. **Two equal-authority write methods conflict.** `CONFLICT`;
    `write_authorized` accessor is false (no selected write).
13. **Advertised enums conflict with observed values.** Both retained.
    Extended already records `current_in_declared_enum`. Do not delete either.
14. **Profile labels a raw value that observation confirms.** PROFILE enum +
    OBSERVED current. Semantic mapping comes from profile enums; current from
    OBSERVED. Confirmation raises consumer confidence; it does not by itself
    set `HARDWARE_VALIDATED`.
15. **Local validated profile vs weak generic family knowledge.** Local source
    class ranks above builtin/research. Family-only research/candidate cannot
    authorize writes (pack loader already rejects that).

Core pipeline:

```text
evidence facts → merge → interpretation → resolved capability
    → effective read method → effective write method → write authority
```

Do not mutate observations into conclusions.

## 13. Write authority / safety rules

Inspected in `profile_store.c` (pack validation +
`write_authorized = writable && HARDWARE_VALIDATED && source != RESEARCH`)
and `monitor_knowledge.c` (resolve copies metadata only).

Characterization **must** follow:

- Successful read does not imply safe write. Probe sets `writable=false`,
  `write_authorized=false` on OBSERVED/DECLARED facts.
- MCCS advertisement does not authorize writes (`DECLARED` facts).
- Stable observation does not authorize writes.
- Correlation (`RSS_DDC_PROFILE_CONFIDENCE_CORRELATED`) does not authorize
  writes (pack loader requires `HARDWARE_VALIDATED` for writable controls).
- Generic family / research knowledge does not authorize writes.
- Hardware-validated profile methods may set `write_authorized` on the
  **profile fact** when existing pack policy supports it.
- Validated local profile may authorize known values/methods under the same
  pack rules.
- Equal-authority write conflicts fail closed.
- Unknown/experimental methods remain non-authorized by default.
- Production consumers distinguish using existing fields:

  | Consumer need | Test |
  | --- | --- |
  | Observable | OBSERVED fact or protocol-valid diagnostic |
  | Readable | preferred read != NULL |
  | Writable candidate | preferred write != NULL && !write_authorized |
  | Validated writable | `rss_ddc_monitor_knowledge_resolution_write_authorized` |
  | Unsafe | no authorized write; research/experimental source |
  | Unresolved | `CONFLICT` or missing semantic |

Characterization never issues a write to “confirm” authority.

Hardcoded production gates (`rss_ddc_set_picture_mode`, `rss_ddc_set_input`
LG_ALT) remain the execution policy for those APIs even if knowledge metadata
says authorized. Knowledge does not call those APIs.

## 14. Profile match / update boundary

### Profile match

Required identity for a match (existing `match()`):

- exact `product_name`, `provider`, `transport`, `external`
- optional exact `manufacturer`, `serial`, `branch_device_id` when the profile
  specifies them

Score = 4 + number of optional predicates present. Duplicate controls compose
by higher confidence, then source rank, then specificity. Equal-authority
non-identical controls → `PROFILE_CONFLICT`.

Profile data may contribute:

- semantic IDs, methods, addresses, enum labels
- `write_authorized` metadata **only** when pack rules already set it

Profile data may **not**:

- skip Quick Probe
- erase live observations
- authorize writes from advertisement or research

Stale profile vs live failure: retain both (case 3). Consumers decide at
command time.

There is **no** `MonitorInputProfile` type. Input persistence is
`RSSDDCProfileControl` `INPUT` plus `enum_values[]`.

### Profile update (optional, default off)

Eligible for persistence only when **all** are true:

- semantic ID is known (not `vendor.unknown.vcp.XX`)
- method is known
- value mapping is hardware-validated **or** an explicit user/policy
  confirmation in a later slice
- not merely MCCS-advertised
- not a one-shot unstable observation
- not an unresolved conflict

Knowledge-only (do not persist): unknown semantics, advertised-only enums,
variable observations, research facts, protocol-valid-but-unlabeled VCPs.

Update is **policy-driven / user-approved**, never automatic in default
characterization. This design does not implement mutation.

Characterized `inputs.switching` maps to a local INPUT control only for
validated enum mappings (e.g. LG_ALT `0x90/0x91/0xd0` already in production
gates). MCCS `0x60` advertised `11 12 0f 00` on LG remains knowledge-only
(existing input-switching.md rule).

## 15. Extended Probe decision policy

Extended Probe stays optional.

**Default mode** (`RSS_DDC_CHARACTERIZE_MODE_DEFAULT`): Quick Probe after
passive retrieval. Extended only if H says yes.

**Deep mode:** always run Extended when GET is supported on a validated
provider, unless transport is known unsupported/aborted.

**Passive-only:** identity + profile + transport + MCCS. No Quick, no Extended.

**Sufficient** (do not auto-trigger Extended) when any of:

- all **product-relevant** controls that this transport can even attempt are
  resolved enough for the consumer (see list below)
- profile + MCCS + Quick Probe already give authorized or explicitly
  non-writable methods for those controls
- remaining unresolved VCPs are not product-relevant
- GET is unsupported (further probing is pointless)
- actionable controls have strong-confidence methods and live reads where
  expected

**Product-relevant set (v1):**

- `display.brightness`
- `display.contrast`
- `display.picture_mode` if profile, `CAP_PICTURE_MODE`, or MCCS advertises `0x15`
- `inputs.switching` if profile, `CAP_ALTERNATE_INPUT`, or MCCS advertises `0x60`
- `display.color_preset` if advertised or profiled

RGB gains are Quick Probe evidence, not Stream Deck-required controls.

**SHOULD trigger Extended:**

- unresolved product-relevant advertised controls
- weak/no profile match where additional observation could identify useful
  advertised VCPs
- equal-authority method conflict that extra observation might disambiguate
  (observation still will not authorize writes)
- explicit deep mode / developer request

**SHOULD NOT automatically trigger:**

- unadvertised VCP space existing
- curiosity
- protocol-valid but semantically strange VCPs (PS190 odd values)
- already sufficient characterization
- known unsupported transport / MCDP
- resolved actionable controls with strong confidence

**Cancel / progress:** not in the v1 synchronous API. Deep scans can take on
the order of tens of seconds (`25 ms` inter-address + `25 ms` repeat × 256).
Product UIs that need progress should call Extended separately later, not
force an async characterization core now. Historical Extended had a progress
callback and `--json` MonitorKnowledge on stdout; restoring that UI is a CLI
slice, not Slice 1.

## 16. Proposed API

Follow existing `rss_ddc_*` naming. Synchronous. No new knowledge schema type.

```c
typedef enum {
    RSS_DDC_CHARACTERIZE_MODE_PASSIVE = 0,
    RSS_DDC_CHARACTERIZE_MODE_DEFAULT,
    RSS_DDC_CHARACTERIZE_MODE_DEEP
} RSSDDCCharacterizeMode;

typedef struct {
    RSSDDCCharacterizeMode mode;
    bool update_profile;                 /* default false; ignored until slice 9 */
    const RSSDDCProfileStore *profiles;  /* borrowed; NULL = builtin-only or none */
} RSSDDCCharacterizeOptions;

typedef struct RSSDDCCharacterization RSSDDCCharacterization;

RSSDDCError rss_ddc_characterize_display(uint32_t list_index,
                                         const RSSDDCCharacterizeOptions *options,
                                         RSSDDCCharacterization **result);
void rss_ddc_characterization_destroy(RSSDDCCharacterization *result);

const RSSDDCDisplay *rss_ddc_characterization_display(const RSSDDCCharacterization *result);
const RSSDDCMonitorKnowledge *rss_ddc_characterization_knowledge(const RSSDDCCharacterization *result);
RSSDDCError rss_ddc_characterization_resolve(const RSSDDCCharacterization *result,
                                             const char *semantic_id,
                                             RSSDDCMonitorKnowledgeResolution **resolution);
```

Additional accessors (still not a competing schema): EDID info if present,
MCCS model if present, Quick/Extended diagnostics if that stage ran, stage
error/warning bits, sufficiency flag.

`RSSDDCCharacterization` is **runtime pipeline state**. Downstream products
consume `rss_ddc_characterization_knowledge()` plus the display snapshot
(current C subset). That return shape must stay compatible with a future
v0.1 serializer of the same facts. Do not add a second public result schema.

Do not implement this header in the design commit.

## 17. Failure semantics

Prefer degraded characterization over total failure where safe.

| Situation | Class | Behavior |
| --- | --- | --- |
| Display not found / discovery failure | fatal | no object |
| Argument / allocation failure | fatal | no object |
| EDID unavailable | stage-local | continue |
| Profile store missing | warning | match = none |
| Profile `NOT_FOUND` | warning | continue |
| Profile `CONFLICT` | stage-local | skip conflicting controls |
| MCCS unsupported | warning | advertised=unknown |
| MCCS malformed/incomplete | stage-local | retain raw error; no DECLARED facts |
| Quick Probe partial errors | incomplete-but-usable | per-control diagnostics retained |
| Extended aborted | incomplete-but-usable | attempted observations retained; rest `unattempted` |
| Provider unknown / MCDP | incomplete-but-usable | identity + transport=unsupported |
| Write methods unresolved | incomplete-but-usable | CONFLICT exposed; no preferred write |
| Conflicting knowledge | incomplete-but-usable | facts retained |

Evidence already merged is kept when a later stage fails. Do not roll back
successful Quick facts because Extended aborted.

## 18. Downstream consumer contract

```text
rss-ddc characterization
        ↓
RSSDDCMonitorKnowledge + RSSDDCDisplay (+ optional diagnostics)
        ↓
┌───────────────┬────────────────────┬─────────────────────┐
│               │                    │                     │
rss-ddc CLI     Rogue Display        Stream Deck Plugin
                Control              macOS backend
```

**CLI:** identity, advertised vs observed, resolved controls, evidence,
confidence, effective methods, developer diagnostics (probe tables already
exist).

**Rogue Display Control:** populate controls from resolved semantic IDs;
choose `preferred_read` / `preferred_write`; expose input / picture mode /
brightness only when methods exist; refuse writes unless
`write_authorized`; optional confidence UI.

**Stream Deck plugin (macOS):** expose only actionable resolved controls with
validated write authority (or read-only tiles for readable-but-not-authorized
values). Use `inputs.switching` resolution, not a plugin-local 0x60 table.
Do not reimplement MCCS/probe.

No consumer-specific capability interpretation. This task does not implement
those integrations.

## 19. Implementation sequence

Prefer no hardware until a slice explicitly needs live GET. Research labs are
not used.

### Slice 1 — Orchestration types + pure merge tests (no hardware)

- **Files:** `src/core/characterize.h`, `src/core/characterize.c`,
  `tests/test_characterize.c`
- **New:** runtime state; merge fixture PROFILE+DECLARED+OBSERVED facts;
  **do not** restore `resolve_value` here
- **Reuse:** current `rss_ddc_monitor_knowledge_*`
- **Hardware:** no
- **schemaVersion:** no change; v0.1 serializer remains deferred
- **Accept:** method resolution vs current-value are tested as two questions
  (G3); write_authorized remains false on OBSERVED/DECLARED fixtures;
  semantic IDs in fixtures use canonical dotted names / `inputs.switching`

#### Slice 1 implementation (this branch)

Internal only. There is no public `rss_ddc_characterize_display`. Identity,
profile, transport, MCCS, and probe stages are not wired.

**Orchestration state.** `RSSDDCCharacterization` owns one accumulated
`RSSDDCMonitorKnowledge`. No display snapshot, EDID, mode, JSON, or
relationship fields are stored; those remain later-slice concerns.

**Semantic ID normalization.**
`rss_ddc_characterization_normalize_semantic_id` is exact and case-sensitive.
Known aliases found in current profile packs or documented schema prose:

| Input | Canonical |
| --- | --- |
| `brightness` | `display.brightness` |
| `contrast` | `display.contrast` |
| `color-preset` | `display.color_preset` |
| `picture-mode`, `picture_mode` | `display.picture_mode` |
| `input`, `input.current`, `inputs.current` | `inputs.switching` |

Canonical IDs, including `inputs.switching` and `vendor.unknown.vcp.XX`, are
copied unchanged. Unknown IDs are copied unchanged. Empty/NULL is
`RSS_DDC_ERROR_ARGUMENT`. Normalization runs before merge and before
method/current-value lookup so `brightness` and `display.brightness` compose
as one semantic.

**Lossless composition.** `rss_ddc_characterization_add_knowledge` copies
incoming routes, normalizes their semantic IDs, then calls existing
`rss_ddc_monitor_knowledge_merge`. Competing routes and provenance are
retained. Capacity remains 128 routes. Overflow returns
`RSS_DDC_ERROR_PROFILE_CONFLICT` and leaves the accumulated object unchanged.
Facts are not dropped silently.

**Method vs current value.** `rss_ddc_characterization_resolve` is the
existing method-authority resolver after normalization. Independently,
`rss_ddc_characterization_current_value` is a bounded v1 runtime rule, not
restored `resolve_value`:

- only `FACT_OBSERVED` routes with UNSIGNED or STRING values compete
- UNKNOWN never outranks a known observation
- PROFILE values are not treated as live current state
- disagreeing observed values yield `CONFLICT` and no selected route
- agreeing observations keep the lexicographically lowest `source_id`
- the selected route retains its provenance/evidence

Example: a hardware-validated PROFILE brightness method with UNKNOWN value
can win `preferred_read`, while a live OBSERVED value `42` is the current
value.

**Known limitations.** No timestamps; freshness is evidence-class only.
DECLARED facts are retained by merge but do not supply current value in v1.
No pipeline stages, no v0.1 JSON, no provenance/risk enum changes, no
hardware.

### Slice 2 — Identity + profile match + transport assembly (no new probing)

- **Files:** `characterize.c`, tests with synthetic `RSSDDCDisplay` + pack data
- **Reuse:** `rss_ddc_profile_identity_from_display`, `profile_store_resolve`,
  `add_profile_control`, `rss_ddc_provider_capabilities`
- **Change from prior plan:** normalize profile pack short names to canonical
  semantic IDs at this boundary before any later evidence is merged
- **Hardware:** none in tests
- **Accept:** mapping table applied; `NOT_FOUND` is non-fatal; no probe

### Slice 3 — Passive MCCS → DECLARED facts

Unchanged except DECLARED facts must not be treated as research-only.

### Slice 4 — Quick Probe → merge/resolution

- **Change:** interpret RESEARCH-tagged probe facts as production `stable_get`
  observations (G7) without changing probe enums yet
- **Accept:** profile facts survive; current value from OBSERVED, not from
  preferred PROFILE method (G3); no SET callbacks

### Slice 5 — Sufficiency decision

Unchanged.

### Slice 6 — Optional Extended Probe

- **Change:** keep `RSSDDCProbeExtendedDiagnostics` as the full diagnostic
  container. Promote into knowledge per G4 (historical A + reconstruction
  bound). Unknown protocol-valid VCPs may enter knowledge as
  `vendor.unknown.vcp.XX` until the 128 bound; they never authorize writes.
- **Accept:** 128-bound never overflowed; abort path retained; diagnostics
  still contain unpromoted addresses

### Slice 7 — Public/internal characterization API

- **Change:** API returns current C runtime knowledge + snapshot, preserving
  compatibility with future v0.1 serialization. No second result type.
- **Accept:** consumer-test still excludes research objects; no schema bump

### Slice 8 — CLI characterization view

Unchanged.

### Slice 9 — Optional profile update policy

Unchanged: gated; never automatic from advertisement or stable GET.

## 20. Non-goals

- No competing characterization schema and no `CharacterizationResult` JSON
  type
- No restoring v0.1 JSON parse/serialize in characterization slices
  (deferred knowledge-model work)
- No automatic unsafe writes; characterization is read-only toward the monitor
- No mandatory Extended Probe
- No Guided Discovery or Experimental Validation in production characterization
- No research-lab behavior in production characterization
- No consumer-specific capability interpretation
- No automatic promotion of arbitrary observations into validated profiles
- No stable public display UUID / EDID fingerprint in this design
- No async/progress API in v1
- No reintroduction of relationships / condition evaluation / structured
  input-route objects in these slices
- No profile-pack `schemaVersion` change
- No modification of research labs
- No hardware experiments as part of implementing this document

## 21. Historical architecture reconciliation

Index and decision record:
[monitor knowledge architecture reconciliation](monitor-knowledge-architecture-reconciliation.md).

Summary of corrections to the first characterization pass:

- **v0.1 is not unused vocabulary.** It was implemented (`c7f6834`–`38cf0b1`)
  and consumed by historical Quick/Extended Probe. Current C is a reconstructed
  subset (`3f922f9`).
- **Semantic IDs:** use historical dotted registry IDs and implemented
  `inputs.switching`; map profile pack short names at orchestration time.
- **Evidence:** production probe observations are `stable_get` /
  `extended_discovery`, not research-only. Current RESEARCH tagging is a
  reconstruction artifact (G7).
- **Method vs current value:** historically separate resolvers. Preserve that
  split in orchestration (G3); do not restore `resolve_value` in Slice 1.
- **Extended Probe:** historically promoted protocol-valid unknowns into
  knowledge as `vendor.unknown.vcp.xx` (`writable:false`). Diagnostics remain
  the full scan. The 128-fact bound forces priority promotion until v0.1
  capacity is restored (G4).
- **Profiles:** remain a narrower validated store. Observation/MCCS/stable GET
  never auto-promote into validated profile mappings.
