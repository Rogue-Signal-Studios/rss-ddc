#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "edid.h"

static void checksum(uint8_t block[RSS_DDC_EDID_BLOCK_SIZE]) {
    uint8_t sum = 0;
    for (size_t index = 0; index < RSS_DDC_EDID_BLOCK_SIZE - 1; ++index) sum = (uint8_t)(sum + block[index]);
    block[RSS_DDC_EDID_BLOCK_SIZE - 1] = (uint8_t)(0u - sum);
}

static RSSDDCEDID fixture(uint8_t extensions, size_t blocks) {
    RSSDDCEDID edid = {.length = blocks * RSS_DDC_EDID_BLOCK_SIZE};
    uint8_t *base = edid.bytes;
    memcpy(base, (const uint8_t[]){0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}, 8);
    /* SAM: 19, 1, 13 encoded as five-bit EISA letters. */
    base[8] = 0x4c; base[9] = 0x2d;
    base[10] = 0x34; base[11] = 0x12;
    base[12] = 0x78; base[13] = 0x56; base[14] = 0x34; base[15] = 0x12;
    base[16] = 12; base[17] = 34; base[18] = 1; base[19] = 4; base[21] = 60; base[22] = 34;
    base[54 + 3] = 0xfc; base[54 + 4] = 0; memcpy(base + 59, "Synth Panel\n", 12);
    base[72 + 3] = 0xff; base[72 + 4] = 0; memcpy(base + 77, "SERIAL 42  \n", 12);
    base[126] = extensions;
    checksum(base);
    for (size_t index = 1; index < blocks; ++index) { edid.bytes[index * 128] = 0x02; checksum(edid.bytes + index * 128); }
    return edid;
}

int main(void) {
    RSSDDCEDID edid = fixture(1, 2);
    RSSDDCEDIDInfo info = {};
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_OK);
    assert(strcmp(info.manufacturer_id, "SAM") == 0 && info.product_code == 0x1234 &&
           info.serial_number_present && info.serial_number == 0x12345678 && info.manufacture_week == 12 &&
           info.manufacture_year == 2024 && info.version == 1 && info.revision == 4 &&
           strcmp(info.monitor_name, "Synth Panel") == 0 && strcmp(info.serial_text, "SERIAL 42") == 0 &&
           info.declared_extension_count == 1 && info.received_block_count == 2 && info.extensions_complete &&
           info.extension_tags[0] == 0x02 && info.extension_types[0] == RSS_DDC_EDID_EXTENSION_CTA_861 &&
           info.extension_revisions[0] == 0);
    assert(rss_ddc_edid_block_checksum_valid(edid.bytes));
    RSSDDCEDID edid_13 = fixture(0, 1);
    edid_13.bytes[19] = 3; checksum(edid_13.bytes);
    assert(rss_ddc_parse_edid(&edid_13, &info) == RSS_DDC_OK && info.version == 1 && info.revision == 3);
    RSSDDCEDID base_only = fixture(1, 1);
    assert(rss_ddc_parse_edid(&base_only, &info) == RSS_DDC_OK && !info.extensions_complete);
    base_only.bytes[0] = 1;
    assert(rss_ddc_parse_edid(&base_only, &info) == RSS_DDC_ERROR_EDID_HEADER);
    base_only = fixture(0, 1); base_only.bytes[20] ^= 1;
    assert(rss_ddc_parse_edid(&base_only, &info) == RSS_DDC_ERROR_EDID_CHECKSUM);
    edid = fixture(1, 2); edid.bytes[128 + 5] ^= 1;
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_ERROR_EDID_CHECKSUM);
    edid = fixture(0, 1); edid.length = 127;
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_ERROR_EDID_LENGTH);
    edid = fixture(0, 1); edid.length = 256;
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_ERROR_EDID_LENGTH);
    edid = fixture(2, 2);
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_OK && !info.extensions_complete &&
           info.received_block_count == 2 && info.declared_extension_count == 2);
    edid = fixture(1, 2); edid.bytes[128] = 0x70; edid.bytes[129] = 0x20; checksum(edid.bytes + 128);
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_OK &&
           info.extension_types[0] == RSS_DDC_EDID_EXTENSION_DISPLAYID && info.extension_revisions[0] == 0x20);
    edid.bytes[128] = 0x99; checksum(edid.bytes + 128);
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_OK &&
           info.extension_types[0] == RSS_DDC_EDID_EXTENSION_UNKNOWN &&
           strcmp(rss_ddc_edid_extension_type_string(info.extension_types[0]), "unknown") == 0);
    RSSDDCEDIDBlockAddress address = {};
    assert(rss_ddc_edid_block_address(0, &address) && address.segment == 0 && address.offset == 0x00 && !address.requires_segment_pointer);
    assert(rss_ddc_edid_block_address(1, &address) && address.segment == 0 && address.offset == 0x80 && !address.requires_segment_pointer);
    assert(rss_ddc_edid_block_address(2, &address) && address.segment == 1 && address.offset == 0x00 && address.requires_segment_pointer);
    assert(!rss_ddc_edid_block_address(RSS_DDC_EDID_MAX_BLOCKS, &address));
    edid = fixture(0, 1); edid.bytes[12] = edid.bytes[13] = edid.bytes[14] = edid.bytes[15] = 0; checksum(edid.bytes);
    assert(rss_ddc_parse_edid(&edid, &info) == RSS_DDC_OK && !info.serial_number_present);
    puts("test_edid: passed");
    return 0;
}
