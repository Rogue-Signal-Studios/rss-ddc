# Profile packs

Profile packs are JSON disk-interchange documents. The internal representation is plain C, so a compact format may be added later.

```json
{
  "schemaVersion": 1,
  "databaseVersion": "2026.08.13.1",
  "minimumRSSDDCVersion": "0.3.0",
  "packId": "rogue-builtin",
  "profiles": [{
    "id": "lg-hdr-qhd-dcpdp13-dcpext0",
    "identity": {"productName": "LG HDR QHD", "provider": "DCPDP13Service", "transport": "DCPEXT0", "external": true},
    "confidence": "hardware-validated",
    "controls": [{"id": "picture-mode", "method": "vcp", "address": 21, "readable": true, "writable": true, "confidence": "hardware-validated", "enums": [
      {"id": "custom", "name": "Custom", "value": 11}, {"id": "vivid", "name": "Vivid", "value": 49}, {"id": "hdr-effect", "name": "HDR Effect", "value": 39}, {"id": "cinema", "name": "Cinema", "value": 48}, {"id": "fps", "name": "FPS", "value": 30}, {"id": "rts", "name": "RTS", "value": 31}, {"id": "color-weakness", "name": "Color Weakness", "value": 6}, {"id": "reader", "name": "Reader", "value": 1}
    ]}]
  }]
}
```

The schema, database version, minimum rss-ddc version, and profiles are required. v1 rejects unsupported schemas, malformed JSON, duplicate identities/controls/enums, invalid methods/VCP addresses/ranges, missing identity fields, incompatible minimum versions, and unsafe writable operations. Unknown optional keys are ignored; a future required field needs a schema revision.

rss-ddc exposes metadata and local file/buffer loading only. A future consumer may fetch a manifest, verify an authenticated signature and SHA-256, validate the candidate with rss-ddc, atomically replace its file, and swap its store. SHA-256 alone is integrity—not authenticity—and signature/key policy belongs outside rss-ddc.

Local packs use the same schema but load as local provenance and are independent from downloaded packs. No GPL ddccontrol-db XML, mappings, generated database, or derived data is copied, imported, or shipped; any future ddccontrol-db hint stays research-only until independently validated.

A future opt-in contribution feature can export the semantic mappings, provider,
transport, non-unique model identity, and evidence state through the export
API, while omitting serial and other uniquely identifying fields. rss-ddc does
not assume consent or implement submission transport.
