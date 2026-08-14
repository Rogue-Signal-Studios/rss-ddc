# Monitor knowledge schema v0.1 proposal

> Status: `monitor-knowledge/v0.1` is implemented as an offline, heap-owned
> rss-ddc C model with JSON parsing, validation, export, semantic lookup, and
> bounded fixture support. It creates no monitor operation or write authority.

## Implemented API boundary

`rss_ddc_monitor_knowledge_*` owns a parsed document and exposes borrowed
views for identity, semantic capabilities, methods, values, input routes, and
relationships. Parsing is bounded and transactional: malformed or incompatible
documents leave no partially returned object. The only accepted schema string
is `monitor-knowledge/v0.1`; future versions fail closed.

The core deliberately has no file, network, UI, product, enumeration, GET, or
SET path. Future Alien Probe Quick/Extended scanning and Guided Discovery are
planned consumers of this model, not part of it.

Quick Auto Probe v1 is one such consumer. Its selected-display, read-only
observations serialize into this same document format: standard VCP reads use
their centralized semantic ids, `stable_get` evidence records stable replies,
and MCCS advertisement uses `mccs_advertised`. Read current/max fields become
observed ranges only; neither observation nor advertisement grants write
authorization.

Multiple compatible source documents can be combined with
`rss_ddc_monitor_knowledge_merge`. The result is retained knowledge, not an
effective operational decision; consumers resolve it separately.

The core stores every supported field with explicit ownership and serializes a
canonical JSON document containing identity evidence, capability ranges,
methods, values and raw aliases, input routes, relationships, and evidence at
each of those levels. Merge works on a temporary deep clone and returns it
only after validation, so an allocation or validation failure never publishes
a partial result. Competing capability records are retained rather than
collapsed into a guessed effective value.

Resolution is also offline. See [monitor knowledge resolution](monitor-knowledge-resolution.md)
for the distinction between retained records, effective method selection, and
write authorization.

Alien Probe™ needs one durable representation for what Rogue knows about a
monitor. The representation is semantic first and protocol second: a product
asks for `input.current` or `display.picture_mode`, then the profile selects a
supported method. A raw VCP address, vendor command, or observed numeric value
is evidence about a capability; it is not the capability's identity.

## Design objectives

- Model the **maximal virtual monitor**: the union of controls found across
  product classes. Its presence in the schema never claims that a particular
  monitor implements every field.
- Keep semantics, access methods, evidence/confidence, and relationship
  observations as four independent axes.
- Represent enums, raw values, aliases, ranges, inputs, and validation as
  first-class data.
- Allow a better local observation to supersede an external candidate without
  erasing the provenance of either.
- Make write authorization intentionally harder than discovery. External
  knowledge may suggest a candidate but never authorizes a write.

## Conceptual document shape

This JSON-like illustration is normative only for meaning, not field spelling
or serialization:

```text
monitor_knowledge {
  schema_version: "monitor-knowledge/v0.1",
  identity: { manufacturer, model, product_code, serial, edid, firmware,
              provider, transport, connection_path },
  capabilities: [ capability ],
  relationships: [ relationship ],
  observations: [ evidence ],
  generated_at, source_profile, compatibility
}

capability {
  id: "display.picture_mode",            // stable semantic identifier
  semantic: { domain: "display", kind: "enum", label: "Picture Mode" },
  availability: { state, conditions },    // e.g. unavailable while HDR is on
  methods: [ method ],
  values: [ value ],
  range: range | null,
  evidence: [ evidence_ref ],
  confidence: confidence,
  validation: validation_state
}

method {
  id, protocol, address_or_command, read, write, operation_risk,
  request_shape, reply_shape, preconditions, evidence, confidence
}

value {
  id: "fps", label: "FPS", raw: 4, raw_aliases: ["game"],
  availability, evidence, confidence, validation
}

range {
  minimum, maximum, step, unit,
  observed: { minimum, maximum }, advertised, validated
}
```

### Availability conditions

`conditions` remains a legacy human-readable note. New records use
`conditionGroups`, an array of `all_of` or `any_of` groups. A condition names
another semantic capability, optionally a value id, and one of `equals`,
`not_equals`, `enabled`, `disabled`, `present`, `absent`, or the numeric
comparison operators. Equality and numeric conditions carry a typed
`RSSDDCRawValue`, plus their own confidence, validation state, and evidence.
The offline model preserves these groups but does not evaluate them or use
them to grant access.

`protocol` may be `mccs_vcp`, a vendor protocol, a software provider control,
or another future transport. For example, the standard input semantic can have
one MCCS VCP `0x60` method and a separately described LG alternate method;
neither protocol name is embedded in `input.current`. A method can be readable
without writable, conditional, or unsupported.

## Semantic taxonomy

The virtual monitor reserves these domains. Implementations may expose a
subset, and vendor extensions live under `vendor.*` until their semantics are
independently established.

```text
monitor.identity       maker, model, serial, firmware, EDID, connection
display                brightness, contrast, sharpness, gamma, color_temperature,
                       color_preset, rgb_gain, rgb_bias, saturation, hue,
                       picture_mode, HDR, local_dimming
gaming                 response_time, adaptive_sync, VRR, black_equalizer,
                       refresh_behavior, aim_or_game_assist
inputs                 input.current, input.available, input.routing
audio                  volume, mute, output, audio_mode
multi_view             PIP, PBP, layout, source assignment
usb_kvm                upstream, downstream routing, KVM assignment
power                  power_mode, standby, USB charging
sensors                ambient_light, proximity, auto_brightness
panel_care             pixel_refresh, panel_protection, maintenance state
connectivity           USB-C/Thunderbolt behavior, Ethernet, webcam
vendor                 explicitly vendor-specific controls and metadata
```

Semantic identifiers are deliberately precise. `display.brightness` remains
brightness even if it changes when `display.picture_mode` changes. Standards
knowledge, model/family evidence, and validation may later establish a new
semantic control; correlation by itself cannot.

### Inputs are structured values

`input.current` is an enum capability whose values describe a route, not just
an integer. A value can carry `connector` (`hdmi`, `displayport`, `usb_c`),
`port` (`1`, `2`, or unknown), an optional user-facing label, raw aliases, and
the method-specific raw representation. This accommodates standard VCP `0x60`,
vendor framing, duplicate connector families, and PBP source assignment while
keeping applications semantic-first.

## Evidence, confidence, and validation

Every profile assertion carries one or more immutable evidence records: source
type, observation time, monitor identity scope, method/raw payload when safe,
and a concise result. Useful types include standards, EDID, MCCS capabilities,
provider/transport inspection, read observation, stability analysis, guided
user change, manufacturer documentation, model-family knowledge, correlation,
and controlled hardware validation.

Confidence is an ordered interpretation of that evidence:
`unknown` → `candidate` → `observed` → `correlated` → `validated` →
`hardware_validated`. A high correlation can advance an observation to
`correlated`; it cannot make the semantic identity validated. Validation is a
separate state because a value may be read successfully yet never be safe to
write.

The operation risk attached to each method is one of:

```text
read_standard | read_extended | guided_read | validate_safe_set |
vendor_experimental_set | high_risk_denied
```

The three read classes distinguish safe discovery paths and user-assisted
observation. A write
requires explicit method-level authorization, identity match, supported value,
current evidence, a policy decision, and a risk below the caller's limit;
`high_risk_denied` is never eligible. Manufacturer documentation and an external
catalog can seed `candidate` records but have **no write authority**.

## Relationships are observations, not aliases

A `relationship` links two semantic capabilities without merging them. It has
a source, target, kind (`secondary_effect`, `correlates_with`, `depends_on`,
`conflicts_with`, or `enables`), direction, conditions, observations,
confidence, and evidence. For example:

```text
display.picture_mode(FPS) --secondary_effect--> display.brightness
```

This records a useful recall or prediction signal while preserving that VCP
`0x10` is brightness. It also models HDR disabling a color preset, PBP changing
input routing, or a KVM route changing an upstream USB path.

## Stress test: known local archetypes

| Archetype | What it exercises | Schema outcome |
| --- | --- | --- |
| LG HDR QHD / DCPDP13Service | MCCS inventory, standard controls, alternate input method, eight Picture Mode values | One semantic input with distinct methods; Picture Mode values and evidence can be hardware-validated without treating Color Preset or brightness as aliases. |
| Samsung Odyssey G75F / PS190 | stable GET inventory, standard brightness/contrast/input, incomplete MCCS support, guided candidate discovery | Provider/path constraints live on methods; a perfectly correlated `0x10` is retained as a `secondary_effect`, not elevated into Picture Mode identity. |
| OLED gaming display | HDR, VRR, pixel care, conditional controls | Availability conditions and panel-care domain avoid flattening transient OSD state into permanent support. |
| USB-C/Thunderbolt hub display | KVM, charging, Ethernet, webcam, multiple inputs | Connectivity and USB/KVM domains separate display routing from host peripherals. |
| ultrawide PIP/PBP display | multi-view layouts and multiple simultaneous sources | `multi_view` and structured input routes model a graph of source assignments rather than a single input integer. |
| reference/professional display | calibration, color modes, local dimming, vendor workflows | Vendor methods remain explicit until standards and evidence justify semantic promotion. |

## External sample-record stress set

The following are proposed *sample records*, selected from public
manufacturer product definitions to test coverage. They are not imported
profiles, current support claims, or write authorizations. Each begins as
external `candidate` evidence only.

| Representative model | Coverage purpose |
| --- | --- |
| LG UltraGear 32GS95UE | dual-mode OLED gaming, HDR, pixel care |
| LG UltraGear 45GS96QB | ultrawide, USB-C, multi-view |
| Samsung Odyssey Neo G9 G95NC | very wide, PBP, high-bandwidth input topology |
| Samsung Odyssey OLED G9 | OLED ultrawide gaming and multi-view |
| Dell UltraSharp U4025QW | Thunderbolt hub and productivity controls |
| Dell UltraSharp U3224KB | conferencing peripherals and hub behavior |
| BenQ PD3225U | professional color, Thunderbolt, KVM |
| ASUS ProArt PA32UCXR | reference color and local dimming |
| ASUS ROG Swift OLED PG32UCDM | OLED gaming, KVM, conditional features |
| Gigabyte AORUS FO32U2P | modern gaming routing and KVM |
| MSI MPG 321URX | OLED gaming controls and panel care |
| Acer Predator X32 X3 | gaming HDR and refresh behavior |
| HP E45c G5 | productivity ultrawide and source layout |
| Philips 49B2U5900CH | webcam, USB-C hub, KVM-oriented workflow |
| Lenovo ThinkVision P40w-20 | Thunderbolt hub and enterprise connectivity |

This set confirms that the taxonomy needs domains beyond DDC VCP controls and
that one monitor may have several valid methods for a semantic control.

## Open refinements discovered by the stress test

1. Multi-view needs a route graph and stateful layout model, not only
   `input.current`.
2. Dynamic OSD availability must be timestamped and conditional on HDR, VRR,
   selected input, PBP, and firmware state.
3. Method selection needs conflict resolution when standard and vendor reads
   disagree, including provider/connection-path scope.
4. Raw values must support numbers, bitfields, strings, and framed vendor
   payloads without losing their original encoding.
5. Profile matching needs scoped identity privacy rules for serials and
   connection paths, plus model-family fallback that cannot over-authorize.
6. A future implementation needs a bounded evidence-retention policy and a
   clear separation between local observations and shareable catalog data.

Those refinements are requirements for a future schema/API milestone, not
permission to build a broad scanner or issue writes today.
