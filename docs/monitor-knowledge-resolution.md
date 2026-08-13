# Monitor knowledge resolution

`monitor-knowledge/v0.1` keeps three decisions separate:

1. Retained knowledge is every compatible source record and its evidence.
2. Effective selection chooses a preferred read method and a preferred write
   method independently.
3. Write authorization is a separate policy outcome, never an inference from
   a method's `writable` flag.

The offline `rss_ddc_monitor_knowledge_resolve_capability` API accepts a set
of already parsed knowledge sources and returns a heap-owned resolution view.
It retains references to all distinct candidate methods, exposes preferred
read/write methods independently, conflict state, and a machine-readable
reason code. Source knowledge must outlive the resolution view.

Identity facts are compatible when shared populated fields agree. More specific
identity records score above broad family facts, while incompatible identities
are surfaced as a conflict. No serial is required.

For a normal production write, a method must be writable, use neither
`high_risk_denied` nor `vendor_experimental_set`, and have scoped strong
evidence: `set_confirmed`, `local_validated`, or
`rogue_validated_profile`. External candidates, family hints, MCCS inventory,
and correlations alone never authorize a write. Equal-scoring incompatible
authorized methods fail closed.

Example: MCCS VCP `0x60` may remain the preferred read method for
`inputs.switching`, while a separately set-confirmed LG protocol becomes the
only production-authorized write method. Both remain visible to consumers.

## Effective values, ranges, and routes

The same source-set resolver now has independent heap-backed queries for enum
values, numeric ranges, and input routes. Values are grouped only by their
stable semantic value ID; labels and coincident raw values do not change that
identity. Raw comparison is type-aware. A value must have both an authorized
capability method and its own hardware/set validation before it is selected for
a production write.

Advertised, observed, and validated ranges are retained separately. Only an
unconflicted validated range becomes an effective production write range.
Conditions are retained as opaque metadata for future evaluation; no rule
engine or live state check is performed.

Input routes are resolved by route ID, retaining DDC-path-change metadata and
separate read/switch raw representations. A switch also needs route-level
strong evidence, so authorizing a switching method never authorizes every
candidate route.
