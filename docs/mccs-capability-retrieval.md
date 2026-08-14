# Guarded MCCS capabilities retrieval

`rss_ddc_get_mccs_capabilities` is a read-only DCPDP13-only operation. It
issues conventional DDC/CI F3 requests through the existing service tuple and
accepts only exact, checksum-valid E3 frames whose echoed offset matches the
request. PS190, DCPDPService, and other providers remain unsupported.

Each request uses a 38-byte receive capacity surrounded by canaries. The
portable collector initializes the receive bytes to `0xcc`, derives the frame
size from the declared length, and hands only that exact prefix to the E3
parser. Tail bytes—including valid-looking stale frames—are never appended or
parsed. Multipart collection is heap-backed, capped at 4,096 text bytes and
129 requests, and completes only on an explicit zero-length fragment.
