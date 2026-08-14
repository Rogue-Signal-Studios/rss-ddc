@import CoreGraphics;
@import ColorSync;

@import Foundation;
@import IOKit;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "correlation.h"
#include "dpcd.h"
#include "enumeration.h"
#include "reader.h"
#include "macos_internal.h"
#include "private/coredisplay_private.h"
#include "private/iodp_private.h"

const char *rss_macos_correlation_failure_string(RSSMacOSCorrelationFailure failure) {
    switch (failure) {
        case RSS_MACOS_CORRELATION_NONE: return "provider correlation failed without a recorded predicate";
        case RSS_MACOS_CORRELATION_NO_SELECTED_DISPLAY: return "correlation: selected CoreGraphics display was not found";
        case RSS_MACOS_CORRELATION_NO_DISPLAY_REGISTRY_NODE: return "correlation: selected display has no matching registry node";
        case RSS_MACOS_CORRELATION_NO_SERVICE_PROXY: return "correlation: zero external DCPAVServiceProxy candidates";
        case RSS_MACOS_CORRELATION_AMBIGUOUS_SERVICE_PROXY: return "correlation: multiple external DCPAVServiceProxy candidates";
        case RSS_MACOS_CORRELATION_MISSING_SERVICE_PROVIDER: return "correlation: service EPICProviderClass is missing";
        case RSS_MACOS_CORRELATION_NOT_EXTERNAL: return "correlation: selected service is not external";
        case RSS_MACOS_CORRELATION_NO_ACTIVE_BRANCH: return "correlation: no active DisplayPort branch matches the selected product";
        case RSS_MACOS_CORRELATION_MISSING_BRANCH_DEVICE_ID: return "correlation: matching active branch has no BranchDeviceID";
        case RSS_MACOS_CORRELATION_AMBIGUOUS_ACTIVE_BRANCH: return "correlation: multiple active branches match the selected product";
        case RSS_MACOS_CORRELATION_NO_DCPDP_DEVICE_PROXY: return "correlation: zero external DCPDPDeviceProxy candidates for BranchDeviceID";
        case RSS_MACOS_CORRELATION_AMBIGUOUS_DCPDP_DEVICE_PROXY: return "correlation: multiple external DCPDPDeviceProxy candidates for BranchDeviceID";
        case RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT: return "correlation: unexpected Service EPIC parent";
        case RSS_MACOS_CORRELATION_PROVIDER_MISMATCH: return "correlation: Service EPICProviderClass does not match the selected backend";
        case RSS_MACOS_CORRELATION_ROLE_MISMATCH: return "correlation: Service EPIC role does not match the selected backend";
        case RSS_MACOS_CORRELATION_UNIT_MISMATCH: return "correlation: Service Unit does not match the selected backend";
        case RSS_MACOS_CORRELATION_UI_UNSUPPORTED: return "correlation: IOAVServiceUserInterfaceSupported is not true";
    }
    return "provider correlation failed with an unrecognized predicate";
}

const char *rss_macos_correlation_detail_string(const RSSMacOSBinding *binding) {
    return binding != NULL && binding->correlation_detail[0] != '\0' ? binding->correlation_detail : NULL;
}

/** Copies only string-valued registry fields into the fixed-size public snapshot. */
static bool copyCFString(CFTypeRef value, char destination[RSS_DDC_TEXT_MAX]) {
    return value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringGetCString(value, destination, RSS_DDC_TEXT_MAX, kCFStringEncodingUTF8);
}

/**
 * Resolves the IOKit adapter for a CoreGraphics display. The returned registry
 * entry is retained and callers must IOObjectRelease it.
 */
static io_service_t adapter_for_display(CGDirectDisplayID display_id) {
    CFDictionaryRef info = CoreDisplay_DisplayCreateInfoDictionary(display_id);
    if (info == NULL) return MACH_PORT_NULL;
    CFStringRef location = CFDictionaryGetValue(info, CFSTR("IODisplayLocation"));
    io_service_t adapter = location == NULL ? MACH_PORT_NULL :
        IORegistryEntryCopyFromPath(kIOMainPortDefault, location);
    CFRelease(info);
    return adapter;
}

/** Reads user-facing product metadata only; absence is non-fatal for discovery. */
static void copy_display_name(CGDirectDisplayID display_id, RSSDDCDisplay *display) {
    snprintf(display->product_name, sizeof(display->product_name), "Unknown Display");
    io_service_t adapter = adapter_for_display(display_id);
    if (adapter == MACH_PORT_NULL) return;
    CFTypeRef attributes = IORegistryEntrySearchCFProperty(adapter, kIOServicePlane,
                                                            CFSTR("DisplayAttributes"),
                                                            kCFAllocatorDefault,
                                                            kIORegistryIterateRecursively);
    if (attributes != NULL && CFGetTypeID(attributes) == CFDictionaryGetTypeID()) {
        NSDictionary *product = [(NSDictionary *)attributes objectForKey:@"ProductAttributes"];
        NSString *name = [product objectForKey:@"ProductName"];
        if (name != nil) [name getCString:display->product_name maxLength:sizeof(display->product_name)
                                  encoding:NSUTF8StringEncoding];
    }
    if (attributes != NULL) CFRelease(attributes);
    IOObjectRelease(adapter);
}

/**
 * The ColorSync UUID derived from the CoreGraphics display is retained only for
 * Set-and-Verify. Without it, a later list-index lookup cannot safely prove it
 * still refers to the same monitor, so that optional operation fails before a write.
 */
static bool copy_display_uuid(CGDirectDisplayID display_id, char destination[RSS_DDC_TEXT_MAX]) {
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(display_id);
    if (uuid == NULL) return false;
    CFStringRef text = CFUUIDCreateString(kCFAllocatorDefault, uuid);
    bool copied = copyCFString(text, destination);
    if (text != NULL) CFRelease(text);
    CFRelease(uuid);
    return copied;
}

static bool copy_identity_from_display(const RSSDDCDisplay *display, RSSMacOSDisplayIdentity *identity) {
    if (display == NULL || identity == NULL) return false;
    *identity = (RSSMacOSDisplayIdentity){.cg_display_id = display->cg_display_id, .provider = display->provider};
    snprintf(identity->product_name, sizeof(identity->product_name), "%s", display->product_name);
    snprintf(identity->branch_device_id, sizeof(identity->branch_device_id), "%s", display->branch_device_id);
    snprintf(identity->transport, sizeof(identity->transport), "%s", display->transport);
    identity->valid = copy_display_uuid(display->cg_display_id, identity->display_uuid);
    return identity->valid;
}

bool rss_macos_binding_matches_identity(const RSSMacOSBinding *binding,
                                        const RSSMacOSDisplayIdentity *identity) {
    if (binding == NULL || identity == NULL || !binding->identity.valid || !identity->valid) return false;
    return binding->identity.cg_display_id == identity->cg_display_id &&
        binding->identity.provider == identity->provider &&
        strcmp(binding->identity.display_uuid, identity->display_uuid) == 0 &&
        strcmp(binding->identity.product_name, identity->product_name) == 0 &&
        strcmp(binding->identity.branch_device_id, identity->branch_device_id) == 0 &&
        strcmp(binding->identity.transport, identity->transport) == 0;
}

bool rss_macos_capture_binding_identity(RSSMacOSBinding *binding) {
    return binding != NULL && copy_identity_from_display(&binding->display, &binding->identity);
}

/** External-only prevents selecting internal panel service proxies for DDC control. */
static bool is_external(io_service_t entry) {
    CFTypeRef location = IORegistryEntryCreateCFProperty(entry, CFSTR("Location"), kCFAllocatorDefault, 0);
    bool external = location != NULL && CFGetTypeID(location) == CFStringGetTypeID() &&
        CFStringCompare(location, CFSTR("External"), 0) == kCFCompareEqualTo;
    if (location != NULL) CFRelease(location);
    return external;
}

/**
 * Classifies a service from its immediate EPIC parent. The provider string is
 * runtime data; no CPU-generation or registry-ID branch is used.
 */
static bool inspect_service(io_service_t service, RSSDDCDisplay *display) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) != KERN_SUCCESS) return false;
    CFTypeRef provider = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef role = IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0);
    char provider_class[RSS_DDC_TEXT_MAX] = {};
    char role_text[RSS_DDC_TEXT_MAX] = {};
    bool provider_ok = copyCFString(provider, provider_class);
    (void)copyCFString(role, role_text);
    display->provider = rss_ddc_provider_from_registry_class(provider_class);
    display->capabilities = rss_ddc_provider_capabilities(display->provider);
    snprintf(display->transport, sizeof(display->transport), "%s", role_text[0] ? role_text : "unknown");
    if (role != NULL) CFRelease(role);
    if (provider != NULL) CFRelease(provider);
    IOObjectRelease(parent);
    return provider_ok;
}

/**
 * Enforces the hardware-validated PS190 Service identity before an IOAV user
 * client can be created. Each predicate distinguishes Endpoint11 Service
 * state from unrelated external device/service proxies.
 */
static bool is_ps190_service_identity(io_service_t service, const char *device_role, RSSMacOSBinding *binding) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    RSSDDCPS190CorrelationFacts facts = {.service_candidate_count = 1, .service_external = is_external(service)};
    if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) == KERN_SUCCESS) facts.epic_parent_present = true;
    CFTypeRef epic_name = parent == MACH_PORT_NULL ? NULL :
        IORegistryEntryCreateCFProperty(parent, CFSTR("EPICName"), kCFAllocatorDefault, 0);
    CFTypeRef role = parent == MACH_PORT_NULL ? NULL :
        IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0);
    CFTypeRef provider = parent == MACH_PORT_NULL ? NULL :
        IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef unit = IORegistryEntryCreateCFProperty(service, CFSTR("Unit"), kCFAllocatorDefault, 0);
    CFTypeRef ui_supported = IORegistryEntryCreateCFProperty(service, CFSTR("IOAVServiceUserInterfaceSupported"),
                                                              kCFAllocatorDefault, 0);
    char epic_name_text[RSS_DDC_TEXT_MAX] = {};
    char role_text[RSS_DDC_TEXT_MAX] = {};
    char provider_text[RSS_DDC_TEXT_MAX] = {};
    int64_t unit_value = -1;
    (void)copyCFString(epic_name, epic_name_text);
    (void)copyCFString(role, role_text);
    if (copyCFString(provider, provider_text)) facts.epic_provider = rss_ddc_provider_from_registry_class(provider_text);
    facts.epic_name_matches = strcmp(epic_name_text, "dcpav-service-epic") == 0;
    facts.unit_zero = unit != NULL && CFGetTypeID(unit) == CFNumberGetTypeID() &&
        CFNumberGetValue(unit, kCFNumberSInt64Type, &unit_value) && unit_value == 0;
    facts.ui_supported = ui_supported != NULL && CFGetTypeID(ui_supported) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(ui_supported);
    facts.branch_device_role_present = device_role != NULL && device_role[0] != '\0';
    facts.service_role_matches_branch_device = facts.branch_device_role_present && strcmp(role_text, device_role) == 0;
    RSSDDCPS190CorrelationResult result = rss_ddc_evaluate_ps190_correlation(&facts);
    if (result != RSS_DDC_PS190_CORRELATION_OK) {
        switch (result) {
            case RSS_DDC_PS190_CORRELATION_NOT_EXTERNAL: binding->correlation_failure = RSS_MACOS_CORRELATION_NOT_EXTERNAL; break;
            case RSS_DDC_PS190_CORRELATION_NO_EPIC_PARENT:
            case RSS_DDC_PS190_CORRELATION_EPIC_NAME_MISMATCH:
            case RSS_DDC_PS190_CORRELATION_BRANCH_DEVICE_ROLE_MISSING:
                binding->correlation_failure = RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT;
                break;
            case RSS_DDC_PS190_CORRELATION_PROVIDER_MISMATCH:
                binding->correlation_failure = RSS_MACOS_CORRELATION_PROVIDER_MISMATCH;
                break;
            case RSS_DDC_PS190_CORRELATION_UNIT_MISMATCH: binding->correlation_failure = RSS_MACOS_CORRELATION_UNIT_MISMATCH; break;
            case RSS_DDC_PS190_CORRELATION_UI_UNSUPPORTED: binding->correlation_failure = RSS_MACOS_CORRELATION_UI_UNSUPPORTED; break;
            case RSS_DDC_PS190_CORRELATION_ROLE_MISMATCH: binding->correlation_failure = RSS_MACOS_CORRELATION_ROLE_MISMATCH; break;
            case RSS_DDC_PS190_CORRELATION_NO_SERVICE:
            case RSS_DDC_PS190_CORRELATION_AMBIGUOUS_SERVICE:
            case RSS_DDC_PS190_CORRELATION_OK:
                binding->correlation_failure = RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT;
                break;
        }
        snprintf(binding->correlation_detail, sizeof(binding->correlation_detail),
                 "correlation candidate provider=%s role=%s location=%s unit=%lld expected-device-role=%s",
                 provider_text[0] ? provider_text : "<missing>", role_text[0] ? role_text : "<missing>",
                 facts.service_external ? "External" : "<not-external>", (long long)unit_value,
                 device_role != NULL && device_role[0] != '\0' ? device_role : "<missing>");
    }
    if (ui_supported != NULL) CFRelease(ui_supported);
    if (unit != NULL) CFRelease(unit);
    if (provider != NULL) CFRelease(provider);
    if (role != NULL) CFRelease(role);
    if (epic_name != NULL) CFRelease(epic_name);
    if (parent != MACH_PORT_NULL) IOObjectRelease(parent);
    return result == RSS_DDC_PS190_CORRELATION_OK;
}

/**
 * DCPDP13 is selected solely from the immediate DCPAV Service EPIC provider,
 * never from a generic DisplayPort transport-state node. The service proxy is
 * already correlated to CoreGraphics' selected display; PS190 can expose a
 * DisplayPort-shaped transport, so connector/transport state cannot select
 * the conventional backend.
 */
static bool is_dp_service_identity(io_service_t service, RSSMacOSCorrelationFailure *failure) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    RSSDDCDPCorrelationFacts facts = {.service_candidate_count = 1, .service_external = is_external(service)};
    if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) == KERN_SUCCESS) {
        facts.epic_parent_present = true;
    }
    CFTypeRef provider = parent == MACH_PORT_NULL ? NULL :
        IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef ui_supported = IORegistryEntryCreateCFProperty(service, CFSTR("IOAVServiceUserInterfaceSupported"),
                                                              kCFAllocatorDefault, 0);
    char provider_text[RSS_DDC_TEXT_MAX] = {};
    if (copyCFString(provider, provider_text)) facts.epic_provider = rss_ddc_provider_from_registry_class(provider_text);
    facts.ui_supported = ui_supported != NULL && CFGetTypeID(ui_supported) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(ui_supported);
    RSSDDCDPCorrelationResult result = rss_ddc_evaluate_dp_correlation(&facts);
    switch (result) {
        case RSS_DDC_DP_CORRELATION_OK: break;
        case RSS_DDC_DP_CORRELATION_NO_SERVICE: *failure = RSS_MACOS_CORRELATION_NO_SERVICE_PROXY; break;
        case RSS_DDC_DP_CORRELATION_AMBIGUOUS_SERVICE: *failure = RSS_MACOS_CORRELATION_AMBIGUOUS_SERVICE_PROXY; break;
        case RSS_DDC_DP_CORRELATION_NOT_EXTERNAL: *failure = RSS_MACOS_CORRELATION_NOT_EXTERNAL; break;
        case RSS_DDC_DP_CORRELATION_NO_EPIC_PARENT: *failure = RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT; break;
        case RSS_DDC_DP_CORRELATION_PROVIDER_MISMATCH:
            *failure = provider == NULL ? RSS_MACOS_CORRELATION_MISSING_SERVICE_PROVIDER :
                RSS_MACOS_CORRELATION_PROVIDER_MISMATCH;
            break;
        case RSS_DDC_DP_CORRELATION_UI_UNSUPPORTED: *failure = RSS_MACOS_CORRELATION_UI_UNSUPPORTED; break;
    }
    if (ui_supported != NULL) CFRelease(ui_supported);
    if (provider != NULL) CFRelease(provider);
    if (parent != MACH_PORT_NULL) IOObjectRelease(parent);
    return result == RSS_DDC_DP_CORRELATION_OK;
}

static bool is_dcpdpservice_service_identity(io_service_t service, RSSMacOSCorrelationFailure *failure) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    RSSDDCDCPDPServiceCorrelationFacts facts = {.service_candidate_count = 1, .service_external = is_external(service)};
    char provider_text[RSS_DDC_TEXT_MAX] = {};
    if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) == KERN_SUCCESS) facts.epic_parent_present = true;
    CFTypeRef provider = parent == MACH_PORT_NULL ? NULL :
        IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef ui_supported = IORegistryEntryCreateCFProperty(service, CFSTR("IOAVServiceUserInterfaceSupported"),
                                                              kCFAllocatorDefault, 0);
    if (copyCFString(provider, provider_text)) facts.epic_provider_class = provider_text;
    facts.ui_supported = ui_supported != NULL && CFGetTypeID(ui_supported) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(ui_supported);
    RSSDDCDCPDPServiceCorrelationResult result = rss_ddc_evaluate_dcpdpservice_correlation(&facts);
    switch (result) {
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_OK: break;
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_NO_SERVICE: *failure = RSS_MACOS_CORRELATION_NO_SERVICE_PROXY; break;
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_AMBIGUOUS_SERVICE:
            *failure = RSS_MACOS_CORRELATION_AMBIGUOUS_SERVICE_PROXY;
            break;
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_NOT_EXTERNAL: *failure = RSS_MACOS_CORRELATION_NOT_EXTERNAL; break;
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_NO_EPIC_PARENT: *failure = RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT; break;
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_PROVIDER_MISMATCH:
            *failure = provider == NULL ? RSS_MACOS_CORRELATION_MISSING_SERVICE_PROVIDER :
                RSS_MACOS_CORRELATION_PROVIDER_MISMATCH;
            break;
        case RSS_DDC_DCPDP_SERVICE_CORRELATION_UI_UNSUPPORTED: *failure = RSS_MACOS_CORRELATION_UI_UNSUPPORTED; break;
    }
    if (ui_supported != NULL) CFRelease(ui_supported);
    if (provider != NULL) CFRelease(provider);
    if (parent != MACH_PORT_NULL) IOObjectRelease(parent);
    return result == RSS_DDC_DCPDP_SERVICE_CORRELATION_OK;
}

/**
 * Correlates one selected display adapter to one external DCPAVServiceProxy.
 * This is intentionally a selected-display scope, not a global service-class
 * lookup: multiple external displays may legitimately have the same provider.
 * Multiple candidates in this display's scope are rejected rather than guessed.
 */
static io_service_t service_for_display(CGDirectDisplayID display_id, RSSMacOSCorrelationFailure *failure) {
    if (failure != NULL) *failure = RSS_MACOS_CORRELATION_NO_SERVICE_PROXY;
    io_service_t adapter = adapter_for_display(display_id);
    if (adapter == MACH_PORT_NULL) {
        if (failure != NULL) *failure = RSS_MACOS_CORRELATION_NO_DISPLAY_REGISTRY_NODE;
        return MACH_PORT_NULL;
    }
    uint64_t adapter_id = 0;
    bool adapter_ok = IORegistryEntryGetRegistryEntryID(adapter, &adapter_id) == KERN_SUCCESS;
    IOObjectRelease(adapter);
    if (!adapter_ok) {
        if (failure != NULL) *failure = RSS_MACOS_CORRELATION_NO_DISPLAY_REGISTRY_NODE;
        return MACH_PORT_NULL;
    }
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    bool frame_buffer_seen = false;
    io_service_t match = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        if (IOObjectConformsTo(entry, "IOMobileFramebuffer")) {
            uint64_t identifier = 0;
            frame_buffer_seen = IORegistryEntryGetRegistryEntryID(entry, &identifier) == KERN_SUCCESS &&
                identifier == adapter_id;
            IOObjectRelease(entry);
            continue;
        }
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        if (frame_buffer_seen && strcmp(name, "DCPAVServiceProxy") == 0 && is_external(entry)) {
            if (match != MACH_PORT_NULL) {
                IOObjectRelease(entry);
                IOObjectRelease(match);
                IOObjectRelease(iterator);
                if (failure != NULL) *failure = RSS_MACOS_CORRELATION_AMBIGUOUS_SERVICE_PROXY;
                return MACH_PORT_NULL;
            }
            match = entry;
            continue;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    if (match == MACH_PORT_NULL && !frame_buffer_seen && failure != NULL) {
        *failure = RSS_MACOS_CORRELATION_NO_DISPLAY_REGISTRY_NODE;
    }
    return match;
}

/**
 * Finds the single active DisplayPort transport for the selected product and
 * copies its BranchDeviceID. Ambiguity is a safety failure, not a tie-break.
 */
static bool active_branch_for_product(const char *product_name, char branch[RSS_DDC_TEXT_MAX],
                                      RSSMacOSCorrelationFailure *failure) {
    *failure = RSS_MACOS_CORRELATION_NO_ACTIVE_BRANCH;
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return false;
    }
    IOObjectRelease(root);
    bool found = false;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t class_name = {};
        IOObjectGetClass(entry, class_name);
        if (strcmp(class_name, "IOPortTransportStateDisplayPort") != 0) {
            IOObjectRelease(entry);
            continue;
        }
        CFTypeRef active = IORegistryEntryCreateCFProperty(entry, CFSTR("Active"), kCFAllocatorDefault, 0);
        CFTypeRef product = IORegistryEntryCreateCFProperty(entry, CFSTR("ProductName"), kCFAllocatorDefault, 0);
        CFTypeRef branch_value = IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
        char product_text[RSS_DDC_TEXT_MAX] = {};
        bool active_true = active != NULL && CFGetTypeID(active) == CFBooleanGetTypeID() && CFBooleanGetValue(active);
        bool selected_transport = active_true && copyCFString(product, product_text) &&
            strcmp(product_text, product_name) == 0;
        bool matches = selected_transport && copyCFString(branch_value, branch);
        if (branch_value != NULL) CFRelease(branch_value);
        if (product != NULL) CFRelease(product);
        if (active != NULL) CFRelease(active);
        IOObjectRelease(entry);
        if (selected_transport && !matches) *failure = RSS_MACOS_CORRELATION_MISSING_BRANCH_DEVICE_ID;
        if (matches && !found) found = true;
        else if (matches) {
            IOObjectRelease(iterator);
            *failure = RSS_MACOS_CORRELATION_AMBIGUOUS_ACTIVE_BRANCH;
            return false;
        }
    }
    IOObjectRelease(iterator);
    return found;
}

/** Confirms the active branch maps to exactly one external DCPDP device proxy. */
static bool branch_has_unique_device_proxy(const char *branch, char device_role[RSS_DDC_TEXT_MAX],
                                           io_service_t *device_proxy, RSSMacOSCorrelationFailure *failure) {
    device_role[0] = '\0';
    if (device_proxy != NULL) *device_proxy = MACH_PORT_NULL;
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return false;
    }
    IOObjectRelease(root);
    unsigned int count = 0;
    io_service_t match = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        if (strcmp(name, "DCPDPDeviceProxy") == 0 && is_external(entry)) {
            CFTypeRef value = IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
            char candidate[RSS_DDC_TEXT_MAX] = {};
            if (copyCFString(value, candidate) && strcmp(candidate, branch) == 0) {
                ++count;
                if (count == 1) {
                    /* Retain the exact branch candidate for the native IODP Device constructor. */
                    match = entry;
                    IOObjectRetain(match);
                }
                io_registry_entry_t parent = MACH_PORT_NULL;
                CFTypeRef role = IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) == KERN_SUCCESS ?
                    IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0) : NULL;
                bool role_ok = copyCFString(role, device_role);
                if (role != NULL) CFRelease(role);
                if (parent != MACH_PORT_NULL) IOObjectRelease(parent);
                if (!role_ok) *failure = RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT;
            }
            if (value != NULL) CFRelease(value);
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    if (count == 0) *failure = RSS_MACOS_CORRELATION_NO_DCPDP_DEVICE_PROXY;
    else if (count > 1) {
        *failure = RSS_MACOS_CORRELATION_AMBIGUOUS_DCPDP_DEVICE_PROXY;
        if (match != MACH_PORT_NULL) { IOObjectRelease(match); match = MACH_PORT_NULL; }
    }
    if (device_proxy != NULL) *device_proxy = match;
    else if (match != MACH_PORT_NULL) IOObjectRelease(match);
    return count == 1 && device_role[0] != '\0';
}

/** Returns the immediate EPIC role without treating it as a globally unique display identity. */
static bool service_epic_role(io_service_t service, char role[RSS_DDC_TEXT_MAX]) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) != KERN_SUCCESS) return false;
    CFTypeRef value = IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0);
    bool copied = copyCFString(value, role);
    if (value != NULL) CFRelease(value);
    IOObjectRelease(parent);
    return copied;
}

/**
 * Registry-only DCPDP13 diagnostic candidate search. The candidate must share
 * the selected Service EPIC role and be a DCPDP-device endpoint. This does not
 * construct IODPDevice or issue DPCD; ties deliberately remain ambiguous.
 */
static unsigned int dp_device_candidates_for_role(const char *role, io_service_t *candidate_out) {
    if (role == NULL || role[0] == '\0') return 0;
    if (candidate_out != NULL) *candidate_out = MACH_PORT_NULL;
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return 0;
    }
    IOObjectRelease(root);
    unsigned int count = 0;
    io_service_t candidate = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        if (strcmp(name, "DCPDPDeviceProxy") == 0 && is_external(entry)) {
            io_registry_entry_t parent = MACH_PORT_NULL;
            CFTypeRef epic_name = IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) == KERN_SUCCESS ?
                IORegistryEntryCreateCFProperty(parent, CFSTR("EPICName"), kCFAllocatorDefault, 0) : NULL;
            CFTypeRef candidate_role = parent == MACH_PORT_NULL ? NULL :
                IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0);
            char epic_name_text[RSS_DDC_TEXT_MAX] = {};
            char candidate_role_text[RSS_DDC_TEXT_MAX] = {};
            bool matches = copyCFString(epic_name, epic_name_text) && copyCFString(candidate_role, candidate_role_text) &&
                rss_ddc_dp_device_proxy_matches(true, epic_name_text, candidate_role_text, role);
            if (candidate_role != NULL) CFRelease(candidate_role);
            if (epic_name != NULL) CFRelease(epic_name);
            if (parent != MACH_PORT_NULL) IOObjectRelease(parent);
            if (matches) {
                ++count;
                if (count == 1) {
                    candidate = entry;
                    IOObjectRetain(candidate);
                }
            }
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    if (count > 1 && candidate != MACH_PORT_NULL) { IOObjectRelease(candidate); candidate = MACH_PORT_NULL; }
    if (candidate_out != NULL) *candidate_out = candidate;
    else if (candidate != MACH_PORT_NULL) IOObjectRelease(candidate);
    return count;
}

/**
 * Produces public display snapshots without opening IOAVService. Registry and
 * CoreFoundation objects created here are released before the function returns.
 */
RSSDDCError rss_macos_discover_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count) {
    if (count == NULL || (displays == NULL && capacity != 0)) return RSS_DDC_ERROR_ARGUMENT;
    CGDisplayCount total = 0;
    if (CGGetOnlineDisplayList(0, NULL, &total) != kCGErrorSuccess) return RSS_DDC_ERROR_DISCOVERY;
    *count = (size_t)total;
    if (total == 0 || capacity == 0) return RSS_DDC_OK;

    CGDirectDisplayID *ids = calloc((size_t)total, sizeof(*ids));
    if (ids == NULL) return RSS_DDC_ERROR_SYSTEM;
    CGDisplayCount observed = 0;
    if (CGGetOnlineDisplayList(total, ids, &observed) != kCGErrorSuccess) {
        free(ids);
        return RSS_DDC_ERROR_DISCOVERY;
    }
    *count = (size_t)observed;
    size_t written = rss_ddc_enumeration_write_count((size_t)observed, capacity);
    for (size_t index = 0; index < written; ++index) {
        RSSDDCDisplay display = {0};
        display.list_index = (uint32_t)index + 1;
        display.cg_display_id = ids[index];
        display.online = true;
        display.external = !CGDisplayIsBuiltin(ids[index]);
        copy_display_name(ids[index], &display);
        io_service_t service = service_for_display(ids[index], NULL);
        if (service != MACH_PORT_NULL) {
            (void)inspect_service(service, &display);
            IOObjectRelease(service);
        }
        displays[index] = display;
    }
    free(ids);
    return RSS_DDC_OK;
}

/* Avoid a hidden display-count limit when an operation resolves a list index. */
static RSSDDCError online_display_id_for_index(uint32_t list_index, CGDirectDisplayID *display_id) {
    if (list_index == 0 || display_id == NULL) return RSS_DDC_ERROR_ARGUMENT;
    CGDisplayCount total = 0;
    if (CGGetOnlineDisplayList(0, NULL, &total) != kCGErrorSuccess) return RSS_DDC_ERROR_DISCOVERY;
    if ((uint64_t)list_index > (uint64_t)total) return RSS_DDC_ERROR_NOT_FOUND;
    CGDirectDisplayID *ids = calloc((size_t)total, sizeof(*ids));
    if (ids == NULL) return RSS_DDC_ERROR_SYSTEM;
    CGDisplayCount observed = 0;
    CGError error = CGGetOnlineDisplayList(total, ids, &observed);
    if (error != kCGErrorSuccess || (uint64_t)list_index > (uint64_t)observed) {
        free(ids);
        return error == kCGErrorSuccess ? RSS_DDC_ERROR_NOT_FOUND : RSS_DDC_ERROR_DISCOVERY;
    }
    *display_id = ids[list_index - 1];
    free(ids);
    return RSS_DDC_OK;
}

/**
 * Builds the private binding used by hardware operations. The exact Service
 * EPIC provider is decisive. PS190 additionally requires its observed active
 * branch/device relationship; conventional DP intentionally does not because
 * its live USB-C topology has no BranchDeviceID and the service-side identity
 * is the evidence-backed relationship for IOAVService construction.
 */
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding) {
    if (binding == NULL || list_index == 0) return RSS_DDC_ERROR_ARGUMENT;
    *binding = (RSSMacOSBinding){0};
    CGDirectDisplayID display_id = 0;
    RSSDDCError list_error = online_display_id_for_index(list_index, &display_id);
    if (list_error != RSS_DDC_OK) {
        binding->correlation_failure = RSS_MACOS_CORRELATION_NO_SELECTED_DISPLAY;
        return list_error;
    }
    binding->display.list_index = list_index;
    binding->display.cg_display_id = display_id;
    binding->display.online = true;
    binding->display.external = !CGDisplayIsBuiltin(display_id);
    copy_display_name(display_id, &binding->display);
    binding->service_proxy = service_for_display(display_id, &binding->correlation_failure);
    if (binding->service_proxy == MACH_PORT_NULL) {
        return binding->correlation_failure == RSS_MACOS_CORRELATION_AMBIGUOUS_SERVICE_PROXY ?
            RSS_DDC_ERROR_SAFETY_GATE : RSS_DDC_ERROR_DISCOVERY;
    }
    if (!inspect_service(binding->service_proxy, &binding->display)) {
        binding->correlation_failure = RSS_MACOS_CORRELATION_MISSING_SERVICE_PROVIDER;
        return RSS_DDC_ERROR_DISCOVERY;
    }
    if (binding->display.provider == RSS_DDC_PROVIDER_DCPDP13) {
        binding->dp_safety_gate = binding->display.external &&
            is_dp_service_identity(binding->service_proxy, &binding->correlation_failure);
        if (!binding->dp_safety_gate) {
            if (!binding->display.external) binding->correlation_failure = RSS_MACOS_CORRELATION_NOT_EXTERNAL;
            return RSS_DDC_ERROR_SAFETY_GATE;
        }
    } else if (binding->display.provider == RSS_DDC_PROVIDER_DCPDP_SERVICE) {
        binding->dp_safety_gate = binding->display.external &&
            is_dcpdpservice_service_identity(binding->service_proxy, &binding->correlation_failure);
        if (!binding->dp_safety_gate) {
            if (!binding->display.external) binding->correlation_failure = RSS_MACOS_CORRELATION_NOT_EXTERNAL;
            return RSS_DDC_ERROR_SAFETY_GATE;
        }
    } else if (binding->display.provider == RSS_DDC_PROVIDER_PS190) {
        RSSMacOSCorrelationFailure branch_failure = RSS_MACOS_CORRELATION_NONE;
        char device_role[RSS_DDC_TEXT_MAX] = {};
        bool branch_ok = active_branch_for_product(binding->display.product_name, binding->display.branch_device_id,
                                                    &branch_failure) &&
            branch_has_unique_device_proxy(binding->display.branch_device_id, device_role,
                                           &binding->dcpdp_device_proxy, &branch_failure);
        binding->ps190_safety_gate = binding->display.external && branch_ok &&
            is_ps190_service_identity(binding->service_proxy, device_role, binding);
        if (!binding->ps190_safety_gate) {
            if (!binding->display.external) binding->correlation_failure = RSS_MACOS_CORRELATION_NOT_EXTERNAL;
            else if (!branch_ok) binding->correlation_failure = branch_failure;
            if (binding->dcpdp_device_proxy != MACH_PORT_NULL) {
                IOObjectRelease(binding->dcpdp_device_proxy);
                binding->dcpdp_device_proxy = MACH_PORT_NULL;
            }
            return RSS_DDC_ERROR_SAFETY_GATE;
        }
    }
    return RSS_DDC_OK;
}

/* Keep the evolving private binding layout out of public convenience frames. */
RSSDDCError rss_macos_get_display_snapshot(uint32_t list_index, RSSDDCDisplay *display,
                                           RSSMacOSCorrelationFailure *failure,
                                           char *detail, size_t detail_capacity) {
    if (display == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (failure != NULL) *failure = RSS_MACOS_CORRELATION_NONE;
    if (detail != NULL && detail_capacity != 0) detail[0] = '\0';

    RSSMacOSBinding *binding = calloc(1, sizeof(*binding));
    if (binding == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCError error = rss_macos_resolve_binding(list_index, binding);
    if (error == RSS_DDC_OK) *display = binding->display;
    if (failure != NULL) *failure = binding->correlation_failure;
    const char *binding_detail = rss_macos_correlation_detail_string(binding);
    if (detail != NULL && detail_capacity != 0 && binding_detail != NULL)
        snprintf(detail, detail_capacity, "%s", binding_detail);
    rss_macos_release_binding(binding);
    free(binding);
    return error;
}

/** Keeps the large private binding and variable MCCS model out of public API frames. */
RSSDDCError rss_macos_get_mccs_capabilities_snapshot(uint32_t list_index,
                                                      RSSDDCMCCSCapabilities *capabilities,
                                                      const RSSDDCDiagnostics *diagnostics) {
    if (capabilities == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding *binding = calloc(1, sizeof(*binding));
    if (binding == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCError error = rss_macos_resolve_binding(list_index, binding);
    if (error == RSS_DDC_OK) {
        error = rss_macos_provider_get_mccs_capabilities(binding, capabilities, diagnostics);
    } else {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding->correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(binding);
        if (detail != NULL) rss_macos_diagnostic(diagnostics, detail);
    }
    rss_macos_release_binding(binding);
    free(binding);
    return error;
}

/** Balances service_for_display's retained proxy on every success and error path. */
void rss_macos_release_binding(RSSMacOSBinding *binding) {
    if (binding != NULL && binding->service_proxy != MACH_PORT_NULL) IOObjectRelease(binding->service_proxy);
    if (binding != NULL && binding->dcpdp_device_proxy != MACH_PORT_NULL) IOObjectRelease(binding->dcpdp_device_proxy);
    if (binding != NULL) *binding = (RSSMacOSBinding){0};
}

RSSDDCError rss_macos_probe_dpcd_path(uint32_t list_index, const RSSDDCDiagnostics *diagnostics) {
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding.correlation_failure));
        rss_macos_release_binding(&binding);
        return error;
    }
    if (binding.display.provider != RSS_DDC_PROVIDER_DCPDP13 &&
        binding.display.provider != RSS_DDC_PROVIDER_DCPDP_SERVICE) {
        rss_macos_diagnostic(diagnostics, "operation=ProbeDPCDPath status=unsupported-provider");
        rss_macos_release_binding(&binding);
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    const char *backend = rss_ddc_backend_name(rss_ddc_provider_backend(binding.display.provider));
    char message[256] = {};
    char role[RSS_DDC_TEXT_MAX] = {};
    if (!service_epic_role(binding.service_proxy, role)) {
        rss_macos_diagnostic(diagnostics, "operation=ProbeDPCDPath status=missing-service-role");
        rss_macos_release_binding(&binding);
        return RSS_DDC_ERROR_SAFETY_GATE;
    }
    unsigned int candidates = dp_device_candidates_for_role(role, NULL);
    RSSDDCDPCDPathStatus status = rss_ddc_dpcd_path_status_for_candidate_count(candidates);
    snprintf(message, sizeof(message),
             "backend=%s operation=ProbeDPCDPath service-role=%s scoped-DCPDPDeviceProxy-candidates=%u IODP-construction=not-attempted",
             backend, role, candidates);
    rss_macos_diagnostic(diagnostics, message);
    rss_macos_release_binding(&binding);
    if (status == RSS_DDC_DPCD_PATH_CANDIDATE) return RSS_DDC_OK;
    return RSS_DDC_ERROR_SAFETY_GATE;
}

typedef struct {
    io_service_t candidate;
    const RSSDDCDiagnostics *diagnostics;
} DPCDReadContext;

static RSSDDCError dpcd_read_construct(void *opaque, void **device_out) {
    DPCDReadContext *context = opaque;
    IODPDeviceRef device = IODPDeviceCreateWithService(kCFAllocatorDefault, context->candidate);
    if (device == NULL || CFGetTypeID(device) != IODPDeviceGetTypeID()) {
        if (device != NULL) CFRelease(device);
        rss_macos_diagnostic(context->diagnostics, "IODPDeviceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    rss_macos_diagnostic(context->diagnostics, "IODPDeviceCreateWithService=success");
    *device_out = (void *)(uintptr_t)device;
    return RSS_DDC_OK;
}

static RSSDDCError dpcd_read_once(void *opaque, void *opaque_device, uint32_t address,
                                  uint8_t *bytes, size_t length) {
    DPCDReadContext *context = opaque;
    IOReturn result = IODPDeviceReadDPCD((IODPDeviceRef)(uintptr_t)opaque_device, address, bytes, (uint32_t)length);
    char message[160] = {};
    snprintf(message, sizeof(message), "read address=0x%05x length=%zu IOReturn=0x%08x", address, length,
             (unsigned int)result);
    rss_macos_diagnostic(context->diagnostics, message);
    return result == KERN_SUCCESS ? RSS_DDC_OK : RSS_DDC_ERROR_DPCD_READ;
}

static void dpcd_read_release(void *opaque, void *opaque_device) {
    (void)opaque;
    CFRelease((IODPDeviceRef)(uintptr_t)opaque_device);
}

/**
 * Uses the exact same scoped candidate search as the registry-only probe.
 * This deliberately rejects global first matches: mixed topologies may contain
 * several native-DP proxies, only one of which belongs to this DCPDP13 role.
 */
RSSDDCError rss_macos_dp_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *bytes,
                                   size_t length, const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || bytes == NULL || !binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    char message[256] = {};
    char role[RSS_DDC_TEXT_MAX] = {};
    if (!service_epic_role(binding->service_proxy, role)) return RSS_DDC_ERROR_SAFETY_GATE;
    io_service_t candidate = MACH_PORT_NULL;
    unsigned int candidates = dp_device_candidates_for_role(role, &candidate);
    const char *backend = rss_ddc_backend_name(rss_ddc_provider_backend(binding->display.provider));
    snprintf(message, sizeof(message),
             "backend=%s operation=ReadDPCD path=DCPDPDeviceProxy->IODPDevice service-role=%s scoped-DCPDPDeviceProxy-candidates=%u",
             backend, role, candidates);
    rss_macos_diagnostic(diagnostics, message);
    DPCDReadContext context = {.candidate = candidate, .diagnostics = diagnostics};
    const RSSDDCDPCDReadCallbacks callbacks = {
        .context = &context,
        .construct = dpcd_read_construct,
        .read = dpcd_read_once,
        .release = dpcd_read_release,
    };
    RSSDDCError error = rss_ddc_run_dpcd_candidate_read(candidates, &callbacks, address, bytes, length);
    if (candidate != MACH_PORT_NULL) IOObjectRelease(candidate);
    if (error == RSS_DDC_OK) {
        snprintf(message, sizeof(message), "dpcd bytes=%zu", length);
        rss_macos_diagnostic(diagnostics, message);
    }
    return error;
}
