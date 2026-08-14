#ifndef RSS_DDC_CLI_PLAIN_H
#define RSS_DDC_CLI_PLAIN_H

#include <stdio.h>

#include "output_settings.h"
#include "rss_ddc.h"

/** Prints one display using the legacy machine-friendly list format. */
void rss_ddc_cli_plain_print_display(FILE *stream, const RSSDDCDisplay *display);

/** Prints Quick Probe diagnostics using the legacy one-record-per-line format. */
void rss_ddc_cli_plain_print_probe_quick(FILE *stream, const RSSDDCProbeDiagnostics *diagnostics);

/** Prints Extended Probe diagnostics using the legacy one-record-per-line format. */
void rss_ddc_cli_plain_print_probe_extended(FILE *stream, const RSSDDCProbeExtendedDiagnostics *diagnostics);

#endif
