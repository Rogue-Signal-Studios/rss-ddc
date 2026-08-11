# Apple Silicon DDC transport notes

## Evidence scope

**Proven hardware behavior** is limited to the PS190 Service-path Get VCP transactions below, on an Odyssey G75F. **Static-analysis conclusions** cover the no-offset transport sentinel. Other provider behavior remains unimplemented or unsupported.

## PS190 Get VCP

For `AppleDCPPS190`, a normal `IOAVServiceWriteI2C(..., data=0x51, ...)` uses register/subaddress preparation and is not the validated DDC/CI GET framing. The working request is raw-framed and uses `UINT32_MAX` for both calls:

```text
write chip=0x37, data=UINT32_MAX, payload=51 82 01 <VCP> <checksum>
delay 50 ms
read  chip=0x37, data=UINT32_MAX, length=11
```

The request checksum is `0x6e ^ 0x51 ^ 0x82 ^ 0x01 ^ VCP`.

| VCP | Request | Reply | Decoded value |
| --- | --- | --- | --- |
| `0x10` | `51 82 01 10 ac` | `6e 88 02 00 10 00 00 32 00 32 a4` | max 50, current 50 |
| `0x60` | `51 82 01 60 dc` | `6e 88 02 00 60 00 00 12 00 12 d4` | max 18, current 18 |

The parser validates source, framing, response length, command, status, requested VCP, decoded maximum/current values, and checksum. BetterDisplay independently reported the same current values; it was used only as a behavior oracle.

No out-of-bounds or canary corruption was observed in the research lab's guarded request/reply buffers. This does not establish behavior on different providers, monitors, or macOS releases.
