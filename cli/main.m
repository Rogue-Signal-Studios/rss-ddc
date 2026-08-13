@import Foundation;

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rss_ddc.h"
#include "input_alt_probe.h"
#include "macos_internal.h"

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s list\n"
            "  %s [--verbose] info <display-index>\n"
            "  %s [--verbose] edid <display-index> [--decode|--hex|--raw <file>]\n"
            "  %s [--verbose] dpcd <display-index> <address> <length>\n"
            "  %s [--verbose] capabilities <display-index>\n"
            "  %s [--verbose] probe-dpcd-path <display-index>\n"
            "  %s probe-input-alt <display-index> <conventional|lg-alt|inline> <value>\n"
            "  %s [--verbose] get <display-index> <vcp>\n"
            "  %s [--verbose] set <display-index> <vcp> <value>\n"
            "  %s [--verbose] set <display-index> <vcp> <value> --verify [--settle-ms <ms>] "
            "[--retries <count>] [--retry-delay-ms <ms>]\n",
            program, program, program, program, program, program, program, program, program, program);
}

static bool parse_unsigned(const char *text, unsigned long maximum, unsigned long *value) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static void print_display(const RSSDDCDisplay *display) {
    printf("%u  %s  provider=%s  capabilities=0x%02x  cg=%u\n", display->list_index,
           display->product_name, rss_ddc_provider_string(display->provider), display->capabilities,
           display->cg_display_id);
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

/** Uses the public caller-owned MCCS result only; it does not infer friendly labels for raw values. */
static void print_mccs_capabilities(const RSSDDCMCCSCapabilities *capabilities) {
    printf("raw-capabilities: %s\n", capabilities->raw);
    for (size_t index = 0; index < capabilities->feature_count; ++index) {
        uint8_t vcp = capabilities->features[index].vcp_code;
        printf("vcp: 0x%02x", vcp);
        const uint8_t *values = NULL;
        size_t count = 0;
        if (rss_ddc_mccs_capabilities_enum_values(capabilities, vcp, &values, &count) == RSS_DDC_OK && count != 0) {
            printf(" values:");
            for (size_t value = 0; value < count; ++value) printf(" %02x", values[value]);
        }
        printf("\n");
    }
}

/** Parses the small public CLI surface; hardware access requires an explicit command. */
int main(int argc, char **argv) {
    bool verbose = false;
    int argument = 1;
    if (argc > argument && strcmp(argv[argument], "--verbose") == 0) {
        verbose = true;
        ++argument;
    }
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
        for (size_t index = 0; index < count; ++index) print_display(&displays[index]);
        free(displays);
        return EXIT_SUCCESS;
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
                                                                  verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        print_display(&display);
        printf("online=%s external=%s branch=%s transport=%s\n", display.online ? "yes" : "no",
               display.external ? "yes" : "no", display.branch_device_id, display.transport);
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
                                                                verbose ? &diagnostics : NULL);
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
    if (strcmp(argv[argument], "capabilities") == 0) {
        if (argc != argument + 2) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCMCCSCapabilities capabilities = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_get_mccs_capabilities_with_diagnostics(
            (uint32_t)display_index, &capabilities, verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        print_mccs_capabilities(&capabilities);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "probe-dpcd-path") == 0) {
        if (argc != argument + 2) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_probe_dpcd_path_with_diagnostics((uint32_t)display_index,
                                                                       verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        printf("DPCD path candidate correlation completed; no IODP construction or DPCD read was performed.\n");
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "probe-input-alt") == 0) {
        if (argc != argument + 4) { usage(argv[0]); return EXIT_FAILURE; }
        RSSDDCInputAltProbeVariant variant;
        unsigned long value = 0;
        if (rss_ddc_input_alt_probe_variant_from_string(argv[argument + 2], &variant) != RSS_DDC_OK ||
            !parse_unsigned(argv[argument + 3], UINT8_MAX, &value)) {
            usage(argv[0]); return EXIT_FAILURE;
        }
        /* A state-changing research probe always emits its full wire trace. */
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_macos_dp_probe_input_alt((uint32_t)display_index, variant, (uint8_t)value,
                                                          &diagnostics);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        printf("input-alt probe write sequence completed; observe the display manually.\n");
        return EXIT_SUCCESS;
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
                                                                bytes, (size_t)length, verbose ? &diagnostics : NULL);
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
                                                              verbose ? &diagnostics : NULL);
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
            unsigned long parsed = 0;
            if (!parse_unsigned(argv[option], UINT32_MAX, &parsed)) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            *destination = (uint32_t)parsed;
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
                                                                 verbose ? &diagnostics : NULL);
            if (error == RSS_DDC_OK) printf("verified %u\n", result.current_value);
        } else {
            error = rss_ddc_set_vcp_with_diagnostics((uint32_t)display_index, (uint8_t)vcp,
                                                      (uint16_t)value, verbose ? &diagnostics : NULL);
        }
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    usage(argv[0]);
    return EXIT_FAILURE;
}
