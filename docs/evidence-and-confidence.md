# Evidence and confidence

Evidence/provenance and confidence/validation are orthogonal. Evidence answers
“why do we believe this?”; confidence answers “how strongly should consumers
trust it?” Readability and writability remain separate capability properties.

## Initial evidence vocabulary

- `standard_defined`
- `mccs_advertised`
- `edid_derived`
- `profile_known`
- `rogue_validated_profile`
- `local_validated`
- `stable_get`
- `extended_discovery`
- `external_candidate`
- `manufacturer_family_hint`
- `model_family_hint`
- `osd_correlated`
- `set_confirmed`

External documentation, public databases, standards, and community
observations may enter only as candidate evidence. They can prioritize
investigation but do not authorize writes or become hardware validation merely
by being imported.

## Confidence ladder

`unknown` → `candidate` → `observed` → `correlated` → `validated` →
`hardware_validated`

For example, a Picture Mode candidate can be `correlated`, readable, and not
writable; a standard brightness method can be `hardware_validated`, readable,
and writable. Confidence is never inferred solely from the semantic name or
the method address.

## Risk belongs to operations

The initial operation safety classes are `read_standard`, `read_extended`,
`guided_read`, `validate_safe_set`, `vendor_experimental_set`, and
`high_risk_denied`. They describe a specific method/operation, not merely a
semantic capability. A capability may expose a safe read method and a denied
write method simultaneously.
