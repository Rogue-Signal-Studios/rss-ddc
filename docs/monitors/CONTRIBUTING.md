# Contributing monitor evidence

Monitor pages document monitor-specific evidence; provider transport behavior
belongs in the project [architecture](../architecture.md) and
[Apple Silicon transport notes](../apple-silicon-ddc.md). Do not promote a
single monitor observation into a provider-wide rule.

Use the evidence levels in the [catalog index](README.md). Only direct,
reproducible command/output evidence should be called **Hardware validated**.
Contributor findings should initially be labelled **Contributor-reported**
unless maintainers reproduce them.

## Suggested report format

### Monitor identity

- Manufacturer and full retail model, if known.
- Exact monitor-reported product name.
- Firmware version and manufacture/revision information, if available.
- Serial number is optional and may be redacted.

### Host and connection

- Mac model and Apple Silicon generation.
- macOS version and build.
- Connection path: built-in HDMI, native DisplayPort, USB-C → DisplayPort,
  USB-C → HDMI, or dock/adapter; identify the adapter/dock when relevant.
- Exact runtime provider: `AppleDCPPS190`, `DCPDP13Service`, `DCPDPService`,
  `AppleDCPMCDP29XX`, or `unknown`.

### Tested operations

For each GET, SET, or Set-and-Verify test, record the VCP code, requested or
expected value, observed value, success/failure, and evidence level. Record
known input codes only after experimental confirmation, and identify labels as
monitor-specific unless a standard source proves otherwise.

### Timing and retry observations

Record immediate success, delayed replies, malformed transients, retries, and
intermittent behavior precisely. Include the explicit verification policy
(`settle_ms`, retry count, and retry delay) when relevant. Do not convert a
single timing observation into a global recommendation.

### DPCD observations

Record the runtime provider, requested DPCD address/length, raw bytes, return
status, decoded revision/link-rate/lane fields, and evidence level. State
whether a read is a single bounded native read or another transport. Do not
infer DPCD values from a display name, and do not document an untested provider
as DPCD-capable. DPCD evidence is diagnostic only: it must not add profiles,
link tuning, or write behavior.

### Version and evidence

Include the exact rss-ddc version or output of:

```sh
git rev-parse HEAD
```

Attach `--verbose` output when possible, after removing serials, registry IDs,
or other sensitive information. Include request/reply bytes and parser results
for protocol claims. State whether a result was user-run, maintainer-run, or
recovered from earlier research.

## Review expectations

Keep provider transport facts and monitor quirks separate, link rather than
duplicate transport documentation, and state unknown identity fields instead
of guessing. Documentation alone is not authorization for a runtime profile:
do not add JSON/YAML, automatic matching, timing overrides, or monitor-specific
behavior as part of a compatibility submission. A production profile requires
separate reviewed implementation, exact fail-closed identity evidence, and
dedicated synthetic tests, as with the LG Picture Mode profile.
