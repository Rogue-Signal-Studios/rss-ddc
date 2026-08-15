/*
 * ioav-device-lab is an isolated experiment for the private, device-level
 * IOAV API.  Its optional EDID mode makes exactly one 0x50/0x00 read after
 * validating the device object.  Its separately gated ddc-vcp-raw mode performs
 * one fixed luminance Get VCP write/read pair, with no retry or address sweep.
 *
 * Migrated from m1ddc-rss tools/ioav-device-lab. Research-only.
 */
@import Darwin;
@import Foundation;
@import IOKit;
@import CoreGraphics;

#include <errno.h>
#include <mach/error.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ddc_parser.h"

/*
 * These declarations are reconstructed from the local arm64e IOKit binary.
 * Create returns a retained Core Foundation object.  ReadI2C uses a fixed
 * uint32_t address/address/byte-count ABI and returns only IOReturn; it does
 * not expose an actual-byte-count result to its caller.
 */
typedef CFTypeRef IOAVDeviceRef;
extern CFDictionaryRef CoreDisplay_DisplayCreateInfoDictionary(CGDirectDisplayID);
extern IOAVDeviceRef IOAVDeviceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern CFTypeID IOAVDeviceGetTypeID(void);
extern IOReturn IOAVDeviceReadI2C(IOAVDeviceRef device, uint32_t chipAddress,
                                  uint32_t dataAddress, void *buffer, uint32_t byteCount);
extern IOReturn IOAVDeviceWriteI2C(IOAVDeviceRef device, uint32_t chipAddress,
                                   uint32_t dataAddress, void *buffer, uint32_t byteCount);

typedef enum {
    LAB_MODE_CONSTRUCT,
    LAB_MODE_PROXY_DIAGNOSTIC,
    LAB_MODE_SAFETY_DIAGNOSTIC,
    LAB_MODE_OPEN_DIAGNOSTIC,
    LAB_MODE_EDID,
    LAB_MODE_DDC_VCP_RAW,
} LabMode;

typedef enum {
    CONSTRUCTION_ORIGIN_TRAVERSAL,
    CONSTRUCTION_ORIGIN_DIRECT_MATCHING,
} ConstructionOrigin;

typedef enum {
    EXPERIMENT_VALID_DDC_REPLY,
    EXPERIMENT_IO_ERROR,
    EXPERIMENT_INVALID_DDC_REPLY,
    EXPERIMENT_MEMORY_ANOMALY,
} ExperimentResult;

typedef struct {
    unsigned int displayIndex;
    LabMode mode;
    ConstructionOrigin constructionOrigin;
} LabOptions;

typedef struct {
    char productName[128];
    char manufacturer[32];
    uint32_t serialNumber;
    bool hasSerialNumber;
} DisplayFingerprint;

typedef struct {
    CGDirectDisplayID displayID;
    DisplayFingerprint fingerprint;
    io_registry_entry_t transport;
    io_service_t dcpdpDeviceProxy;
    io_service_t dcpavDeviceProxy;
    io_service_t dcpavServiceProxy;
} SelectedDevices;

typedef struct {
    void *mapping;
    size_t pageSize;
    size_t byteCount;
    uint8_t *buffer;
} GuardedBuffer;

/** Registry-only structural facts used to pair a DCPDP proxy with a DCPAV proxy. */
typedef struct {
    bool immediateEpicFound;
    bool registered;
    uint64_t proxyID;
    uint64_t epicID;
    uint64_t endpointID;
    uint64_t afkepInterfaceID;
    int64_t epicUnit;
    bool hasEpicUnit;
    char proxyPath[1024];
    char epicName[128];
    char epicProtocolName[128];
    char epicPath[1024];
    char epicProvider[128];
    char epicLocation[128];
    char endpointName[128];
    char endpointPath[1024];
    char afkepInterfaceName[128];
    char afkepInterfacePath[1024];
    char interfaceID[128];
    char role[128];
} ProxyTopology;

/** Prints the small, deliberately constrained command surface. */
static void printUsage(const char *program) {
    fprintf(stderr,
            "Usage: %s [--display N] [--mode construct|proxy-diagnostic|safety-diagnostic|open-diagnostic|edid|ddc-vcp-raw] [--origin traversal|direct]\n"
            "\nDefaults: display 1; mode construct.\n"
            "construct: resolve and type-check DCPAVDeviceProxy only; no I2C call.\n"
            "proxy-diagnostic: registry-only DCPDP/DCPAV pairing report; no open or IOAV call.\n"
            "safety-diagnostic: registry/CoreGraphics target-gate report only; no open or IOAV call.\n"
            "open-diagnostic: one IOServiceOpen/Close only; no external method or I2C call.\n"
            "edid: after the same validation, make one 0x50/0x00, 128-byte read.\n"
            "ddc-vcp-raw: one 0x37/0x51 Get VCP 0x10 write and one 0x37/0xffffffff raw read; no retry.\n"
            "origin: registry traversal (default) or a fresh IOService class match.\n",
            program);
}

/** Parses the lab's explicit display and mode options. */
static bool parseOptions(int argc, char **argv, LabOptions *options) {
    *options = (LabOptions){.displayIndex = 1, .mode = LAB_MODE_CONSTRUCT,
                             .constructionOrigin = CONSTRUCTION_ORIGIN_TRAVERSAL};
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) return false;
        if (index + 1 >= argc) return false;
        const char *value = argv[++index];
        if (strcmp(argv[index - 1], "--display") == 0) {
            char *end = NULL;
            errno = 0;
            unsigned long display = strtoul(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0' || display == 0 || display > 16) return false;
            options->displayIndex = (unsigned int)display;
        } else if (strcmp(argv[index - 1], "--mode") == 0) {
            if (strcmp(value, "construct") == 0) options->mode = LAB_MODE_CONSTRUCT;
            else if (strcmp(value, "proxy-diagnostic") == 0) options->mode = LAB_MODE_PROXY_DIAGNOSTIC;
            else if (strcmp(value, "safety-diagnostic") == 0) options->mode = LAB_MODE_SAFETY_DIAGNOSTIC;
            else if (strcmp(value, "open-diagnostic") == 0) options->mode = LAB_MODE_OPEN_DIAGNOSTIC;
            else if (strcmp(value, "edid") == 0) options->mode = LAB_MODE_EDID;
            else if (strcmp(value, "ddc-vcp-raw") == 0) options->mode = LAB_MODE_DDC_VCP_RAW;
            else return false;
        } else if (strcmp(argv[index - 1], "--origin") == 0) {
            if (strcmp(value, "traversal") == 0) options->constructionOrigin = CONSTRUCTION_ORIGIN_TRAVERSAL;
            else if (strcmp(value, "direct") == 0) options->constructionOrigin = CONSTRUCTION_ORIGIN_DIRECT_MATCHING;
            else return false;
        } else {
            return false;
        }
    }
    return true;
}

/** Maps a one-based online display index to its CoreGraphics display ID. */
static bool displayIDForIndex(unsigned int index, CGDirectDisplayID *displayID) {
    CGDirectDisplayID displays[16] = {};
    CGDisplayCount count = 0;
    if (CGGetOnlineDisplayList(16, displays, &count) != kCGErrorSuccess || index == 0 || index > count) return false;
    *displayID = displays[index - 1];
    return true;
}

/** Gets CoreDisplay's backing IOKit adapter for a CoreGraphics display. */
static io_service_t adapterForDisplay(CGDirectDisplayID displayID) {
    CFDictionaryRef information = CoreDisplay_DisplayCreateInfoDictionary(displayID);
    if (information == NULL) return MACH_PORT_NULL;
    CFStringRef location = CFDictionaryGetValue(information, CFSTR("IODisplayLocation"));
    io_service_t adapter = location == NULL ? MACH_PORT_NULL :
        IORegistryEntryCopyFromPath(kIOMainPortDefault, location);
    CFRelease(information);
    return adapter;
}

/** Extracts display identity used to reject devices belonging to another monitor. */
static void fingerprintForDisplay(CGDirectDisplayID displayID, DisplayFingerprint *fingerprint) {
    *fingerprint = (DisplayFingerprint){};
    snprintf(fingerprint->productName, sizeof(fingerprint->productName), "Unknown Display");
    io_service_t adapter = adapterForDisplay(displayID);
    if (adapter == MACH_PORT_NULL) return;
    CFTypeRef attributes = IORegistryEntrySearchCFProperty(adapter, kIOServicePlane,
                                                            CFSTR("DisplayAttributes"),
                                                            kCFAllocatorDefault,
                                                            kIORegistryIterateRecursively);
    if (attributes != NULL && CFGetTypeID(attributes) == CFDictionaryGetTypeID()) {
        NSDictionary *product = [(NSDictionary *)attributes objectForKey:@"ProductAttributes"];
        NSString *name = [product objectForKey:@"ProductName"];
        NSString *manufacturer = [product objectForKey:@"ManufacturerID"];
        NSNumber *serial = [product objectForKey:@"SerialNumber"];
        if (name != nil) [name getCString:fingerprint->productName maxLength:sizeof(fingerprint->productName)
                                  encoding:NSUTF8StringEncoding];
        if (manufacturer != nil) [manufacturer getCString:fingerprint->manufacturer maxLength:sizeof(fingerprint->manufacturer)
                                                  encoding:NSUTF8StringEncoding];
        if (serial != nil) {
            fingerprint->serialNumber = serial.unsignedIntValue;
            fingerprint->hasSerialNumber = true;
        }
    }
    if (attributes != NULL) CFRelease(attributes);
    IOObjectRelease(adapter);
}

/** Returns true only for an external proxy. */
static bool serviceIsExternal(io_service_t service) {
    CFTypeRef location = IORegistryEntryCreateCFProperty(service, CFSTR("Location"), kCFAllocatorDefault, 0);
    bool external = location != NULL && CFGetTypeID(location) == CFStringGetTypeID() &&
        CFStringCompare(location, CFSTR("External"), 0) == kCFCompareEqualTo;
    if (location != NULL) CFRelease(location);
    return external;
}

/** Returns true only for a Boolean registry property explicitly set to true. */
static bool entryBooleanPropertyIsTrue(io_registry_entry_t entry, CFStringRef key) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    bool matches = value != NULL && CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(value);
    if (value != NULL) CFRelease(value);
    return matches;
}

/** Confirms the expected active HDMI transport identity for the user-authorized display. */
static bool transportHasExpectedIdentity(io_registry_entry_t transport) {
    io_name_t name = {};
    io_name_t className = {};
    io_string_t path = {};
    IORegistryEntryGetName(transport, name);
    IOObjectGetClass(transport, className);
    if (IORegistryEntryGetPath(transport, kIOServicePlane, path) != KERN_SUCCESS) return false;
    return strstr(path, "/Port-HDMI@1/") != NULL && strcmp(className, "IOPortTransportStateDisplayPort") == 0 &&
        entryBooleanPropertyIsTrue(transport, CFSTR("Active"));
}

/** Compares an active port transport's published monitor identity with the selected display. */
static bool transportMatchesFingerprint(io_registry_entry_t transport, const DisplayFingerprint *fingerprint) {
    CFTypeRef name = IORegistryEntryCreateCFProperty(transport, CFSTR("ProductName"), kCFAllocatorDefault, 0);
    CFTypeRef manufacturer = IORegistryEntryCreateCFProperty(transport, CFSTR("ManufacturerName"), kCFAllocatorDefault, 0);
    CFTypeRef serial = IORegistryEntryCreateCFProperty(transport, CFSTR("SerialNumber"), kCFAllocatorDefault, 0);
    CFTypeRef active = IORegistryEntryCreateCFProperty(transport, CFSTR("Active"), kCFAllocatorDefault, 0);
    char transportName[128] = {};
    char transportManufacturer[32] = {};
    uint32_t transportSerial = 0;
    bool matches = active != NULL && CFGetTypeID(active) == CFBooleanGetTypeID() && CFBooleanGetValue(active) &&
        name != NULL && manufacturer != NULL && serial != NULL && CFGetTypeID(name) == CFStringGetTypeID() &&
        CFGetTypeID(manufacturer) == CFStringGetTypeID() && CFGetTypeID(serial) == CFNumberGetTypeID() &&
        CFStringGetCString(name, transportName, sizeof(transportName), kCFStringEncodingUTF8) &&
        CFStringGetCString(manufacturer, transportManufacturer, sizeof(transportManufacturer), kCFStringEncodingUTF8) &&
        CFNumberGetValue(serial, kCFNumberSInt32Type, &transportSerial) &&
        strcmp(transportName, fingerprint->productName) == 0 &&
        strcmp(transportManufacturer, fingerprint->manufacturer) == 0 &&
        fingerprint->hasSerialNumber && transportSerial == fingerprint->serialNumber;
    if (active != NULL) CFRelease(active);
    if (serial != NULL) CFRelease(serial);
    if (manufacturer != NULL) CFRelease(manufacturer);
    if (name != NULL) CFRelease(name);
    return matches;
}

/** Finds the unique active DisplayPort transport for the requested display. */
static io_registry_entry_t selectedTransport(const DisplayFingerprint *fingerprint) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    io_registry_entry_t match = MACH_PORT_NULL;
    io_registry_entry_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        if (strcmp(className, "IOPortTransportStateDisplayPort") == 0 &&
            transportMatchesFingerprint(entry, fingerprint)) {
            if (match != MACH_PORT_NULL) {
                IOObjectRelease(match);
                IOObjectRelease(entry);
                IOObjectRelease(iterator);
                return MACH_PORT_NULL;
            }
            match = entry;
        } else {
            IOObjectRelease(entry);
        }
    }
    IOObjectRelease(iterator);
    return match;
}

/** Uses the unique active DisplayPort transport only when CoreGraphics is unavailable to this process. */
static io_registry_entry_t uniqueActiveTransport(void) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    io_registry_entry_t match = MACH_PORT_NULL;
    io_registry_entry_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        CFTypeRef active = strcmp(className, "IOPortTransportStateDisplayPort") == 0 ?
            IORegistryEntryCreateCFProperty(entry, CFSTR("Active"), kCFAllocatorDefault, 0) : NULL;
        bool isActive = active != NULL && CFGetTypeID(active) == CFBooleanGetTypeID() && CFBooleanGetValue(active);
        if (active != NULL) CFRelease(active);
        if (!isActive || match != MACH_PORT_NULL) {
            if (match != MACH_PORT_NULL && isActive) {
                IOObjectRelease(match);
                match = MACH_PORT_NULL;
            }
            IOObjectRelease(entry);
            continue;
        }
        match = entry;
    }
    IOObjectRelease(iterator);
    return match;
}

/** Copies the monitor identity published by an already selected active transport. */
static void fingerprintForTransport(io_registry_entry_t transport, DisplayFingerprint *fingerprint) {
    *fingerprint = (DisplayFingerprint){};
    snprintf(fingerprint->productName, sizeof(fingerprint->productName), "Unknown Display");
    CFTypeRef name = IORegistryEntryCreateCFProperty(transport, CFSTR("ProductName"), kCFAllocatorDefault, 0);
    CFTypeRef manufacturer = IORegistryEntryCreateCFProperty(transport, CFSTR("ManufacturerName"), kCFAllocatorDefault, 0);
    CFTypeRef serial = IORegistryEntryCreateCFProperty(transport, CFSTR("SerialNumber"), kCFAllocatorDefault, 0);
    if (name != NULL && CFGetTypeID(name) == CFStringGetTypeID()) {
        CFStringGetCString(name, fingerprint->productName, sizeof(fingerprint->productName), kCFStringEncodingUTF8);
    }
    if (manufacturer != NULL && CFGetTypeID(manufacturer) == CFStringGetTypeID()) {
        CFStringGetCString(manufacturer, fingerprint->manufacturer, sizeof(fingerprint->manufacturer), kCFStringEncodingUTF8);
    }
    if (serial != NULL && CFGetTypeID(serial) == CFNumberGetTypeID()) {
        fingerprint->hasSerialNumber = CFNumberGetValue(serial, kCFNumberSInt32Type, &fingerprint->serialNumber);
    }
    if (serial != NULL) CFRelease(serial);
    if (manufacturer != NULL) CFRelease(manufacturer);
    if (name != NULL) CFRelease(name);
}

/** Finds the unique External DCPDPDeviceProxy bearing the selected transport's BranchDeviceID. */
static io_service_t selectedDCPDPDeviceProxy(io_registry_entry_t transport) {
    CFTypeRef branchID = IORegistryEntryCreateCFProperty(transport, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
    if (branchID == NULL) return MACH_PORT_NULL;
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        CFRelease(branchID);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    io_service_t match = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        CFTypeRef candidate = strcmp(className, "DCPDPDeviceProxy") == 0 ?
            IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0) : NULL;
        if (candidate != NULL && CFEqual(candidate, branchID) && serviceIsExternal(entry)) {
            if (match != MACH_PORT_NULL) {
                IOObjectRelease(match);
                IOObjectRelease(entry);
                CFRelease(candidate);
                CFRelease(branchID);
                IOObjectRelease(iterator);
                return MACH_PORT_NULL;
            }
            match = entry;
        } else {
            IOObjectRelease(entry);
        }
        if (candidate != NULL) CFRelease(candidate);
    }
    CFRelease(branchID);
    IOObjectRelease(iterator);
    return match;
}

/** Checks an EPIC node's identifying values before accepting it as the paired AV device endpoint. */
static bool isPairedAVDeviceEpic(io_registry_entry_t entry, CFTypeRef location, CFTypeRef unit, CFTypeRef role) {
    CFTypeRef epicName = IORegistryEntryCreateCFProperty(entry, CFSTR("EPICName"), kCFAllocatorDefault, 0);
    CFTypeRef provider = IORegistryEntryCreateCFProperty(entry, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0);
    CFTypeRef candidateLocation = IORegistryEntryCreateCFProperty(entry, CFSTR("EPICLocation"), kCFAllocatorDefault, 0);
    CFTypeRef candidateUnit = IORegistryEntryCreateCFProperty(entry, CFSTR("EPICUnit"), kCFAllocatorDefault, 0);
    CFTypeRef candidateRole = IORegistryEntryCreateCFProperty(entry, CFSTR("role"), kCFAllocatorDefault, 0);
    bool matches = epicName != NULL && provider != NULL && candidateLocation != NULL && candidateUnit != NULL && candidateRole != NULL &&
        CFGetTypeID(epicName) == CFStringGetTypeID() && CFGetTypeID(provider) == CFStringGetTypeID() &&
        CFStringCompare(epicName, CFSTR("dcpav-device-epic"), 0) == kCFCompareEqualTo &&
        CFStringCompare(provider, CFSTR("DCPDPDevice"), 0) == kCFCompareEqualTo &&
        CFEqual(candidateLocation, location) && CFEqual(candidateUnit, unit) && CFEqual(candidateRole, role);
    if (candidateRole != NULL) CFRelease(candidateRole);
    if (candidateUnit != NULL) CFRelease(candidateUnit);
    if (candidateLocation != NULL) CFRelease(candidateLocation);
    if (provider != NULL) CFRelease(provider);
    if (epicName != NULL) CFRelease(epicName);
    return matches;
}

/** Resolves the one DCPAVDeviceProxy paired with the selected DCPDPDeviceProxy's EPIC interface. */
static io_service_t pairedDCPAVDeviceProxy(io_service_t dcpdpDevice) {
    io_registry_entry_t dpEpic = MACH_PORT_NULL;
    io_registry_entry_t interface = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(dcpdpDevice, kIOServicePlane, &dpEpic) != KERN_SUCCESS ||
        IORegistryEntryGetParentEntry(dpEpic, kIOServicePlane, &interface) != KERN_SUCCESS) {
        if (dpEpic != MACH_PORT_NULL) IOObjectRelease(dpEpic);
        return MACH_PORT_NULL;
    }
    CFTypeRef location = IORegistryEntryCreateCFProperty(dpEpic, CFSTR("EPICLocation"), kCFAllocatorDefault, 0);
    CFTypeRef unit = IORegistryEntryCreateCFProperty(dpEpic, CFSTR("EPICUnit"), kCFAllocatorDefault, 0);
    CFTypeRef role = IORegistryEntryCreateCFProperty(dpEpic, CFSTR("role"), kCFAllocatorDefault, 0);
    IOObjectRelease(dpEpic);
    if (location == NULL || unit == NULL || role == NULL) {
        if (role != NULL) CFRelease(role);
        if (unit != NULL) CFRelease(unit);
        if (location != NULL) CFRelease(location);
        IOObjectRelease(interface);
        return MACH_PORT_NULL;
    }
    io_iterator_t epics = MACH_PORT_NULL;
    if (IORegistryEntryGetChildIterator(interface, kIOServicePlane, &epics) != KERN_SUCCESS) {
        CFRelease(role);
        CFRelease(unit);
        CFRelease(location);
        IOObjectRelease(interface);
        return MACH_PORT_NULL;
    }
    io_service_t match = MACH_PORT_NULL;
    bool ambiguous = false;
    io_registry_entry_t epic = MACH_PORT_NULL;
    while ((epic = IOIteratorNext(epics)) != MACH_PORT_NULL) {
        if (isPairedAVDeviceEpic(epic, location, unit, role)) {
            io_iterator_t children = MACH_PORT_NULL;
            if (IORegistryEntryGetChildIterator(epic, kIOServicePlane, &children) == KERN_SUCCESS) {
                io_service_t child = MACH_PORT_NULL;
                while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
                    io_name_t className = {};
                    IOObjectGetClass(child, className);
                    if (strcmp(className, "DCPAVDeviceProxy") == 0 && serviceIsExternal(child)) {
                        if (match == MACH_PORT_NULL) {
                            match = child;
                            child = MACH_PORT_NULL;
                        } else {
                            ambiguous = true;
                        }
                    }
                    IOObjectRelease(child);
                }
                IOObjectRelease(children);
            }
        }
        IOObjectRelease(epic);
    }
    IOObjectRelease(epics);
    CFRelease(role);
    CFRelease(unit);
    CFRelease(location);
    IOObjectRelease(interface);
    if (ambiguous && match != MACH_PORT_NULL) {
        IOObjectRelease(match);
        return MACH_PORT_NULL;
    }
    return match;
}

/** Returns true only when an entry holds the exact required string-valued property. */
static bool entryStringPropertyEquals(io_registry_entry_t entry, CFStringRef key, CFStringRef expected) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    bool matches = value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringCompare(value, expected, 0) == kCFCompareEqualTo;
    if (value != NULL) CFRelease(value);
    return matches;
}

/** Validates the immediate service EPIC independently of the device endpoint. */
static bool serviceHasPS190Role(io_service_t service) {
    io_registry_entry_t epic = MACH_PORT_NULL;
    if (!serviceIsExternal(service) || IORegistryEntryGetParentEntry(service, kIOServicePlane, &epic) != KERN_SUCCESS) {
        return false;
    }
    bool matches = entryStringPropertyEquals(epic, CFSTR("EPICName"), CFSTR("dcpav-service-epic")) &&
        entryStringPropertyEquals(epic, CFSTR("EPICProviderClass"), CFSTR("AppleDCPPS190")) &&
        entryStringPropertyEquals(epic, CFSTR("role"), CFSTR("DCPEXT0"));
    IOObjectRelease(epic);
    return matches;
}

/** Validates that the paired device endpoint belongs to the same DCP role, not the same endpoint number. */
static bool deviceHasDCPRole(io_service_t device) {
    io_registry_entry_t epic = MACH_PORT_NULL;
    if (!serviceIsExternal(device) || IORegistryEntryGetParentEntry(device, kIOServicePlane, &epic) != KERN_SUCCESS) {
        return false;
    }
    bool matches = entryStringPropertyEquals(epic, CFSTR("EPICName"), CFSTR("dcpav-device-epic")) &&
        entryStringPropertyEquals(epic, CFSTR("role"), CFSTR("DCPEXT0"));
    IOObjectRelease(epic);
    return matches;
}

/** Uses the proven display-to-framebuffer ordering to find one PS190 service proxy for the selected display. */
static io_service_t selectedDCPAVServiceProxy(CGDirectDisplayID displayID) {
    io_service_t adapter = adapterForDisplay(displayID);
    if (adapter == MACH_PORT_NULL) return MACH_PORT_NULL;
    uint64_t adapterID = 0;
    bool haveAdapterID = IORegistryEntryGetRegistryEntryID(adapter, &adapterID) == KERN_SUCCESS;
    IOObjectRelease(adapter);
    if (!haveAdapterID) return MACH_PORT_NULL;

    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    bool matchingFramebufferSeen = false;
    bool ambiguous = false;
    io_service_t match = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        if (IOObjectConformsTo(entry, "IOMobileFramebuffer")) {
            uint64_t registryID = 0;
            matchingFramebufferSeen = IORegistryEntryGetRegistryEntryID(entry, &registryID) == KERN_SUCCESS &&
                registryID == adapterID;
            IOObjectRelease(entry);
            continue;
        }
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        bool candidate = matchingFramebufferSeen && strcmp(name, "DCPAVServiceProxy") == 0 &&
            serviceHasPS190Role(entry);
        if (candidate && match == MACH_PORT_NULL && !ambiguous) {
            match = entry;
        } else {
            if (candidate) ambiguous = true;
            IOObjectRelease(entry);
        }
    }
    IOObjectRelease(iterator);
    if (ambiguous && match != MACH_PORT_NULL) {
        IOObjectRelease(match);
        return MACH_PORT_NULL;
    }
    return match;
}

/** Resolves only the selected display, active transport, and unique pHDMIg DCPDP anchor. */
static bool resolveSelectedDeviceAnchor(const LabOptions *options, SelectedDevices *selected) {
    *selected = (SelectedDevices){};
    if (displayIDForIndex(options->displayIndex, &selected->displayID)) {
        fingerprintForDisplay(selected->displayID, &selected->fingerprint);
        selected->transport = selectedTransport(&selected->fingerprint);
    } else {
        fprintf(stderr, "Display index %u is unavailable through CoreGraphics; using unique active transport fallback.\n",
                options->displayIndex);
        selected->transport = uniqueActiveTransport();
        if (selected->transport != MACH_PORT_NULL) fingerprintForTransport(selected->transport, &selected->fingerprint);
    }
    if (selected->transport == MACH_PORT_NULL) return false;
    selected->dcpdpDeviceProxy = selectedDCPDPDeviceProxy(selected->transport);
    return selected->dcpdpDeviceProxy != MACH_PORT_NULL;
}

/** Resolves the selected monitor's BranchDeviceID anchor, paired AV device proxy, and separate service proxy. */
static bool resolveSelectedDevices(const LabOptions *options, SelectedDevices *selected) {
    if (!resolveSelectedDeviceAnchor(options, selected)) return false;
    selected->dcpavDeviceProxy = pairedDCPAVDeviceProxy(selected->dcpdpDeviceProxy);
    if (selected->dcpavDeviceProxy == MACH_PORT_NULL || selected->displayID == 0) return false;
    selected->dcpavServiceProxy = selectedDCPAVServiceProxy(selected->displayID);
    return selected->dcpavServiceProxy != MACH_PORT_NULL;
}

/** Releases only references retained by the selected-device resolver. */
static void releaseSelectedDevices(SelectedDevices *selected) {
    if (selected->dcpavServiceProxy != MACH_PORT_NULL) IOObjectRelease(selected->dcpavServiceProxy);
    if (selected->dcpavDeviceProxy != MACH_PORT_NULL) IOObjectRelease(selected->dcpavDeviceProxy);
    if (selected->dcpdpDeviceProxy != MACH_PORT_NULL) IOObjectRelease(selected->dcpdpDeviceProxy);
    if (selected->transport != MACH_PORT_NULL) IOObjectRelease(selected->transport);
    *selected = (SelectedDevices){};
}

/** Re-acquires the selected proxy through the IOService class matcher and exact registry identity. */
static io_service_t directlyMatchedSelectedDCPAVDeviceProxy(io_service_t expected) {
    uint64_t expectedID = 0;
    if (IORegistryEntryGetRegistryEntryID(expected, &expectedID) != KERN_SUCCESS) return MACH_PORT_NULL;
    CFMutableDictionaryRef matching = IOServiceMatching("DCPAVDeviceProxy");
    if (matching == NULL) return MACH_PORT_NULL;
    io_iterator_t iterator = MACH_PORT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != KERN_SUCCESS) return MACH_PORT_NULL;
    io_service_t match = MACH_PORT_NULL;
    io_service_t candidate = MACH_PORT_NULL;
    while ((candidate = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        uint64_t candidateID = 0;
        bool isExpected = IORegistryEntryGetRegistryEntryID(candidate, &candidateID) == KERN_SUCCESS &&
            candidateID == expectedID;
        if (isExpected && match == MACH_PORT_NULL) {
            match = candidate;
        } else {
            IOObjectRelease(candidate);
        }
    }
    IOObjectRelease(iterator);
    return match;
}

/** Prints stable registry identity for a selected proxy. */
static void printIdentity(const char *label, io_registry_entry_t entry) {
    uint64_t registryID = 0;
    io_name_t name = {};
    io_name_t className = {};
    io_string_t path = {};
    IORegistryEntryGetRegistryEntryID(entry, &registryID);
    IORegistryEntryGetName(entry, name);
    IOObjectGetClass(entry, className);
    if (IORegistryEntryGetPath(entry, kIOServicePlane, path) != KERN_SUCCESS) snprintf(path, sizeof(path), "<unavailable>");
    printf("%s registry ID: 0x%016llx\n", label, (unsigned long long)registryID);
    printf("%s class: %s\n", label, className);
    printf("%s name: %s\n", label, name);
    printf("%s path: %s\n", label, path);
}

/** Prints a property when it has a compact textual, numeric, or Boolean representation. */
static void printProperty(const char *label, io_registry_entry_t entry, CFStringRef key) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    if (value == NULL) return;
    char keyText[64] = {};
    CFStringGetCString(key, keyText, sizeof(keyText), kCFStringEncodingUTF8);
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char text[128] = {};
        if (CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8)) printf("%s %s: %s\n", label, keyText, text);
    } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        int64_t number = 0;
        if (CFNumberGetValue(value, kCFNumberSInt64Type, &number)) printf("%s %s: %lld\n", label, keyText, (long long)number);
    } else if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
        printf("%s %s: %s\n", label, keyText, CFBooleanGetValue(value) ? "Yes" : "No");
    }
    CFRelease(value);
}

/** Copies a compact registry property for registry-only topology diagnostics. */
static void copyPropertyText(io_registry_entry_t entry, CFStringRef key, char *text, size_t textSize) {
    text[0] = '\0';
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
        CFStringGetCString(value, text, textSize, kCFStringEncodingUTF8);
    } else if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
        int64_t number = 0;
        if (CFNumberGetValue(value, kCFNumberSInt64Type, &number)) snprintf(text, textSize, "%lld", (long long)number);
    } else if (value != NULL && CFGetTypeID(value) == CFBooleanGetTypeID()) {
        snprintf(text, textSize, "%s", CFBooleanGetValue(value) ? "true" : "false");
    }
    if (value != NULL) CFRelease(value);
}

/** Captures the immediate EPIC parent plus the bounded endpoint and AFKEP ancestors of one proxy. */
static void captureProxyTopology(io_service_t proxy, ProxyTopology *topology) {
    *topology = (ProxyTopology){};
    topology->registered = IORegistryEntryGetRegistryEntryID(proxy, &topology->proxyID) == KERN_SUCCESS;
    if (IORegistryEntryGetPath(proxy, kIOServicePlane, topology->proxyPath) != KERN_SUCCESS) {
        snprintf(topology->proxyPath, sizeof(topology->proxyPath), "<unavailable>");
    }

    io_registry_entry_t epic = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(proxy, kIOServicePlane, &epic) != KERN_SUCCESS) return;
    topology->immediateEpicFound = true;
    IORegistryEntryGetRegistryEntryID(epic, &topology->epicID);
    IORegistryEntryGetName(epic, topology->epicName);
    if (IORegistryEntryGetPath(epic, kIOServicePlane, topology->epicPath) != KERN_SUCCESS) {
        snprintf(topology->epicPath, sizeof(topology->epicPath), "<unavailable>");
    }
    copyPropertyText(epic, CFSTR("EPICProviderClass"), topology->epicProvider, sizeof(topology->epicProvider));
    copyPropertyText(epic, CFSTR("EPICName"), topology->epicProtocolName, sizeof(topology->epicProtocolName));
    copyPropertyText(epic, CFSTR("EPICLocation"), topology->epicLocation, sizeof(topology->epicLocation));
    copyPropertyText(epic, CFSTR("interface-id"), topology->interfaceID, sizeof(topology->interfaceID));
    copyPropertyText(epic, CFSTR("role"), topology->role, sizeof(topology->role));
    CFTypeRef unit = IORegistryEntryCreateCFProperty(epic, CFSTR("EPICUnit"), kCFAllocatorDefault, 0);
    topology->hasEpicUnit = unit != NULL && CFGetTypeID(unit) == CFNumberGetTypeID() &&
        CFNumberGetValue(unit, kCFNumberSInt64Type, &topology->epicUnit);
    if (unit != NULL) CFRelease(unit);

    io_registry_entry_t current = epic;
    while (current != MACH_PORT_NULL) {
        io_name_t name = {};
        io_name_t className = {};
        io_string_t path = {};
        uint64_t registryID = 0;
        IORegistryEntryGetName(current, name);
        IOObjectGetClass(current, className);
        IORegistryEntryGetRegistryEntryID(current, &registryID);
        if (IORegistryEntryGetPath(current, kIOServicePlane, path) != KERN_SUCCESS) snprintf(path, sizeof(path), "<unavailable>");
        if (topology->endpointID == 0 && strstr(name, "DCPEXT") != NULL && strstr(name, "Endpoint") != NULL) {
            topology->endpointID = registryID;
            snprintf(topology->endpointName, sizeof(topology->endpointName), "%s", name);
            snprintf(topology->endpointPath, sizeof(topology->endpointPath), "%s", path);
        }
        if (topology->afkepInterfaceID == 0 &&
            (strcmp(className, "AFKEPInterfaceServiceKextV2") == 0 || strstr(name, "AFKEPInterfaceServiceKextV2") != NULL)) {
            topology->afkepInterfaceID = registryID;
            snprintf(topology->afkepInterfaceName, sizeof(topology->afkepInterfaceName), "%s", name);
            snprintf(topology->afkepInterfacePath, sizeof(topology->afkepInterfacePath), "%s", path);
        }
        io_registry_entry_t parent = MACH_PORT_NULL;
        if (IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) != KERN_SUCCESS) parent = MACH_PORT_NULL;
        IOObjectRelease(current);
        current = parent;
    }
}

/** Prints one proxy's required registry-only topology facts. */
static void printProxyTopology(const char *label, io_service_t proxy, const ProxyTopology *topology) {
    printf("=== %s ===\n", label);
    printIdentity(label, proxy);
    printProperty(label, proxy, CFSTR("Location"));
    printProperty(label, proxy, CFSTR("Unit"));
    printProperty(label, proxy, CFSTR("IOAVDeviceUserInterfaceSupported"));
    printf("registered: %s\n", topology->registered ? "yes (live iterator entry)" : "no");
    printf("inactive/terminated: no public user-space registry state flag; live iterator entry retained\n");
    if (!topology->immediateEpicFound) {
        printf("immediate EPIC parent: <unavailable>\n");
        return;
    }
    printf("immediate EPIC parent registry ID: 0x%016llx\n", (unsigned long long)topology->epicID);
    printf("immediate EPIC parent name: %s\n", topology->epicName);
    printf("immediate EPIC parent path: %s\n", topology->epicPath);
    printf("immediate EPIC EPICName: %s\n", topology->epicProtocolName[0] ? topology->epicProtocolName : "<unavailable>");
    printf("immediate EPIC EPICProviderClass: %s\n", topology->epicProvider[0] ? topology->epicProvider : "<unavailable>");
    printf("immediate EPIC EPICLocation: %s\n", topology->epicLocation[0] ? topology->epicLocation : "<unavailable>");
    if (topology->hasEpicUnit) printf("immediate EPIC EPICUnit: %lld\n", (long long)topology->epicUnit);
    else printf("immediate EPIC EPICUnit: <unavailable>\n");
    printf("immediate EPIC interface-id: %s\n", topology->interfaceID[0] ? topology->interfaceID : "<unavailable>");
    printf("immediate EPIC role: %s\n", topology->role[0] ? topology->role : "<unavailable>");
    printf("DCPEXT endpoint: %s (registry ID 0x%016llx)\n", topology->endpointName[0] ? topology->endpointName : "<unavailable>",
           (unsigned long long)topology->endpointID);
    printf("DCPEXT endpoint path: %s\n", topology->endpointPath[0] ? topology->endpointPath : "<unavailable>");
    printf("AFKEPInterfaceServiceKextV2 ancestor: %s (registry ID 0x%016llx)\n",
           topology->afkepInterfaceName[0] ? topology->afkepInterfaceName : "<unavailable>",
           (unsigned long long)topology->afkepInterfaceID);
    printf("AFKEP ancestor path: %s\n", topology->afkepInterfacePath[0] ? topology->afkepInterfacePath : "<unavailable>");
}

/** Prints every structural predicate used to decide whether this AV candidate pairs with the selected DP proxy. */
static bool printPairingPredicates(io_service_t candidate, const ProxyTopology *candidateTopology,
                                   const ProxyTopology *dpTopology) {
    bool external = serviceIsExternal(candidate);
    bool supported = entryBooleanPropertyIsTrue(candidate, CFSTR("IOAVDeviceUserInterfaceSupported"));
    bool avEpic = strcmp(candidateTopology->epicProtocolName, "dcpav-device-epic") == 0;
    bool dpEpic = strcmp(dpTopology->epicProtocolName, "dcpdp-device-epic") == 0;
    bool sameRole = strcmp(candidateTopology->role, dpTopology->role) == 0 &&
        strcmp(candidateTopology->role, "DCPEXT0") == 0;
    bool sameUnit = candidateTopology->hasEpicUnit && dpTopology->hasEpicUnit &&
        candidateTopology->epicUnit == dpTopology->epicUnit;
    bool sameEndpoint = candidateTopology->endpointID != 0 && candidateTopology->endpointID == dpTopology->endpointID;
    bool sameAFKEP = candidateTopology->afkepInterfaceID != 0 &&
        candidateTopology->afkepInterfaceID == dpTopology->afkepInterfaceID;
    bool paired = external && supported && avEpic && dpEpic && sameRole && sameUnit && sameEndpoint && sameAFKEP;
    printf("pairing predicates: External=%s UI-supported=%s AV-EPIC=%s DP-EPIC=%s role=DCPEXT0=%s ",
           external ? "pass" : "FAIL", supported ? "pass" : "FAIL", avEpic ? "pass" : "FAIL",
           dpEpic ? "pass" : "FAIL", sameRole ? "pass" : "FAIL");
    printf("EPICUnit=%s common-DCPEXT-endpoint=%s common-AFKEP-interface=%s => %s\n",
           sameUnit ? "pass" : "FAIL", sameEndpoint ? "pass" : "FAIL", sameAFKEP ? "pass" : "FAIL",
           paired ? "INCLUDED" : "EXCLUDED");
    return paired;
}

/** Performs only registry reads while reporting every DCPAV device candidate and its structural pairing status. */
static bool runProxyDiagnostic(const LabOptions *options) {
    SelectedDevices selected;
    if (!resolveSelectedDeviceAnchor(options, &selected)) {
        fprintf(stderr, "proxy-diagnostic: could not resolve display %u / pHDMIg DCPDP anchor. No IOAV or I2C call was made.\n",
                options->displayIndex);
        releaseSelectedDevices(&selected);
        return false;
    }
    ProxyTopology dpTopology;
    captureProxyTopology(selected.dcpdpDeviceProxy, &dpTopology);
    printf("proxy-diagnostic: registry-only; no IOServiceOpen, IOAV construction, or I2C method is called.\n");
    printf("=== Selected Display Anchor ===\n");
    printf("index: %u; online CoreGraphics display ID: %u; product: %s\n", options->displayIndex, selected.displayID,
           selected.fingerprint.productName);
    printIdentity("active transport", selected.transport);
    printProperty("active transport", selected.transport, CFSTR("Active"));
    printProperty("active transport", selected.transport, CFSTR("BranchDeviceID"));
    printProxyTopology("Selected DCPDPDeviceProxy", selected.dcpdpDeviceProxy, &dpTopology);
    printProperty("Selected DCPDPDeviceProxy", selected.dcpdpDeviceProxy, CFSTR("BranchDeviceID"));

    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        releaseSelectedDevices(&selected);
        return false;
    }
    IOObjectRelease(root);
    unsigned int total = 0;
    unsigned int paired = 0;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        if (strcmp(className, "DCPAVDeviceProxy") != 0) {
            IOObjectRelease(entry);
            continue;
        }
        ++total;
        ProxyTopology topology;
        captureProxyTopology(entry, &topology);
        char label[64] = {};
        snprintf(label, sizeof(label), "DCPAVDeviceProxy candidate %u", total);
        printProxyTopology(label, entry, &topology);
        if (printPairingPredicates(entry, &topology, &dpTopology)) ++paired;
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    printf("DCPAVDeviceProxy candidates: %u; structurally paired candidates: %u\n", total, paired);
    printf("proxy-diagnostic final: %s\n", paired == 1 ? "unique structural pair found" : "no unique structural pair");
    releaseSelectedDevices(&selected);
    return paired == 1;
}

/** Verifies a retained service object immediately before the CoreDisplay constructor. */
static bool verifyConstructionService(const char *origin, io_service_t service) {
    printf("=== Construction Service Validation (%s) ===\n", origin);
    printf("IOObjectConformsTo(DCPAVDeviceProxy): %s\n",
           IOObjectConformsTo(service, "DCPAVDeviceProxy") ? "yes" : "no");
    printIdentity("construction service", service);
    printProperty("construction service", service, CFSTR("IOAVDeviceUserInterfaceSupported"));
    printProperty("construction service", service, CFSTR("Location"));
    printProperty("construction service", service, CFSTR("Unit"));
    IOObjectRetain(service);
    printf("IOObjectRetain: completed\n");
    uint64_t retainedID = 0;
    bool stillValid = IORegistryEntryGetRegistryEntryID(service, &retainedID) == KERN_SUCCESS &&
        IOObjectConformsTo(service, "DCPAVDeviceProxy");
    printf("service valid immediately before CreateWithService: %s\n", stillValid ? "yes" : "no");
    IOObjectRelease(service);
    return stillValid;
}

/** Returns the matching SDK symbol for IOReturn values relevant to this open diagnostic. */
static const char *symbolicIOReturn(kern_return_t result) {
    switch (result) {
        case KERN_SUCCESS: return "KERN_SUCCESS";
        case kIOReturnError: return "kIOReturnError";
        case kIOReturnNoDevice: return "kIOReturnNoDevice";
        case kIOReturnNotPrivileged: return "kIOReturnNotPrivileged";
        case kIOReturnExclusiveAccess: return "kIOReturnExclusiveAccess";
        case kIOReturnUnsupported: return "kIOReturnUnsupported";
        case kIOReturnNotOpen: return "kIOReturnNotOpen";
        case kIOReturnBusy: return "kIOReturnBusy";
        case kIOReturnNotReady: return "kIOReturnNotReady";
        case kIOReturnNotPermitted: return "kIOReturnNotPermitted";
        default: return "<unmapped IOReturn>";
    }
}

/** Performs the one authorized open/close diagnostic and no external method invocation. */
static bool runOpenDiagnostic(io_service_t service) {
    printf("=== IOServiceOpen Diagnostic ===\n");
    printf("call: IOServiceOpen(selectedDCPAVDeviceProxy, mach_task_self(), 0, &connection)\n");
    io_connect_t connection = IO_OBJECT_NULL;
    kern_return_t result = IOServiceOpen(service, mach_task_self(), 0, &connection);
    printf("IOServiceOpen IOReturn: 0x%08x\n", (unsigned int)result);
    printf("IOServiceOpen decimal: %d\n", (int)result);
    printf("IOServiceOpen symbol: %s\n", symbolicIOReturn(result));
    printf("IOServiceOpen message: %s\n", mach_error_string(result));
    printf("connection: 0x%x\n", connection);
    if (result != KERN_SUCCESS) {
        printf("open diagnostic: failed; stopping before IOAVDeviceCreateWithService\n");
        return false;
    }
    kern_return_t closeResult = IOServiceClose(connection);
    printf("IOServiceClose IOReturn: 0x%08x (%d; %s; %s)\n", (unsigned int)closeResult,
           (int)closeResult, symbolicIOReturn(closeResult), mach_error_string(closeResult));
    printf("open diagnostic: connection closed; now performing the one authorized constructor call\n");
    IOAVDeviceRef device = IOAVDeviceCreateWithService(kCFAllocatorDefault, service);
    printf("IOAVDeviceCreateWithService: %p\n", device);
    if (device == NULL) {
        printf("type match: no (NULL)\n");
        return false;
    }
    CFTypeID actualType = CFGetTypeID(device);
    CFTypeID expectedType = IOAVDeviceGetTypeID();
    printf("CFGetTypeID: 0x%lx\n", (unsigned long)actualType);
    printf("IOAVDeviceGetTypeID: 0x%lx\n", (unsigned long)expectedType);
    bool valid = actualType == expectedType;
    printf("type match: %s\n", valid ? "yes" : "no");
    CFRelease(device);
    printf("release: CFRelease completed\n");
    return valid;
}

/** Reports enough correlation evidence to make device selection auditable. */
static void printSelectedDevices(const LabOptions *options, const SelectedDevices *selected) {
    printf("=== Selected Display ===\n");
    printf("index: %u\n", options->displayIndex);
    printf("CG display ID: %u\n", selected->displayID);
    printf("product: %s\n", selected->fingerprint.productName);
    printf("manufacturer: %s\n", selected->fingerprint.manufacturer[0] == '\0' ? "<unavailable>" : selected->fingerprint.manufacturer);
    if (selected->fingerprint.hasSerialNumber) printf("serial: %u\n", selected->fingerprint.serialNumber);
    else printf("serial: <unavailable>\n");
    printf("=== Branch Anchor ===\n");
    printIdentity("transport", selected->transport);
    printProperty("transport", selected->transport, CFSTR("Active"));
    printProperty("transport", selected->transport, CFSTR("BranchDeviceID"));
    printProperty("transport", selected->transport, CFSTR("BranchIEEEOUI"));
    printf("=== Paired Device Proxies ===\n");
    printIdentity("DCPDPDeviceProxy", selected->dcpdpDeviceProxy);
    printProperty("DCPDPDeviceProxy", selected->dcpdpDeviceProxy, CFSTR("BranchDeviceID"));
    printIdentity("DCPAVDeviceProxy", selected->dcpavDeviceProxy);
    printProperty("DCPAVDeviceProxy", selected->dcpavDeviceProxy, CFSTR("IOAVDeviceUserInterfaceSupported"));
    printf("=== Independently Resolved Service Branch ===\n");
    printIdentity("DCPAVServiceProxy", selected->dcpavServiceProxy);
    printProperty("DCPAVServiceProxy", selected->dcpavServiceProxy, CFSTR("Location"));
    io_registry_entry_t serviceEpic = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(selected->dcpavServiceProxy, kIOServicePlane, &serviceEpic) == KERN_SUCCESS) {
        printIdentity("DCPAVServiceProxy immediate parent", serviceEpic);
        printProperty("DCPAVServiceProxy immediate parent", serviceEpic, CFSTR("EPICName"));
        printProperty("DCPAVServiceProxy immediate parent", serviceEpic, CFSTR("EPICLocation"));
        printProperty("DCPAVServiceProxy immediate parent", serviceEpic, CFSTR("EPICProviderClass"));
        IOObjectRelease(serviceEpic);
    }
    printf("mapping: selected active transport -> unique External DCPDPDeviceProxy with equal BranchDeviceID -> ");
    printf("unique External dcpav-device-epic/DCPAVDeviceProxy (DCPEXT0) plus independently display-resolved ");
    printf("External dcpav-service-epic/DCPAVServiceProxy (DCPEXT0, AppleDCPPS190).\n");
}

/** Copies a property from a proxy's immediate EPIC parent without retaining the parent. */
static void copyImmediateParentPropertyText(io_service_t proxy, CFStringRef key, char *text, size_t textSize) {
    text[0] = '\0';
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(proxy, kIOServicePlane, &parent) == KERN_SUCCESS) {
        copyPropertyText(parent, key, text, textSize);
        IOObjectRelease(parent);
    }
}

/** Prints one explicit component of the target safety decision. */
static void printSafetyPredicate(const char *name, const char *actual, const char *expected, bool passed) {
    printf("%s:\n  actual: %s\n  expected: %s\n  result: %s\n", name, actual, expected,
           passed ? "PASS" : "FAIL");
}

/** Expands and optionally prints every exact constituent of the final target safety gate. */
static bool selectedTargetIsAuthorized(const LabOptions *options, const SelectedDevices *selected, bool printPredicates) {
    CFTypeRef branch = IORegistryEntryCreateCFProperty(selected->transport, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
    CFTypeRef deviceBranch = IORegistryEntryCreateCFProperty(selected->dcpdpDeviceProxy, CFSTR("BranchDeviceID"),
                                                               kCFAllocatorDefault, 0);
    CFTypeRef deviceInterface = IORegistryEntryCreateCFProperty(selected->dcpavDeviceProxy,
                                                                  CFSTR("IOAVDeviceUserInterfaceSupported"),
                                                                  kCFAllocatorDefault, 0);
    bool displayIndex = options->displayIndex == 1;
    bool displayOnline = selected->displayID != 0;
    bool product = strcmp(selected->fingerprint.productName, "Odyssey G75F") == 0;
    bool branchPresent = branch != NULL;
    bool deviceBranchPresent = deviceBranch != NULL;
    bool deviceInterfacePresent = deviceInterface != NULL;
    bool branchString = branchPresent && CFGetTypeID(branch) == CFStringGetTypeID();
    bool deviceBranchString = deviceBranchPresent && CFGetTypeID(deviceBranch) == CFStringGetTypeID();
    bool transportIdentity = transportHasExpectedIdentity(selected->transport);
    bool branchExpected = branchString && CFStringCompare(branch, CFSTR("pHDMIg"), 0) == kCFCompareEqualTo;
    bool deviceBranchExpected = deviceBranchString && CFStringCompare(deviceBranch, CFSTR("pHDMIg"), 0) == kCFCompareEqualTo;
    bool deviceInterfaceTrue = deviceInterfacePresent && CFGetTypeID(deviceInterface) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(deviceInterface);
    bool deviceRole = deviceHasDCPRole(selected->dcpavDeviceProxy);
    bool serviceRole = serviceHasPS190Role(selected->dcpavServiceProxy);
    bool valid = displayIndex && displayOnline && product && branchPresent && deviceBranchPresent && deviceInterfacePresent &&
        branchString && deviceBranchString && transportIdentity && branchExpected && deviceBranchExpected &&
        deviceInterfaceTrue && deviceRole && serviceRole;

    if (printPredicates) {
        char transportPath[1024] = {};
        char transportClass[128] = {};
        char branchText[128] = {};
        char deviceBranchText[128] = {};
        char deviceLocation[128] = {};
        char deviceUnit[128] = {};
        char deviceEpicName[128] = {};
        char deviceEpicRole[128] = {};
        char serviceLocation[128] = {};
        char serviceEpicName[128] = {};
        char serviceEpicRole[128] = {};
        char serviceProvider[128] = {};
        io_name_t transportName = {};
        IOObjectGetClass(selected->transport, transportClass);
        IORegistryEntryGetName(selected->transport, transportName);
        if (IORegistryEntryGetPath(selected->transport, kIOServicePlane, transportPath) != KERN_SUCCESS) {
            snprintf(transportPath, sizeof(transportPath), "<unavailable>");
        }
        copyPropertyText(selected->transport, CFSTR("BranchDeviceID"), branchText, sizeof(branchText));
        copyPropertyText(selected->dcpdpDeviceProxy, CFSTR("BranchDeviceID"), deviceBranchText, sizeof(deviceBranchText));
        copyPropertyText(selected->dcpavDeviceProxy, CFSTR("Location"), deviceLocation, sizeof(deviceLocation));
        copyPropertyText(selected->dcpavDeviceProxy, CFSTR("Unit"), deviceUnit, sizeof(deviceUnit));
        copyImmediateParentPropertyText(selected->dcpavDeviceProxy, CFSTR("EPICName"), deviceEpicName, sizeof(deviceEpicName));
        copyImmediateParentPropertyText(selected->dcpavDeviceProxy, CFSTR("role"), deviceEpicRole, sizeof(deviceEpicRole));
        copyPropertyText(selected->dcpavServiceProxy, CFSTR("Location"), serviceLocation, sizeof(serviceLocation));
        copyImmediateParentPropertyText(selected->dcpavServiceProxy, CFSTR("EPICName"), serviceEpicName, sizeof(serviceEpicName));
        copyImmediateParentPropertyText(selected->dcpavServiceProxy, CFSTR("role"), serviceEpicRole, sizeof(serviceEpicRole));
        copyImmediateParentPropertyText(selected->dcpavServiceProxy, CFSTR("EPICProviderClass"), serviceProvider,
                                        sizeof(serviceProvider));
        printf("=== Target Safety Predicates ===\n");
        char displayText[32] = {};
        char displayIDText[32] = {};
        snprintf(displayText, sizeof(displayText), "%u", options->displayIndex);
        snprintf(displayIDText, sizeof(displayIDText), "%u", selected->displayID);
        printSafetyPredicate("display_index", displayText, "1", displayIndex);
        printSafetyPredicate("display_online", displayIDText, "non-zero CoreGraphics online display ID", displayOnline);
        printSafetyPredicate("product", selected->fingerprint.productName, "Odyssey G75F", product);
        printSafetyPredicate("transport_branch_property", branchText[0] ? branchText : "<missing/non-string>",
                             "CFString pHDMIg", branchPresent && branchString && branchExpected);
        printSafetyPredicate("dcpdp_branch_property", deviceBranchText[0] ? deviceBranchText : "<missing/non-string>",
                             "CFString pHDMIg", deviceBranchPresent && deviceBranchString && deviceBranchExpected);
        printSafetyPredicate("device_ui_supported", deviceInterfaceTrue ? "true" : "false or missing", "CFBoolean true",
                             deviceInterfacePresent && deviceInterfaceTrue);
        bool transportActive = entryBooleanPropertyIsTrue(selected->transport, CFSTR("Active"));
        bool transportPort = strstr(transportPath, "/Port-HDMI@1/") != NULL;
        bool transportClassExpected = strcmp(transportClass, "IOPortTransportStateDisplayPort") == 0;
        printSafetyPredicate("transport_active", transportActive ? "true" : "false", "true", transportActive);
        printSafetyPredicate("transport_port", transportPath, "path contains /Port-HDMI@1/", transportPort);
        printSafetyPredicate("transport_class", transportClass, "IOPortTransportStateDisplayPort", transportClassExpected);
        printSafetyPredicate("transport_identity_combined", transportIdentity ? "active DisplayPort transport at Port-HDMI@1" :
                             transportName, "active IOPortTransportStateDisplayPort at Port-HDMI@1", transportIdentity);
        printSafetyPredicate("device_location", deviceLocation[0] ? deviceLocation : "<missing>", "External",
                             serviceIsExternal(selected->dcpavDeviceProxy));
        printSafetyPredicate("device_unit_pairing_evidence", deviceUnit[0] ? deviceUnit : "<missing>", "0", strcmp(deviceUnit, "0") == 0);
        printSafetyPredicate("device_epic_name", deviceEpicName[0] ? deviceEpicName : "<missing>", "dcpav-device-epic",
                             strcmp(deviceEpicName, "dcpav-device-epic") == 0);
        printSafetyPredicate("device_role", deviceEpicRole[0] ? deviceEpicRole : "<missing>", "DCPEXT0", deviceRole);
        printSafetyPredicate("service_location", serviceLocation[0] ? serviceLocation : "<missing>", "External",
                             serviceIsExternal(selected->dcpavServiceProxy));
        printSafetyPredicate("service_epic_name", serviceEpicName[0] ? serviceEpicName : "<missing>", "dcpav-service-epic",
                             strcmp(serviceEpicName, "dcpav-service-epic") == 0);
        printSafetyPredicate("service_role", serviceEpicRole[0] ? serviceEpicRole : "<missing>", "DCPEXT0",
                             strcmp(serviceEpicRole, "DCPEXT0") == 0);
        printSafetyPredicate("service_provider", serviceProvider[0] ? serviceProvider : "<missing>", "AppleDCPPS190",
                             strcmp(serviceProvider, "AppleDCPPS190") == 0);
        printSafetyPredicate("service_ps190_correlation", serviceRole ? "true" : "false",
                             "external dcpav-service-epic / DCPEXT0 / AppleDCPPS190", serviceRole);
        const char *firstFailed = !displayIndex ? "display_index" : !displayOnline ? "display_online" :
            !product ? "product" : !branchPresent ? "transport_branch_property_present" :
            !deviceBranchPresent ? "dcpdp_branch_property_present" : !deviceInterfacePresent ? "device_ui_property_present" :
            !branchString ? "transport_branch_property_type" : !deviceBranchString ? "dcpdp_branch_property_type" :
            !transportIdentity ? "transport_identity_combined" : !branchExpected ? "transport_branch_property_value" :
            !deviceBranchExpected ? "dcpdp_branch_property_value" : !deviceInterfaceTrue ? "device_ui_supported" :
            !deviceRole ? "device_role" : !serviceRole ? "service_ps190_correlation" : "<none>";
        printf("FIRST FAILED PREDICATE: %s\n", firstFailed);
        printf("target safety gate: %s\n", valid ? "PASS" : "FAIL");
    }
    if (deviceInterface != NULL) CFRelease(deviceInterface);
    if (deviceBranch != NULL) CFRelease(deviceBranch);
    if (branch != NULL) CFRelease(branch);
    return valid;
}

/** Runs the complete target gate using registry/CoreGraphics reads only. */
static bool runSafetyDiagnostic(const LabOptions *options) {
    SelectedDevices selected;
    if (!resolveSelectedDevices(options, &selected)) {
        fprintf(stderr, "safety-diagnostic: full target correlation is unavailable; no IOServiceOpen, IOAV, or I2C call was made.\n");
        releaseSelectedDevices(&selected);
        return false;
    }
    printSelectedDevices(options, &selected);
    bool passed = selectedTargetIsAuthorized(options, &selected, true);
    releaseSelectedDevices(&selected);
    return passed;
}

/** Allocates an exact-size buffer bounded by writable canaries and inaccessible guard pages. */
static bool createGuardedBuffer(GuardedBuffer *guarded, size_t byteCount) {
    *guarded = (GuardedBuffer){};
    long systemPageSize = sysconf(_SC_PAGESIZE);
    if (systemPageSize <= 0) return false;
    guarded->pageSize = (size_t)systemPageSize;
    if (byteCount == 0 || byteCount + 64 > guarded->pageSize) return false;
    guarded->mapping = mmap(NULL, guarded->pageSize * 3, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (guarded->mapping == MAP_FAILED) {
        guarded->mapping = NULL;
        return false;
    }
    uint8_t *middle = (uint8_t *)guarded->mapping + guarded->pageSize;
    if (mprotect(middle, guarded->pageSize, PROT_READ | PROT_WRITE) != 0) {
        munmap(guarded->mapping, guarded->pageSize * 3);
        *guarded = (GuardedBuffer){};
        return false;
    }
    guarded->byteCount = byteCount;
    guarded->buffer = middle + guarded->pageSize - 32 - guarded->byteCount;
    memset(guarded->buffer - 32, 0xa5, 32);
    memset(guarded->buffer, 0xcc, guarded->byteCount);
    memset(guarded->buffer + guarded->byteCount, 0x5a, 32);
    return true;
}

/** Checks the local canaries surrounding a returned guarded buffer. */
static bool guardedBufferIntact(const GuardedBuffer *guarded) {
    for (size_t index = 0; index < 32; ++index) {
        if (guarded->buffer[-32 + (ptrdiff_t)index] != 0xa5 ||
            guarded->buffer[guarded->byteCount + index] != 0x5a) return false;
    }
    return true;
}

/** Releases the mapping created for the single guarded control read. */
static void destroyGuardedBuffer(GuardedBuffer *guarded) {
    if (guarded->mapping != NULL) munmap(guarded->mapping, guarded->pageSize * 3);
    *guarded = (GuardedBuffer){};
}

/** Prints a bounded byte range as lowercase-independent hex. */
static void printHex(const char *label, const uint8_t *bytes, size_t count) {
    printf("%s", label);
    for (size_t index = 0; index < count; ++index) printf("%s%02x", index == 0 ? "" : " ", bytes[index]);
    printf("\n");
}

/** Runs the one explicitly guarded EDID control read; this function never accesses DDC address 0x37. */
static bool runEDIDControl(IOAVDeviceRef device) {
    GuardedBuffer guarded = {};
    if (!createGuardedBuffer(&guarded, 128)) {
        fprintf(stderr, "Could not allocate guarded EDID buffer.\n");
        return false;
    }
    printf("=== One Device-Level EDID Control Read ===\n");
    printf("tuple: chip=0x50 data=0x00 length=128\n");
    printf("buffer before: ");
    printHex("", guarded.buffer, 16);
    IOReturn result = IOAVDeviceReadI2C(device, 0x50, 0x00, guarded.buffer, 128);
    printf("IOReturn: 0x%08x (%s)\n", result, mach_error_string(result));
    printHex("first 32 bytes: ", guarded.buffer, 32);
    bool canariesIntact = guardedBufferIntact(&guarded);
    printf("canaries: %s\n", canariesIntact ? "intact" : "CHANGED");
    bool header = memcmp(guarded.buffer, (const uint8_t[]){0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}, 8) == 0;
    unsigned int sum = 0;
    for (size_t index = 0; index < 128; ++index) sum += guarded.buffer[index];
    printf("EDID header: %s\n", header ? "valid" : "invalid");
    printf("EDID checksum: %s\n", (sum & 0xffu) == 0 ? "valid" : "invalid");
    destroyGuardedBuffer(&guarded);
    return result == kIOReturnSuccess && canariesIntact && header && (sum & 0xffu) == 0;
}

/** Returns the required final classification text for the one-shot DDC experiment. */
static const char *experimentResultString(ExperimentResult result) {
    switch (result) {
        case EXPERIMENT_VALID_DDC_REPLY: return "VALID_DDC_REPLY";
        case EXPERIMENT_IO_ERROR: return "IO_ERROR";
        case EXPERIMENT_INVALID_DDC_REPLY: return "INVALID_DDC_REPLY";
        case EXPERIMENT_MEMORY_ANOMALY: return "MEMORY_ANOMALY";
    }
    return "UNKNOWN";
}

/**
 * Performs exactly one canonical device-level Get VCP luminance transaction.
 * This function deliberately has no loop, retry, alternate address, or Set
 * VCP path.  It returns immediately after its one write and at most one read.
 */
static ExperimentResult runOneRawDDCGetVCP(IOAVDeviceRef device) {
    const uint32_t chipAddress = 0x37;
    const uint32_t writeDataAddress = 0x51;
    const uint32_t readDataAddress = UINT32_MAX;
    const uint8_t vcpCode = 0x10;
    uint8_t request[4] = {0x82, 0x01, 0x10, 0xfd};

    GuardedBuffer guarded = {};
    if (!createGuardedBuffer(&guarded, DDC_GET_VCP_REPLY_SIZE)) {
        printf("guarded response allocation: failed\n");
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXPERIMENT_MEMORY_ANOMALY;
    }
    bool canariesBeforeRead = guardedBufferIntact(&guarded);

    printf("=== One Device-Level DDC/CI Get VCP ===\n");
    printf("write tuple: chip=0x%02x data=0x%02x length=%u\n", chipAddress, writeDataAddress,
           (unsigned int)sizeof(request));
    printHex("request: ", request, sizeof(request));
    printf("read tuple: chip=0x%02x data=0x%08x length=%u\n", chipAddress, readDataAddress,
           DDC_GET_VCP_REPLY_SIZE);
    printHex("buffer before read: ", guarded.buffer, guarded.byteCount);
    printf("canaries before read: %s\n", canariesBeforeRead ? "intact" : "CHANGED");
    if (!canariesBeforeRead) {
        destroyGuardedBuffer(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXPERIMENT_MEMORY_ANOMALY;
    }
    IOReturn writeResult = IOAVDeviceWriteI2C(device, chipAddress, writeDataAddress, request, sizeof(request));
    printf("write IOReturn: 0x%08x (%s)\n", writeResult, mach_error_string(writeResult));
    if (writeResult != kIOReturnSuccess) {
        destroyGuardedBuffer(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_IO_ERROR));
        return EXPERIMENT_IO_ERROR;
    }

    printf("post-write delay: 50 ms\n");
    usleep(50000);

    IOReturn readResult = IOAVDeviceReadI2C(device, chipAddress, readDataAddress,
                                             guarded.buffer, guarded.byteCount);
    printf("read IOReturn: 0x%08x (%s)\n", readResult, mach_error_string(readResult));
    printHex("reply: ", guarded.buffer, guarded.byteCount);
    bool canariesAfterRead = guardedBufferIntact(&guarded);
    printf("canaries after read: %s\n", canariesAfterRead ? "intact" : "CHANGED");
    if (!canariesAfterRead) {
        destroyGuardedBuffer(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXPERIMENT_MEMORY_ANOMALY;
    }

    DDCGetVCPResponse response = {};
    DDCGetVCPParseError parserResult = parseDDCGetVCPReply(guarded.buffer, guarded.byteCount,
                                                            vcpCode, &response);
    printf("strict parser: %s\n", ddcGetVCPParseErrorString(parserResult));
    printf("checksum: received=0x%02x calculated=0x%02x (%s)\n", response.receivedChecksum,
           response.calculatedChecksum, response.checksumValid ? "valid" : "invalid");

    ExperimentResult result;
    if (readResult != kIOReturnSuccess) {
        result = EXPERIMENT_IO_ERROR;
    } else if (parserResult == DDC_GET_VCP_PARSE_OK) {
        printf("returned VCP: 0x%02x\n", response.vcpCode);
        printf("maximum value: %u\n", response.maximumValue);
        printf("current value: %u\n", response.currentValue);
        result = EXPERIMENT_VALID_DDC_REPLY;
    } else {
        result = EXPERIMENT_INVALID_DDC_REPLY;
    }
    destroyGuardedBuffer(&guarded);
    printf("final classification: %s\n", experimentResultString(result));
    return result;
}

/** Constructs, type-checks, and releases one device-level IOAV object. */
static bool runLab(const LabOptions *options, const SelectedDevices *selected) {
    printSelectedDevices(options, selected);
    if (!selectedTargetIsAuthorized(options, selected, true)) {
        printf("target safety gate: FAILED; no I2C call made\n");
        return false;
    }
    printf("target safety gate: index=1 online=Yes product=Odyssey G75F transport=Port-HDMI@1 ");
    printf("BranchDeviceID=pHDMIg device-role=DCPEXT0 service-role=DCPEXT0 provider=AppleDCPPS190; passed\n");
    io_service_t constructionService = selected->dcpavDeviceProxy;
    bool ownsConstructionService = false;
    const char *origin = "paired registry traversal";
    if (options->constructionOrigin == CONSTRUCTION_ORIGIN_DIRECT_MATCHING) {
        constructionService = directlyMatchedSelectedDCPAVDeviceProxy(selected->dcpavDeviceProxy);
        ownsConstructionService = constructionService != MACH_PORT_NULL;
        origin = "IOServiceGetMatchingServices(DCPAVDeviceProxy), exact registry-ID match";
    }
    if (constructionService == MACH_PORT_NULL || !verifyConstructionService(origin, constructionService)) {
        if (ownsConstructionService) IOObjectRelease(constructionService);
        printf("construction service validation: FAILED; no I2C call made\n");
        return false;
    }
    if (options->mode == LAB_MODE_OPEN_DIAGNOSTIC) {
        bool success = runOpenDiagnostic(constructionService);
        if (ownsConstructionService) IOObjectRelease(constructionService);
        return success;
    }
    printf("=== IOAVDevice Construction ===\n");
    IOAVDeviceRef device = IOAVDeviceCreateWithService(kCFAllocatorDefault, constructionService);
    if (ownsConstructionService) IOObjectRelease(constructionService);
    printf("IOAVDeviceCreateWithService: %p\n", device);
    if (device == NULL) {
        printf("type match: no (NULL)\n");
        return false;
    }
    CFTypeID actualType = CFGetTypeID(device);
    CFTypeID expectedType = IOAVDeviceGetTypeID();
    printf("CFGetTypeID: 0x%lx\n", (unsigned long)actualType);
    printf("IOAVDeviceGetTypeID: 0x%lx\n", (unsigned long)expectedType);
    bool valid = actualType == expectedType;
    printf("type match: %s\n", valid ? "yes" : "no");
    printf("ownership: Create returned a retained (+1) CF object; releasing it after validation.\n");
    bool success = valid;
    if (valid && options->mode == LAB_MODE_EDID) success = runEDIDControl(device);
    if (valid && options->mode == LAB_MODE_DDC_VCP_RAW) {
        success = runOneRawDDCGetVCP(device) == EXPERIMENT_VALID_DDC_REPLY;
    }
    CFRelease(device);
    printf("release: CFRelease completed\n");
    return success;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        LabOptions options;
        if (!parseOptions(argc, argv, &options)) {
            printUsage(argv[0]);
            return 2;
        }
        if (options.mode == LAB_MODE_PROXY_DIAGNOSTIC) {
            return runProxyDiagnostic(&options) ? 0 : 1;
        }
        if (options.mode == LAB_MODE_SAFETY_DIAGNOSTIC) {
            return runSafetyDiagnostic(&options) ? 0 : 1;
        }
        SelectedDevices selected;
        if (!resolveSelectedDevices(&options, &selected)) {
            fprintf(stderr, "Could not uniquely resolve a DCPAVDeviceProxy for display %u. No I2C call was made.\n",
                    options.displayIndex);
            releaseSelectedDevices(&selected);
            return 1;
        }
        bool success = runLab(&options, &selected);
        releaseSelectedDevices(&selected);
        return success ? 0 : 1;
    }
}
