@import Foundation;

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rss_ddc.h"
#include "presentation/args.h"
#include "presentation/config.h"
#include "presentation/output_settings.h"
#include "presentation/plain.h"
#include "presentation/profile_update.h"
#include "presentation/render.h"
#include "presentation/terminal.h"

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s [--color=yes|no|auto] [--table=yes|no|auto] [--unicode=yes|no|auto] list\n"
            "  %s [--verbose] [--color=yes|no|auto] [--table=yes|no|auto] [--unicode=yes|no|auto] info <display-index>\n"
            "  %s [--color=yes|no|auto] [--table=yes|no|auto] [--unicode=yes|no|auto] characterize <display-index> [--mode passive|default|deep]\n"
            "  %s [--color=yes|no|auto] [--table=yes|no|auto] [--unicode=yes|no|auto] profile update <display-index> --output <file>\n"
            "  %s [--verbose] edid <display-index> [--decode|--hex|--raw <file>]\n"
            "  %s [--verbose] dpcd <display-index> <address> <length>\n"
            "  %s [--verbose] probe-dpcd-path <display-index>\n"
            "  %s [--color=yes|no|auto] [--table=yes|no|auto] [--unicode=yes|no|auto] probe-quick <display-index>\n"
            "  %s [--color=yes|no|auto] [--table=yes|no|auto] [--unicode=yes|no|auto] probe-extended <display-index>\n"
            "  %s [--verbose] mccs <display-index>\n"
            "  %s [--verbose] input <display-index> <standard|lg-alt> <value>\n"
            "  %s [--verbose] picture-mode <display-index> <vivid|reader>\n"
            "  %s [--verbose] get <display-index> <vcp>\n"
            "  %s [--verbose] set <display-index> <vcp> <value>\n"
            "  %s [--verbose] set <display-index> <vcp> <value> --verify [--settle-ms <ms>] "
            "[--retries <count>] [--retry-delay-ms <ms>]\n",
            program, program, program, program, program, program, program, program, program, program, program, program,
            program, program, program);
}

static bool parse_unsigned(const char *text, unsigned long maximum, unsigned long *value) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) return false;
    *value = parsed;
    return true;
}

/*
 * The public API reports the total online count even when its destination is
 * undersized. A topology may change between the sizing and fill calls, so the
 * CLI retries once with the new total and otherwise fails instead of printing
 * an ambiguous partial list.
 */
static RSSDDCError list_displays_dynamic(RSSDDCDisplay **displays_out, size_t *count_out) {
    if (displays_out == NULL || count_out == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *displays_out = NULL;
    *count_out = 0;
    for (unsigned int attempt = 0; attempt < 2; ++attempt) {
        size_t required = 0;
        RSSDDCError error = rss_ddc_list_displays(NULL, 0, &required);
        if (error != RSS_DDC_OK || required == 0) return error;
        if (required > SIZE_MAX / sizeof(**displays_out)) return RSS_DDC_ERROR_SYSTEM;
        RSSDDCDisplay *displays = calloc(required, sizeof(*displays));
        if (displays == NULL) return RSS_DDC_ERROR_SYSTEM;
        size_t observed = required;
        error = rss_ddc_list_displays(displays, required, &observed);
        if (error == RSS_DDC_OK && observed <= required) {
            *displays_out = displays;
            *count_out = observed;
            return RSS_DDC_OK;
        }
        free(displays);
        if (error != RSS_DDC_OK) return error;
    }
    return RSS_DDC_ERROR_DISCOVERY;
}

/** CLI adapter for portable diagnostics; stderr preserves concise script-friendly stdout. */
static void write_diagnostic(void *context, const char *message) {
    (void)context;
    fprintf(stderr, "rss-ddc: %s\n", message);
}

static void config_warn(void *context, const char *message) {
    if (context == NULL) {
        return;
    }
    fprintf(stderr, "rss-ddc: %s\n", message);
}

static RSSDDCCliEffectiveOutput resolve_presentation(const RSSDDCCliParsedArgs *parsed, bool command_supports_table) {
    RSSDDCCliConfig config = {};
    char config_path[512];
    if (rss_ddc_cli_config_default_path(config_path, sizeof(config_path)) == RSS_DDC_OK) {
        RSSDDCCliConfigOptions options = {.warn = parsed->verbose ? config_warn : NULL, .warn_context = (void *)1};
        (void)rss_ddc_cli_config_load(config_path, &config, &options);
    }
    RSSDDCCliTerminalEnv terminal = rss_ddc_cli_terminal_env_default();
    RSSDDCCliEffectiveOutput effective = {};
    rss_ddc_cli_resolve_output(&parsed->overrides, &config, &terminal, command_supports_table, &effective);
    return effective;
}

static void print_edid_decode(const RSSDDCEDIDInfo *info) {
    printf("manufacturer: %s\nproduct-code: 0x%04x\n", info->manufacturer_id, info->product_code);
    printf("serial: %s\n", info->serial_number_present ? "present" : "unavailable");
    if (info->serial_number_present) printf("serial-number: %u\n", info->serial_number);
    if (info->serial_text[0] != '\0') printf("serial-text: %s\n", info->serial_text);
    if (info->monitor_name[0] != '\0') printf("monitor-name: %s\n", info->monitor_name);
    printf("edid-version: %u.%u\nsize-cm: %ux%u\nextensions: %u declared, %zu received\nchecksum: valid\n",
           info->version, info->revision, info->width_cm, info->height_cm, info->declared_extension_count,
           info->received_block_count > 0 ? info->received_block_count - 1 : 0);
    printf("extensions-complete: %s\n", info->extensions_complete ? "yes" : "no");
    for (size_t block = 1; block < info->received_block_count; ++block) {
        printf("extension-%zu-tag: 0x%02x type: %s revision: %u checksum: valid\n", block,
               info->extension_tags[block - 1], rss_ddc_edid_extension_type_string(info->extension_types[block - 1]),
               info->extension_revisions[block - 1]);
    }
}

static void print_edid_hex(const RSSDDCEDID *edid) {
    for (size_t block = 0; block < edid->length / RSS_DDC_EDID_BLOCK_SIZE; ++block) {
        printf("EDID block %zu\n", block);
        for (size_t offset = 0; offset < RSS_DDC_EDID_BLOCK_SIZE; ++offset) {
            size_t index = block * RSS_DDC_EDID_BLOCK_SIZE + offset;
            if (offset % 16 == 0) printf("%04zx: ", index);
            printf("%02x%s", edid->bytes[index], offset % 16 == 15 ? "\n" : " ");
        }
    }
}

static void print_dpcd_hex(uint32_t address, const uint8_t *bytes, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (index % 16 == 0) printf("%05x: ", address + (uint32_t)index);
        printf("%02x%s", bytes[index], index % 16 == 15 || index + 1 == length ? "\n" : " ");
    }
}

static void print_dpcd_decode(uint32_t address, const uint8_t *bytes, size_t length) {
    RSSDDCDPCDCapabilities capabilities = {};
    if (rss_ddc_decode_dpcd_capabilities(address, bytes, length, &capabilities) != RSS_DDC_OK) return;
    printf("dpcd-revision: 0x%02x\nmax-link-rate-raw: 0x%02x\nmax-link-rate: %s\n", capabilities.revision,
           capabilities.max_link_rate_raw, capabilities.max_link_rate_name);
    printf("max-lane-count: %u\nenhanced-framing: %s\ndownstream-port-present: %s\n", capabilities.max_lane_count,
           capabilities.enhanced_framing ? "yes" : "no", capabilities.downstream_port_present ? "yes" : "no");
}

/** Parses the small public CLI surface; hardware access is limited to explicit GET/SET/EDID/DPCD commands. */
int main(int argc, char **argv) {
    RSSDDCCliParsedArgs parsed = {};
    if (!rss_ddc_cli_parse_global_args(argc, argv, &parsed)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    const int argument = parsed.command_index;
    if (argc <= argument) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "list") == 0) {
        if (argc != argument + 1) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCDisplay *displays = NULL;
        size_t count = 0;
        RSSDDCError error = list_displays_dynamic(&displays, &count);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        RSSDDCCliEffectiveOutput output = resolve_presentation(&parsed, true);
        rss_ddc_cli_render_display_list(stdout, displays, count, &output);
        free(displays);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "profile") == 0) {
        RSSDDCCliProfileUpdateOptions profile_options = {};
        unsigned long profile_index = 0;
        RSSDDCProfileStore *store = NULL;
        RSSDDCCharacterization *result = NULL;
        RSSDDCCharacterizeOptions options = rss_ddc_default_characterize_options();
        RSSDDCCharacterizationProfileUpdateResult update = {};
        RSSDDCEffectiveProfile effective = {};
        const RSSDDCEffectiveProfile *effective_ptr = NULL;
        RSSDDCError error = RSS_DDC_OK;
        bool written = false;
        if (argc <= argument + 1 || strcmp(argv[argument + 1], "update") != 0) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (argc <= argument + 2 || !parse_unsigned(argv[argument + 2], UINT32_MAX, &profile_index) ||
            profile_index == 0) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (!rss_ddc_cli_parse_profile_update_options(argc, argv, argument + 3, &profile_options)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        store = rss_ddc_profile_store_create();
        if (store == NULL) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(RSS_DDC_ERROR_SYSTEM));
            return EXIT_FAILURE;
        }
        (void)rss_ddc_profile_store_load_builtin(store);
        if (access(profile_options.output_path, F_OK) == 0) {
            error = rss_ddc_profile_store_load_local_file(store, profile_options.output_path);
            if (error != RSS_DDC_OK) {
                fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
                rss_ddc_profile_store_destroy(store);
                return EXIT_FAILURE;
            }
        }
        options.mode = RSS_DDC_CHARACTERIZE_MODE_DEFAULT;
        error = rss_ddc_characterize_display((uint32_t)profile_index, store, &options, &result);
        if (error != RSS_DDC_OK || result == NULL) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            rss_ddc_characterization_destroy(result);
            rss_ddc_profile_store_destroy(store);
            return EXIT_FAILURE;
        }
        error = rss_ddc_characterization_update_profile(result, store, &update);
        if (error != RSS_DDC_OK && error != RSS_DDC_ERROR_PROFILE_CONFLICT) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            rss_ddc_characterization_destroy(result);
            rss_ddc_profile_store_destroy(store);
            return EXIT_FAILURE;
        }
        if (rss_ddc_characterization_profile_identity(result) != NULL &&
            rss_ddc_profile_store_resolve(store, rss_ddc_characterization_profile_identity(result),
                                          &effective) == RSS_DDC_OK) {
            effective_ptr = &effective;
        }
        if (error == RSS_DDC_OK) {
            RSSDDCError save_error =
                rss_ddc_cli_profile_update_save_if_needed(store, update.status, profile_options.output_path,
                                                          &written);
            if (save_error != RSS_DDC_OK) {
                fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(save_error));
                rss_ddc_characterization_destroy(result);
                rss_ddc_profile_store_destroy(store);
                return EXIT_FAILURE;
            }
        }
        {
            RSSDDCCliEffectiveOutput output = resolve_presentation(&parsed, false);
            rss_ddc_cli_render_profile_update(stdout, result, &update, effective_ptr,
                                              written ? profile_options.output_path : NULL, &output);
        }
        rss_ddc_characterization_destroy(result);
        rss_ddc_profile_store_destroy(store);
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    unsigned long display_index = 0;
    if (argc <= argument + 1 || !parse_unsigned(argv[argument + 1], UINT32_MAX, &display_index) || display_index == 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "info") == 0) {
        RSSDDCDisplay display = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_get_display_with_diagnostics((uint32_t)display_index, &display,
                                                                  parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        RSSDDCCliEffectiveOutput output = resolve_presentation(&parsed, false);
        if (!output.table) {
            rss_ddc_cli_plain_print_display(stdout, &display);
        } else {
            rss_ddc_cli_render_display_list(stdout, &display, 1, &output);
        }
        printf("online=%s external=%s branch=%s transport=%s\n", display.online ? "yes" : "no",
               display.external ? "yes" : "no", display.branch_device_id, display.transport);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "characterize") == 0) {
        RSSDDCCharacterizeMode mode = RSS_DDC_CHARACTERIZE_MODE_DEFAULT;
        if (!rss_ddc_cli_parse_characterize_options(argc, argv, argument + 2, &mode)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        RSSDDCCharacterizeOptions options = rss_ddc_default_characterize_options();
        options.mode = mode;
        RSSDDCProfileStore *store = rss_ddc_profile_store_create();
        if (store != NULL && rss_ddc_profile_store_load_builtin(store) != RSS_DDC_OK) {
            rss_ddc_profile_store_destroy(store);
            store = NULL;
        }
        RSSDDCCharacterization *result = NULL;
        RSSDDCError error = rss_ddc_characterize_display((uint32_t)display_index, store, &options, &result);
        rss_ddc_profile_store_destroy(store);
        if (error != RSS_DDC_OK || result == NULL) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            rss_ddc_characterization_destroy(result);
            return EXIT_FAILURE;
        }
        RSSDDCCliEffectiveOutput output = resolve_presentation(&parsed, true);
        rss_ddc_cli_render_characterization(stdout, result, mode, &output);
        rss_ddc_characterization_destroy(result);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "edid") == 0) {
        bool hex = false;
        const char *raw_path = NULL;
        if (argc > argument + 4 || (argc == argument + 4 &&
            strcmp(argv[argument + 2], "--raw") != 0)) { usage(argv[0]); return EXIT_FAILURE; }
        if (argc == argument + 3) {
            if (strcmp(argv[argument + 2], "--hex") == 0) hex = true;
            else if (strcmp(argv[argument + 2], "--decode") != 0) { usage(argv[0]); return EXIT_FAILURE; }
        } else if (argc == argument + 4) raw_path = argv[argument + 3];
        RSSDDCEDID edid = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_read_edid_with_diagnostics((uint32_t)display_index, &edid,
                                                                parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        RSSDDCEDIDInfo info = {};
        error = rss_ddc_parse_edid(&edid, &info);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        if (raw_path != NULL) {
            int file = open(raw_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (file < 0 || write(file, edid.bytes, edid.length) != (ssize_t)edid.length || close(file) != 0) {
                if (file >= 0) close(file);
                fprintf(stderr, "rss-ddc: could not create raw EDID file\n");
                return EXIT_FAILURE;
            }
            printf("wrote %zu bytes to %s\n", edid.length, raw_path);
        } else if (hex) print_edid_hex(&edid);
        else print_edid_decode(&info);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "probe-dpcd-path") == 0) {
        if (argc != argument + 2) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_probe_dpcd_path_with_diagnostics((uint32_t)display_index,
                                                                       parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        printf("DPCD path candidate correlation completed; no IODP construction or DPCD read was performed.\n");
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "probe-quick") == 0) {
        if (argc != argument + 2) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCProbe *probe = NULL;
        RSSDDCError error = rss_ddc_probe_quick_for_display((uint32_t)display_index, &probe);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        RSSDDCProbeDiagnostics diagnostics = {};
        error = rss_ddc_probe_diagnostics(probe, &diagnostics);
        if (error == RSS_DDC_OK) {
            RSSDDCCliEffectiveOutput output = resolve_presentation(&parsed, true);
            rss_ddc_cli_render_probe_quick(stdout, &diagnostics, &output);
        }
        rss_ddc_probe_destroy(probe);
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "probe-extended") == 0) {
        if (argc != argument + 2) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCProbe *probe = NULL;
        RSSDDCError error = rss_ddc_probe_extended_for_display((uint32_t)display_index, &probe);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        RSSDDCProbeExtendedDiagnostics diagnostics = {};
        error = rss_ddc_probe_extended_diagnostics(probe, &diagnostics);
        if (error == RSS_DDC_OK) {
            RSSDDCCliEffectiveOutput output = resolve_presentation(&parsed, true);
            rss_ddc_cli_render_probe_extended(stdout, &diagnostics, &output);
        }
        rss_ddc_probe_destroy(probe);
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "mccs") == 0) {
        if (argc != argument + 2) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCMCCSCapabilities *capabilities = calloc(1, sizeof(*capabilities));
        if (capabilities == NULL) { fprintf(stderr, "rss-ddc: allocation failed\n"); return EXIT_FAILURE; }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_get_mccs_capabilities_with_diagnostics((uint32_t)display_index, capabilities,
                                                                            parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        else {
            fwrite(capabilities->raw, 1, capabilities->raw_length, stdout);
            fputc('\n', stdout);
        }
        free(capabilities);
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "input") == 0) {
        if (argc != argument + 4) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCInputSwitchMethod method;
        if (strcmp(argv[argument + 2], "standard") == 0) method = RSS_DDC_INPUT_SWITCH_STANDARD;
        else if (strcmp(argv[argument + 2], "lg-alt") == 0) method = RSS_DDC_INPUT_SWITCH_LG_ALT;
        else { usage(argv[0]); return EXIT_FAILURE; }
        unsigned long value = 0;
        if (!parse_unsigned(argv[argument + 3], UINT16_MAX, &value)) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_set_input_with_diagnostics((uint32_t)display_index, method, (uint16_t)value,
                                                                parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "picture-mode") == 0) {
        if (argc != argument + 3) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCPictureMode mode;
        if (strcmp(argv[argument + 2], "vivid") == 0) mode = RSS_DDC_PICTURE_MODE_VIVID;
        else if (strcmp(argv[argument + 2], "reader") == 0) mode = RSS_DDC_PICTURE_MODE_READER;
        else { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_set_picture_mode_with_diagnostics((uint32_t)display_index, mode,
                                                                       parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "dpcd") == 0) {
        if (argc != argument + 4) { usage(argv[0]); return EXIT_FAILURE; }
        unsigned long address = 0;
        unsigned long length = 0;
        if (!parse_unsigned(argv[argument + 2], RSS_DDC_DPCD_MAX_ADDRESS, &address) ||
            !parse_unsigned(argv[argument + 3], RSS_DDC_DPCD_MAX_READ_BYTES, &length)) {
            usage(argv[0]); return EXIT_FAILURE;
        }
        uint8_t bytes[RSS_DDC_DPCD_MAX_READ_BYTES] = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_read_dpcd_with_diagnostics((uint32_t)display_index, (uint32_t)address,
                                                                bytes, (size_t)length, parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        print_dpcd_hex((uint32_t)address, bytes, (size_t)length);
        print_dpcd_decode((uint32_t)address, bytes, (size_t)length);
        return EXIT_SUCCESS;
    }
    unsigned long vcp = 0;
    if (argc <= argument + 2 || !parse_unsigned(argv[argument + 2], UINT8_MAX, &vcp)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "get") == 0) {
        RSSDDCVCPResult result = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_get_vcp_with_diagnostics((uint32_t)display_index, (uint8_t)vcp, &result,
                                                              parsed.verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        /* Normal mode is intentionally machine-friendly; verbose detail was sent to stderr. */
        printf("%u\n", result.current_value);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "set") == 0 && argc >= argument + 4) {
        unsigned long value = 0;
        if (!parse_unsigned(argv[argument + 3], UINT16_MAX, &value)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        bool verify = false;
        bool has_policy_option = false;
        RSSDDCVerifyPolicy policy = rss_ddc_default_verify_policy();
        for (int option = argument + 4; option < argc; ++option) {
            if (strcmp(argv[option], "--verify") == 0 && !verify) {
                verify = true;
                continue;
            }
            uint32_t *destination = NULL;
            if (strcmp(argv[option], "--settle-ms") == 0) destination = &policy.settle_ms;
            else if (strcmp(argv[option], "--retries") == 0) destination = &policy.retry_count;
            else if (strcmp(argv[option], "--retry-delay-ms") == 0) destination = &policy.retry_delay_ms;
            if (destination == NULL || ++option >= argc) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            unsigned long parsed_value = 0;
            if (!parse_unsigned(argv[option], UINT32_MAX, &parsed_value)) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            *destination = (uint32_t)parsed_value;
            has_policy_option = true;
        }
        if (has_policy_option && !verify) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        RSSDDCError error;
        if (verify) {
            RSSDDCVCPResult result = {};
            error = rss_ddc_set_vcp_and_verify_with_diagnostics((uint32_t)display_index, (uint8_t)vcp,
                                                                 (uint16_t)value, &policy, &result,
                                                                 parsed.verbose ? &diagnostics : NULL);
            if (error == RSS_DDC_OK) printf("verified %u\n", result.current_value);
        } else {
            error = rss_ddc_set_vcp_with_diagnostics((uint32_t)display_index, (uint8_t)vcp,
                                                      (uint16_t)value, parsed.verbose ? &diagnostics : NULL);
        }
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    usage(argv[0]);
    return EXIT_FAILURE;
}
