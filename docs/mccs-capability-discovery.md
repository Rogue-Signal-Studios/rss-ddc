# MCCS capability discovery

## Status

rss-ddc has a portable, bounded MCCS capabilities-string parser and strict
parsers/builders for individual DDC/CI capability packets. It provides one
developer-only DCPDP13Service experiment, `probe-mccs-capabilities`, which
sends exactly one offset-zero request and reads exactly one bounded reply. It
does **not** retrieve a full string, advertise a provider capability, or
provide a normal `rss-ddc capabilities` command.

| Claim | Status |
| --- | --- |
| MCCS `F3` request / `E3` reply packet format | Research-backed |
| Portable capability-string parser | Synthetic-test validated |
| PS190 GET VCP transport | Hardware validated in its documented scope |
| DCPDP13Service GET VCP transport | Hardware validated in its documented scope |
| DCPDPService GET VCP transport | Hardware validated in its documented scope |
| DCPDP13Service one-fragment `probe-mccs-capabilities` | Research-backed validation harness |
| Any macOS runtime MCCS capability retrieval | Unsupported |
| AppleDCPMCDP29xx MCCS capability retrieval | Unsupported |

MCCS capability data is neither EDID nor DPCD. EDID describes display identity
and video modes; DPCD describes the DisplayPort receiver/link; an MCCS
capabilities string is monitor-supplied control metadata. It can advertise a
VCP feature and, for selected non-continuous features, raw candidate values.
It is evidence for UI construction, not proof that a GET or SET is safe.

For example, `vcp(60(0f 11 12))` advertises raw values `0x0f`, `0x11`, and
`0x12` for Input Source. rss-ddc does not label those values, infer a physical
connector, or test a power/input control. Consumers may apply standardized
semantics or their own monitor knowledge separately.

## Wire protocol research

The DDC/CI Capabilities Request command is `0xf3`; its reply command is
`0xe3`. A request at offset `0x0120` has the ordinary DDC/CI bytes:

```text
51 83 f3 01 20 6e    inline-source/raw representation
83 f3 01 20 3f       conventional Service payload; 0x51 is supplied out-of-band
```

`0x83` means three message bytes (`f3`, offset high, offset low). The offset is
a 16-bit byte offset into the capabilities string. Every fragment must be
requested explicitly; the next request uses `requested_offset + returned_data
length`. The monitor must echo the requested offset in the reply. A zero-byte
fragment is the protocol completion marker.

One reply has this form:

```text
6e 80+N e3 offset-high offset-low <0..32 ASCII bytes> checksum
```

`N` is the number of bytes after the length byte and before the checksum. It is
at least three (`e3` plus two offset bytes) and at most 35, leaving 0–32
capability-string bytes. IOAV returns the source byte but not the implicit host
read address, so its valid reply frame is 6–38 bytes (not 39). The
request checksum is XOR of `0x6e`, source `0x51`, and all request bytes before
the checksum. A reply checksum is XOR of the host read address `0x50` and every
received reply byte before its checksum. The portable packet parser validates
the reply source, length, `e3`, exact received size, and checksum before it
exposes the transient fragment view.

The reference implementation research confirms the request/reply opcodes,
offset echo, bounded 32-byte fragment payload, sequential fragment collection,
and completion behavior. It also documents the required 50 ms delay before the
next capability/table segment. See ddcutil's
[packet builder](https://github.com/rockowitz/ddcutil/blob/master/src/base/ddc_packets.c),
[multi-part reader](https://github.com/rockowitz/ddcutil/blob/master/src/ddc/ddc_multi_part_io.c),
and [capabilities documentation](https://www.ddcutil.com/command_capabilities/).

## Portable API and ownership

`rss_ddc_parse_mccs_capabilities(raw, length, &capabilities)` parses bytes the
caller has already obtained. `RSSDDCMCCSCapabilities` is entirely
caller-owned: it contains a verbatim, NUL-terminated raw copy plus bounded VCP
entries and raw enum-value storage. No release function or macOS type is
involved. `rss_ddc_mccs_capabilities_has_vcp()` checks an advertised feature;
`rss_ddc_mccs_capabilities_enum_values()` returns a borrowed slice that remains
valid until the caller overwrites its `RSSDDCMCCSCapabilities` object.

The public `RSSDDCDisplay.capabilities` bitmask remains provider/runtime
evidence only (`GET`, `SET`, `EDID`, `DPCD`). It intentionally does not encode
monitor-advertised MCCS data and has no premature capabilities-string bit.

## Parser and size policy

The parser accepts upper- or lower-case two-digit hexadecimal codes,
whitespace, nested enum lists, unknown top-level sections, and either a full
outer capabilities wrapper or a sequence of sections. It rejects unbalanced
syntax, malformed hex tokens, nested enum syntax, empty enum lists, duplicate
VCP features, and embedded NUL bytes. It never recurses.

Input is capped at 4,096 bytes and only copied after that bound is checked. The
parsed object stores at most 256 VCP features and 1,400 enum values. An input
that exceeds a bound returns `RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE`; malformed
or truncated syntax returns `RSS_DDC_ERROR_CAPABILITIES_MALFORMED`. These
bounds are intentionally much larger than common strings while remaining a
small caller-owned object. A future transport accumulator must additionally
cap itself at 4,096 bytes and 129 requests (128 non-empty 32-byte fragments
plus one zero-byte completion), reject a wrong offset, and stop immediately on
malformed framing or a size limit.

## One-fragment DCPDP13 validation probe

`rss-ddc probe-mccs-capabilities <display-index>` is intentionally available
only for a selected `DCPDP13Service` display that passes the existing external,
single-service, correct-EPIC-provider, and UI-support safety gate. It performs:

1. one conventional Service write at `chip=0x37`, `data=0x51`, containing
   `83 f3 00 00 1e`;
2. one 50 ms delay, reusing the existing conventional-GET timing evidence;
3. one `chip=0x37`, `data=0x51`, **38-byte** read into a buffer prefilled with
   `0xcc`; and
4. strict parsing of only the declared `E3` frame prefix.

It prints the complete 38-byte buffer before and after the read, requested
length, IOReturns, declared frame length, tail sentinel count, echoed offset,
printable fragment text, and 16-byte before/after canary status. It sends no second offset, retries nothing, does
not parse the complete capabilities string, and never writes a VCP value.

The IOAV ABI declaration has caller-supplied `outputBufferSize` but no
actual-bytes-read out parameter. The fixed 38-byte window is therefore safe
only as a bounded experiment: the monitor framing byte determines the 6–38
byte prefix passed to the strict parser. Bytes after that prefix remain
diagnostic-only; a changed tail is not interpreted as protocol data. A malformed
header, impossible size, bad checksum, or wrong echoed offset fails closed.

## Why runtime retrieval is not yet enabled

`DCPDP13Service` and `DCPDPService` have hardware-validated conventional GET
VCP transactions: a four-byte Service write at `chip=0x37, data=0x51`, 50 ms,
then a fixed 11-byte read. The DCPDP13 probe is a deliberately isolated
experiment that tests whether the same caller-sized IOAV window can carry a
single standard, read-only MCCS transaction. It is not evidence for a second
transaction or a full retrieval loop. `AppleDCPPS190` has a different
hardware-validated raw GET shape: inline `0x51`, `UINT32_MAX` no-offset, 50
ms, then a fixed 11-byte read. No source establishes whether PS190 `F3`
requires the raw or conventional representation, so PS190 is excluded.

Only a successful DCPDP13 first fragment can justify an equally constrained
next-offset experiment. Multipart retrieval additionally needs evidence that a
second `F3` request at the exact next offset produces a valid, matching `E3`
reply after the documented inter-fragment delay. DCPDPService needs its own
first-fragment evidence, PS190 needs a framed-request decision, and MCDP
remains out of scope.

The capabilities string is also often incomplete or wrong. ddcutil explicitly
warns that monitors can omit features they implement and that multi-exchange
retrieval is error-prone. Rogue Display Control should eventually use the C API
directly to present *candidate* raw controls, retain its friendly-label/profile
layer, and never scrape CLI output.
