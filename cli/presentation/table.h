#ifndef RSS_DDC_CLI_TABLE_H
#define RSS_DDC_CLI_TABLE_H

#include <stddef.h>
#include <stdio.h>

#include "output_settings.h"

#define RSS_DDC_CLI_TABLE_MAX_COLUMNS 16
#define RSS_DDC_CLI_TABLE_MAX_ROWS 512
#define RSS_DDC_CLI_TABLE_CELL_SIZE 160

typedef struct {
    char text[RSS_DDC_CLI_TABLE_CELL_SIZE];
} RSSDDCCliTableCell;

typedef struct {
    size_t column_count;
    size_t row_count;
    size_t widths[RSS_DDC_CLI_TABLE_MAX_COLUMNS];
    RSSDDCCliTableCell headers[RSS_DDC_CLI_TABLE_MAX_COLUMNS];
    RSSDDCCliTableCell rows[RSS_DDC_CLI_TABLE_MAX_ROWS][RSS_DDC_CLI_TABLE_MAX_COLUMNS];
} RSSDDCCliTable;

/** Initializes an empty table with the given column headers. */
bool rss_ddc_cli_table_init(RSSDDCCliTable *table, const char *const *headers, size_t column_count);

/** Appends one row of string cells. Returns false when capacity is exceeded. */
bool rss_ddc_cli_table_add_row(RSSDDCCliTable *table, const char *const *cells, size_t column_count);

/** Computes column widths from headers and row contents. */
void rss_ddc_cli_table_measure(RSSDDCCliTable *table);

/** Renders the table using ASCII or Unicode borders according to `output`. */
void rss_ddc_cli_table_render(FILE *stream, const RSSDDCCliTable *table, const RSSDDCCliEffectiveOutput *output);

#endif
