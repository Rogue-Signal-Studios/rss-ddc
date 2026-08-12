#include "rss_ddc.h"
#include "macos_internal.h"
#include "verify.h"

#include <stdio.h>
#include <unistd.h>

/** Bridges the portable callback to platform code without exposing platform handles publicly. */
void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message) {
    if (diagnostics != NULL && diagnostics->callback != NULL) diagnostics->callback(diagnostics->context, message);
}

/** Emits stable selected-display evidence without leaking transient IOKit handles. */
static void diagnostic_binding(const RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics) {
    char message[512] = {};
    snprintf(message, sizeof(message),
             "display=%u product=%s manufacturer=%s serial=%s provider=%s transport=%s branch=%s",
             binding->display.list_index, binding->display.product_name,
             binding->display.manufacturer[0] ? binding->display.manufacturer : "<unavailable>",
             binding->display.serial[0] ? binding->display.serial : "<unavailable>",
             rss_ddc_provider_string(binding->display.provider), binding->display.transport,
             binding->display.branch_device_id[0] ? binding->display.branch_device_id : "<unavailable>");
    rss_macos_diagnostic(diagnostics, message);
}

/** Delegates discovery to macOS while retaining the public API's value-only contract. */
RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count) {
    return rss_macos_discover_displays(displays, capacity, count);
}

/** Resolves and then releases the private binding; callers receive only its public snapshot. */
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display) {
    return rss_ddc_get_display_with_diagnostics(list_index, display, NULL);
}

RSSDDCError rss_ddc_get_display_with_diagnostics(uint32_t list_index, RSSDDCDisplay *display,
                                                  const RSSDDCDiagnostics *diagnostics) {
    if (display == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) *display = binding.display;
    else {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding.correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(&binding);
        if (detail != NULL) rss_macos_diagnostic(diagnostics, detail);
    }
    rss_macos_release_binding(&binding);
    return error;
}

/** Keeps the concise API free of diagnostics while sharing the same validation path. */
RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result) {
    return rss_ddc_get_vcp_with_diagnostics(list_index, vcp_code, result, NULL);
}

RSSDDCError rss_ddc_get_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result,
                                              const RSSDDCDiagnostics *diagnostics) {
    if (result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) {
        diagnostic_binding(&binding, diagnostics);
        error = rss_macos_provider_get_vcp(&binding, vcp_code, result, diagnostics);
    } else {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding.correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(&binding);
        if (detail != NULL) rss_macos_diagnostic(diagnostics, detail);
    }
    rss_macos_release_binding(&binding);
    return error;
}

/** Keeps the concise SET API free of diagnostics while sharing the full safety path. */
RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code, uint16_t value) {
    return rss_ddc_set_vcp_with_diagnostics(list_index, vcp_code, value, NULL);
}

RSSDDCError rss_ddc_set_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code, uint16_t value,
                                              const RSSDDCDiagnostics *diagnostics) {
    /* VCP code zero is reserved/invalid for a Set VCP request; reject before any platform lookup. */
    if (vcp_code == 0) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) {
        diagnostic_binding(&binding, diagnostics);
        error = rss_macos_provider_set_vcp(&binding, vcp_code, value, diagnostics);
    } else {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding.correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(&binding);
        if (detail != NULL) rss_macos_diagnostic(diagnostics, detail);
    }
    rss_macos_release_binding(&binding);
    return error;
}

typedef struct {
    uint32_t list_index;
    RSSMacOSBinding initial_binding;
    RSSMacOSDisplayIdentity identity;
    const RSSDDCDiagnostics *diagnostics;
} RSSMacOSVerifyContext;

/** SET is intentionally delegated unchanged to the selected provider backend. */
static RSSDDCError macos_verify_set(void *opaque, uint8_t vcp_code, uint16_t value) {
    RSSMacOSVerifyContext *context = opaque;
    return rss_macos_provider_set_vcp(&context->initial_binding, vcp_code, value, context->diagnostics);
}

/**
 * Re-resolves the original list position but refuses to use it unless all
 * retained identity evidence agrees. This prevents an input switch, unplug, or
 * list reorder from verifying a sibling monitor that inherited the index.
 */
static RSSDDCError macos_verify_get(void *opaque, uint8_t vcp_code, RSSDDCVCPResult *result) {
    RSSMacOSVerifyContext *context = opaque;
    RSSMacOSBinding verification_binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(context->list_index, &verification_binding);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(context->diagnostics,
                             rss_macos_correlation_failure_string(verification_binding.correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(&verification_binding);
        if (detail != NULL) rss_macos_diagnostic(context->diagnostics, detail);
        rss_macos_diagnostic(context->diagnostics,
                             "verify target identity unavailable; refusing to reuse the display index");
        rss_macos_release_binding(&verification_binding);
        return RSS_DDC_ERROR_VERIFY_UNAVAILABLE;
    }
    if (!rss_macos_capture_binding_identity(&verification_binding) ||
        !rss_macos_binding_matches_identity(&verification_binding, &context->identity)) {
        rss_macos_diagnostic(context->diagnostics,
                             "verify target identity changed; refusing to address the current display index");
        rss_macos_release_binding(&verification_binding);
        return RSS_DDC_ERROR_VERIFY_UNAVAILABLE;
    }
    diagnostic_binding(&verification_binding, context->diagnostics);
    error = rss_macos_provider_get_vcp(&verification_binding, vcp_code, result, context->diagnostics);
    rss_macos_release_binding(&verification_binding);
    return error;
}

/** Delays are invoked only by the explicit verification orchestrator, never by plain GET or SET. */
static void macos_verify_sleep(void *opaque, uint32_t milliseconds) {
    (void)opaque;
    usleep(milliseconds * 1000u);
}

RSSDDCError rss_ddc_set_vcp_and_verify(uint32_t list_index, uint8_t vcp_code, uint16_t value,
                                        const RSSDDCVerifyPolicy *policy, RSSDDCVCPResult *result) {
    return rss_ddc_set_vcp_and_verify_with_diagnostics(list_index, vcp_code, value, policy, result, NULL);
}

RSSDDCError rss_ddc_set_vcp_and_verify_with_diagnostics(uint32_t list_index, uint8_t vcp_code,
                                                         uint16_t value, const RSSDDCVerifyPolicy *policy,
                                                         RSSDDCVCPResult *result,
                                                         const RSSDDCDiagnostics *diagnostics) {
    if (vcp_code == 0 || result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCVerifyPolicy effective_policy = policy == NULL ? rss_ddc_default_verify_policy() : *policy;
    if (!rss_ddc_verify_policy_is_valid(&effective_policy)) return RSS_DDC_ERROR_ARGUMENT;

    RSSMacOSVerifyContext context = {.list_index = list_index, .diagnostics = diagnostics};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &context.initial_binding);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(context.initial_binding.correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(&context.initial_binding);
        if (detail != NULL) rss_macos_diagnostic(diagnostics, detail);
        rss_macos_release_binding(&context.initial_binding);
        return error;
    }
    if (!rss_macos_capture_binding_identity(&context.initial_binding)) {
        rss_macos_diagnostic(diagnostics, "verify target identity unavailable before SET; refusing to write");
        rss_macos_release_binding(&context.initial_binding);
        return RSS_DDC_ERROR_SAFETY_GATE;
    }
    context.identity = context.initial_binding.identity;
    diagnostic_binding(&context.initial_binding, diagnostics);
    const RSSDDCVerifyOperations operations = {
        .context = &context,
        .set_vcp = macos_verify_set,
        .get_vcp = macos_verify_get,
        .sleep_ms = macos_verify_sleep,
        .diagnostics = diagnostics,
    };
    error = rss_ddc_orchestrate_set_vcp_and_verify(vcp_code, value, &effective_policy, result, &operations);
    rss_macos_release_binding(&context.initial_binding);
    return error;
}
