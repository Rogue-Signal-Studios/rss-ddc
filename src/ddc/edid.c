#include "edid.h"

#include <string.h>

enum {
    RSS_EDID_MANUFACTURER_OFFSET = 8,
    RSS_EDID_PRODUCT_OFFSET = 10,
    RSS_EDID_SERIAL_OFFSET = 12,
    RSS_EDID_WEEK_OFFSET = 16,
    RSS_EDID_YEAR_OFFSET = 17,
    RSS_EDID_VERSION_OFFSET = 18,
    RSS_EDID_REVISION_OFFSET = 19,
    RSS_EDID_WIDTH_OFFSET = 21,
    RSS_EDID_HEIGHT_OFFSET = 22,
    RSS_EDID_DESCRIPTOR_OFFSET = 54,
    RSS_EDID_DESCRIPTOR_SIZE = 18,
    RSS_EDID_DESCRIPTOR_COUNT = 4,
    RSS_EDID_EXTENSION_COUNT_OFFSET = 126,
    RSS_EDID_DESCRIPTOR_NAME = 0xfc,
    RSS_EDID_DESCRIPTOR_SERIAL = 0xff,
};

static const uint8_t rss_edid_header[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

bool rss_ddc_edid_block_checksum_valid(const uint8_t block[RSS_DDC_EDID_BLOCK_SIZE]) {
    if (block == NULL) return false;
    uint8_t sum = 0;
    for (size_t index = 0; index < RSS_DDC_EDID_BLOCK_SIZE; ++index) sum = (uint8_t)(sum + block[index]);
    return sum == 0;
}

bool rss_ddc_edid_block_address(size_t block_index, RSSDDCEDIDBlockAddress *address) {
    if (address == NULL || block_index >= RSS_DDC_EDID_MAX_BLOCKS) return false;
    *address = (RSSDDCEDIDBlockAddress){
        .segment = (uint8_t)(block_index / 2),
        .offset = (uint8_t)((block_index % 2) == 0 ? 0x00 : 0x80),
        .requires_segment_pointer = block_index >= 2,
    };
    return true;
}

const char *rss_ddc_edid_extension_type_string(RSSDDCEDIDExtensionType type) {
    switch (type) {
        case RSS_DDC_EDID_EXTENSION_CTA_861: return "CTA-861";
        case RSS_DDC_EDID_EXTENSION_DISPLAYID: return "DisplayID";
        case RSS_DDC_EDID_EXTENSION_UNKNOWN: return "unknown";
    }
    return "unknown";
}

static RSSDDCEDIDExtensionType extension_type(uint8_t tag) {
    if (tag == 0x02) return RSS_DDC_EDID_EXTENSION_CTA_861;
    if (tag == 0x70) return RSS_DDC_EDID_EXTENSION_DISPLAYID;
    return RSS_DDC_EDID_EXTENSION_UNKNOWN;
}

/* Descriptor text is fixed-width binary data; retain printable ASCII only and trim EDID padding. */
static void copy_descriptor_text(const uint8_t descriptor[RSS_EDID_DESCRIPTOR_SIZE], char output[RSS_DDC_TEXT_MAX]) {
    size_t written = 0;
    for (size_t index = 5; index < RSS_EDID_DESCRIPTOR_SIZE && written + 1 < RSS_DDC_TEXT_MAX; ++index) {
        uint8_t byte = descriptor[index];
        if (byte == 0x0a || byte == 0x00) break;
        if (byte >= 0x20 && byte <= 0x7e) output[written++] = (char)byte;
    }
    while (written != 0 && output[written - 1] == ' ') --written;
    output[written] = '\0';
}

RSSDDCError rss_ddc_parse_edid(const RSSDDCEDID *edid, RSSDDCEDIDInfo *info) {
    if (edid == NULL || info == NULL || edid->length < RSS_DDC_EDID_BLOCK_SIZE ||
        edid->length > RSS_DDC_EDID_MAX_BYTES || edid->length % RSS_DDC_EDID_BLOCK_SIZE != 0) {
        return RSS_DDC_ERROR_EDID_LENGTH;
    }
    const uint8_t *base = edid->bytes;
    if (memcmp(base, rss_edid_header, sizeof(rss_edid_header)) != 0) return RSS_DDC_ERROR_EDID_HEADER;
    if (!rss_ddc_edid_block_checksum_valid(base)) return RSS_DDC_ERROR_EDID_CHECKSUM;
    size_t blocks = edid->length / RSS_DDC_EDID_BLOCK_SIZE;
    uint8_t declared_extensions = base[RSS_EDID_EXTENSION_COUNT_OFFSET];
    if (blocks > (size_t)declared_extensions + 1) return RSS_DDC_ERROR_EDID_LENGTH;
    for (size_t block = 1; block < blocks; ++block) {
        if (!rss_ddc_edid_block_checksum_valid(base + block * RSS_DDC_EDID_BLOCK_SIZE)) return RSS_DDC_ERROR_EDID_CHECKSUM;
    }

    *info = (RSSDDCEDIDInfo){.received_block_count = blocks, .declared_extension_count = declared_extensions,
                              .extensions_complete = blocks == (size_t)declared_extensions + 1,
                              .present_extension_checksums_valid = true};
    for (size_t block = 1; block < blocks; ++block) {
        const uint8_t *extension = base + block * RSS_DDC_EDID_BLOCK_SIZE;
        info->extension_tags[block - 1] = extension[0];
        info->extension_types[block - 1] = extension_type(extension[0]);
        info->extension_revisions[block - 1] = extension[1];
    }
    uint16_t manufacturer = ((uint16_t)base[RSS_EDID_MANUFACTURER_OFFSET] << 8) | base[RSS_EDID_MANUFACTURER_OFFSET + 1];
    for (size_t index = 0; index < 3; ++index) {
        uint8_t letter = (manufacturer >> (10 - index * 5)) & 0x1f;
        if (letter == 0 || letter > 26) return RSS_DDC_ERROR_EDID_HEADER;
        info->manufacturer_id[index] = (char)('A' + letter - 1);
    }
    info->product_code = ((uint16_t)base[RSS_EDID_PRODUCT_OFFSET + 1] << 8) | base[RSS_EDID_PRODUCT_OFFSET];
    info->serial_number = (uint32_t)base[RSS_EDID_SERIAL_OFFSET] |
        ((uint32_t)base[RSS_EDID_SERIAL_OFFSET + 1] << 8) |
        ((uint32_t)base[RSS_EDID_SERIAL_OFFSET + 2] << 16) |
        ((uint32_t)base[RSS_EDID_SERIAL_OFFSET + 3] << 24);
    info->serial_number_present = info->serial_number != 0;
    info->manufacture_week = base[RSS_EDID_WEEK_OFFSET];
    if (info->manufacture_week != 0) {
        info->manufacture_year = (uint16_t)(1990 + base[RSS_EDID_YEAR_OFFSET]);
        info->manufacture_date_present = true;
    }
    info->version = base[RSS_EDID_VERSION_OFFSET];
    info->revision = base[RSS_EDID_REVISION_OFFSET];
    info->width_cm = base[RSS_EDID_WIDTH_OFFSET];
    info->height_cm = base[RSS_EDID_HEIGHT_OFFSET];
    for (size_t index = 0; index < RSS_EDID_DESCRIPTOR_COUNT; ++index) {
        const uint8_t *descriptor = base + RSS_EDID_DESCRIPTOR_OFFSET + index * RSS_EDID_DESCRIPTOR_SIZE;
        if (descriptor[0] != 0 || descriptor[1] != 0 || descriptor[2] != 0 || descriptor[4] != 0) continue;
        if (descriptor[3] == RSS_EDID_DESCRIPTOR_NAME) copy_descriptor_text(descriptor, info->monitor_name);
        else if (descriptor[3] == RSS_EDID_DESCRIPTOR_SERIAL) copy_descriptor_text(descriptor, info->serial_text);
    }
    return RSS_DDC_OK;
}
