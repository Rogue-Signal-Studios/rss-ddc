# Semantic controls taxonomy

Monitor knowledge is semantic-first. A semantic identifier states *what* a
capability means; it does not imply a DDC/VCP transport, a writable method, or
an evidence level.

The initial taxonomy is deliberately extensible rather than a fixed C enum:

```text
monitor
├── identity: manufacturer, model, product identifiers, EDID, serial, family, matching evidence
├── display: brightness, contrast, sharpness, gamma, color temperature/preset,
│           RGB gains, saturation, hue, picture mode, HDR, local dimming
├── gaming: response time/overdrive, adaptive sync/VRR, black stabilizer,
│           motion blur reduction, aspect/refresh modes, game assists
├── inputs: available inputs, current input, switching, auto switching, per-input behavior
├── audio: volume, mute, speaker/source
├── multi_view: PiP, PbP, layouts, source assignment
├── usb_kvm: KVM, upstream routing, USB-C power delivery, hub behavior
├── power: power, standby, energy saving, proximity behavior
├── sensors: ambient light/proximity and automatic brightness/color behavior
├── panel_care: pixel refresh/shift, static/logo detection, uniform brightness, OLED protection
├── connectivity: DisplayPort, HDMI, USB-C, Thunderbolt, MST, networking
└── vendor: unclassified vendor-specific capabilities
```

Example semantic IDs are `display.brightness`, `display.picture_mode`,
`input.current`, `gaming.response_time`, and `audio.volume`. Refinement is
expected when research establishes a better hierarchy; unknown capabilities
remain representable under `vendor` without inventing a false semantic claim.

## Registry

`rss_ddc_semantic_registry_lookup` provides a small standards-known catalog
without turning semantic IDs into a closed enum: brightness (`0x10`), contrast
(`0x12`), color preset (`0x14`), and RGB gain (`0x16`, `0x18`, `0x1A`). It
records the expected value class and that these controls conflict as candidates
for unrelated semantic discovery, so a brightness observation cannot be
promoted to Picture Mode merely through correlation.

## Inputs are structured capabilities

An input is not one global integer. Each input value has a stable semantic ID,
friendly label, one or more method raw values, independent read/switch support,
transport implications, and a declaration of whether switching may remove or
change the DDC path. `inputs.current` can use standard MCCS `0x60`, an LG
alternate protocol, or another future transport without changing its semantic
identity.
