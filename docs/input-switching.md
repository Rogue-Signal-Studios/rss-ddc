# Input switching

rss-ddc exposes input selection through the structured public API:

```c
RSSDDCError rss_ddc_set_input(uint32_t display_index,
                              RSSDDCInputSwitchMethod method,
                              uint16_t value);
```

`RSS_DDC_INPUT_SWITCH_STANDARD` is exactly the existing provider-specific
`SetVCP(0x60, value)` path. It remains standards-oriented: input values and
whether a monitor accepts them are monitor concerns, while normal provider
dispatch and any separate verification policy remain unchanged.

`RSS_DDC_INPUT_SWITCH_LG_ALT` is a write-only alternate input transport proven
on the documented LG HDR QHD / `DCPDP13Service` / `DCPEXT0` topology. rss-ddc
keeps its framing internal: applications supply only the alternate value. The
transport performs two writes with a 10 ms pre-delay each, and does no GET,
verification, restore, fallback, or retry policy.

## Provider support

`RSS_DDC_CAP_ALTERNATE_INPUT` means a provider can issue the alternate
transport. It does **not** claim that every monitor on that provider uses or
supports it. At present only `DCPDP13Service` advertises this capability.
`DCPDPService`, `AppleDCPPS190`, `AppleDCPMCDP29XX`, and unknown providers fail
closed for `LG_ALT`.

Applications must select `LG_ALT` only from monitor-specific evidence or an
explicit user override; rss-ddc does not detect monitor brands or infer this
choice from MCCS capabilities.

## Validated LG alternate values

These are alternate-transport values, not ordinary MCCS VCP `0x60` values:

| Physical input | Value |
| --- | --- |
| DisplayPort 1 | `0xD0` |
| HDMI 1 | `0x90` |
| HDMI 2 | `0x91` |
