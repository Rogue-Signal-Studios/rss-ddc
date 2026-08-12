@import CoreGraphics;
@import Foundation;
@import IOKit;

#include <stdio.h>
#include <string.h>

#include "macos_internal.h"

/* Private CoreDisplay entry point reconstructed from Apple runtime research. Returned dictionary follows Create ownership. */
extern CFDictionaryRef CoreDisplay_DisplayCreateInfoDictionary(CGDirectDisplayID display);

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
static bool is_ps190_service_identity(io_service_t service) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (!is_external(service) || IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) != KERN_SUCCESS) {
        return false;
    }
    CFTypeRef epic_name = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICName"), kCFAllocatorDefault, 0);
    CFTypeRef role = IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0);
    CFTypeRef provider = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef unit = IORegistryEntryCreateCFProperty(service, CFSTR("Unit"), kCFAllocatorDefault, 0);
    CFTypeRef ui_supported = IORegistryEntryCreateCFProperty(service, CFSTR("IOAVServiceUserInterfaceSupported"),
                                                              kCFAllocatorDefault, 0);
    char epic_name_text[RSS_DDC_TEXT_MAX] = {};
    char role_text[RSS_DDC_TEXT_MAX] = {};
    char provider_text[RSS_DDC_TEXT_MAX] = {};
    int64_t unit_value = -1;
    bool matches = copyCFString(epic_name, epic_name_text) &&
        copyCFString(role, role_text) && copyCFString(provider, provider_text) &&
        unit != NULL && CFGetTypeID(unit) == CFNumberGetTypeID() &&
        CFNumberGetValue(unit, kCFNumberSInt64Type, &unit_value) &&
        ui_supported != NULL && CFGetTypeID(ui_supported) == CFBooleanGetTypeID() && CFBooleanGetValue(ui_supported) &&
        strcmp(epic_name_text, "dcpav-service-epic") == 0 && strcmp(role_text, "DCPEXT0") == 0 &&
        strcmp(provider_text, "AppleDCPPS190") == 0 && unit_value == 0;
    if (ui_supported != NULL) CFRelease(ui_supported);
    if (unit != NULL) CFRelease(unit);
    if (provider != NULL) CFRelease(provider);
    if (role != NULL) CFRelease(role);
    if (epic_name != NULL) CFRelease(epic_name);
    IOObjectRelease(parent);
    return matches;
}

/**
 * DCPDP13 is selected solely from the immediate Service EPIC provider, never
 * from a generic DisplayPort transport-state node. PS190 HDMI also exposes a
 * DisplayPort-shaped active transport, so that state is correlation evidence
 * only and cannot select the conventional backend.
 */
static bool is_dp_service_identity(io_service_t service) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (!is_external(service) || IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) != KERN_SUCCESS) {
        return false;
    }
    CFTypeRef provider = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef ui_supported = IORegistryEntryCreateCFProperty(service, CFSTR("IOAVServiceUserInterfaceSupported"),
                                                              kCFAllocatorDefault, 0);
    char provider_text[RSS_DDC_TEXT_MAX] = {};
    bool matches = copyCFString(provider, provider_text) &&
        strcmp(provider_text, "DCPDP13Service") == 0 && ui_supported != NULL &&
        CFGetTypeID(ui_supported) == CFBooleanGetTypeID() && CFBooleanGetValue(ui_supported);
    if (ui_supported != NULL) CFRelease(ui_supported);
    if (provider != NULL) CFRelease(provider);
    IOObjectRelease(parent);
    return matches;
}

/**
 * Correlates a display adapter to one external DCPAVServiceProxy. Returns a
 * retained proxy; multiple matches are rejected rather than guessed.
 */
static io_service_t service_for_display(CGDirectDisplayID display_id, bool *ambiguous) {
    if (ambiguous != NULL) *ambiguous = false;
    io_service_t adapter = adapter_for_display(display_id);
    if (adapter == MACH_PORT_NULL) return MACH_PORT_NULL;
    uint64_t adapter_id = 0;
    bool adapter_ok = IORegistryEntryGetRegistryEntryID(adapter, &adapter_id) == KERN_SUCCESS;
    IOObjectRelease(adapter);
    if (!adapter_ok) return MACH_PORT_NULL;
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
                if (ambiguous != NULL) *ambiguous = true;
                return MACH_PORT_NULL;
            }
            match = entry;
            continue;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    return match;
}

/**
 * Finds the single active DisplayPort transport for the selected product and
 * copies its BranchDeviceID. Ambiguity is a safety failure, not a tie-break.
 */
static bool active_branch_for_product(const char *product_name, char branch[RSS_DDC_TEXT_MAX]) {
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
        bool matches = active_true && copyCFString(product, product_text) && strcmp(product_text, product_name) == 0 &&
            copyCFString(branch_value, branch);
        if (branch_value != NULL) CFRelease(branch_value);
        if (product != NULL) CFRelease(product);
        if (active != NULL) CFRelease(active);
        IOObjectRelease(entry);
        if (matches && !found) found = true;
        else if (matches) {
            IOObjectRelease(iterator);
            return false;
        }
    }
    IOObjectRelease(iterator);
    return found;
}

/** Confirms the active branch maps to exactly one external DCPDP device proxy. */
static bool branch_has_unique_device_proxy(const char *branch) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return false;
    }
    IOObjectRelease(root);
    unsigned int count = 0;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        if (strcmp(name, "DCPDPDeviceProxy") == 0 && is_external(entry)) {
            CFTypeRef value = IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
            char candidate[RSS_DDC_TEXT_MAX] = {};
            if (copyCFString(value, candidate) && strcmp(candidate, branch) == 0) ++count;
            if (value != NULL) CFRelease(value);
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    return count == 1;
}

/**
 * Produces public display snapshots without opening IOAVService. Registry and
 * CoreFoundation objects created here are released before the function returns.
 */
RSSDDCError rss_macos_discover_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count) {
    if (count == NULL) return RSS_DDC_ERROR_ARGUMENT;
    CGDirectDisplayID ids[16] = {};
    CGDisplayCount total = 0;
    if (CGGetOnlineDisplayList(16, ids, &total) != kCGErrorSuccess) return RSS_DDC_ERROR_DISCOVERY;
    size_t written = 0;
    for (CGDisplayCount index = 0; index < total; ++index) {
        if (written == capacity) break;
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
        displays[written++] = display;
    }
    *count = written;
    return RSS_DDC_OK;
}

/**
 * Builds the private binding used by hardware operations. Both enabled
 * providers require active-transport/branch/device/service correlation before
 * a backend can construct IOAVService. The exact EPIC provider is decisive;
 * the shared transport state is never used to choose a backend.
 */
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding) {
    if (binding == NULL || list_index == 0) return RSS_DDC_ERROR_ARGUMENT;
    *binding = (RSSMacOSBinding){0};
    CGDirectDisplayID ids[16] = {};
    CGDisplayCount total = 0;
    if (CGGetOnlineDisplayList(16, ids, &total) != kCGErrorSuccess || list_index > total) return RSS_DDC_ERROR_NOT_FOUND;
    CGDirectDisplayID display_id = ids[list_index - 1];
    binding->display.list_index = list_index;
    binding->display.cg_display_id = display_id;
    binding->display.online = true;
    binding->display.external = !CGDisplayIsBuiltin(display_id);
    copy_display_name(display_id, &binding->display);
    bool ambiguous_service = false;
    binding->service_proxy = service_for_display(display_id, &ambiguous_service);
    if (binding->service_proxy == MACH_PORT_NULL) {
        return ambiguous_service ? RSS_DDC_ERROR_SAFETY_GATE : RSS_DDC_ERROR_DISCOVERY;
    }
    if (!inspect_service(binding->service_proxy, &binding->display)) return RSS_DDC_ERROR_DISCOVERY;
    bool branch_ok = active_branch_for_product(binding->display.product_name, binding->display.branch_device_id) &&
        branch_has_unique_device_proxy(binding->display.branch_device_id);
    if (binding->display.provider == RSS_DDC_PROVIDER_DCPDP13) {
        binding->dp_safety_gate = binding->display.external && branch_ok &&
            is_dp_service_identity(binding->service_proxy);
        if (!binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    } else if (binding->display.provider == RSS_DDC_PROVIDER_PS190) {
        binding->ps190_safety_gate = binding->display.external && branch_ok &&
            is_ps190_service_identity(binding->service_proxy);
        if (!binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    }
    return RSS_DDC_OK;
}

/** Balances service_for_display's retained proxy on every success and error path. */
void rss_macos_release_binding(RSSMacOSBinding *binding) {
    if (binding != NULL && binding->service_proxy != MACH_PORT_NULL) IOObjectRelease(binding->service_proxy);
    if (binding != NULL) *binding = (RSSMacOSBinding){0};
}
