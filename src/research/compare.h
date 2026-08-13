#ifndef RSS_DDC_RESEARCH_COMPARE_H
#define RSS_DDC_RESEARCH_COMPARE_H

#include <stdio.h>

/* Offline-only: this function parses report files and never links to or calls rss-ddc hardware APIs. */
int rss_ddc_research_compare_files(int path_count, const char *const *paths, const char *json_output_path,
                                   FILE *human_output, FILE *error_output);

#endif
