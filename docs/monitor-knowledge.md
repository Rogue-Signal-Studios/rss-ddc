# Monitor knowledge

Slice 6 provides a bounded, heap-owned, offline representation of monitor
knowledge. It does not enumerate a display, open an IOAV service, read DDC/CI,
or issue a command. A route is a fact about a possible control path, not
permission to use that path.

## Historical intent reconstructed

The first core model (`c7f6834`) made the model semantic-first rather than VCP
first. `e43eb43` added a separate effective-resolution view with independently
chosen read and write routes. `e860d9f` extended that separation to values and
routes. `9547541` established that provenance survives merging and resolution;
an effective answer must never become an unattributed scalar. `a905c4b` made
merge ownership explicit: it returns a fresh deep copy and never transfers or
borrows either input. Finally, `38cf0b1` made unknown, unsupported, unresolved,
and conflict states explicit and retained all compatible observations.

This reconstruction implements those final rules directly in a small bounded
C model. It is intentionally not a replay of the historical parser, probe, or
transport work.

## Retained facts

`RSSDDCMonitorKnowledge` stores copied `RSSDDCKnowledgeRoute` facts. A fact has
a stable semantic ID, route ID, route kind, address, explicit value state,
read/write capability, separate `write_authorized` metadata, transport and
applicability text, and provenance. Provenance records a source ID, source
class, confidence, fact class (`declared`, `profile`, `observed`, `inferred`,
or `resolved`), and optional evidence ID.

`UNKNOWN` and `UNSUPPORTED` are different value states and both are retained.
Two facts are coalesced only when all route and provenance fields are equal.
Thus matching observations from distinct sources, competing values, and
alternate routes all remain inspectable after a merge.

Slice 5 integration is data-only:
`rss_ddc_monitor_knowledge_add_profile_control` copies a resolved profile
control as a `PROFILE` fact. It does not match a live display, apply a profile,
or call an input, picture-mode, GET, or SET API.

## Merge, resolution, and ownership

`rss_ddc_monitor_knowledge_merge(first, second, &merged)` is transactional:
on success `merged` is a new heap-owned deep copy; callers retain and later
destroy both inputs independently. On failure `merged` is NULL and neither
input changes.

`rss_ddc_monitor_knowledge_resolve` returns a heap-owned view of every route
for one semantic ID. Its candidate and preferred-route pointers are borrowed
from the supplied source objects, so the sources must outlive the resolution.
Destroy the resolution with `rss_ddc_monitor_knowledge_resolution_destroy`;
it does not free source knowledge.

Resolution ranks confidence first, then source class (`local`, validated pack,
builtin, research). Read and write selection are independent. Equal-authority,
non-equivalent routes produce the explicit `CONFLICT` state and expose no
preferred route. Equal, provenance-distinct facts remain non-conflicting, and
the lexicographically lowest source ID makes their selected representative
independent of source ordering. A lower-authority alternative remains a
candidate but cannot replace a higher-authority route.

`write_authorized` remains separate from route selection:
`rss_ddc_monitor_knowledge_resolution_write_authorized` can only report the
metadata of a selected route. It never performs a write and does not make a
route reachable through an existing transport API.

## Limits

Each knowledge object retains at most 128 facts. The limit is explicit so
merging and resolving stay bounded and allocation failure cannot publish a
partial object. No serialization is included in this slice; malformed external
model data therefore has no parser entry point.
