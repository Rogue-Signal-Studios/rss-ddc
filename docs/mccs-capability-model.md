# MCCS capability model

`rss_ddc_parse_mccs_capabilities` is a pure, bounded parser. It consumes only
caller-provided bytes and never enumerates displays, constructs an IOAV service,
or sends an I2C request. The raw capability string is preserved verbatim for
diagnostics; recognized `vcp(...)` declarations are exposed as raw VCP codes
and optional raw enum bytes.

The model is evidence, not authorization. A parsed VCP declaration does not
claim that a read is reliable or that a write is safe. Live retrieval, provider
support, and any transport-specific framing are deliberately deferred to a
separate reconstruction slice.
