#include "terminal.h"

#include <langinfo.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool default_is_stdout_tty(void *context) {
    (void)context;
    return isatty(STDOUT_FILENO);
}

static bool term_is_dumb(void) {
    const char *term = getenv("TERM");
    return term == NULL || strcmp(term, "dumb") == 0;
}

static bool default_is_interactive_terminal(void *context) {
    (void)context;
    return isatty(STDOUT_FILENO) && !term_is_dumb();
}

static bool default_is_no_color(void *context) {
    (void)context;
    const char *value = getenv("NO_COLOR");
    return value != NULL && value[0] != '\0';
}

static bool locale_mentions_utf8(const char *value) {
    if (value == NULL) {
        return false;
    }
    return strstr(value, "UTF-8") != NULL || strstr(value, "utf-8") != NULL || strstr(value, "UTF8") != NULL ||
           strstr(value, "utf8") != NULL;
}

static bool default_locale_supports_unicode(void *context) {
    (void)context;
    const char *codeset = nl_langinfo(CODESET);
    if (codeset != NULL && (strcmp(codeset, "UTF-8") == 0 || strcmp(codeset, "utf-8") == 0)) {
        return true;
    }
    if (locale_mentions_utf8(getenv("LC_ALL"))) {
        return true;
    }
    if (locale_mentions_utf8(getenv("LC_CTYPE"))) {
        return true;
    }
    return locale_mentions_utf8(getenv("LANG"));
}

RSSDDCCliTerminalEnv rss_ddc_cli_terminal_env_default(void) {
    RSSDDCCliTerminalEnv environment = {
        .is_stdout_tty = default_is_stdout_tty,
        .is_interactive_terminal = default_is_interactive_terminal,
        .is_no_color = default_is_no_color,
        .locale_supports_unicode = default_locale_supports_unicode,
        .context = NULL,
    };
    return environment;
}
