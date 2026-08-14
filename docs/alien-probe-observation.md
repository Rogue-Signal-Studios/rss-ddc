# Alien Probe observation runs

Quick and Extended Auto Probe create `monitor-knowledge/v0.1` observation
documents. They are read-only records, not authorization to change monitor
state.

Quick Probe reads the six registry-defined standard VCPs and MCCS capabilities
where the selected provider already supports that read. Extended Probe starts
with the same identity/provenance context, then inventories the bounded
one-byte VCP space using only GET requests. A successful read retains the raw
current value, separately typed protocol-reported maximum, and a second-read
stability classification. A variable reply remains useful observation data.

MCCS capability text is retained once in a document `sources` record; evidence
uses `sourceId` rather than copying the raw string. MCCS advertisement means
only that the monitor advertised an address or enum raw value. It does not
identify an unknown control or authorize writing it.

An Extended run stages advertised VCPs first, standard controls second, and
the remainder of `0x00..0xff` last. The selected monitor receives no more than
one initial request per address and one repeat after a successful initial read.
Production scans use a conservative 25 ms inter-request delay. Eight
consecutive transport failures abort the remaining inventory and return a
partial document with structured diagnostics.

Unknown readable addresses use `vendor.unknown.vcp.xx`. That is a stable
observation identity, not a semantic claim. Later Guided Discovery or explicit
validation may interpret an inventory, but neither is part of an Auto Probe.
