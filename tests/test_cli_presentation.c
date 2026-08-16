#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "color.h"
#include "config.h"
#include "output_settings.h"
#include "plain.h"
#include "render.h"
#include "table.h"
#include "terminal.h"
#include "tri_state.h"
#include "visible_width.h"

static bool g_stdout_tty = false;
static bool g_interactive_terminal = false;
static bool g_no_color = false;
static bool g_locale_unicode = false;

static bool test_is_stdout_tty(void *context) {
    (void)context;
    return g_stdout_tty;
}

static bool test_is_interactive_terminal(void *context) {
    (void)context;
    return g_interactive_terminal;
}

static bool test_is_no_color(void *context) {
    (void)context;
    return g_no_color;
}

static bool test_locale_supports_unicode(void *context) {
    (void)context;
    return g_locale_unicode;
}

static RSSDDCCliTerminalEnv test_terminal_env(void) {
    return (RSSDDCCliTerminalEnv){
        .is_stdout_tty = test_is_stdout_tty,
        .is_interactive_terminal = test_is_interactive_terminal,
        .is_no_color = test_is_no_color,
        .locale_supports_unicode = test_locale_supports_unicode,
        .context = NULL,
    };
}

static char *capture_render(void (*render)(FILE *, void *), void *context) {
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&buffer, &length);
    assert(stream != NULL);
    render(stream, context);
    fclose(stream);
    return buffer;
}

typedef struct {
    const RSSDDCDisplay *displays;
    size_t count;
    const RSSDDCCliEffectiveOutput *output;
} ListRenderContext;

static void render_list_capture(FILE *stream, void *context) {
    const ListRenderContext *ctx = context;
    rss_ddc_cli_render_display_list(stream, ctx->displays, ctx->count, ctx->output);
}

static bool contains_ansi(const char *text) {
    return text != NULL && strstr(text, "\033[") != NULL;
}

static bool contains_non_ascii(const char *text) {
    if (text == NULL) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor > 0x7f) {
            return true;
        }
    }
    return false;
}

static void assert_stripped_equals(const char *colored_output, const char *plain_output) {
    char *stripped = calloc(strlen(colored_output) + 1, 1);
    assert(stripped != NULL);
    rss_ddc_cli_strip_ansi(stripped, strlen(colored_output) + 1, colored_output);
    assert(strcmp(stripped, plain_output) == 0);
    free(stripped);
}

static void assert_border_columns_match(const char *text) {
    size_t expected = 0;
    bool have_expected = false;
    for (const char *line = text; line != NULL && *line != '\0'; line = strchr(line, '\n')) {
        if (line[0] == '|' || line[0] == '+') {
            size_t columns = 0;
            for (const char *cursor = line; *cursor != '\0' && *cursor != '\n'; ++cursor) {
                if (*cursor == '|' || *cursor == '+') {
                    ++columns;
                }
            }
            if (!have_expected) {
                expected = columns;
                have_expected = true;
            } else {
                assert(columns == expected);
            }
        }
        if (*line == '\0') {
            break;
        }
        ++line;
    }
}

static void write_config(const char *path, const char *contents) {
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fputs(contents, file);
    fclose(file);
}

static void test_tri_state_parse(void) {
    RSSDDCCliTriState value = RSS_DDC_CLI_TRI_AUTO;
    assert(rss_ddc_cli_tri_state_parse("yes", &value) && value == RSS_DDC_CLI_TRI_YES);
    assert(rss_ddc_cli_tri_state_parse("no", &value) && value == RSS_DDC_CLI_TRI_NO);
    assert(rss_ddc_cli_tri_state_parse("auto", &value) && value == RSS_DDC_CLI_TRI_AUTO);
    assert(strcmp(rss_ddc_cli_tri_state_string(RSS_DDC_CLI_TRI_YES), "yes") == 0);
    assert(!rss_ddc_cli_tri_state_parse("on", &value));
    assert(!rss_ddc_cli_tri_state_parse("true", &value));
}

static void test_config_missing_and_valid(void) {
    char path[] = "/tmp/rss-ddc-test-config-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    unlink(path);
    RSSDDCCliConfig config = {};
    assert(rss_ddc_cli_config_load(path, &config, NULL) == RSS_DDC_OK);
    assert(!config.has_color && !config.has_table && !config.has_unicode);
    write_config(path, "[output]\ncolor = no\ntable = yes\nunicode = auto\n");
    assert(rss_ddc_cli_config_load(path, &config, NULL) == RSS_DDC_OK);
    assert(config.has_color && config.color == RSS_DDC_CLI_TRI_NO);
    assert(config.has_table && config.table == RSS_DDC_CLI_TRI_YES);
    assert(config.has_unicode && config.unicode == RSS_DDC_CLI_TRI_AUTO);
    unlink(path);
}

static void test_config_malformed_value(void) {
    char path[] = "/tmp/rss-ddc-test-config-bad-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    write_config(path, "[output]\ncolor = maybe\n");
    RSSDDCCliConfig config = {};
    assert(rss_ddc_cli_config_load(path, &config, NULL) == RSS_DDC_ERROR_ARGUMENT);
    unlink(path);
}

static void test_config_path_precedence(void) {
    char buffer[512];
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/tmp/rss-home", 1);
    assert(rss_ddc_cli_config_default_path(buffer, sizeof(buffer)) == RSS_DDC_OK);
    assert(strcmp(buffer, "/tmp/rss-home/.config/rss-ddc/rss-ddc.conf") == 0);
    setenv("XDG_CONFIG_HOME", "/tmp/rss-xdg", 1);
    assert(rss_ddc_cli_config_default_path(buffer, sizeof(buffer)) == RSS_DDC_OK);
    assert(strcmp(buffer, "/tmp/rss-xdg/rss-ddc/rss-ddc.conf") == 0);
}

static void test_precedence_and_auto(void) {
    RSSDDCCliConfig config = {.color = RSS_DDC_CLI_TRI_YES,
                              .table = RSS_DDC_CLI_TRI_YES,
                              .unicode = RSS_DDC_CLI_TRI_YES,
                              .has_color = true,
                              .has_table = true,
                              .has_unicode = true};
    RSSDDCCliArgOverrides cli = {.color = RSS_DDC_CLI_TRI_NO, .has_color = true};
    RSSDDCCliTerminalEnv terminal = test_terminal_env();
    g_stdout_tty = true;
    g_interactive_terminal = true;
    g_no_color = false;
    g_locale_unicode = true;
    RSSDDCCliEffectiveOutput effective = {};
    rss_ddc_cli_resolve_output(&cli, &config, &terminal, true, &effective);
    assert(!effective.color && effective.table && effective.unicode);

    cli = (RSSDDCCliArgOverrides){};
    g_no_color = true;
    rss_ddc_cli_resolve_output(&cli, &config, &terminal, true, &effective);
    assert(!effective.color);

    cli = (RSSDDCCliArgOverrides){.color = RSS_DDC_CLI_TRI_YES, .has_color = true};
    rss_ddc_cli_resolve_output(&cli, &config, &terminal, true, &effective);
    assert(effective.color);

    config = (RSSDDCCliConfig){};
    cli = (RSSDDCCliArgOverrides){};
    g_no_color = false;
    g_stdout_tty = true;
    g_interactive_terminal = true;
    rss_ddc_cli_resolve_output(&cli, &config, &terminal, true, &effective);
    assert(effective.color && effective.table && effective.unicode);

    g_stdout_tty = false;
    g_interactive_terminal = false;
    rss_ddc_cli_resolve_output(&cli, &config, &terminal, true, &effective);
    assert(!effective.color && !effective.table);

    cli = (RSSDDCCliArgOverrides){.unicode = RSS_DDC_CLI_TRI_NO, .has_unicode = true};
    g_stdout_tty = true;
    g_interactive_terminal = true;
    rss_ddc_cli_resolve_output(&cli, &config, &terminal, true, &effective);
    assert(!effective.unicode);
}

static void test_global_args(void) {
    char *argv[] = {(char *)"rss-ddc", (char *)"--color=no", (char *)"--table=yes", (char *)"list"};
    RSSDDCCliParsedArgs parsed = {};
    assert(rss_ddc_cli_parse_global_args(4, argv, &parsed));
    assert(parsed.overrides.has_color && parsed.overrides.color == RSS_DDC_CLI_TRI_NO);
    assert(parsed.overrides.has_table && parsed.overrides.table == RSS_DDC_CLI_TRI_YES);
    assert(parsed.command_index == 3 && strcmp(argv[parsed.command_index], "list") == 0);
}

static void test_characterize_args(void) {
    RSSDDCCharacterizeOptions options = {};
    char *none[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1"};
    assert(rss_ddc_cli_parse_characterize_options(3, none, 3, &options));
    assert(options.mode == RSS_DDC_CHARACTERIZE_MODE_DEFAULT);
    assert(options.knowledge_policy == RSS_DDC_CHARACTERIZE_KNOWLEDGE_NORMAL);
    char *passive[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--mode",
                       (char *)"passive"};
    assert(rss_ddc_cli_parse_characterize_options(5, passive, 3, &options));
    assert(options.mode == RSS_DDC_CHARACTERIZE_MODE_PASSIVE);
    char *def[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--mode=default"};
    assert(rss_ddc_cli_parse_characterize_options(4, def, 3, &options));
    assert(options.mode == RSS_DDC_CHARACTERIZE_MODE_DEFAULT);
    char *deep[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--mode", (char *)"deep"};
    assert(rss_ddc_cli_parse_characterize_options(5, deep, 3, &options));
    assert(options.mode == RSS_DDC_CHARACTERIZE_MODE_DEEP);
    char *bad[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--mode", (char *)"unsafe"};
    assert(!rss_ddc_cli_parse_characterize_options(5, bad, 3, &options));
    char *extra[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--mode", (char *)"passive",
                     (char *)"--json"};
    assert(!rss_ddc_cli_parse_characterize_options(6, extra, 3, &options));
    char *output[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--output",
                      (char *)"/tmp/x.json"};
    assert(!rss_ddc_cli_parse_characterize_options(5, output, 3, &options));
    char *no_profiles[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--no-profiles"};
    assert(rss_ddc_cli_parse_characterize_options(4, no_profiles, 3, &options));
    assert(options.knowledge_policy == RSS_DDC_CHARACTERIZE_KNOWLEDGE_IGNORE_KNOWN);
    assert(options.mode == RSS_DDC_CHARACTERIZE_MODE_DEFAULT);
    char *deep_no_profiles[] = {(char *)"rss-ddc", (char *)"characterize", (char *)"1", (char *)"--mode",
                                (char *)"deep", (char *)"--no-profiles"};
    assert(rss_ddc_cli_parse_characterize_options(6, deep_no_profiles, 3, &options));
    assert(options.mode == RSS_DDC_CHARACTERIZE_MODE_DEEP);
    assert(options.knowledge_policy == RSS_DDC_CHARACTERIZE_KNOWLEDGE_IGNORE_KNOWN);
    assert(strcmp(rss_ddc_cli_characterize_mode_name(RSS_DDC_CHARACTERIZE_MODE_PASSIVE), "passive") == 0);
    assert(strcmp(rss_ddc_cli_characterize_mode_name(RSS_DDC_CHARACTERIZE_MODE_DEFAULT), "default") == 0);
    assert(strcmp(rss_ddc_cli_characterize_mode_name(RSS_DDC_CHARACTERIZE_MODE_DEEP), "deep") == 0);
}

static void test_help_args(void) {
    RSSDDCCliParsedArgs parsed = {};
    char *help[] = {(char *)"rss-ddc", (char *)"--help"};
    assert(rss_ddc_cli_parse_global_args(2, help, &parsed));
    assert(parsed.help);
    char *short_help[] = {(char *)"rss-ddc", (char *)"-h"};
    parsed = (RSSDDCCliParsedArgs){};
    assert(rss_ddc_cli_parse_global_args(2, short_help, &parsed));
    assert(parsed.help);
    char *unknown[] = {(char *)"rss-ddc", (char *)"--not-a-flag"};
    parsed = (RSSDDCCliParsedArgs){};
    assert(!rss_ddc_cli_parse_global_args(2, unknown, &parsed));
}

static void test_profile_update_args(void) {
    RSSDDCCliProfileUpdateOptions options = {};
    char *missing[] = {(char *)"rss-ddc", (char *)"profile", (char *)"update", (char *)"1"};
    assert(!rss_ddc_cli_parse_profile_update_options(4, missing, 4, &options));
    char *space[] = {(char *)"rss-ddc", (char *)"profile", (char *)"update", (char *)"1", (char *)"--output",
                     (char *)"/tmp/local.json"};
    assert(rss_ddc_cli_parse_profile_update_options(6, space, 4, &options));
    assert(strcmp(options.output_path, "/tmp/local.json") == 0);
    char *equals[] = {(char *)"rss-ddc", (char *)"profile", (char *)"update", (char *)"1",
                      (char *)"--output=/tmp/equals.json"};
    assert(rss_ddc_cli_parse_profile_update_options(5, equals, 4, &options));
    assert(strcmp(options.output_path, "/tmp/equals.json") == 0);
    char *empty[] = {(char *)"rss-ddc", (char *)"profile", (char *)"update", (char *)"1", (char *)"--output="};
    assert(!rss_ddc_cli_parse_profile_update_options(5, empty, 4, &options));
    char *extra[] = {(char *)"rss-ddc", (char *)"profile", (char *)"update", (char *)"1", (char *)"--output",
                     (char *)"/tmp/local.json", (char *)"--mode", (char *)"deep"};
    assert(!rss_ddc_cli_parse_profile_update_options(8, extra, 4, &options));
}

static RSSDDCDisplay sample_display(void) {
    RSSDDCDisplay display = {.list_index = 1,
                             .cg_display_id = 2,
                             .online = true,
                             .provider = RSS_DDC_PROVIDER_PS190,
                             .capabilities = 0x0b};
    snprintf(display.product_name, sizeof(display.product_name), "Odyssey G75F");
    snprintf(display.transport, sizeof(display.transport), "DCPEXT1");
    return display;
}

static void test_plain_and_table_renderers(void) {
    RSSDDCDisplay display = sample_display();
    RSSDDCCliEffectiveOutput plain = {.color = false, .table = false, .unicode = false};
    ListRenderContext plain_ctx = {.displays = &display, .count = 1, .output = &plain};
    char *plain_output = capture_render(render_list_capture, &plain_ctx);
    assert(strstr(plain_output, "provider=AppleDCPPS190") != NULL);
    assert(!contains_ansi(plain_output));
    free(plain_output);

    RSSDDCCliEffectiveOutput table = {.color = false, .table = true, .unicode = false};
    ListRenderContext table_ctx = {.displays = &display, .count = 1, .output = &table};
    char *ascii_table = capture_render(render_list_capture, &table_ctx);
    assert(strstr(ascii_table, "Odyssey G75F") != NULL);
    assert(strstr(ascii_table, "+") != NULL);
    assert(!contains_non_ascii(ascii_table));
    free(ascii_table);

    RSSDDCCliEffectiveOutput unicode_table = {.color = false, .table = true, .unicode = true};
    ListRenderContext unicode_ctx = {.displays = &display, .count = 1, .output = &unicode_table};
    char *unicode_output = capture_render(render_list_capture, &unicode_ctx);
    assert(contains_non_ascii(unicode_output));
    free(unicode_output);

    RSSDDCCliEffectiveOutput color = {.color = true, .table = true, .unicode = false};
    char state_text[64];
    rss_ddc_cli_color_format(state_text, sizeof(state_text), &color, RSS_DDC_CLI_COLOR_GREEN, "stable");
    assert(contains_ansi(state_text));
}

static RSSDDCProbeDiagnostics sample_probe_quick(void) {
    static RSSDDCProbeObservation observations[4];
    observations[0] = (RSSDDCProbeObservation){.semantic_id = "brightness",
                                               .requested_vcp = 0x10,
                                               .category = RSS_DDC_PROBE_RESULT_STABLE,
                                               .transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
                                               .protocol_valid = true,
                                               .stable = true,
                                               .advertised = RSS_DDC_PROBE_KNOWLEDGE_YES,
                                               .current_value = 50,
                                               .maximum_value = 100,
                                               .repeat_attempted = true};
    observations[1] = (RSSDDCProbeObservation){.semantic_id = "contrast",
                                               .requested_vcp = 0x12,
                                               .category = RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED,
                                               .transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
                                               .protocol_valid = false,
                                               .repeat_attempted = false};
    observations[2] = (RSSDDCProbeObservation){.semantic_id = "display.color_preset",
                                               .requested_vcp = 0x14,
                                               .category = RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH,
                                               .transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
                                               .protocol_valid = false,
                                               .repeat_attempted = false};
    observations[3] = (RSSDDCProbeObservation){.semantic_id = "custom-unadvertised",
                                               .requested_vcp = 0xe2,
                                               .category = RSS_DDC_PROBE_RESULT_STABLE,
                                               .transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
                                               .protocol_valid = true,
                                               .stable = true,
                                               .advertised = RSS_DDC_PROBE_KNOWLEDGE_NO,
                                               .current_value = 255,
                                               .maximum_value = 100,
                                               .current_exceeds_maximum = true,
                                               .repeat_attempted = true};
    return (RSSDDCProbeDiagnostics){.display = sample_display(),
                                    .mccs_available = true,
                                    .controls_attempted = 4,
                                    .controls_protocol_valid = 2,
                                    .controls_stable = 2,
                                    .controls_protocol_reported = 1,
                                    .observation_count = 4,
                                    .observations = observations};
}

typedef struct {
    const RSSDDCProbeDiagnostics *diagnostics;
    const RSSDDCCliEffectiveOutput *output;
} ProbeQuickRenderContext;

typedef struct {
    const RSSDDCProbeExtendedDiagnostics *diagnostics;
    const RSSDDCCliEffectiveOutput *output;
} ProbeExtendedRenderContext;

static void render_probe_quick_capture(FILE *stream, void *context) {
    const ProbeQuickRenderContext *ctx = context;
    rss_ddc_cli_render_probe_quick(stream, ctx->diagnostics, ctx->output);
}

static void test_probe_renderers(void) {
    RSSDDCProbeDiagnostics diagnostics = sample_probe_quick();
    RSSDDCCliEffectiveOutput plain = {.color = false, .table = false, .unicode = false};
    ProbeQuickRenderContext plain_ctx = {.diagnostics = &diagnostics, .output = &plain};
    char *plain_output = capture_render(render_probe_quick_capture, &plain_ctx);
    assert(strstr(plain_output, "vcp=0x10") != NULL);
    assert(strstr(plain_output, "repeat=not-attempted") != NULL);
    assert(!contains_ansi(plain_output));
    free(plain_output);

    RSSDDCCliEffectiveOutput table = {.color = true, .table = true, .unicode = false};
    ProbeQuickRenderContext table_ctx = {.diagnostics = &diagnostics, .output = &table};
    char *table_output = capture_render(render_probe_quick_capture, &table_ctx);
    assert(strstr(table_output, "VCP") != NULL);
    assert(strstr(table_output, "protocol-reported") != NULL);
    assert(contains_ansi(table_output));
    free(table_output);
}

static RSSDDCProbeExtendedDiagnostics sample_probe_extended(void) {
    static RSSDDCProbeExtendedObservation observations[2];
    observations[0].observation = (RSSDDCProbeObservation){.semantic_id = "brightness",
                                                           .requested_vcp = 0x10,
                                                           .category = RSS_DDC_PROBE_RESULT_STABLE,
                                                           .transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
                                                           .protocol_valid = true,
                                                           .stable = true,
                                                           .advertised = RSS_DDC_PROBE_KNOWLEDGE_YES,
                                                           .current_value = 50,
                                                           .maximum_value = 100,
                                                           .repeat_attempted = true};
    observations[1].observation = (RSSDDCProbeObservation){.semantic_id = "custom",
                                                           .requested_vcp = 0xe2,
                                                           .category = RSS_DDC_PROBE_RESULT_STABLE,
                                                           .transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
                                                           .protocol_valid = true,
                                                           .stable = true,
                                                           .advertised = RSS_DDC_PROBE_KNOWLEDGE_NO,
                                                           .current_value = 1,
                                                           .maximum_value = 1,
                                                           .repeat_attempted = true};
    observations[1].interpretation = RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED;
    return (RSSDDCProbeExtendedDiagnostics){.display = sample_display(),
                                            .mccs_available = true,
                                            .requested = 256,
                                            .attempted = 256,
                                            .strict_valid = 2,
                                            .stable_valid = 2,
                                            .advertised_valid = 1,
                                            .unadvertised_valid = 1,
                                            .protocol_reported = 197,
                                            .observation_count = 2,
                                            .observations = observations};
}

static void render_probe_extended_capture(FILE *stream, void *context) {
    const ProbeExtendedRenderContext *ctx = context;
    rss_ddc_cli_render_probe_extended(stream, ctx->diagnostics, ctx->output);
}

static void test_extended_probe_renderer(void) {
    RSSDDCProbeExtendedDiagnostics diagnostics = sample_probe_extended();
    RSSDDCCliEffectiveOutput plain = {.color = false, .table = false, .unicode = false};
    ProbeExtendedRenderContext plain_ctx = {.diagnostics = &diagnostics, .output = &plain};
    char *plain_output = capture_render(render_probe_extended_capture, &plain_ctx);
    assert(strstr(plain_output, "strict-valid=2") != NULL);
    assert(strstr(plain_output, "advertised-valid=1") != NULL);
    assert(strstr(plain_output, "unadvertised-valid=1") != NULL);
    free(plain_output);

    RSSDDCCliEffectiveOutput table = {.color = false, .table = true, .unicode = false};
    ProbeExtendedRenderContext table_ctx = {.diagnostics = &diagnostics, .output = &table};
    char *first = capture_render(render_probe_extended_capture, &table_ctx);
    char *second = capture_render(render_probe_extended_capture, &table_ctx);
    assert(strcmp(first, second) == 0);
    assert(strstr(first, "256 requested | 2 strict-valid") != NULL);
    free(first);
    free(second);
}

static void test_table_deterministic(void) {
    RSSDDCCliTable table = {};
    const char *headers[] = {"A", "B"};
    assert(rss_ddc_cli_table_init(&table, headers, 2));
    const char *row[] = {"one", "two"};
    assert(rss_ddc_cli_table_add_row(&table, row, 2));
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&buffer, &length);
    RSSDDCCliEffectiveOutput output = {.color = false, .table = true, .unicode = false};
    rss_ddc_cli_table_render(stream, &table, &output);
    fclose(stream);
    assert(strstr(buffer, "one") != NULL);
    assert(strstr(buffer, "two") != NULL);
    free(buffer);
}

static void test_visible_width_helper(void) {
    assert(rss_ddc_cli_visible_width("stable") == 6);
    assert(rss_ddc_cli_visible_width("\033[32mstable\033[0m") == 6);
    assert(rss_ddc_cli_visible_width("\033[2mprotocol-reported\033[0m") == strlen("protocol-reported"));
    char stripped[32];
    rss_ddc_cli_strip_ansi(stripped, sizeof(stripped), "\033[31moffline\033[0m");
    assert(strcmp(stripped, "offline") == 0);
}

static void test_list_status_color(void) {
    RSSDDCDisplay displays[2] = {sample_display(), sample_display()};
    displays[1].list_index = 2;
    displays[1].online = false;
    snprintf(displays[1].product_name, sizeof(displays[1].product_name), "LG HDR QHD");
    snprintf(displays[1].transport, sizeof(displays[1].transport), "DCPEXT0");
    displays[1].provider = RSS_DDC_PROVIDER_DCPDP13;

    RSSDDCCliEffectiveOutput plain = {.color = false, .table = true, .unicode = false};
    ListRenderContext plain_ctx = {.displays = displays, .count = 2, .output = &plain};
    char *plain_output = capture_render(render_list_capture, &plain_ctx);
    assert(!contains_ansi(plain_output));

    RSSDDCCliEffectiveOutput color = {.color = true, .table = true, .unicode = false};
    ListRenderContext color_ctx = {.displays = displays, .count = 2, .output = &color};
    char *color_output = capture_render(render_list_capture, &color_ctx);
    assert(contains_ansi(color_output));
    assert(strstr(color_output, "\033[32m") != NULL);
    assert(strstr(color_output, "\033[31m") != NULL);
    assert_stripped_equals(color_output, plain_output);
    assert_border_columns_match(plain_output);
    assert_border_columns_match(color_output);
    free(plain_output);
    free(color_output);
}

static void test_colored_table_alignment(void) {
    RSSDDCDisplay display = sample_display();
    RSSDDCProbeDiagnostics quick = sample_probe_quick();
    RSSDDCProbeExtendedDiagnostics extended = sample_probe_extended();

    RSSDDCCliEffectiveOutput plain = {.color = false, .table = true, .unicode = false};
    RSSDDCCliEffectiveOutput color = {.color = true, .table = true, .unicode = false};
    RSSDDCCliEffectiveOutput unicode = {.color = true, .table = true, .unicode = true};

    ListRenderContext list_plain = {.displays = &display, .count = 1, .output = &plain};
    ListRenderContext list_color = {.displays = &display, .count = 1, .output = &color};
    char *list_plain_output = capture_render(render_list_capture, &list_plain);
    char *list_color_output = capture_render(render_list_capture, &list_color);
    assert_stripped_equals(list_color_output, list_plain_output);
    assert_border_columns_match(list_plain_output);

    ProbeQuickRenderContext quick_plain = {.diagnostics = &quick, .output = &plain};
    ProbeQuickRenderContext quick_color = {.diagnostics = &quick, .output = &color};
    char *quick_plain_output = capture_render(render_probe_quick_capture, &quick_plain);
    char *quick_color_output = capture_render(render_probe_quick_capture, &quick_color);
    assert_stripped_equals(quick_color_output, quick_plain_output);
    assert_border_columns_match(quick_plain_output);

    ProbeExtendedRenderContext extended_plain = {.diagnostics = &extended, .output = &plain};
    ProbeExtendedRenderContext extended_color = {.diagnostics = &extended, .output = &color};
    ProbeExtendedRenderContext extended_unicode_plain = {.diagnostics = &extended, .output = &(RSSDDCCliEffectiveOutput){.color = false, .table = true, .unicode = true}};
    ProbeExtendedRenderContext extended_unicode_color = {.diagnostics = &extended, .output = &unicode};
    char *extended_plain_output = capture_render(render_probe_extended_capture, &extended_plain);
    char *extended_color_output = capture_render(render_probe_extended_capture, &extended_color);
    char *extended_unicode_plain_output = capture_render(render_probe_extended_capture, &extended_unicode_plain);
    char *extended_unicode_color_output = capture_render(render_probe_extended_capture, &extended_unicode_color);
    assert_stripped_equals(extended_color_output, extended_plain_output);
    assert_border_columns_match(extended_plain_output);
    assert_stripped_equals(extended_unicode_color_output, extended_unicode_plain_output);

    free(list_plain_output);
    free(list_color_output);
    free(quick_plain_output);
    free(quick_color_output);
    free(extended_plain_output);
    free(extended_color_output);
    free(extended_unicode_plain_output);
    free(extended_unicode_color_output);
}

int main(void) {
    test_tri_state_parse();
    test_config_missing_and_valid();
    test_config_malformed_value();
    test_config_path_precedence();
    test_precedence_and_auto();
    test_global_args();
    test_characterize_args();
    test_help_args();
    test_profile_update_args();
    test_plain_and_table_renderers();
    test_probe_renderers();
    test_extended_probe_renderer();
    test_table_deterministic();
    test_visible_width_helper();
    test_list_status_color();
    test_colored_table_alignment();
    fputs("test_cli_presentation: passed\n", stdout);
    return 0;
}
