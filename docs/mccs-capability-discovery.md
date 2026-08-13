# MCCS capability discovery

## Status

rss-ddc has a portable, bounded MCCS capabilities-string parser and strict
parsers/builders for individual DDC/CI capability packets. Normal runtime
retrieval is available only for `DCPDP13Service`, through the public
caller-owned API and `rss-ddc capabilities <display-index>` command.

| Claim | Status |
| --- | --- |
| MCCS `F3` request / `E3` reply packet format | Research-backed |
| Portable capability-string parser | Synthetic-test validated |
| PS190 GET VCP transport | Hardware validated in its documented scope |
| DCPDP13Service GET VCP transport | Hardware validated in its documented scope |
| DCPDPService GET VCP transport | Hardware validated in its documented scope |
| DCPDP13Service/LG HDR QHD one-fragment `probe-mccs-capabilities` | Hardware-observed valid prefix; read-window tail unresolved |
| DCPDP13Service/LG HDR QHD exact-first-frame repeat | Hardware validated at offset zero / 16-byte read |
| DCPDP13Service/LG HDR QHD one offset-`0x000a` probe | Hardware validated sequential fragment |
| DCPDP13Service/LG HDR QHD bounded multipart retrieval | Hardware validated: 35 requests / 336 text bytes / explicit completion |
| DCPDP13Service runtime MCCS capability retrieval | Supported, bounded public API |
| PS190, DCPDPService, and MCDP MCCS capability retrieval | Unsupported |
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
checksum representation follows the provider framing: conventional Service
payloads XOR `0x6e` with the payload bytes before the checksum, while raw
inline-source frames also include their inline `0x51`. A reply checksum is XOR
of the host read address `0x50` and every received reply byte before its
checksum. The portable packet parser validates
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

The public `RSSDDCDisplay.capabilities` bitmask includes both provider/runtime
and narrowly validated profile evidence. `RSS_DDC_CAP_MCCS_CAPABILITIES` is a
transport-support bit, not monitor content: it is enabled only for
`DCPDP13Service`. The separate Picture Mode profile never derives its mapping
from these enum values. The returned
`RSSDDCMCCSCapabilities` remains wholly caller-owned, including raw text,
features, and enum storage; no release function or macOS object is involved.
`rss_ddc_mccs_capabilities_enum_values()` returns a borrowed slice valid until
the caller overwrites that result object.

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

## Historical one-fragment DCPDP13 validation probe

Before normal runtime support, the removed
`probe-mccs-capabilities <display-index>` command was available only for a
selected `DCPDP13Service` display that passed the existing external,
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
actual-bytes-read out parameter. The fixed 38-byte window was originally a
bounded experiment: the monitor framing byte determines the 6–38 byte prefix
passed to the strict parser. A malformed header, impossible size, bad checksum,
or wrong echoed offset fails closed. Actual LG hardware evidence below shows
that bytes after that prefix cannot yet be treated as benign merely because the
canaries remain intact.

## LG HDR QHD / DCPDP13Service hardware evidence (2026-08-12)

The user manually ran the one-fragment probe against list index 2 after
confirming `LG HDR QHD`, `DCPDP13Service`, and the existing safety-gate
correlation. The conventional offset-zero write succeeded, the 50 ms read
succeeded, and the 38-byte returned window was:

```text
6e 8d e3 00 00 28 70 72 6f 74 28 6d 6f 6e 69 4c
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
6e 8d e3 00 00 28
```

The canaries surrounding the 38-byte allocation were intact. `0x8d` encodes a
13-byte DDC data field, so the declared frame is exactly 16 bytes: source,
length, 13 data bytes, and checksum. The complete validated fragment is:

```text
6e 8d e3 00 00 28 70 72 6f 74 28 6d 6f 6e 69 4c
```

It echoes offset `0x0000` and carries the 10-byte text `(prot(moni`. Its reply
checksum is `0x4c`: `0x50 ^ 0x6e ^ 0x8d ^ 0xe3 ^ 0x00 ^ 0x00 ^ 0x28 ^ 0x70 ^
0x72 ^ 0x6f ^ 0x74 ^ 0x28 ^ 0x6d ^ 0x6f ^ 0x6e ^ 0x69 = 0x4c`.

All 22 bytes after that declared frame changed from their `0xcc` sentinel:
16 zero bytes followed by `6e 8d e3 00 00 28`, the beginning of the validated
frame. This is **not** part of the declared E3 packet, and the strict parser
does not read or parse it. It is also not proof that the tail is harmless.
The observed pattern is compatible with an IOAV/driver fixed-block or fill
behavior; a transport-level replay/extra-read effect is another possibility.
It is incompatible with simple unchanged caller memory, and it is not the F3
request (which begins `83 f3`). The present evidence cannot distinguish the
driver, lower transport, or monitor as the source. Multipart retrieval remains
unsupported.

The historical exact-window follow-up,
`probe-mccs-capabilities-exact-first-frame <display-index>`, repeated the
same conventional F3 offset-zero transaction once, waits 50 ms, and passes an
**exact 16-byte** output length to IOAV. The surrounding allocation still has
22 sentinel bytes after that requested range plus the existing outer canaries.
It accepts only the exact observed 16-byte LG frame and requires all 22
unrequested bytes and both canaries to remain unchanged. It sends no next
offset, retries nothing, and is refused unless the selected DCPDP13 display is
named exactly `LG HDR QHD`.

The user then manually ran that exact-first-frame follow-up against the same
selected display. The one F3 offset-zero write and 16-byte read both returned
success. The returned bytes exactly matched the valid frame above; its declared
size was 16, both outer canaries remained intact, and all 22 bytes after the
requested read range remained `0xcc`. This ties the earlier overwritten tail
to the larger requested read window. It does not promote that tail to MCCS
data, nor does it validate a second offset.

## Validated one-offset-`0x000a` probe

The validated first text fragment has length 10, so the only candidate next
offset is `0x000a`. The conventional Service request is:

```text
83 f3 00 0a 14
```

Its checksum is independently derived as `0x6e ^ 0x83 ^ 0xf3 ^ 0x00 ^ 0x0a =
0x14`. The developer-only
removed `probe-mccs-capabilities-next-fragment <display-index>` command was refused
unless the selected display is exactly the recorded `LG HDR QHD` DCPDP13
target. It sends exactly that one request, waits 50 ms, and performs one
guarded 38-byte read. A smaller pre-read is intentionally not attempted: there
is no evidence that IOAV supports a non-consuming length discovery read. An
exact 16-byte read is also unavailable for an unknown second-frame size.

The user manually ran this command on the same LG/DCPDP13Service/DCPEXT0
binding. It returned the valid 16-byte E3 frame at echoed offset `0x000a`, with
text `tor)type(l`, coherent with the first fragment `(prot(moni` to form
`(prot(monitor)type(l`. The 38-byte window again had the 22-byte modified tail,
but canaries remained intact. The command validates only the declared E3
prefix, checksum, and echoed offset; the tail remains diagnostic-only and is
never parsed.

## Bounded DCPDP13 runtime retrieval

The full developer harness completed successfully on `LG HDR QHD` /
`DCPDP13Service` / `DCPEXT0`: 35 requests produced 336 text bytes, every
write/read returned success, canaries remained intact, every E3 offset and
checksum validated, and a zero-length E3 explicitly completed the sequence.
The assembled string parsed successfully and advertised VCP `0x60` raw values
`11 12 0f 00` in that order.

That evidence promotes the same bounded transport to
`rss_ddc_get_mccs_capabilities()` and its diagnostic form. It is currently
supported for `DCPDP13Service` only; PS190, DCPDPService, and MCDP return an
explicit unsupported error. The normal CLI command is `rss-ddc capabilities
<display-index>` and uses the public API rather than a duplicate transport.

Starting at offset zero, runtime retrieval issues exactly one conventional F3
request, 50 ms delay, and guarded 38-byte read per requested offset. It
strictly validates the declared E3 prefix and echoed offset, copies only the
declared text bytes, advances by exactly that text length, and stops only on a
valid zero-length E3 completion frame.

The collector fails closed at 4,096 accumulated bytes, 32 text bytes per
fragment, 129 total requests (128 data fragments plus completion), and uint16
offset overflow. It never retries, scans an ignored tail, falls back to another
framing/provider, or advances by frame/read/tail length. Each transaction logs
the request number/offset, raw reply, canary state, declared size, text length,
ignored-tail diagnostics, and resulting next offset. At explicit completion,
the public library API parses the bounded assembled string into caller-owned
`RSSDDCMCCSCapabilities` and returns it; optional diagnostics report transport
activity but do not print presentation output. The normal `rss-ddc capabilities
<display-index>` command calls that public API, then prints the raw capabilities
string, advertised VCP codes, and raw VCP `0x60` enum values without
connector-name mapping. The historical probe CLI commands were removed once
this normal path was validated; their packet fixtures and hardware evidence
remain in tests and this document.

The public error model preserves write/read and strict E3 checksum/framing
errors. It separately reports unsupported provider/capability, accumulated-size
overflow, request-limit exhaustion, offset overflow, incomplete completion, and
the final portable-parser error; it never silently treats a failure as an empty
capabilities string.

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

The validation establishes normal bounded DCPDP13 transport support, but not a
portability claim for every monitor, topology, adapter, firmware, or macOS
release. The 38-byte window remains a bounded practical read for an unknown
fragment length, not a claim about actual-byte-count semantics; only its
declared E3 prefix is protocol data. Public retrieval returns raw advertised
values as candidate/support evidence and never performs a SET based on them.
DCPDPService needs its own first-fragment evidence, PS190 needs a
framed-request decision, and MCDP remains out of scope.

The capabilities string is also often incomplete or wrong. ddcutil explicitly
warns that monitors can omit features they implement and that multi-exchange
retrieval is error-prone. Rogue Display Control should eventually use the C API
directly to present *candidate* raw controls, retain its friendly-label/profile
layer, and never scrape CLI output. The LG Picture Mode profile is the current
example: its validated operational values deliberately override the conflicting
MCCS `0x15` enum advertisement.
