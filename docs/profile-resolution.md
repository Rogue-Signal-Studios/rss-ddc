# Profile resolution

`rss_ddc_profile_store_resolve` is offline: it accepts an identity value and returns an effective profile without enumerating displays, constructing an IOAV client, or sending DDC/CI traffic.

Matching requires exact product name, provider, transport, and external flag. Optional manufacturer, serial, and branch IDs must match if supplied. Controls compose across every matching profile. For a duplicate semantic control the order is higher confidence, then source precedence (local, validated pack, built-in, research), then greater identity specificity. Equal-authority conflicting operations fail closed.

Consequently a local hardware-validated response-time control can compose with a bundled Picture Mode control, while a lower-confidence local observation cannot override a stronger validated control. The only v1 write authorization is non-research, writable, `hardware-validated` data with a supported safe method. Existing generic input, brightness, contrast, G75, and LG alternate-input paths remain unchanged in this milestone.

The LG Picture Mode semantic API now resolves a profile before issuing its single VCP operation. The effective profile lists all eight named choices. Unknown raw values are unmapped and unknown semantic choices fail closed.
