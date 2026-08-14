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

## Extended Probe observation core

Slice 8 restores read-only Extended Probe on top of the validated Quick Probe
semantics. It scans exactly `0x00` through `0xFF` in ascending order with no
write callback, input switching, picture-mode mutation, fallback transport, or
write authorization.

### Shared observation machinery

Quick and Extended Probe share one single-VCP observation path:

- the same `RSSDDCProbeResultCategory` classification rules
- the same strict parser acceptance inherited from `rss_ddc_get_vcp`
- the same echoed-VCP request-match defense at the injected transport boundary
- the same two-read stability policy (`stable` vs `variable`)
- the same conservative monitor-knowledge provenance (`OBSERVED` live reads and
  separate `DECLARED` MCCS facts, always `write_authorized=false`)

Extended Probe does not implement a looser parser or a separate “readable”
capability counter.

### Scan bounds and timing

| Parameter | Value |
| --- | --- |
| Address range | `0x00`..`0xFF` (256 requested VCPs) |
| Repeat count | `2` immediate GETs per address |
| Inter-address delay | `25 ms` |
| Repeat delay | `25 ms` before the second GET |
| Maximum GET count | `512` (256 × 2) |
| Optional MCCS retrieval | `1` when `RSS_DDC_CAP_MCCS_CAPABILITIES` is already supported |
| Transport-failure abort | `8` consecutive transport-level failures stop the scan; remaining addresses are `unattempted` |

Providers without a validated GET path (`AppleDCPMCDP29XX`, unknown) fail
closed. `AppleDCPPS190`, `DCPDP13Service`, and `DCPDPService` are supported.

There is no retry-until-success behavior.

### Classifications and interpretation

Each address records transport state, strict protocol validity, request match,
first/repeat errors, MCCS-advertised state (`yes`/`no`/`unknown`), profile-known
state, stability, current/max when protocol-valid, and an interpretation label:

- `observed-protocol-valid`
- `observed-advertised`
- `observed-unadvertised`

Counters distinguish `strict-valid`, `stable-valid`, `variable-valid`,
`protocol-reported`, `semantic-mismatch`, `malformed`, and `transport-errors`,
plus `advertised-valid` and `unadvertised-valid` among strict-valid results.

A stable, protocol-valid, unadvertised reply with unusual `current > maximum`
remains an observation only. It is not labeled “supported” and does not create
write authority. When MCCS advertises enum values for a VCP, observed currents
are correlated against that declared enum set where present; `current <= maximum`
is not required for enumerated controls. Scalar advertised VCPs without a
parenthesized value list report `enum-list=absent` and
`current-in-declared-enum=unknown`; only explicit lists use `present` with
`yes`/`no` membership.

### Ownership and bounds

Extended observations live in a heap-backed array of 256
`RSSDDCProbeExtendedObservation` records. Knowledge routes are individually
heap-allocated and copied into the bounded monitor-knowledge object. The scan
loop keeps only pointer/index state and two `RSSDDCVCPResult` locals on its stack
frame; it never constructs `RSSMacOSBinding` or places the observation array on
the stack.

### CLI

Run `./rss-ddc probe-extended <display-index>`. The summary line states
`READ-ONLY; writes=0`, reports request/timing policy, aggregate counters,
duration, and abort state. Each attempted VCP prints transport, protocol,
semantic, advertisement, enum-correlation, value, and stability fields.
Obtain a fresh index with `./rss-ddc list`.
