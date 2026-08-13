# Picture Mode

`rss-ddc` exposes Picture Mode only when the selected display matches a
validated monitor profile. It is deliberately a semantic API: consumers choose
a friendly mode and do not supply a raw VCP code or value.

```c
RSSDDCPictureMode mode = RSS_DDC_PICTURE_MODE_UNKNOWN;
if (rss_ddc_get_picture_mode(display_index, &mode) == RSS_DDC_OK) {
    /* `mode` is one of the validated values, or UNKNOWN for an unrecognized raw reply. */
}

RSSDDCError error = rss_ddc_set_picture_mode(display_index,
                                              RSS_DDC_PICTURE_MODE_VIVID);
```

## Validated profile

The current production profile is intentionally narrow:

| Identity field | Required value |
| --- | --- |
| External display | yes |
| Provider | `DCPDP13Service` |
| Reported product name | `LG HDR QHD` |
| Transport | `DCPEXT0` |

Only that exact selected-display identity receives
`RSS_DDC_CAP_PICTURE_MODE`. A different LG display, another transport, another
provider, or an unknown display fails closed with an unsupported result. This
does **not** state that VCP `0x15` is Picture Mode on every monitor.

## LG HDR QHD semantic modes

| API value | OSD label | Validated internal value |
| --- | --- | --- |
| `RSS_DDC_PICTURE_MODE_CUSTOM` | Custom | `0x0B` |
| `RSS_DDC_PICTURE_MODE_VIVID` | Vivid | `0x31` |
| `RSS_DDC_PICTURE_MODE_HDR_EFFECT` | HDR Effect | `0x27` |
| `RSS_DDC_PICTURE_MODE_CINEMA` | Cinema | `0x30` |
| `RSS_DDC_PICTURE_MODE_FPS` | FPS | `0x1E` |
| `RSS_DDC_PICTURE_MODE_RTS` | RTS | `0x1F` |
| `RSS_DDC_PICTURE_MODE_COLOR_WEAKNESS` | Color Weakness | `0x06` |
| `RSS_DDC_PICTURE_MODE_READER` | Reader | `0x01` |

The implementation keeps the VCP `0x15` operation and these raw values
internal. SET accepts only the eight semantic values. GET maps a known raw
reply to a semantic value; an unrecognized raw reply returns success with
`RSS_DDC_PICTURE_MODE_UNKNOWN`, never a guessed label.

The evidence comes from manually selecting all eight OSD modes, recording
read-only full fingerprints, and comparing the reports offline. VCP `0x15`
was the sole strong stable correlator. User-run one-shot SET validation then
confirmed Vivid and Reader.

## Deliberate boundaries

Picture Mode is distinct from all of the following:

- **Color Preset (VCP `0x14`)** is a standard color-temperature/user-preset
  control; it is not this monitor's broader OSD Picture Mode selector.
- **Brightness and contrast** remain their normal independent controls.
- **Advanced/raw VCP** access is separate from this API and does not gain an
  arbitrary Picture Mode-value escape hatch.

The monitor's MCCS capability string advertises `0x15(01 06 11 13 14 18 28 29
32 48)`, while validated working modes include values outside that list, such
as Vivid `0x31`. MCCS advertisement is therefore retained only as evidence; it
is not an operational mapping source for Picture Mode.

A Picture Mode SET issues exactly one semantic mode operation. It does not
write brightness (`0x10`), `F6`, `F7`, `F9`, `FE`, Color Preset (`0x14`), or any
other correlated secondary control. The monitor remains responsible for any
internal preset side effects.

The discovery provenance and broader LG transport evidence are recorded in
[the LG monitor entry](monitors/lg-hdr-qhd.md). The reusable research harness
remains separate from this production capability.
