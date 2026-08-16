# Monitor characterization architecture

**Normative execution and knowledge boundaries:**
[Canonical Alien Probe™ architecture](alien-probe-architecture.md).
If code and that architecture disagree, the implementation must be corrected
or the architectural conflict must be explicitly reviewed before proceeding.

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

Characterization populates the current C subset and emits
`monitor-knowledge/v0.1` JSON from discovery-only knowledge. Do not invent a
new `CharacterizationResult` type.

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
| Characterization | Process / orchestrator combining identity, profile, passive MCCS, Alien Probe™ Quick and Extended Auto Probe, and sufficiency | Slice 6 Extended Auto Probe ingest/promotion implemented internally; Guided Discovery and Experimental Validation deferred |
| `monitor-knowledge/v0.1` | Canonical durable discovery/evidence document: identity, capabilities, methods, values, empty input routes/relationships, evidence | Restored as a serialization mapping from the current route bag (`src/core/monitor_knowledge_json.c`) |
| Current `RSSDDCMonitorKnowledge` | Reconstructed subset: copied `RSSDDCKnowledgeRoute` facts, max 128 | Implemented (`src/core/monitor_knowledge.c`) |
| `RSSDDCDisplay` / `RSSDDCEDIDInfo` / `RSSDDCProfileIdentity` | Identity and connection evidence (historically also inside the v0.1 document) | Implemented beside knowledge |
| Profile packs `schemaVersion` 1 | Narrower persistable mappings; evidence source / gated update target | Implemented; distinct from v0.1 |

Characterization **populates, merges, and resolves** the current C subset.
It does not invent a parallel result schema.

```text
characterization runtime model
        ↓ deterministic mapping
monitor-knowledge/v0.1 JSON     ← discovery / evidence artifact

validated operational subset
        ↓
profile schemaVersion 1         ← durable reusable validated control knowledge
```

Restoring v0.1 JSON is implemented as a deterministic mapping from discovery
knowledge plus identity. Profile update is not that artifact.

`monitor-knowledge/v0.1` MAY serialize observed current/max values.
`profile schemaVersion` 1 MUST NOT persist transient current state.
PROFILE augmentation is not part of discovery JSON.

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

No profile-pack `schemaVersion` bump. `RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA`
is `"monitor-knowledge/v0.1"`. JSON parse/serialize map the current 128-route
bag plus an identity snapshot; they do not restore the historical heap
capability/relationship graph as a second source of truth.

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
- Knowledge JSON serialization: restored as monitor-knowledge/v0.1 mapping
  from discovery-only `RSSDDCMonitorKnowledge` plus identity. Bounds:
  256 KiB document, 128 capabilities, 32 methods/values per capability.

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

**Known production mechanisms** are characterization evidence. After identity
and transport are resolved, characterization injects already hardware-validated
production write methods without I/O, Alien Probe, or MCCS. Probe observation
and production methods are independent facts: effective read and write may
differ. The concrete case is LG HDR QHD `inputs.switching`: live GET of VCP
`0x60` may be the effective read, while production `LG_ALT` is the effective
write and is write-authorized only because that SET path is already gated and
hardware-validated. MCCS advertisement and successful GET still never authorize
the write. This slice does not validate new hardware; it exposes existing
production knowledge. Bounded `RSSDDCKnowledgeRoute` still cannot attach a
separate LG_ALT value domain (`0x90` / `0x91` / `0xd0`) beside MCCS `0x60`
enums; the write method is represented now, and those value spaces remain
distinct.

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
characterization. `rss_ddc_characterization_update_profile` mutates only an
explicitly supplied in-memory store. It does not SET, does not save to disk,
and does not rewrite builtin packs. Compatible additions become LOCAL
overlays. Live current values are not stored. Schema v1 persists LG_ALT as
`method=lg-alt-input` with production enums `0x90`/`0x91`/`0xd0`; it does not
store LG_ALT as VCP `0x60`.

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
consume `rss_ddc_characterization_discovered_knowledge()` serialized as
`monitor-knowledge/v0.1`, or the effective runtime view via
`rss_ddc_characterization_knowledge()`. Do not add a second public result schema.

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

#### Slice 2 implementation (this branch)

Internal only. There is still no public `rss_ddc_characterize_display`.

**Identity source.** Tests and later pipeline stages that already have a
snapshot call `rss_ddc_characterization_assemble`. The platform wrapper
`rss_ddc_characterization_prepare` resolves `list_index` with existing
`rss_ddc_get_display` and then assembles. Display resolution failure is fatal
and leaves characterization unchanged. Manufacturer/serial on
`RSSDDCDisplay` remain whatever discovery populated; this slice does not copy
EDID identity fields into the display snapshot (reconstruction gap, deferred).

**EDID optionality.** Assemble copies a caller-supplied `RSSDDCEDIDInfo` when
present. Prepare attaches EDID only when `rss_ddc_read_edid` plus
`rss_ddc_parse_edid` both succeed. Missing/failed EDID is non-fatal; identity
still succeeds with the display snapshot.

**Profile match inputs.** Match uses `rss_ddc_profile_identity_from_display`
plus `rss_ddc_profile_store_resolve`. No MCCS or probe evidence is consulted.
A NULL or empty store, or `NOT_FOUND`, is non-fatal and adds no PROFILE facts.
Equal-authority profile ambiguity returns `RSS_DDC_ERROR_PROFILE_CONFLICT` and
sets profile status CONFLICT without merging either side. Write authorization
remains the existing resolver/loader policy.

**Profile semantic normalization boundary.** Profile pack IDs are not renamed
in storage. At assemble time, `rss_ddc_profile_control_name` is normalized
before `rss_ddc_monitor_knowledge_add_profile_control`, then merged through
Slice 1 `add_knowledge`. Authoritative aliases remain those from Slice 1 plus
historical registry IDs. Remaining pack IDs (`gamma`, `sharpness`,
`response-time`, `adaptive-sync`, `energy-saving`, `black-stabilizer`,
`audio-mute`) are left unchanged: taxonomy examples exist
(`gaming.response_time`, `display.sharpness`, …) but they are not registry
entries and conflict with the `display.<profile-name>` heuristic.

**Provider capability storage.** Assemble stores
`rss_ddc_provider_capabilities(display.provider)` separately from the copied
`RSSDDCDisplay`. `RSS_DDC_CAP_PICTURE_MODE` is a profile exact-gate, not a
provider transport bit, and is not OR'd into this field.

**Transport vs monitor capability.** Provider bits answer whether this path
can attempt GET/SET/MCCS/DPCD/EDID/alternate-input. They do not create
DECLARED monitor-knowledge routes. Slice 3 is the first stage that may add
DECLARED facts from MCCS retrieval.

**Degraded behavior.** No profile → success, empty PROFILE set. No useful
provider bits (`UNKNOWN` / `MCDP29XX`) → success; later stages may be
unavailable. Knowledge overflow while merging PROFILE facts →
`RSS_DDC_ERROR_PROFILE_CONFLICT`, accumulated knowledge unchanged, profile
status remains NONE.

**Slice 3 boundary.** Stop before `rss_ddc_get_mccs_capabilities` in `prepare`.
Do not emit DECLARED facts from transport bits. Passive MCCS is a separate
stage (`collect_passive`).

### Slice 3 — Passive MCCS → DECLARED facts

- **Files:** `characterize.c`, `characterize_prepare.c`, `tests/test_characterize.c`
- **Reuse:** `rss_ddc_parse_mccs_capabilities`, `rss_ddc_get_mccs_capabilities`,
  `rss_ddc_mccs_capabilities_enum_values`, Slice 1 `add_knowledge`
- **Hardware:** none in tests; `collect_passive` is the library-only retrieval
  wrapper
- **Accept:** DECLARED facts are not write-authorized; PROFILE facts survive

#### Slice 3 implementation (this branch)

`prepare` / `assemble` still stop at identity + profile + transport.
`rss_ddc_characterization_collect_passive` is a later explicit stage.

**Transport vs advertisement.** `RSS_DDC_CAP_MCCS_CAPABILITIES` means this
provider path may retrieve a capabilities string. It does not create DECLARED
knowledge. Only a successfully parsed MCCS document produces DECLARED routes.

**Passive stage.** Tests call `collect_passive_mccs` /
`collect_passive_mccs_raw` with parser fixtures. The platform wrapper
retrieves via `rss_ddc_get_mccs_capabilities` then converts. No Get VCP.

**DECLARED mapping.** One knowledge route per advertised VCP:

| VCP | Semantic ID |
| --- | --- |
| `0x10` | `display.brightness` |
| `0x12` | `display.contrast` |
| `0x14` | `display.color_preset` |
| `0x15` | `display.picture_mode` |
| `0x16` / `0x18` / `0x1a` | `display.rgb.red_gain` / `green_gain` / `blue_gain` |
| `0x60` | `inputs.switching` |
| other | `vendor.unknown.vcp.XX` (`%02x`, same as probe) |

Routes use `FACT_DECLARED`, `source_id=mccs-capabilities`,
`evidence_id=mccs-advertised`, value UNKNOWN, `writable=false`,
`write_authorized=false`. RESEARCH source tagging is unchanged reconstruction
baggage.

**Enum handling.** Advertised enum bytes stay on the copied
`RSSDDCMCCSCapabilities` (query with `rss_ddc_mccs_capabilities_enum_values`).
They are not exploded into extra knowledge routes: that would overflow the
128-route bound and would imply current/writable values the advertisement does
not prove.

**Unknown VCPs.** Represented as `vendor.unknown.vcp.XX` DECLARED routes, plus
the retained MCCS model.

**Write authorization.** MCCS advertisement never sets `write_authorized`.
A PROFILE write-authorized route for the same semantic remains; resolver
policy still selects the validated method.

**Degraded behavior.** No MCCS cap → OK, no DECLARED facts. Retrieval or parse
failure → OK, status stored, identity/profile preserved, no fabricated facts.
Empty feature list → OK, no DECLARED routes. Merge overflow →
`RSS_DDC_ERROR_PROFILE_CONFLICT`, knowledge unchanged.

**Slice 4 boundary.** Stop before Alien Probe™ Quick Auto Probe / Get VCP
observation. Slice 4 is a later explicit stage (`collect_quick`).

### Slice 4 — Alien Probe™ Quick Auto Probe → OBSERVED facts

- **Files:** `characterize.c`, `characterize_prepare.c`, `tests/test_characterize.c`,
  `Makefile`
- **Reuse:** `rss_ddc_probe_create` / `rss_ddc_probe_quick` /
  `rss_ddc_probe_quick_for_display`, `rss_ddc_probe_knowledge`,
  `rss_ddc_probe_diagnostics`, Slice 1 `add_knowledge` / `current_value` /
  existing resolver
- **Hardware:** none in tests; tests drive the existing Quick Auto Probe with a
  mock GET transport. `collect_quick` is the library-only platform wrapper
- **Accept:** PROFILE facts survive; current value from OBSERVED, not from
  preferred PROFILE method; no SET callbacks; no Extended Probe

#### Slice 4 implementation (this branch)

This stage ingests **existing** Alien Probe™ Quick Auto Probe results. It does
not add a parallel Quick Probe engine, conversion schema, or GET framing.

**Stage.** Tests call `rss_ddc_characterization_collect_quick_probe` after
`rss_ddc_probe_quick`. The platform wrapper
`rss_ddc_characterization_collect_quick` calls
`rss_ddc_probe_quick_for_display` then ingest. Gate is
`RSS_DDC_CAP_GET_VCP`. No SET, verify, or Extended Probe.

**Quick Auto Probe VCPs (unchanged).** `0x10 display.brightness`,
`0x12 display.contrast`, `0x14 display.color_preset`,
`0x16`/`0x18`/`0x1a display.rgb.*`. Quick Auto Probe does **not** read
`0x15 display.picture_mode` or `0x60 inputs.switching`; those remain
DECLARED/PROFILE-only unless a later stage observes them.

**OBSERVED merge.** Characterization copies probe diagnostics into owned
storage (`observations` borrowed until the next Quick collect or destroy) and
merges only `FACT_OBSERVED` routes from `rss_ddc_probe_knowledge` through
`add_knowledge` (semantic IDs normalized). Probe DECLARED routes from the
probe's own MCCS load are not re-imported here; Slice 3 remains the DECLARED
owner. RESEARCH `source` tagging on probe facts is reconstruction baggage:
within characterization they are production live GET evidence.

**Method vs current value.** Resolver still prefers a validated PROFILE method
when present. Slice 1 current-value selection uses OBSERVED UNSIGNED/STRING
only. DECLARED UNKNOWN and PROFILE UNKNOWN never become current. A successful
GET does not set `write_authorized`.

**Variable / current > max.** Probe keeps the first protocol-valid GET as the
single OBSERVED route and marks diagnostics VARIABLE when the repeat differs.
`current > maximum` is stored unchanged (`current_exceeds_maximum`).
Characterization does not average, pick the second read, or clamp.

**Unknown VCPs.** `vendor.unknown.vcp.XX` remains the probe convention. Quick
Auto Probe's six codes are all known; unknown-VCP OBSERVED facts are an
Extended Probe concern, not invented here.

**Degraded behavior.** No GET cap → OK, unsupported, no OBSERVED facts. Probe
or transport failure → OK (or recorded status), PROFILE/DECLARED preserved.
Protocol-reported / malformed / transport / semantic-mismatch reads stay in
diagnostics only. Merge overflow → `RSS_DDC_ERROR_PROFILE_CONFLICT`, prior
knowledge unchanged.

**Slice 5 boundary.** Stop before sufficiency policy. Sufficiency is a later
pure stage (`rss_ddc_characterization_sufficiency`) and must not invoke
Extended Probe.

### Slice 5 — Sufficiency decision

- **Files:** `characterize.h`, `characterize.c`, `tests/test_characterize.c`
- **Reuse:** existing resolver, current-value accessor, Quick diagnostics,
  profile status, provider bits, MCCS model
- **Hardware:** none; pure function over already-collected evidence
- **Accept:** Quick success is not sufficiency; Extended is recommended or not,
  never invoked

#### Slice 5 implementation (this branch)

`rss_ddc_characterization_sufficiency` is a pure DEFAULT-mode decision. It
does not GET, SET, cache a pipeline flag, or run Alien Probe™ Extended.

**Product-relevant set (v1).** Always: `display.brightness`,
`display.contrast`. Conditionally: `display.color_preset` if advertised or
present in knowledge; `display.picture_mode` if advertised, present in
knowledge, or `display.capabilities` includes `RSS_DDC_CAP_PICTURE_MODE`;
`inputs.switching` if advertised, present in knowledge, or provider bits
include `RSS_DDC_CAP_ALTERNATE_INPUT`. RGB gains and `vendor.unknown.vcp.XX`
never block sufficiency.

**Usable method.** Resolver `RESOLVED` with a preferred read or write route.
`display.picture_mode` is also usable when the copied display snapshot already
has the production Picture Mode gate bit. DECLARED-only routes are not
methods. Lack of a live OBSERVED current value does not fail a control whose
method is already known.

**Quick coverage.** Alien Probe™ Quick Auto Probe still reads only six known
VCPs. It does not observe `display.picture_mode` or `inputs.switching`.
`quick_status == OK` is therefore not sufficiency. Those controls can still
be sufficient from PROFILE, DECLARED plus an already known method, or the
Picture Mode production gate.

**Variable observations.** Quick VARIABLE stays in diagnostics; the OBSERVED
route still holds the first GET. Sufficiency records
`REASON_VARIABLE_OBSERVATION` for brightness/contrast and does not treat that
first value as stable current state. VARIABLE alone does not fail the
characterization when methods are already resolved, and it does not recommend
Extended (more GETs will not authorize a method or stabilize a fluctuating
control).

**Extended recommendation.** True only when status is INSUFFICIENT, GET VCP is
available, and an in-scope product-relevant control still has no usable
method. False when already sufficient, GET is unavailable, the gap is profile
conflict / write-authority-only, or only unknown vendor VCPs remain.

**Statuses.** SUFFICIENT; INSUFFICIENT (unresolved in-scope methods);
CONFLICT (profile match conflict or equal-authority method conflict);
UNAVAILABLE (no assembled display).

**Slice 6 boundary.** Stop before invoking Alien Probe™ Extended Auto Probe.
Slice 6 is a later explicit stage (`collect_extended`).

### Slice 6 — Alien Probe™ Extended Auto Probe

- **Files:** `characterize.h`, `characterize.c`, `characterize_prepare.c`,
  `tests/test_characterize.c`
- **Reuse:** `rss_ddc_probe_extended` / `rss_ddc_probe_extended_for_display`,
  `rss_ddc_probe_extended_diagnostics`, Slice 5 sufficiency, Slice 1
  `add_knowledge`
- **Hardware:** none; tests drive the existing Extended Auto Probe with a mock
  GET transport. `collect_extended` is the library-only platform wrapper
- **Accept:** full diagnostics retained separately from promoted knowledge;
  128-route cap never silently drops diagnostics; GET never authorizes write

#### Slice 6 implementation (this branch)

This stage ingests **existing** Alien Probe™ Extended Auto Probe results. It
does not add a parallel scan engine, change GET pacing, or SET.

**When it runs.** `rss_ddc_characterization_collect_extended` runs Extended
only if Slice 5 `extended_recommended` is true and GET VCP is available.
CONFLICT and already-sufficient characterizations do not run it. Tests ingest
via `collect_extended_probe` after `rss_ddc_probe_extended`.

**Diagnostics vs knowledge.** The full `RSSDDCProbeExtendedDiagnostics`
(256 observations, abort/status counts) is copied into owned storage.
`semantic_id` pointers into per-observation buffers are retargeted after copy.
Canonical knowledge receives only selectively promoted OBSERVED routes.

**Promotion priority.** Protocol-valid observations only, in order:

1. unresolved product-relevant IDs (`display.brightness`, `display.contrast`,
   `display.color_preset`, `display.picture_mode`, `inputs.switching`)
2. advertised MCCS controls that remain unresolved
3. known semantic controls that already have PROFILE/DECLARED evidence but no
   live OBSERVED fact
4. other known semantic mappings (`0x10`/`0x12`/`0x14`/`0x15`/`0x16`/`0x18`/
   `0x1a`/`0x60`)
5. `vendor.unknown.vcp.XX` last

Promotion remaps `0x15` → `display.picture_mode` and `0x60` →
`inputs.switching` using the Slice 3 VCP table (Extended's own diagnostic
strings may still say `vendor.unknown.vcp.15` / `.60`). Failures stay
diagnostic. `current > maximum` and VARIABLE classification are preserved.
A successful GET never sets `write_authorized`.

**Capacity.** Promotion is one route at a time. Overflow increments
`skipped_capacity` and returns OK; prior knowledge and full diagnostics stay.
Summary: `considered`, `promoted`, `skipped_capacity`,
`skipped_nonpromotable`.

**Sufficiency.** Callers recompute `rss_ddc_characterization_sufficiency`
after promotion. Extended may turn INSUFFICIENT into SUFFICIENT when a
read method is now resolved, or leave it INSUFFICIENT when DECLARED-only
picture mode / missing input remain, or when observation does not authorize
writes.

**Slice 7 boundary.** Stop before public `rss_ddc_characterize_display`.

### Slice 7 — Public characterization API

- **Files:** `include/rss_ddc.h`, `characterize.h`, `characterize.c`,
  `characterize_prepare.c`, `tests/test_characterize.c`, README version note
- **Reuse:** existing prepare / collect_passive / Quick / Extended ingest
  stages; private `rss_ddc_characterization_execute` plus a small ops table
- **Hardware:** none. Public tests drive execute with mocked display/EDID/MCCS/
  Quick/Extended. Live `rss_ddc_characterize_display` wires the existing
  public read APIs and is not invoked against hardware in this slice
- **Accept:** opaque owned result; PASSIVE / DEFAULT / DEEP; borrowed
  accessors; no SET; no profile mutation; no CLI command

#### Slice 7 implementation (this branch)

**Public entry point.**

```c
RSSDDCError rss_ddc_characterize_display(
    uint32_t list_index,
    const RSSDDCProfileStore *profiles,
    const RSSDDCCharacterizeOptions *options,
    RSSDDCCharacterization **out);
```

`list_index` uses the existing 1-based current-list convention.
`rss_ddc_default_characterize_options()` and NULL `options` both select
DEFAULT mode. `profiles` is borrowed and may be NULL.

**Ownership.** On success or safe degradation the caller owns `*out` and
releases it with `rss_ddc_characterization_destroy`. Accessor pointers are
borrowed from that object and remain valid until destroy. On fatal failure
`*out` is NULL and nothing is leaked.

**PASSIVE.** Identity, profile match, transport bits, passive MCCS,
merge/resolution. Does not run Alien Probe Quick or Extended.

**DEFAULT.** PASSIVE plus Quick, then sufficiency. Extended runs only when
Slice 5 `extended_recommended` is true and GET VCP is available. Slice 5
policy is unchanged.

**DEEP.** Same pipeline, but Extended is forced when GET VCP is available even
if DEFAULT would already be sufficient. DEEP is still read-only. It is not
Guided Discovery, Experimental Validation, or SET testing. If GET is
unavailable, DEEP degrades and returns the best available characterization.
DEEP does not imply SUFFICIENT.

**Degraded vs fatal.** Unresolvable display, bad arguments, and allocation
failure are fatal (no object). EDID/MCCS/Quick/Extended stage failures, missing
profiles, and INSUFFICIENT or CONFLICT sufficiency return an owned
characterization. Extended failure preserves pre-Extended knowledge.

**Read-only guarantee.** The public entry point and private executor call only
display/EDID/profile/MCCS/Get-VCP probe APIs. There is no SET VCP,
set-and-verify, alternate-input write, picture-mode SET, or profile
persistence path. The private ops table has no write callback.

**Accessors.** Display snapshot, EDID, provider capability bits, profile
status/identity/effective profile, merged knowledge, MCCS model/status, Quick
and Extended diagnostics, promotion summary, sufficiency, semantic method
resolution, and current observed value. Alias normalization
(`brightness` / `display.brightness`) continues to work through resolve and
current-value.

**Slice 8 boundary.** Stop before a CLI `characterize` command or view. The
public library API is the consumer contract for CLI, Rogue Display Control,
and the macOS Stream Deck backend.

### Slice 8 — CLI characterization view

- **Files:** `cli/main.m`, `cli/presentation/args.c`,
  `cli/presentation/characterize_render.c`, `tests/test_cli_presentation.c`,
  `tests/test_cli_characterize.c`, README, `docs/cli-output.md`
- **Reuse:** public `rss_ddc_characterize_display`, existing table/color/unicode
  presentation, 1-based display index parsing
- **Hardware:** targeted read-only smoke after the implementation commit
- **Accept:** presentation-only CLI; no SET; no profile update; no JSON;
  no forced DEEP unless DEFAULT itself recommends Extended

#### Slice 8 implementation (this branch)

```sh
rss-ddc --help
rss-ddc -h
rss-ddc characterize 1
rss-ddc characterize 1 --mode passive
rss-ddc characterize 1 --mode default
rss-ddc characterize 1 --mode deep
rss-ddc characterize 1 --no-profiles
rss-ddc characterize 1 --mode deep --no-profiles
rss-ddc characterize 1 --json
rss-ddc characterize 1 --mode default --json --output /tmp/monitor.json
```

The command parses the display index, optional `--mode`, optional
`--no-profiles`, optional `--json`, and optional `--output <file>`. Unless `--no-profiles` is set, it loads the builtin profile
store if available (read-only; never saved). It calls
`rss_ddc_characterize_display`. Default output is the human report.
`--json` / `--output` emit discovery-only `monitor-knowledge/v0.1`.
It does not call MCCS, Quick, or Extended APIs directly.

`--no-profiles` selects `IGNORE_KNOWN`: identity is still discovered, but
monitor-specific structured prior knowledge is not loaded. Canonical
pipeline, COMPLETE vs PARTIAL lookup, and DEEP-never-skips semantics are
in [Canonical Alien Probe™ architecture](alien-probe-architecture.md).

Exit 0 when the public API returns a characterization, including INSUFFICIENT
or CONFLICT. Fatal API failure exits non-zero.

Report sections: MONITOR, CHARACTERIZATION, RESOLVED CONTROLS, EVIDENCE
SUMMARY, MCCS SUMMARY, Alien Probe Quick summary, and Extended summary only
when Extended ran or DEEP was requested. Observed GET never implies write
authorization.

**Slice 9 boundary.** Stop before injecting known production write methods
that rss-ddc already hardware-validates outside characterization.

### Slice 9 — Validated production methods

- **Files:** `characterize.h`, `characterize.c`, `tests/test_characterize.c`,
  `cli/presentation/characterize_render.c`, this document
- **Reuse:** `rss_ddc_validate_lg_alt_input_target`,
  `rss_ddc_lg_alt_input_value_is_supported`, existing
  `add_profile_control` / resolver, builtin Picture Mode profile
- **Hardware:** none in implementation; targeted read-only characterize smoke
  after commit
- **Accept:** matching LG identity receives write-only authorized `LG_ALT` for
  `inputs.switching`; nonmatching monitors do not; VCP `0x60` GET remains
  read-only; Picture Mode is not duplicated; Odyssey is not write-authorized

#### Slice 9 implementation (superseded)

Slice 9 injected LG_ALT from `rss_ddc_validate_lg_alt_input_target` product/transport
gates inside characterization. That is **architectural drift** and has been
removed. Monitor-specific LG_ALT applicability and values now live in the
builtin profile pack (`lg-hdr-qhd-dcpdp13-dcpext0`). See
[Canonical Alien Probe™ architecture](alien-probe-architecture.md).
The SET path may still fail closed on the same identity predicates as write
safety; characterization must not use them to inject methods.

**Slice 10 boundary.** Stop before optional profile update policy.

### Slice 10 — Optional profile update policy

- **Files:** `rss_ddc.h`, `characterize.c`, `profile_store.c`,
  `tests/test_characterize.c`, this document, `docs/monitor-profiles.md`
- **Reuse:** existing schema v1 pack format, `put_local_profile` overlay,
  production LG_ALT enums, builtin Picture Mode
- **Hardware:** none for implementation tests; optional temp-store smoke after
  commit
- **Accept:** explicit mutation only; no authority escalation; no disk write
  inside the update call; CLI `characterize` remains read-only

#### Slice 10 implementation (this branch)

`rss_ddc_characterization_update_profile` is an explicit caller action. It is
not invoked by `rss_ddc_characterize_display`. It performs no monitor I/O and
does not save files. Builtin/pack/research records are never rewritten; new
facts are LOCAL overlays via `rss_ddc_profile_store_put_local_profile`. Disk
persistence of a user overlay is `rss_ddc_profile_store_save_local_file`.
`rss_ddc_profile_store_save_file` still dumps the whole store, including
builtin records and last-loaded pack metadata, and must not be used for mixed
builtin+local user files.

**Eligible (authoritative).** Hardware-validated PROFILE or production write
methods the current schema can represent: Picture Mode VCP `0x15` with its
existing enums; LG_ALT input method `lg-alt-input` with production enums
`0x90` / `0x91` / `0xd0`. Persistence copies or preserves that authority; it
never increases it.

**Not persisted.** Live current values; MCCS advertisement; DECLARED facts;
Quick/Extended GET observations; vendor-unknown VCPs; variable diagnostics;
VCP `0x60` as a substitute for LG_ALT. Observed reads do not create writable
or write-authorized profile controls.

**LG_ALT.** Schema v1 can represent both the method (`lg-alt-input`) and the
validated value domain (`enums`). Those enum values come from existing
production support (`rss_ddc_lg_alt_input_value_is_supported`), not from MCCS
`0x60` or live GET. Equal-authority conflict with an existing non-equivalent
INPUT control fails closed.

**Outcomes.** `created` (no prior match, persistable facts exist);
`updated` (match existed, compatible LOCAL overlay added); `unchanged`
(everything persistable already present); `conflict` (equal-authority
non-identical write method, or bounds); `unsupported` (identity incomplete or
nothing safely representable — Odyssey DEFAULT).

No `rss-ddc profile` CLI command existed in Slice 10. `rss-ddc characterize`
does not mutate stores.

### Slice 11 — Explicit profile update CLI

- **Files:** `cli/main.m`, `cli/presentation/args.c`,
  `cli/presentation/profile_update.c`, `profile_store.c`, `rss_ddc.h`, CLI
  tests, this document, `docs/monitor-profiles.md`, `README.md`
- **Reuse:** public characterize + profile-update APIs; LOCAL-only export
- **Hardware:** none for implementation tests; optional `/tmp` file smoke after
  commit
- **Accept:** `profile update --output` required; characterize remains
  read-only; no monitor SET; LOCAL overlay metadata is `local-export`

`rss-ddc profile update <display-index> --output <file>` characterizes in
DEFAULT mode, updates the in-memory store explicitly, and saves LOCAL overlay
records only when the result is CREATED or UPDATED. Builtin profiles stay
builtin. Reload of builtin plus that file restores Picture Mode from builtin
and LG_ALT input from the local overlay when those facts exist.

`rss-ddc characterize` remains read-only and never modifies profiles.
`profile update` still performs no monitor writes. Automatic persistence only
keeps already-authoritative, representable knowledge; not every monitor gains
write support.

## 20. Non-goals

- No competing characterization schema and no `CharacterizationResult` JSON
  type
- Guided Discovery and Experimental Validation remain deferred
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
