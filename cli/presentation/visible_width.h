#ifndef RSS_DDC_CLI_VISIBLE_WIDTH_H
#define RSS_DDC_CLI_VISIBLE_WIDTH_H

#include <stddef.h>

/**
 * Returns the number of terminal columns occupied by `text`, ignoring ANSI CSI
 * style sequences. UTF-8 code points outside ASCII count as one column each.
 */
size_t rss_ddc_cli_visible_width(const char *text);

/**
 * Copies `text` into `buffer`, removing ANSI CSI sequences. Returns the number
 * of bytes written, excluding the terminating NUL.
 */
size_t rss_ddc_cli_strip_ansi(char *buffer, size_t capacity, const char *text);

#endif
