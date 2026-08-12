@import Foundation;

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "macos_internal.h"
#include "edid.h"
#include "protocol.h"

/*
 * Private IOAV ABI declarations reconstructed from Apple interfaces and our
 * research. They remain backend-private so public headers stay portable.
 * CreateWithService returns a retained CF object released with CFRelease.
 */
typedef CFTypeRef IOAVServiceRef;
typedef CFTypeRef IOAVDeviceRef;
typedef CFTypeRef IODPDeviceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceReadI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);
extern IOAVDeviceRef IOAVDeviceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVDeviceGetTypeID(void);
extern IOReturn IOAVDeviceReadI2C(IOAVDeviceRef, uint32_t, uint32_t, void *, uint32_t);
/* Private IODP ABI recovered from Apple's arm64e implementation and prior guarded PS190 reads. */
extern IODPDeviceRef IODPDeviceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IODPDeviceGetTypeID(void);
extern IOReturn IODPDeviceReadDPCD(IODPDeviceRef, uint32_t, void *, uint32_t);

enum {
    RSS_PS190_SET_WRITE_COUNT = 2,
    RSS_PS190_SET_PREWRITE_DELAY_US = 10000,
};

/** Formats a bounded byte trace for operator diagnostics without exposing pointers. */
static void diagnostic_bytes(const RSSDDCDiagnostics *diagnostics, const char *label,
                             const uint8_t *bytes, size_t byte_count) {
    char message[256] = {};
    int written = snprintf(message, sizeof(message), "%s", label);
    for (size_t index = 0; index < byte_count && written > 0 && (size_t)written < sizeof(message); ++index) {
        written += snprintf(message + written, sizeof(message) - (size_t)written, "%s%02x",
                            index == 0 ? "=" : " ", bytes[index]);
    }
    rss_macos_diagnostic(diagnostics, message);
}

static bool device_is_external(io_service_t service) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("Location"), kCFAllocatorDefault, 0);
    bool result = value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringCompare(value, CFSTR("External"), 0) == kCFCompareEqualTo;
    if (value != NULL) CFRelease(value);
    return result;
}

/* Finds the one branch-anchored DCPDP device proxy established by PS190 research. */
static io_service_t ps190_dcpdp_device_for_branch(const char *branch) {
    if (branch == NULL || branch[0] == '\0') return MACH_PORT_NULL;
    CFStringRef expected = CFStringCreateWithCString(kCFAllocatorDefault, branch, kCFStringEncodingUTF8);
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (expected == NULL || root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
        kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        if (expected != NULL) CFRelease(expected);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    io_service_t match = MACH_PORT_NULL;
    bool ambiguous = false;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t class_name = {};
        IOObjectGetClass(entry, class_name);
        CFTypeRef candidate = strcmp(class_name, "DCPDPDeviceProxy") == 0 ?
            IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0) : NULL;
        bool matches = candidate != NULL && CFEqual(candidate, expected) && device_is_external(entry);
        if (candidate != NULL) CFRelease(candidate);
        if (!matches || match != MACH_PORT_NULL || ambiguous) {
            if (matches && match != MACH_PORT_NULL) { IOObjectRelease(match); ambiguous = true; }
            IOObjectRelease(entry);
            if (ambiguous) match = MACH_PORT_NULL; /* Scoped ambiguity fails closed. */
        } else {
            match = entry;
        }
    }
    IOObjectRelease(iterator);
    CFRelease(expected);
    return match;
}

/* Matches the sibling AV endpoint structurally instead of by a global proxy count. */
static io_service_t ps190_paired_av_device(io_service_t dcpdp_device) {
    io_registry_entry_t dp_epic = MACH_PORT_NULL;
    io_registry_entry_t interface = MACH_PORT_NULL;
    io_service_t match = MACH_PORT_NULL;
    bool ambiguous = false;
    if (dcpdp_device == MACH_PORT_NULL || IORegistryEntryGetParentEntry(dcpdp_device, kIOServicePlane, &dp_epic) != KERN_SUCCESS ||
        IORegistryEntryGetParentEntry(dp_epic, kIOServicePlane, &interface) != KERN_SUCCESS) {
        if (dp_epic != MACH_PORT_NULL) IOObjectRelease(dp_epic);
        return MACH_PORT_NULL;
    }
    CFTypeRef location = IORegistryEntryCreateCFProperty(dp_epic, CFSTR("EPICLocation"), kCFAllocatorDefault, 0);
    CFTypeRef unit = IORegistryEntryCreateCFProperty(dp_epic, CFSTR("EPICUnit"), kCFAllocatorDefault, 0);
    CFTypeRef role = IORegistryEntryCreateCFProperty(dp_epic, CFSTR("role"), kCFAllocatorDefault, 0);
    IOObjectRelease(dp_epic);
    if (location == NULL || unit == NULL || role == NULL) goto done;
    io_iterator_t epics = MACH_PORT_NULL;
    if (IORegistryEntryGetChildIterator(interface, kIOServicePlane, &epics) != KERN_SUCCESS) goto done;
    io_registry_entry_t epic = MACH_PORT_NULL;
    while ((epic = IOIteratorNext(epics)) != MACH_PORT_NULL) {
        CFTypeRef name = IORegistryEntryCreateCFProperty(epic, CFSTR("EPICName"), kCFAllocatorDefault, 0);
        CFTypeRef provider = IORegistryEntryCreateCFProperty(epic, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
        CFTypeRef candidate_location = IORegistryEntryCreateCFProperty(epic, CFSTR("EPICLocation"), kCFAllocatorDefault, 0);
        CFTypeRef candidate_unit = IORegistryEntryCreateCFProperty(epic, CFSTR("EPICUnit"), kCFAllocatorDefault, 0);
        CFTypeRef candidate_role = IORegistryEntryCreateCFProperty(epic, CFSTR("role"), kCFAllocatorDefault, 0);
        bool paired = name != NULL && provider != NULL && candidate_location != NULL && candidate_unit != NULL && candidate_role != NULL &&
            CFStringCompare(name, CFSTR("dcpav-device-epic"), 0) == kCFCompareEqualTo &&
            CFStringCompare(provider, CFSTR("DCPDPDevice"), 0) == kCFCompareEqualTo &&
            CFEqual(candidate_location, location) && CFEqual(candidate_unit, unit) && CFEqual(candidate_role, role);
        if (candidate_role != NULL) CFRelease(candidate_role);
        if (candidate_unit != NULL) CFRelease(candidate_unit);
        if (candidate_location != NULL) CFRelease(candidate_location);
        if (provider != NULL) CFRelease(provider);
        if (name != NULL) CFRelease(name);
        if (paired) {
            io_iterator_t children = MACH_PORT_NULL;
            if (IORegistryEntryGetChildIterator(epic, kIOServicePlane, &children) == KERN_SUCCESS) {
                io_service_t child = MACH_PORT_NULL;
                while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
                    io_name_t class_name = {};
                    IOObjectGetClass(child, class_name);
                    CFTypeRef supported = IORegistryEntryCreateCFProperty(child, CFSTR("IOAVDeviceUserInterfaceSupported"), kCFAllocatorDefault, 0);
                    bool candidate = strcmp(class_name, "DCPAVDeviceProxy") == 0 && device_is_external(child) &&
                        supported != NULL && CFGetTypeID(supported) == CFBooleanGetTypeID() && CFBooleanGetValue(supported);
                    if (supported != NULL) CFRelease(supported);
                    if (candidate && match == MACH_PORT_NULL && !ambiguous) match = child;
                    else {
                        if (candidate && match != MACH_PORT_NULL) { IOObjectRelease(match); match = MACH_PORT_NULL; ambiguous = true; }
                        IOObjectRelease(child);
                    }
                }
                IOObjectRelease(children);
            }
        }
        IOObjectRelease(epic);
    }
    IOObjectRelease(epics);
done:
    if (role != NULL) CFRelease(role);
    if (unit != NULL) CFRelease(unit);
    if (location != NULL) CFRelease(location);
    if (interface != MACH_PORT_NULL) IOObjectRelease(interface);
    return match;
}

/**
 * The base tuple is hardware-validated in rss-ddc. E-EDID assigns block 1 to
 * segment 0/offset 0x80, so this backend makes one additional read only when
 * the base declares it. The documented PS190/Odyssey block-1 IOAV mapping is
 * hardware-validated; blocks >= 2 require a 0x30 segment-pointer write whose
 * PS190 IOAV semantics are unproven, so they remain explicitly incomplete.
 */
RSSDDCError rss_macos_ps190_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                      const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || edid == NULL || !binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    io_service_t dcpdp_device = ps190_dcpdp_device_for_branch(binding->display.branch_device_id);
    io_service_t av_device = ps190_paired_av_device(dcpdp_device);
    if (dcpdp_device != MACH_PORT_NULL) IOObjectRelease(dcpdp_device);
    if (av_device == MACH_PORT_NULL) return RSS_DDC_ERROR_SAFETY_GATE;
    rss_macos_diagnostic(diagnostics, "backend=AppleDCPPS190 operation=ReadEDID path=IOAVDevice base+block1-only");
    IOAVDeviceRef device = IOAVDeviceCreateWithService(kCFAllocatorDefault, av_device);
    IOObjectRelease(av_device);
    if (device == NULL || CFGetTypeID(device) != IOAVDeviceGetTypeID()) {
        if (device != NULL) CFRelease(device);
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    memset(edid->bytes, 0xcc, sizeof(edid->bytes));
    IOReturn result = IOAVDeviceReadI2C(device, 0x50, 0x00, edid->bytes, RSS_DDC_EDID_BLOCK_SIZE);
    char message[160] = {};
    snprintf(message, sizeof(message), "read chip=0x50 data=0x00000000 length=128 IOReturn=0x%08x", (unsigned int)result);
    rss_macos_diagnostic(diagnostics, message);
    if (result != KERN_SUCCESS) { CFRelease(device); return RSS_DDC_ERROR_READ; }
    edid->length = RSS_DDC_EDID_BLOCK_SIZE;
    RSSDDCEDIDInfo base_info = {};
    RSSDDCError base_parse = rss_ddc_parse_edid(edid, &base_info);
    if (base_parse != RSS_DDC_OK) {
        CFRelease(device);
        return base_parse;
    }
    uint8_t declared_extensions = base_info.declared_extension_count;
    if (declared_extensions != 0) {
        RSSDDCEDIDBlockAddress block1 = {};
        if (!rss_ddc_edid_block_address(1, &block1) || block1.requires_segment_pointer) {
            CFRelease(device);
            return RSS_DDC_ERROR_SYSTEM;
        }
        snprintf(message, sizeof(message),
                 "extension=1 segment=0x%02x data=0x%08x length=128 acquisition=hardware-validated-PS190-block1",
                 block1.segment, block1.offset);
        rss_macos_diagnostic(diagnostics, message);
        result = IOAVDeviceReadI2C(device, 0x50, block1.offset,
                                   edid->bytes + RSS_DDC_EDID_BLOCK_SIZE, RSS_DDC_EDID_BLOCK_SIZE);
        snprintf(message, sizeof(message), "read chip=0x50 data=0x%08x length=128 IOReturn=0x%08x",
                 block1.offset, (unsigned int)result);
        rss_macos_diagnostic(diagnostics, message);
        if (result == KERN_SUCCESS) edid->length += RSS_DDC_EDID_BLOCK_SIZE;
        else rss_macos_diagnostic(diagnostics, "extension=1 status=unread; returning validated base block as incomplete");
        if (declared_extensions > 1) {
            rss_macos_diagnostic(diagnostics,
                                 "extension>=2 status=unread; PS190 segment-pointer writes are intentionally unsupported");
        }
    }
    CFRelease(device);
    return RSS_DDC_OK;
}

/**
 * Executes the only DPCD operation established by prior PS190 research:
 * selected branch -> unique DCPDPDeviceProxy -> IODPDevice -> one native
 * DPCD read. The API supports neither chunking nor writes. Runtime use of
 * this reproduction remains pending a separate rss-ddc hardware validation.
 */
RSSDDCError rss_macos_ps190_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                      size_t length, const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || buffer == NULL || !binding->ps190_safety_gate ||
        binding->dcpdp_device_proxy == MACH_PORT_NULL) return RSS_DDC_ERROR_SAFETY_GATE;
    rss_macos_diagnostic(diagnostics, "backend=AppleDCPPS190 operation=ReadDPCD path=DCPDPDeviceProxy->IODPDevice");
    IODPDeviceRef device = IODPDeviceCreateWithService(kCFAllocatorDefault, binding->dcpdp_device_proxy);
    if (device == NULL || CFGetTypeID(device) != IODPDeviceGetTypeID()) {
        if (device != NULL) CFRelease(device);
        rss_macos_diagnostic(diagnostics, "IODPDeviceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    char message[192] = {};
    snprintf(message, sizeof(message), "read address=0x%05x length=%zu IOReturn=", address, length);
    IOReturn result = IODPDeviceReadDPCD(device, address, buffer, (uint32_t)length);
    CFRelease(device);
    size_t used = strlen(message);
    snprintf(message + used, sizeof(message) - used, "0x%08x", (unsigned int)result);
    rss_macos_diagnostic(diagnostics, message);
    if (result != KERN_SUCCESS) return RSS_DDC_ERROR_DPCD_READ;
    snprintf(message, sizeof(message), "dpcd bytes=%zu", length);
    rss_macos_diagnostic(diagnostics, message);
    return RSS_DDC_OK;
}

/**
 * Executes the only currently enabled PS190 capability: raw-framed Get VCP.
 * Hardware validation confirmed this exact rss-ddc path on macOS 25F84 with
 * an Odyssey G75F and AppleDCPPS190; other providers and configurations are
 * not implied by that result.
 *
 * Passing 0x51 as IOAV's data/subaddress caused DCP offset preparation and
 * invalid replies. The validated form sends 0x51 inline and uses UINT32_MAX
 * as the no-offset sentinel for both the 5-byte write and 11-byte read.
 */
RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                     const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || result == NULL || !binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    rss_macos_diagnostic(diagnostics, "backend=AppleDCPPS190 operation=GetVCP");
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE];
    uint8_t reply[RSS_DDC_GET_VCP_REPLY_SIZE];
    memset(reply, 0xcc, sizeof(reply));
    rss_ddc_build_raw_get_vcp(vcp_code, request);
    const uint32_t no_offset = UINT32_MAX; /* DCP firmware's no-subaddress sentinel. */
    diagnostic_bytes(diagnostics, "request", request, sizeof(request));
    IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, no_offset, request, sizeof(request));
    char message[256] = {};
    snprintf(message, sizeof(message), "write chip=0x37 data=0xffffffff length=5 IOReturn=0x%08x",
             (unsigned int)write_result);
    rss_macos_diagnostic(diagnostics, message);
    if (write_result == KERN_SUCCESS) {
        rss_macos_diagnostic(diagnostics, "delay=50ms"); /* Validated PS190 reply construction delay. */
        usleep(50000);
    }
    IOReturn read_result = KERN_SUCCESS;
    if (write_result == KERN_SUCCESS) {
        read_result = IOAVServiceReadI2C(service, 0x37, no_offset, reply, sizeof(reply));
    }
    CFRelease(service);
    if (write_result != KERN_SUCCESS) {
        rss_macos_diagnostic(diagnostics, "read=skipped because write failed");
        return RSS_DDC_ERROR_WRITE;
    }
    snprintf(message, sizeof(message), "read chip=0x37 data=0xffffffff length=11 IOReturn=0x%08x",
             (unsigned int)read_result);
    rss_macos_diagnostic(diagnostics, message);
    if (read_result != KERN_SUCCESS) return RSS_DDC_ERROR_READ;
    diagnostic_bytes(diagnostics, "reply", reply, sizeof(reply));
    RSSDDCError parse_result = rss_ddc_parse_get_vcp_reply(reply, sizeof(reply), vcp_code, result);
    if (parse_result != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_ddc_error_string(parse_result));
        return parse_result;
    }
    snprintf(message, sizeof(message), "decoded vcp=0x%02x maximum=%u current=%u checksum=valid",
             result->vcp_code, result->maximum_value, result->current_value);
    rss_macos_diagnostic(diagnostics, message);
    return RSS_DDC_OK;
}

/**
 * Executes the hardware-validated PS190 Set VCP path recovered from m1ddc:
 * two conventional Service writes, each preceded by 10 ms, with no response
 * read. This is intentionally distinct from raw PS190 GET. The legacy path
 * supplied 0x51 as the IOAV data/subaddress argument and formed the matching
 * six-byte payload/checksum; rss-ddc validation confirmed that VCP 0x60 can
 * visibly switch the documented Odyssey G75F input.
 *
 * The repeated write is preserved as evidence-backed transaction behavior,
 * not presented as a retry policy. A failed call returns WRITE immediately;
 * no response is implied or parsed after a successful Set VCP write.
 */
RSSDDCError rss_macos_ps190_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                     const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || !binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    char message[256] = {};
    snprintf(message, sizeof(message), "backend=AppleDCPPS190 operation=SetVCP framing=conventional requested-vcp=0x%02x requested-value=%u",
             vcp_code, value);
    rss_macos_diagnostic(diagnostics, message);

    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }

    uint8_t request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    rss_ddc_build_conventional_set_vcp(vcp_code, value, request);
    diagnostic_bytes(diagnostics, "request", request, sizeof(request));
    for (unsigned int write_index = 0; write_index < RSS_PS190_SET_WRITE_COUNT; ++write_index) {
        usleep(RSS_PS190_SET_PREWRITE_DELAY_US);
        IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, 0x51, request, sizeof(request));
        snprintf(message, sizeof(message),
                 "write=%u/%u chip=0x37 data=0x00000051 length=6 pre-delay=10ms IOReturn=0x%08x",
                 write_index + 1, RSS_PS190_SET_WRITE_COUNT, (unsigned int)write_result);
        rss_macos_diagnostic(diagnostics, message);
        if (write_result != KERN_SUCCESS) {
            CFRelease(service);
            return RSS_DDC_ERROR_WRITE;
        }
    }
    CFRelease(service);
    rss_macos_diagnostic(diagnostics, "response=none historical SetVCP path is write-only");
    return RSS_DDC_OK;
}
