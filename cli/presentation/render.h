#ifndef RSS_DDC_CLI_RENDER_H
#define RSS_DDC_CLI_RENDER_H

#include <stdio.h>

#include "output_settings.h"
#include "rss_ddc.h"

/** Renders the display list using plain or table presentation. */
void rss_ddc_cli_render_display_list(FILE *stream, const RSSDDCDisplay *displays, size_t count,
                                     const RSSDDCCliEffectiveOutput *output);

/** Renders Quick Probe diagnostics using plain or table presentation. */
void rss_ddc_cli_render_probe_quick(FILE *stream, const RSSDDCProbeDiagnostics *diagnostics,
                                    const RSSDDCCliEffectiveOutput *output);

/** Renders Extended Probe diagnostics using plain or table presentation. */
void rss_ddc_cli_render_probe_extended(FILE *stream, const RSSDDCProbeExtendedDiagnostics *diagnostics,
                                       const RSSDDCCliEffectiveOutput *output);

/**
 * Renders a public characterization result. Presentation only: it does not
 * discover displays, retrieve MCCS, or run Alien Probe.
 */
void rss_ddc_cli_render_characterization(FILE *stream, const RSSDDCCharacterization *characterization,
                                         RSSDDCCharacterizeMode mode, const RSSDDCCliEffectiveOutput *output);

#endif
