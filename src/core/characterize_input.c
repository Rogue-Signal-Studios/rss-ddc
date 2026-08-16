#include "rss_ddc.h"

#include <string.h>

static bool display_identity_matches(const RSSDDCDisplay *expected, const RSSDDCDisplay *live) {
    return expected != NULL && live != NULL && expected->cg_display_id == live->cg_display_id &&
           expected->provider == live->provider &&
           strcmp(expected->product_name, live->product_name) == 0 &&
           strcmp(expected->manufacturer, live->manufacturer) == 0 &&
           strcmp(expected->serial, live->serial) == 0 &&
           strcmp(expected->transport, live->transport) == 0 &&
           strcmp(expected->branch_device_id, live->branch_device_id) == 0;
}

static RSSDDCProfileMethod write_method_for_route(const RSSDDCKnowledgeRoute *write) {
    if (write == NULL) {
        return RSS_DDC_PROFILE_METHOD_UNKNOWN;
    }
    if (write->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) {
        return RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT;
    }
    if (write->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP) {
        return RSS_DDC_PROFILE_METHOD_VCP;
    }
    return RSS_DDC_PROFILE_METHOD_UNKNOWN;
}

static bool input_value_in_authorized_domain(const RSSDDCCharacterization *characterization,
                                             const RSSDDCKnowledgeRoute *write, uint16_t value) {
    const RSSDDCEffectiveProfile *effective =
        rss_ddc_characterization_effective_profile(characterization);
    RSSDDCProfileMethod expected = write_method_for_route(write);
    if (effective == NULL || expected == RSS_DDC_PROFILE_METHOD_UNKNOWN) {
        return false;
    }
    for (size_t index = 0; index < rss_ddc_effective_profile_control_count(effective); ++index) {
        RSSDDCProfileControl control = {0};
        if (rss_ddc_effective_profile_control(effective, index, &control) != RSS_DDC_OK) {
            continue;
        }
        if (control.id != RSS_DDC_PROFILE_CONTROL_INPUT || control.method != expected) {
            continue;
        }
        for (size_t enum_index = 0; enum_index < control.enum_value_count; ++enum_index) {
            RSSDDCProfileEnumValue item = {0};
            if (rss_ddc_profile_control_enum_value(&control, enum_index, &item) == RSS_DDC_OK &&
                item.raw_value == value) {
                return true;
            }
        }
        return false;
    }
    return false;
}

static RSSDDCError dispatch_authorized_input(const RSSDDCKnowledgeRoute *write, uint32_t list_index,
                                             uint16_t value, const RSSDDCDiagnostics *diagnostics) {
    if (write->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP) {
        return rss_ddc_set_input_with_diagnostics(list_index, RSS_DDC_INPUT_SWITCH_STANDARD, value,
                                                  diagnostics);
    }
    if (write->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) {
        return rss_ddc_set_input_with_diagnostics(list_index, RSS_DDC_INPUT_SWITCH_LG_ALT, value,
                                                  diagnostics);
    }
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}

RSSDDCError rss_ddc_characterization_set_input(const RSSDDCCharacterization *characterization,
                                               uint16_t value) {
    return rss_ddc_characterization_set_input_with_diagnostics(characterization, value, NULL);
}

RSSDDCError rss_ddc_characterization_set_input_with_diagnostics(
    const RSSDDCCharacterization *characterization, uint16_t value,
    const RSSDDCDiagnostics *diagnostics) {
    const RSSDDCDisplay *display = rss_ddc_characterization_display(characterization);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCKnowledgeRoute *write = NULL;
    RSSDDCDisplay live = {0};
    RSSDDCError error = RSS_DDC_OK;

    if (characterization == NULL || display == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    error = rss_ddc_characterization_resolve(characterization, "inputs.switching", &resolution);
    if (error == RSS_DDC_ERROR_NOT_FOUND) {
        return error;
    }
    if (error != RSS_DDC_OK) {
        return error;
    }
    if (rss_ddc_monitor_knowledge_resolution_has_conflict(resolution)) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return RSS_DDC_ERROR_PROFILE_CONFLICT;
    }

    write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
    if (write == NULL) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    if (!rss_ddc_monitor_knowledge_resolution_write_authorized(resolution)) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return RSS_DDC_ERROR_PROFILE_UNSAFE;
    }
    if (write->kind != RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP &&
        write->kind != RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    if (!input_value_in_authorized_domain(characterization, write, value)) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return RSS_DDC_ERROR_ARGUMENT;
    }

    error = rss_ddc_get_display(display->list_index, &live);
    if (error != RSS_DDC_OK) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return error;
    }
    if (!display_identity_matches(display, &live)) {
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
        return RSS_DDC_ERROR_SAFETY_GATE;
    }

    error = dispatch_authorized_input(write, display->list_index, value, diagnostics);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    return error;
}
