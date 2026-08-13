# Rogue display-control product architecture

`rss-ddc` is the public MIT backend monitor-intelligence engine in a
three-product Rogue Signal Studios ecosystem:

```text
                         rss-ddc
              backend intelligence engine
                       /           \
                      v             v
       Rogue Display Control   Rogue Display Control
          Configurator Full      for Stream Deck
                                  Configurator Light
```

## rss-ddc ownership

rss-ddc owns physical display identity/correlation, provider and transport
abstractions, DDC/MCCS/EDID/DPCD, VCP GET/SET primitives, standard semantic
knowledge, monitor probing, safe readable-control inventories,
characterization algorithms and session/data models, evidence/provenance,
semantic conflict detection, feature-specific correlation/ranking, validation
policy, and profile schema/parsing/generation/resolution. It exposes these
capabilities through public plain-C APIs and research harnesses.

It must remain independently useful and must not own product GUI, Stream Deck
concepts, product-specific onboarding, HTTP download scheduling, telemetry or
submission UX, menu-bar UX, or automation UX.

## Product ownership

The standalone private **Rogue Display Control** product owns the native macOS
system-utility experience: Configurator Full, rich monitor dashboard,
interactive characterization, diagnostics, profiles/presets, control surfaces,
and future system-utility features.

The private **Rogue Display Control for Stream Deck** product owns Stream Deck
actions, Property Inspectors, plugin lifecycle, and Configurator Light. It is
autonomous-first and intentionally exposes less complexity. These products do
not need to share a UI implementation; their common source of truth is
rss-ddc.

## Profile ownership

rss-ddc owns profile semantic meaning, schema, parser, validation, resolver,
and generated characterization format. Products own Application Support
placement, user-facing management, profile-feed network transport, and
commercial update behavior. A curated Rogue profile database/service may be
private/commercial and is not part of this MIT library.

## Planned characterization architecture

The following is an architecture roadmap, not an implemented API claim:

- **Quick Auto Probe**: identity, transport, MCCS, standards knowledge, known
  profiles, and safe GET-only observation.
- **Extended Auto Probe**: broader GET-only inventory, stability analysis,
  vendor/private readable controls, and cached monitor-wide inventory.
- **Guided Discovery**: user-changed physical OSD states captured and
  correlated by feature-specific investigation modules.
- **Experimental Validation**: explicit, narrowly scoped candidate SET with a
  strong safety policy and no blind writes.

Picture Mode is feature module #1, not the generic characterization design.
Future modules may investigate inputs, response time, adaptive sync, black
equalizer, volume, sharpness, and more.

Correlation is not semantic identity. A Picture Mode preset can alter
brightness while `0x10` remains brightness, not a Picture Mode selector.
Characterization therefore combines mathematical correlation, semantic
knowledge, evidence/provenance, and explicit validation.
