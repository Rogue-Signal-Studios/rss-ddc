# Monitor profiles

Slice 5 adds a pure, offline JSON profile store. It can parse, persist, and
resolve monitor metadata but it cannot enumerate displays, create IOAV objects,
read DDC/CI, or execute a control.

The heap-owned `RSSDDCProfileStore` is created with
`rss_ddc_profile_store_create` and released by its caller with
`rss_ddc_profile_store_destroy`. Inputs are copied into the store; resolver and
getter outputs are caller-owned copies. Failed loads and resolutions never
partially change their supplied store or result.

Schema v1 requires `schemaVersion`, `databaseVersion`,
`minimumRSSDDCVersion`, and `profiles`. Each identity requires exact external,
product name, provider, and transport values; manufacturer, serial, and branch
device ID are optional exact additional predicates. Unknown optional JSON keys
are skipped, preserving the historical forward-compatible behavior. Unknown
required/malformed values fail closed.

Matching is deterministic: required identity values, then optional predicates;
controls compose from all matches. A duplicate control resolves by higher
confidence, then source (`local`, validated pack, builtin, research), then
identity specificity. Equal-authority non-identical controls fail with a
profile conflict. No match returns `RSS_DDC_ERROR_NOT_FOUND`.

Limits: 65,536-byte files, 32 profiles, 16 controls per profile, 32 enum
values per control, 64-byte IDs/version strings, and 128-byte display text.
File saves export to a complete temporary file, `fsync` it, then rename it over
the target; failed writes remove only the temporary file.

The historical `fc48972` bug arose when the multi-megabyte bounded store was
copied as a stack local during parsing/appending/builtin resolution. This
implementation allocates every store, parse candidate, append replacement, and
small-stack test result on the heap from the outset.
