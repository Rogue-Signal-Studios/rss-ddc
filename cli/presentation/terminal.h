#ifndef RSS_DDC_CLI_TERMINAL_H
#define RSS_DDC_CLI_TERMINAL_H

#include <stdbool.h>

/**
 * Injectable terminal/environment probes used to resolve `auto` presentation
 * settings without binding tests to the real stdout terminal state.
 */
typedef struct {
    bool (*is_stdout_tty)(void *context);
    bool (*is_interactive_terminal)(void *context);
    bool (*is_no_color)(void *context);
    bool (*locale_supports_unicode)(void *context);
    void *context;
} RSSDDCCliTerminalEnv;

/** Returns a terminal environment backed by isatty(1), getenv, and nl_langinfo. */
RSSDDCCliTerminalEnv rss_ddc_cli_terminal_env_default(void);

#endif
