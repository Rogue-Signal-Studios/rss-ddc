#include "table.h"

#include <stdio.h>
#include <string.h>

static size_t cell_display_width(const char *text) {
    return text == NULL ? 0 : strlen(text);
}

static void copy_cell(RSSDDCCliTableCell *cell, const char *text) {
    if (text == NULL) {
        cell->text[0] = '\0';
        return;
    }
    snprintf(cell->text, sizeof(cell->text), "%s", text);
}

bool rss_ddc_cli_table_init(RSSDDCCliTable *table, const char *const *headers, size_t column_count) {
    if (table == NULL || headers == NULL || column_count == 0 || column_count > RSS_DDC_CLI_TABLE_MAX_COLUMNS) {
        return false;
    }
    memset(table, 0, sizeof(*table));
    table->column_count = column_count;
    for (size_t index = 0; index < column_count; ++index) {
        copy_cell(&table->headers[index], headers[index]);
    }
    return true;
}

bool rss_ddc_cli_table_add_row(RSSDDCCliTable *table, const char *const *cells, size_t column_count) {
    if (table == NULL || cells == NULL || column_count != table->column_count ||
        table->row_count >= RSS_DDC_CLI_TABLE_MAX_ROWS) {
        return false;
    }
    for (size_t index = 0; index < column_count; ++index) {
        copy_cell(&table->rows[table->row_count][index], cells[index]);
    }
    ++table->row_count;
    return true;
}

void rss_ddc_cli_table_measure(RSSDDCCliTable *table) {
    if (table == NULL) {
        return;
    }
    for (size_t column = 0; column < table->column_count; ++column) {
        size_t width = cell_display_width(table->headers[column].text);
        for (size_t row = 0; row < table->row_count; ++row) {
            size_t cell_width = cell_display_width(table->rows[row][column].text);
            if (cell_width > width) {
                width = cell_width;
            }
        }
        table->widths[column] = width;
    }
}

static void render_border(FILE *stream, const RSSDDCCliTable *table, const RSSDDCCliEffectiveOutput *output,
                          const char *left, const char *right, const char *cross, const char *horizontal) {
    (void)output;
    fputs(left, stream);
    for (size_t column = 0; column < table->column_count; ++column) {
        if (column > 0) {
            fputs(cross, stream);
        }
        for (size_t index = 0; index < table->widths[column] + 2; ++index) {
            fputs(horizontal, stream);
        }
    }
    fputs(right, stream);
    if (!output->unicode) {
        fputc('\n', stream);
    } else {
        fputc('\n', stream);
    }
}

static void render_row(FILE *stream, const RSSDDCCliTable *table, const RSSDDCCliEffectiveOutput *output,
                       const RSSDDCCliTableCell *cells) {
    const char *vertical = output->unicode ? "\xe2\x94\x82" : "|";
    fputs(vertical, stream);
    for (size_t column = 0; column < table->column_count; ++column) {
        fprintf(stream, " %-*s ", (int)table->widths[column], cells[column].text);
        fputs(vertical, stream);
    }
    fputc('\n', stream);
}

void rss_ddc_cli_table_render(FILE *stream, const RSSDDCCliTable *table, const RSSDDCCliEffectiveOutput *output) {
    if (stream == NULL || table == NULL || output == NULL || table->column_count == 0) {
        return;
    }
    RSSDDCCliTable measured = *table;
    rss_ddc_cli_table_measure(&measured);
    if (output->unicode) {
        render_border(stream, &measured, output, "\xe2\x94\x8c", "\xe2\x94\x90", "\xe2\x94\xac", "\xe2\x94\x80");
        render_row(stream, &measured, output, measured.headers);
        render_border(stream, &measured, output, "\xe2\x94\x9c", "\xe2\x94\xa4", "\xe2\x94\xbc", "\xe2\x94\x80");
        for (size_t row = 0; row < measured.row_count; ++row) {
            render_row(stream, &measured, output, measured.rows[row]);
        }
        render_border(stream, &measured, output, "\xe2\x94\x94", "\xe2\x94\x98", "\xe2\x94\xb4", "\xe2\x94\x80");
        return;
    }
    render_border(stream, &measured, output, "+", "+", "+", "-");
    render_row(stream, &measured, output, measured.headers);
    render_border(stream, &measured, output, "+", "+", "+", "-");
    for (size_t row = 0; row < measured.row_count; ++row) {
        render_row(stream, &measured, output, measured.rows[row]);
    }
    render_border(stream, &measured, output, "+", "+", "+", "-");
}
