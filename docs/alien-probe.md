# Alien Probe Quick observation core

Slice 7 restores the first, deliberately narrow Alien Probe phase: Quick
Probe. It is a bounded read-only observation collector, not a monitor scanner
or control engine. It has no SET, input-switch, picture-mode, verify, restore,
fallback, or private-transport callback.

## Historical final semantics

`b1cf501` introduced a six-control Quick Probe which used only Get VCP and,
when already available, MCCS capabilities retrieval. It made two reads per
control and preserved the first valid result when the second differed.
`bfac138` made the crucial interpretation corrections:

- A protocol-reported maximum is not an observed numeric range and never a
  writable range.
- An observed raw value, an MCCS declaration, and profile knowledge remain
  distinct evidence with separate provenance.
- Stable and changing reads are observations; neither creates write authority.
- Empty identity facts are not synthesized, and MCCS provenance is represented
  once as a source rather than duplicated into a presumed capability fact.

The reconstructed model carries these semantics into Slice 6's smaller route
model. Live reads create `OBSERVED` routes with `write_authorized=false`;
MCCS advertisement creates a separate `DECLARED` route. Existing profile
knowledge is consulted only to label an observation `profile-known`; it is not
merged, overwritten, or applied.

## Exact read set and timing

Quick Probe performs exactly two immediate Get VCP calls (repeat delay: 0 ms)
for each of the following known standard controls, in this order:

| VCP | Semantic |
| --- | --- |
| `0x10` | `display.brightness` |
| `0x12` | `display.contrast` |
| `0x14` | `display.color_preset` |
| `0x16` | `display.rgb.red_gain` |
| `0x18` | `display.rgb.green_gain` |
| `0x1a` | `display.rgb.blue_gain` |

MCCS capabilities are retrieved once only when the selected display already
advertises the existing `RSS_DDC_CAP_MCCS_CAPABILITIES` support. This gives a
maximum of 13 reads. No VCP `0x60`, unknown code, vendor code, or `0x00`–`0xff`
sweep is part of this slice.

There is no retry-until-valid behavior. Two identical strict results are
`stable`; a different or failed second result is `variable`. A failed first
read remains a partial observation failure and does not infer unsupported
support from a single error.

## Validity and classifications

Quick Probe calls only the existing public `rss_ddc_get_vcp` API. Therefore a
normal successful result has already passed the established source, length,
command, status, echoed-VCP, and checksum checks. The observer additionally
defends its injected/test callback boundary by rejecting a success with the
wrong echoed VCP.

Each observation records transport state, strict protocol acceptance,
request-match, first and repeat error, MCCS-advertised state, profile-known
state, stability, current value, and reported maximum. The result category is
one of `stable`, `variable`, `protocol-reported`, `malformed`,
`semantic-mismatch`, or `transport-error`. `current > maximum` remains valid
protocol data but is flagged as unusual; it is not treated as either malformed
or a conventional bounded scalar range.

## Ownership and bounds

An `RSSDDCProbe` and its six observations are heap-backed. The parsed MCCS
model is allocated only when requested and is probe-owned. Knowledge routes are
individually heap-allocated then copied into the bounded Slice 6 knowledge
object; no variable-size collection is placed on a stack frame. The collection
limit is six observations and at most twelve retained knowledge facts (one live
observation plus one MCCS declaration for each control).

The main quick routine has only pointer, index, and two `RSSDDCVCPResult`
locals (roughly a few dozen bytes); its potentially multi-kilobyte MCCS model
and the public display snapshot used by the convenience API are heap-backed.
It never constructs `RSSMacOSBinding`, and Make's existing public/private
header dependency protection remains unchanged.

## CLI

Run `./rss-ddc probe-quick <display-index>`. Its first line explicitly states
`READ-ONLY; writes=0`, and each line reports the separate transport, protocol,
semantic, advertisement, profile, value, and stability states. Obtain a fresh
index with `./rss-ddc list`; list indexes are process-local snapshots.
