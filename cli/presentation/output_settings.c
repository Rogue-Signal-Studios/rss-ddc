#include "output_settings.h"

static bool resolve_color(RSSDDCCliTriState preference, const RSSDDCCliTerminalEnv *terminal, bool no_color_env,
                          bool cli_explicit_color) {
    if (preference == RSS_DDC_CLI_TRI_NO) {
        return false;
    }
    if (preference == RSS_DDC_CLI_TRI_YES) {
        return true;
    }
    if (no_color_env && !cli_explicit_color) {
        return false;
    }
    if (terminal == NULL || terminal->is_interactive_terminal == NULL) {
        return false;
    }
    return terminal->is_interactive_terminal(terminal->context);
}

static bool resolve_table(RSSDDCCliTriState preference, const RSSDDCCliTerminalEnv *terminal,
                          bool command_supports_table) {
    if (!command_supports_table) {
        return false;
    }
    if (preference == RSS_DDC_CLI_TRI_NO) {
        return false;
    }
    if (preference == RSS_DDC_CLI_TRI_YES) {
        return true;
    }
    if (terminal == NULL || terminal->is_stdout_tty == NULL) {
        return false;
    }
    return terminal->is_stdout_tty(terminal->context);
}

static bool resolve_unicode(RSSDDCCliTriState preference, const RSSDDCCliTerminalEnv *terminal) {
    if (preference == RSS_DDC_CLI_TRI_NO) {
        return false;
    }
    if (preference == RSS_DDC_CLI_TRI_YES) {
        return true;
    }
    if (terminal == NULL || terminal->is_interactive_terminal == NULL ||
        terminal->locale_supports_unicode == NULL) {
        return false;
    }
    return terminal->is_interactive_terminal(terminal->context) &&
           terminal->locale_supports_unicode(terminal->context);
}

static RSSDDCCliTriState color_preference(const RSSDDCCliArgOverrides *cli, const RSSDDCCliConfig *config,
                                          bool no_color_env) {
    if (cli != NULL && cli->has_color) {
        return cli->color;
    }
    if (no_color_env) {
        return RSS_DDC_CLI_TRI_NO;
    }
    if (config != NULL && config->has_color) {
        return config->color;
    }
    return RSS_DDC_CLI_TRI_AUTO;
}

static RSSDDCCliTriState table_preference(const RSSDDCCliArgOverrides *cli, const RSSDDCCliConfig *config) {
    if (cli != NULL && cli->has_table) {
        return cli->table;
    }
    if (config != NULL && config->has_table) {
        return config->table;
    }
    return RSS_DDC_CLI_TRI_AUTO;
}

static RSSDDCCliTriState unicode_preference(const RSSDDCCliArgOverrides *cli, const RSSDDCCliConfig *config) {
    if (cli != NULL && cli->has_unicode) {
        return cli->unicode;
    }
    if (config != NULL && config->has_unicode) {
        return config->unicode;
    }
    return RSS_DDC_CLI_TRI_AUTO;
}

void rss_ddc_cli_resolve_output(const RSSDDCCliArgOverrides *cli, const RSSDDCCliConfig *config,
                                const RSSDDCCliTerminalEnv *terminal, bool command_supports_table,
                                RSSDDCCliEffectiveOutput *effective) {
    const bool no_color_env =
        terminal != NULL && terminal->is_no_color != NULL && terminal->is_no_color(terminal->context);
    const bool cli_explicit_color = cli != NULL && cli->has_color;
    const RSSDDCCliTriState color_pref = color_preference(cli, config, no_color_env);
    const RSSDDCCliTriState table_pref = table_preference(cli, config);
    const RSSDDCCliTriState unicode_pref = unicode_preference(cli, config);
    effective->color = resolve_color(color_pref, terminal, no_color_env, cli_explicit_color);
    effective->table = resolve_table(table_pref, terminal, command_supports_table);
    effective->unicode = resolve_unicode(unicode_pref, terminal);
}
