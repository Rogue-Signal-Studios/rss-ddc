#include "visible_width.h"

#include <stdbool.h>
#include <string.h>

static bool is_csi_final(unsigned char value) { return value >= 0x40 && value <= 0x7e; }

static const unsigned char *skip_ansi_sequence(const unsigned char *cursor) {
    if (cursor[0] != '\033' && cursor[0] != 0x1b) {
        return cursor;
    }
    if (cursor[1] != '[') {
        return cursor + 1;
    }
    cursor += 2;
    while (*cursor != '\0' && !is_csi_final(*cursor)) {
        ++cursor;
    }
    if (*cursor != '\0') {
        ++cursor;
    }
    return cursor;
}

static size_t utf8_display_width(const unsigned char *cursor, size_t *bytes_consumed) {
    if ((*cursor & 0x80) == 0) {
        *bytes_consumed = 1;
        return 1;
    }
    if ((*cursor & 0xe0) == 0xc0 && cursor[1] != '\0') {
        *bytes_consumed = 2;
        return 1;
    }
    if ((*cursor & 0xf0) == 0xe0 && cursor[1] != '\0' && cursor[2] != '\0') {
        *bytes_consumed = 3;
        return 1;
    }
    if ((*cursor & 0xf8) == 0xf0 && cursor[1] != '\0' && cursor[2] != '\0' && cursor[3] != '\0') {
        *bytes_consumed = 4;
        return 1;
    }
    *bytes_consumed = 1;
    return 1;
}

size_t rss_ddc_cli_visible_width(const char *text) {
    if (text == NULL) {
        return 0;
    }
    size_t width = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        if (*cursor == '\033' || *cursor == 0x1b) {
            cursor = skip_ansi_sequence(cursor);
            continue;
        }
        size_t bytes = 0;
        width += utf8_display_width(cursor, &bytes);
        cursor += bytes;
    }
    return width;
}

size_t rss_ddc_cli_strip_ansi(char *buffer, size_t capacity, const char *text) {
    if (buffer == NULL || capacity == 0) {
        return 0;
    }
    if (text == NULL) {
        buffer[0] = '\0';
        return 0;
    }
    size_t written = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0' && written + 1 < capacity) {
        if (*cursor == '\033' || *cursor == 0x1b) {
            cursor = skip_ansi_sequence(cursor);
            continue;
        }
        size_t bytes = 0;
        (void)utf8_display_width(cursor, &bytes);
        for (size_t index = 0; index < bytes && written + 1 < capacity; ++index) {
            buffer[written++] = (char)cursor[index];
        }
        cursor += bytes;
    }
    buffer[written] = '\0';
    return written;
}
