/*
 * iodp-ddc-lab is an isolated, read-only experiment for the private IODP
 * DisplayPort DPCD API.  It deliberately contains no IOAV, I2C, DDC/CI, AUX
 * write, or MCCS calls.  Its only hardware transaction is IODPDeviceReadDPCD.
 *
 * Migrated from m1ddc-rss tools/iodp-ddc-lab. Research-only.
 */
@import Darwin;
@import Foundation;
@import IOKit;
@import CoreGraphics;

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "coredisplay_private.h"
#include "iodp_private.h"
#include "rss_ddc.h"

/*
 * IODPService construction remains research-private. Production iodp_private.h
 * currently exposes only the validated IODPDevice Create/ReadDPCD surface.
 */
typedef CFTypeRef IODPServiceRef;
extern IODPServiceRef IODPServiceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern IODPDeviceRef IODPServiceGetDevice(IODPServiceRef service);
extern CFTypeRef IODPServiceGetAVService(IODPServiceRef service);
extern CFTypeID IODPServiceGetTypeID(void);

typedef enum {
    LAB_MODE_TOPOLOGY,
    LAB_MODE_PORT_TOPOLOGY,
    LAB_MODE_MATRIX,
    LAB_MODE_DPCD,
} LabMode;

typedef struct {
    unsigned int displayIndex;
    LabMode mode;
} LabOptions;

typedef struct {
    char productName[128];
    char manufacturer[32];
    uint32_t serialNumber;
    bool hasSerialNumber;
} DisplayFingerprint;

typedef struct {
    CGDirectDisplayID displayID;
    io_service_t dcpavProxy;
    io_service_t dcpdpProxy;
    io_registry_entry_t dcpdpEpic;
    io_registry_entry_t portTransport;
    io_service_t iodpPortService;
    io_service_t dcpdpDeviceProxy;
    DisplayFingerprint fingerprint;
} DisplayServices;

typedef struct {
    void *mapping;
    size_t pageSize;
    uint8_t *buffer;
} GuardedBuffer;

/** Prints command-line help.  DPCD mode is the only mode that performs a read. */
static void printUsage(const char *program) {
    fprintf(stderr,
            "Usage: %s [--display N] [--mode topology|port-topology|matrix|dpcd]\n"
            "\nDefaults: display 1; mode topology.\n"
            "topology: registry inspection only (no hardware transaction).\n"
            "port-topology: inspect selected-display IODP port topology only.\n"
            "matrix: construct only evidence-backed IODP candidates; no DPCD read.\n"
            "dpcd: two read-only native DPCD reads: 0x00000/16, then 0x00200/8 if base succeeds.\n",
            program);
}

/** Parses only the deliberately tiny option surface of this diagnostic. */
static bool parseOptions(int argc, char **argv, LabOptions *options) {
    *options = (LabOptions){.displayIndex = 1, .mode = LAB_MODE_TOPOLOGY};
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
            if (strcmp(value, "topology") == 0) options->mode = LAB_MODE_TOPOLOGY;
            else if (strcmp(value, "port-topology") == 0) options->mode = LAB_MODE_PORT_TOPOLOGY;
            else if (strcmp(value, "matrix") == 0) options->mode = LAB_MODE_MATRIX;
            else if (strcmp(value, "dpcd") == 0) options->mode = LAB_MODE_DPCD;
            else return false;
        } else {
            return false;
        }
    }
    return true;
}

/** Maps a one-based online-display index through rss-ddc's canonical discovery. */
static bool displayIDForIndex(unsigned int index, CGDirectDisplayID *displayID) {
    RSSDDCDisplay display = {};
    if (rss_ddc_get_display(index, &display) != RSS_DDC_OK || !display.online) return false;
    *displayID = display.cg_display_id;
    return true;
}

/** Obtains CoreDisplay's backing IOKit adapter entry for one CoreGraphics display. */
static io_service_t adapterForDisplay(CGDirectDisplayID displayID) {
    CFDictionaryRef information = CoreDisplay_DisplayCreateInfoDictionary(displayID);
    if (information == NULL) return MACH_PORT_NULL;
    CFStringRef location = CFDictionaryGetValue(information, CFSTR("IODisplayLocation"));
    io_service_t adapter = location == NULL ? MACH_PORT_NULL :
        IORegistryEntryCopyFromPath(kIOMainPortDefault, location);
    CFRelease(information);
    return adapter;
}

/** Copies the display name if IOKit publishes it, otherwise leaves a stable fallback. */
static void productNameForDisplay(CGDirectDisplayID displayID, char name[128]) {
    snprintf(name, 128, "Unknown Display");
    io_service_t adapter = adapterForDisplay(displayID);
    if (adapter == MACH_PORT_NULL) return;
    CFTypeRef attributes = IORegistryEntrySearchCFProperty(adapter, kIOServicePlane,
                                                            CFSTR("DisplayAttributes"),
                                                            kCFAllocatorDefault,
                                                            kIORegistryIterateRecursively);
    if (attributes != NULL && CFGetTypeID(attributes) == CFDictionaryGetTypeID()) {
        NSDictionary *productAttributes = [(NSDictionary *)attributes objectForKey:@"ProductAttributes"];
        NSString *productName = [productAttributes objectForKey:@"ProductName"];
        if (productName != nil) [productName getCString:name maxLength:128 encoding:NSUTF8StringEncoding];
    }
    if (attributes != NULL) CFRelease(attributes);
    IOObjectRelease(adapter);
}

/** Extracts stable selected-display identity used to correlate a port transport. */
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
        if (name != nil) [name getCString:fingerprint->productName
                                 maxLength:sizeof(fingerprint->productName)
                                  encoding:NSUTF8StringEncoding];
        if (manufacturer != nil) [manufacturer getCString:fingerprint->manufacturer
                                                 maxLength:sizeof(fingerprint->manufacturer)
                                                  encoding:NSUTF8StringEncoding];
        if (serial != nil) {
            fingerprint->serialNumber = serial.unsignedIntValue;
            fingerprint->hasSerialNumber = true;
        }
    }
    if (attributes != NULL) CFRelease(attributes);
    IOObjectRelease(adapter);
}

/** Checks the external marker so internal display services are never selected. */
static bool serviceIsExternal(io_service_t service) {
    CFTypeRef location = IORegistryEntryCreateCFProperty(service, CFSTR("Location"),
                                                          kCFAllocatorDefault, 0);
    bool external = location != NULL && CFGetTypeID(location) == CFStringGetTypeID() &&
        CFStringCompare(location, CFSTR("External"), 0) == kCFCompareEqualTo;
    if (location != NULL) CFRelease(location);
    return external;
}

/** Returns a retained DCPAVServiceProxy that belongs to the selected display. */
static io_service_t resolveDCPAVProxy(CGDirectDisplayID displayID) {
    io_service_t adapter = adapterForDisplay(displayID);
    if (adapter == MACH_PORT_NULL) return MACH_PORT_NULL;
    uint64_t adapterID = 0;
    bool haveID = IORegistryEntryGetRegistryEntryID(adapter, &adapterID) == KERN_SUCCESS;
    IOObjectRelease(adapter);
    if (!haveID) return MACH_PORT_NULL;

    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    bool matchingFramebufferSeen = false;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        if (IOObjectConformsTo(entry, "IOMobileFramebuffer")) {
            uint64_t identifier = 0;
            matchingFramebufferSeen = IORegistryEntryGetRegistryEntryID(entry, &identifier) == KERN_SUCCESS &&
                identifier == adapterID;
            IOObjectRelease(entry);
            continue;
        }
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        if (matchingFramebufferSeen && strcmp(name, "DCPAVServiceProxy") == 0 && serviceIsExternal(entry)) {
            IOObjectRelease(iterator);
            return entry;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    return MACH_PORT_NULL;
}

/** Finds the paired DCPDPServiceProxy under the same EPIC interface as DCPAV. */
static bool resolvePairedDCPDP(io_service_t dcpav, io_service_t *dcpdp, io_registry_entry_t *epic) {
    *dcpdp = MACH_PORT_NULL;
    *epic = MACH_PORT_NULL;
    io_registry_entry_t avEpic = MACH_PORT_NULL;
    io_registry_entry_t interface = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(dcpav, kIOServicePlane, &avEpic) != KERN_SUCCESS ||
        IORegistryEntryGetParentEntry(avEpic, kIOServicePlane, &interface) != KERN_SUCCESS) {
        if (avEpic != MACH_PORT_NULL) IOObjectRelease(avEpic);
        return false;
    }
    IOObjectRelease(avEpic);
    io_iterator_t children = MACH_PORT_NULL;
    if (IORegistryEntryGetChildIterator(interface, kIOServicePlane, &children) != KERN_SUCCESS) {
        IOObjectRelease(interface);
        return false;
    }
    io_registry_entry_t candidateEpic = MACH_PORT_NULL;
    while ((candidateEpic = IOIteratorNext(children)) != MACH_PORT_NULL) {
        io_iterator_t endpoints = MACH_PORT_NULL;
        if (IORegistryEntryGetChildIterator(candidateEpic, kIOServicePlane, &endpoints) == KERN_SUCCESS) {
            io_service_t endpoint = MACH_PORT_NULL;
            while ((endpoint = IOIteratorNext(endpoints)) != MACH_PORT_NULL) {
                io_name_t name = {};
                IORegistryEntryGetName(endpoint, name);
                if (strcmp(name, "DCPDPServiceProxy") == 0) {
                    *dcpdp = endpoint;
                    *epic = candidateEpic;
                    IOObjectRelease(endpoints);
                    IOObjectRelease(children);
                    IOObjectRelease(interface);
                    return true;
                }
                IOObjectRelease(endpoint);
            }
            IOObjectRelease(endpoints);
        }
        IOObjectRelease(candidateEpic);
    }
    IOObjectRelease(children);
    IOObjectRelease(interface);
    return false;
}

/** Returns true only for an active DP transport whose published identity matches the selected display. */
static bool transportMatchesFingerprint(io_registry_entry_t transport, const DisplayFingerprint *fingerprint) {
    CFTypeRef name = IORegistryEntryCreateCFProperty(transport, CFSTR("ProductName"), kCFAllocatorDefault, 0);
    CFTypeRef manufacturer = IORegistryEntryCreateCFProperty(transport, CFSTR("ManufacturerName"), kCFAllocatorDefault, 0);
    CFTypeRef serial = IORegistryEntryCreateCFProperty(transport, CFSTR("SerialNumber"), kCFAllocatorDefault, 0);
    CFTypeRef active = IORegistryEntryCreateCFProperty(transport, CFSTR("Active"), kCFAllocatorDefault, 0);
    char transportName[128] = {};
    char transportManufacturer[32] = {};
    uint32_t transportSerial = 0;
    bool matches = active != NULL && CFGetTypeID(active) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(active) && name != NULL && manufacturer != NULL && serial != NULL &&
        CFGetTypeID(name) == CFStringGetTypeID() && CFGetTypeID(manufacturer) == CFStringGetTypeID() &&
        CFGetTypeID(serial) == CFNumberGetTypeID() &&
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

/** Confirms that an IODP port service represents the same physical port as a DP transport. */
static bool portServiceMatchesTransport(io_service_t portService, io_registry_entry_t transport) {
    CFTypeRef portNumber = IORegistryEntryCreateCFProperty(portService, CFSTR("PortNumber"), kCFAllocatorDefault, 0);
    CFTypeRef portType = IORegistryEntryCreateCFProperty(portService, CFSTR("PortType"), kCFAllocatorDefault, 0);
    CFTypeRef parentPortNumber = IORegistryEntryCreateCFProperty(transport, CFSTR("ParentPortNumber"), kCFAllocatorDefault, 0);
    CFTypeRef parentPortType = IORegistryEntryCreateCFProperty(transport, CFSTR("ParentPortType"), kCFAllocatorDefault, 0);
    int32_t number = 0;
    int32_t type = 0;
    int32_t parentNumber = 0;
    int32_t parentType = 0;
    bool matches = portNumber != NULL && portType != NULL && parentPortNumber != NULL && parentPortType != NULL &&
        CFGetTypeID(portNumber) == CFNumberGetTypeID() && CFGetTypeID(portType) == CFNumberGetTypeID() &&
        CFGetTypeID(parentPortNumber) == CFNumberGetTypeID() && CFGetTypeID(parentPortType) == CFNumberGetTypeID() &&
        CFNumberGetValue(portNumber, kCFNumberSInt32Type, &number) &&
        CFNumberGetValue(portType, kCFNumberSInt32Type, &type) &&
        CFNumberGetValue(parentPortNumber, kCFNumberSInt32Type, &parentNumber) &&
        CFNumberGetValue(parentPortType, kCFNumberSInt32Type, &parentType) &&
        number == parentNumber && type == parentType;
    if (parentPortType != NULL) CFRelease(parentPortType);
    if (parentPortNumber != NULL) CFRelease(parentPortNumber);
    if (portType != NULL) CFRelease(portType);
    if (portNumber != NULL) CFRelease(portNumber);
    return matches;
}

/** Finds the selected active transport and its IODP port service at their nearest common port ancestor. */
static bool resolveSelectedIODPPort(DisplayServices *services) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return false;
    }
    IOObjectRelease(root);
    io_registry_entry_t transport = MACH_PORT_NULL;
    while ((transport = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(transport, className);
        if (strcmp(className, "IOPortTransportStateDisplayPort") != 0 ||
            !transportMatchesFingerprint(transport, &services->fingerprint)) {
            IOObjectRelease(transport);
            continue;
        }
        io_registry_entry_t current = transport;
        for (unsigned int depth = 0; depth < 5; ++depth) {
            io_registry_entry_t parent = MACH_PORT_NULL;
            if (IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) != KERN_SUCCESS) break;
            io_iterator_t children = MACH_PORT_NULL;
            if (IORegistryEntryGetChildIterator(parent, kIOServicePlane, &children) == KERN_SUCCESS) {
                io_service_t child = MACH_PORT_NULL;
                while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
                    io_name_t className = {};
                    IOObjectGetClass(child, className);
                    if (strcmp(className, "IODPPortService") == 0 &&
                        portServiceMatchesTransport(child, transport)) {
                        services->portTransport = transport;
                        services->iodpPortService = child;
                        IOObjectRelease(children);
                        IOObjectRelease(parent);
                        IOObjectRelease(iterator);
                        return true;
                    }
                    IOObjectRelease(child);
                }
                IOObjectRelease(children);
            }
            if (current != transport) IOObjectRelease(current);
            current = parent;
        }
        if (current != transport) IOObjectRelease(current);
        IOObjectRelease(transport);
    }
    IOObjectRelease(iterator);
    return false;
}

/**
 * Resolves the one DCPDPDeviceProxy whose PS190 BranchDeviceID equals the
 * selected active transport's BranchDeviceID.  Ambiguity is rejected.
 */
static bool resolveSelectedDCPDPDeviceProxy(DisplayServices *services) {
    CFTypeRef branchDeviceID = IORegistryEntryCreateCFProperty(services->portTransport, CFSTR("BranchDeviceID"),
                                                                kCFAllocatorDefault, 0);
    if (branchDeviceID == NULL) return false;
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        CFRelease(branchDeviceID);
        return false;
    }
    IOObjectRelease(root);
    io_service_t match = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        CFTypeRef candidateID = strcmp(className, "DCPDPDeviceProxy") == 0 ?
            IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0) : NULL;
        if (candidateID != NULL && CFEqual(candidateID, branchDeviceID) && serviceIsExternal(entry)) {
            if (match != MACH_PORT_NULL) {
                IOObjectRelease(match);
                IOObjectRelease(entry);
                CFRelease(candidateID);
                CFRelease(branchDeviceID);
                IOObjectRelease(iterator);
                return false;
            }
            match = entry;
        } else {
            IOObjectRelease(entry);
        }
        if (candidateID != NULL) CFRelease(candidateID);
    }
    CFRelease(branchDeviceID);
    IOObjectRelease(iterator);
    services->dcpdpDeviceProxy = match;
    return match != MACH_PORT_NULL;
}

/** Resolves the selected display and the DCPDP proxy paired with its DCPAV service. */
static bool resolveDisplayServices(unsigned int index, DisplayServices *services) {
    *services = (DisplayServices){};
    if (!displayIDForIndex(index, &services->displayID)) return false;
    fingerprintForDisplay(services->displayID, &services->fingerprint);
    services->dcpavProxy = resolveDCPAVProxy(services->displayID);
    if (services->dcpavProxy == MACH_PORT_NULL) return false;
    if (!resolvePairedDCPDP(services->dcpavProxy, &services->dcpdpProxy, &services->dcpdpEpic)) {
        IOObjectRelease(services->dcpavProxy);
        *services = (DisplayServices){};
        return false;
    }
    (void)resolveSelectedIODPPort(services);
    if (services->portTransport != MACH_PORT_NULL) (void)resolveSelectedDCPDPDeviceProxy(services);
    return true;
}

/** Releases exactly the registry-entry references retained by resolveDisplayServices. */
static void releaseDisplayServices(DisplayServices *services) {
    if (services->iodpPortService != MACH_PORT_NULL) IOObjectRelease(services->iodpPortService);
    if (services->dcpdpDeviceProxy != MACH_PORT_NULL) IOObjectRelease(services->dcpdpDeviceProxy);
    if (services->portTransport != MACH_PORT_NULL) IOObjectRelease(services->portTransport);
    if (services->dcpdpEpic != MACH_PORT_NULL) IOObjectRelease(services->dcpdpEpic);
    if (services->dcpdpProxy != MACH_PORT_NULL) IOObjectRelease(services->dcpdpProxy);
    if (services->dcpavProxy != MACH_PORT_NULL) IOObjectRelease(services->dcpavProxy);
    *services = (DisplayServices){};
}

/** Prints a registry entry's stable identity fields for comparison across connectors. */
static void printIdentity(const char *prefix, io_registry_entry_t entry) {
    uint64_t identifier = 0;
    io_name_t name = {};
    io_name_t className = {};
    io_string_t path = {};
    IORegistryEntryGetRegistryEntryID(entry, &identifier);
    IORegistryEntryGetName(entry, name);
    IOObjectGetClass(entry, className);
    if (IORegistryEntryGetPath(entry, kIOServicePlane, path) != KERN_SUCCESS) snprintf(path, sizeof(path), "<unavailable>");
    printf("%sregistry ID: 0x%016llx\n", prefix, (unsigned long long)identifier);
    printf("%sclass: %s\n", prefix, className);
    printf("%sname: %s\n", prefix, name);
    printf("%spath: %s\n", prefix, path);
}

/** Prints selected string or numeric transport properties without dumping the registry. */
static void printProperty(io_registry_entry_t entry, CFStringRef key) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    if (value == NULL) return;
    char keyText[128] = {};
    CFStringGetCString(key, keyText, sizeof(keyText), kCFStringEncodingUTF8);
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char text[256] = {};
        if (CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8)) printf("%s: %s\n", keyText, text);
    } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        int64_t number = 0;
        if (CFNumberGetValue(value, kCFNumberSInt64Type, &number)) printf("%s: %lld\n", keyText, (long long)number);
    } else if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
        printf("%s: %s\n", keyText, CFBooleanGetValue(value) ? "Yes" : "No");
    } else if (CFGetTypeID(value) == CFDataGetTypeID()) {
        printf("%s: <data: %ld bytes>\n", keyText, (long)CFDataGetLength(value));
    }
    CFRelease(value);
}

/** Prints just the immediate relationship data needed to compare port-service topology. */
static void printPortRelationships(io_registry_entry_t entry) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) == KERN_SUCCESS) {
        printf("parent:\n");
        printIdentity("  ", parent);
        IOObjectRelease(parent);
    } else {
        printf("parent: <unavailable>\n");
    }
    io_iterator_t children = MACH_PORT_NULL;
    if (IORegistryEntryGetChildIterator(entry, kIOServicePlane, &children) != KERN_SUCCESS) {
        printf("children: <unavailable>\n");
        return;
    }
    printf("children:\n");
    unsigned int count = 0;
    io_registry_entry_t child = MACH_PORT_NULL;
    while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
        io_name_t name = {};
        io_name_t className = {};
        IORegistryEntryGetName(child, name);
        IOObjectGetClass(child, className);
        printf("  %s (%s)\n", name, className);
        ++count;
        IOObjectRelease(child);
    }
    if (count == 0) printf("  <none>\n");
    IOObjectRelease(children);
}

/** Prints a bounded set of transport-identifying properties for one IODP/DP entry. */
static void printPortEntry(io_registry_entry_t entry) {
    printIdentity("", entry);
    static const CFStringRef keys[] = {
        CFSTR("IOProviderClass"), CFSTR("IONameMatched"), CFSTR("Location"), CFSTR("Unit"),
        CFSTR("PortNumber"), CFSTR("PortType"), CFSTR("PortVariant"), CFSTR("Index"),
        CFSTR("TransportDescription"), CFSTR("TransportTypeDescription"), CFSTR("ParentPortNumber"),
        CFSTR("ParentPortTypeDescription"), CFSTR("ProductName"), CFSTR("ManufacturerName"),
        CFSTR("SerialNumber"), CFSTR("ProductID"), CFSTR("Active"), CFSTR("SinkCount"),
        CFSTR("LinkRateDescription"), CFSTR("LaneCount"), CFSTR("EDID"),
        CFSTR("BranchIEEEOUI"), CFSTR("BranchDeviceID")
    };
    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) printProperty(entry, keys[index]);
    printPortRelationships(entry);
}

/** Returns whether an entry carries one of the two PS190 updater matching properties. */
static bool entryHasPS190BranchIdentity(io_registry_entry_t entry) {
    CFTypeRef oui = IORegistryEntryCreateCFProperty(entry, CFSTR("BranchIEEEOUI"), kCFAllocatorDefault, 0);
    CFTypeRef deviceID = IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
    bool matches = oui != NULL || deviceID != NULL;
    if (deviceID != NULL) CFRelease(deviceID);
    if (oui != NULL) CFRelease(oui);
    return matches;
}

/** Enumerates only IODP port services and DisplayPort transport states, never arbitrary registry nodes. */
static void printPortTopology(const DisplayServices *services) {
    printf("=== Selected Display Correlation ===\n");
    printf("product name: %s\n", services->fingerprint.productName);
    printf("manufacturer: %s\n", services->fingerprint.manufacturer[0] == '\0' ? "<unavailable>" : services->fingerprint.manufacturer);
    if (services->fingerprint.hasSerialNumber) printf("serial number: %u\n", services->fingerprint.serialNumber);
    else printf("serial number: <unavailable>\n");
    printf("=== Relevant IODP / DisplayPort Nodes ===\n");
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        printf("<registry enumeration unavailable>\n");
        return;
    }
    IOObjectRelease(root);
    io_registry_entry_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        if (strcmp(className, "IODPPortService") == 0 ||
            strcmp(className, "IOPortTransportStateDisplayPort") == 0) {
            printf("--- %s ---\n", className);
            printPortEntry(entry);
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    printf("=== PS190 Updater Property-Match Candidates ===\n");
    root = IORegistryGetRootEntry(kIOMainPortDefault);
    iterator = MACH_PORT_NULL;
    if (root != MACH_PORT_NULL && IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) == KERN_SUCCESS) {
        IOObjectRelease(root);
        bool found = false;
        while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
            if (entryHasPS190BranchIdentity(entry)) {
                printf("--- BranchIEEEOUI / BranchDeviceID candidate ---\n");
                printPortEntry(entry);
                found = true;
            }
            IOObjectRelease(entry);
        }
        if (!found) printf("<none>\n");
        IOObjectRelease(iterator);
    } else {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        printf("<registry enumeration unavailable>\n");
    }
    printf("=== Selected IODPPortService Mapping ===\n");
    if (services->portTransport == MACH_PORT_NULL || services->iodpPortService == MACH_PORT_NULL) {
        printf("mapping: <not established>\n");
        return;
    }
    printf("evidence: active DisplayPort transport matches selected product name, manufacturer, and serial number;\n");
    printf("          IODPPortService matches ParentPortNumber/ParentPortType at the nearest common port ancestor.\n");
    printf("--- matched transport ---\n");
    printPortEntry(services->portTransport);
    printf("--- matched IODPPortService ---\n");
    printPortEntry(services->iodpPortService);
    printf("=== Selected PS190 DCPDPDeviceProxy Mapping ===\n");
    if (services->dcpdpDeviceProxy == MACH_PORT_NULL) {
        printf("mapping: <not established or ambiguous>\n");
    } else {
        printf("evidence: DCPDPDeviceProxy is the unique External proxy with the selected transport's BranchDeviceID.\n");
        printPortEntry(services->dcpdpDeviceProxy);
    }
}

/** Validates the CF runtime type of a non-null IODP object before reporting it. */
static bool hasExpectedType(CFTypeRef object, CFTypeID expectedType) {
    return object != NULL && CFGetTypeID(object) == expectedType;
}

/**
 * Runs one explicit, non-transactional construction candidate.  It stops at
 * object discovery: no DPCD or other transport method is called from here.
 */
static bool runConstructionCandidate(const char *label, io_service_t registryService) {
    uint64_t registryID = 0;
    IORegistryEntryGetRegistryEntryID(registryService, &registryID);
    printf("=== Candidate: %s ===\n", label);
    printf("registry ID: 0x%016llx\n", (unsigned long long)registryID);
    IODPServiceRef service = IODPServiceCreateWithService(kCFAllocatorDefault, registryService);
    printf("IODPServiceCreateWithService: %p\n", service);
    if (!hasExpectedType(service, IODPServiceGetTypeID())) {
        printf("IODPService type: invalid or NULL\n");
        if (service != NULL) CFRelease(service);
        return false;
    }
    printf("IODPService type: valid (0x%lx)\n", (unsigned long)CFGetTypeID(service));
    IODPDeviceRef device = IODPServiceGetDevice(service);
    CFTypeRef avService = IODPServiceGetAVService(service);
    bool validDevice = hasExpectedType(device, IODPDeviceGetTypeID());
    printf("IODPServiceGetDevice: %p\n", device);
    printf("IODPDevice type: %s\n", validDevice ? "valid" : "invalid or NULL");
    printf("IODPServiceGetAVService: %p\n", avService);
    CFRelease(service);
    return validDevice;
}

/** Runs Apple's observed direct IODP device-construction path without any transport operation. */
static bool runDirectDeviceCandidate(const char *label, io_service_t registryService) {
    uint64_t registryID = 0;
    IORegistryEntryGetRegistryEntryID(registryService, &registryID);
    printf("=== Candidate: %s ===\n", label);
    printf("registry ID: 0x%016llx\n", (unsigned long long)registryID);
    IODPDeviceRef device = IODPDeviceCreateWithService(kCFAllocatorDefault, registryService);
    bool validDevice = hasExpectedType(device, IODPDeviceGetTypeID());
    printf("IODPDeviceCreateWithService: %p\n", device);
    printf("IODPDevice type: %s\n", validDevice ? "valid" : "invalid or NULL");
    if (device != NULL) CFRelease(device);
    return validDevice;
}

/** Runs only the two topology-backed constructor candidates, stopping on the first valid device. */
static int runConstructionMatrix(const DisplayServices *services) {
    if (runConstructionCandidate("DCPDPServiceProxy (negative control)", services->dcpdpProxy)) return 0;
    if (services->iodpPortService == MACH_PORT_NULL) {
        printf("IODPPortService candidate: skipped; selected-display mapping was not established.\n");
        return 1;
    }
    if (runConstructionCandidate("selected sibling IODPPortService", services->iodpPortService)) return 0;
    if (services->dcpdpDeviceProxy == MACH_PORT_NULL) {
        printf("DCPDPDeviceProxy candidate: skipped; selected PS190 BranchDeviceID mapping was not established.\n");
        return 1;
    }
    return runDirectDeviceCandidate("selected PS190 DCPDPDeviceProxy (Apple updater path)",
                                    services->dcpdpDeviceProxy) ? 0 : 1;
}

/** Classifies only from the live EPICProviderClass; it never infers connector type. */
static void printTopology(unsigned int index, const DisplayServices *services) {
    RSSDDCDisplay canonical = {};
    if (rss_ddc_get_display(index, &canonical) == RSS_DDC_OK) {
        printf("=== rss-ddc selected display ===\n");
        printf("list_index: %u\n", canonical.list_index);
        printf("product: %s\n", canonical.product_name);
        printf("provider: %s\n", rss_ddc_provider_string(canonical.provider));
        printf("branch: %s\n", canonical.branch_device_id[0] ? canonical.branch_device_id : "<unavailable>");
        printf("transport: %s\n", canonical.transport[0] ? canonical.transport : "<unavailable>");
    }
    char productName[128] = {};
    productNameForDisplay(services->displayID, productName);
    printf("=== Display ===\n");
    printf("display index: %u\n", index);
    printf("product name: %s\n", productName);
    printf("CG display ID: %u\n", services->displayID);
    printf("=== DCPAVServiceProxy ===\n");
    printIdentity("", services->dcpavProxy);
    printf("=== DCPDPServiceProxy ===\n");
    printIdentity("", services->dcpdpProxy);
    printf("=== Immediate DCPDP EPIC Parent ===\n");
    printIdentity("", services->dcpdpEpic);
    printProperty(services->dcpdpEpic, CFSTR("EPICProviderClass"));
    printProperty(services->dcpdpEpic, CFSTR("EPICName"));
    printProperty(services->dcpdpEpic, CFSTR("EPICLocation"));
    printProperty(services->dcpdpEpic, CFSTR("EPICUnit"));
    printProperty(services->dcpdpEpic, CFSTR("interface-id"));
}

/** Allocates one writable page bounded by inaccessible guard pages and canary bytes. */
static bool createGuardedBuffer(GuardedBuffer *guarded) {
    *guarded = (GuardedBuffer){};
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    void *mapping = mmap(NULL, (size_t)pageSize * 3, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (mapping == MAP_FAILED) return false;
    uint8_t *middle = (uint8_t *)mapping + pageSize;
    if (mprotect(middle, (size_t)pageSize, PROT_READ | PROT_WRITE) != 0) {
        munmap(mapping, (size_t)pageSize * 3);
        return false;
    }
    memset(middle, 0xa5, (size_t)pageSize);
    guarded->mapping = mapping;
    guarded->pageSize = (size_t)pageSize;
    guarded->buffer = middle + pageSize / 2;
    return true;
}

/** Verifies all in-page canaries surrounding the DPCD destination. */
static bool canariesIntact(const GuardedBuffer *guarded, uint32_t length) {
    const uint8_t *middle = (const uint8_t *)guarded->mapping + guarded->pageSize;
    const uint8_t *end = guarded->buffer + length;
    for (const uint8_t *byte = middle; byte < guarded->buffer; ++byte) if (*byte != 0xa5) return false;
    for (const uint8_t *byte = end; byte < middle + guarded->pageSize; ++byte) if (*byte != 0xa5) return false;
    return true;
}

/** Releases the guarded allocation after its canary result has been reported. */
static void destroyGuardedBuffer(GuardedBuffer *guarded) {
    if (guarded->mapping != NULL) munmap(guarded->mapping, guarded->pageSize * 3);
    *guarded = (GuardedBuffer){};
}

/** Prints a complete byte sequence on one stable line for compact hardware-test logs. */
static void printHex(const uint8_t *bytes, uint32_t length) {
    for (uint32_t index = 0; index < length; ++index) printf("%s%02x", index == 0 ? "" : " ", bytes[index]);
    putchar('\n');
}

/** Performs exactly one bounded, native read-only DPCD transaction. */
static IOReturn readDPCD(IODPDeviceRef device, uint32_t address, uint32_t length) {
    GuardedBuffer guarded;
    if (!createGuardedBuffer(&guarded)) {
        fprintf(stderr, "Could not allocate guarded destination buffer.\n");
        return kIOReturnNoMemory;
    }
    memset(guarded.buffer, 0xcc, length);
    printf("DPCD read: address=0x%05x; length=%u; buffer before=", address, length);
    printHex(guarded.buffer, length);
    IOReturn result = IODPDeviceReadDPCD(device, address, guarded.buffer, length);
    printf("IOReturn: 0x%08x\n", result);
    printf("buffer after: ");
    printHex(guarded.buffer, length);
    printf("buffer canaries: %s\n", canariesIntact(&guarded, length) ? "intact" : "CHANGED");
    destroyGuardedBuffer(&guarded);
    return result;
}

/** Creates the Apple-updater-evidenced IODP device and reads base DPCD before link status. */
static int runDPCD(const DisplayServices *services) {
    if (services->dcpdpDeviceProxy == MACH_PORT_NULL) {
        fprintf(stderr, "No unambiguous selected PS190 DCPDPDeviceProxy is available.\n");
        return 1;
    }
    IODPDeviceRef device = IODPDeviceCreateWithService(kCFAllocatorDefault, services->dcpdpDeviceProxy);
    printf("IODPDeviceCreateWithService: %p\n", device);
    if (!hasExpectedType(device, IODPDeviceGetTypeID())) {
        fprintf(stderr, "IODPDeviceCreateWithService did not return a valid IODPDevice.\n");
        if (device != NULL) CFRelease(device);
        return 1;
    }
    printf("IODPDevice type: valid (0x%lx)\n", (unsigned long)CFGetTypeID(device));

    printf("=== Native DPCD base capability region ===\n");
    IOReturn result = readDPCD(device, 0x00000, 16);
    if (result == kIOReturnSuccess) {
        /* DPCD 0x00200..0x00207 is the standard sink-count/link-status block. */
        printf("=== Native DPCD link-status region ===\n");
        (void)readDPCD(device, 0x00200, 8);
    } else {
        printf("link-status read skipped because the base DPCD read did not succeed.\n");
    }
    CFRelease(device);
    return result == kIOReturnSuccess ? 0 : 1;
}

/** Entry point: resolves topology first, then performs DPCD only when explicitly requested. */
int main(int argc, char **argv) {
    LabOptions options;
    if (!parseOptions(argc, argv, &options)) {
        printUsage(argv[0]);
        return 2;
    }
    DisplayServices services;
    if (!resolveDisplayServices(options.displayIndex, &services)) {
        fprintf(stderr, "Could not resolve selected external display %u and its paired DCPDPServiceProxy.\n",
                options.displayIndex);
        return 1;
    }
    printTopology(options.displayIndex, &services);
    int status = 0;
    if (options.mode == LAB_MODE_PORT_TOPOLOGY) printPortTopology(&services);
    else if (options.mode == LAB_MODE_MATRIX) {
        printPortTopology(&services);
        status = runConstructionMatrix(&services);
    } else if (options.mode == LAB_MODE_DPCD) {
        status = runDPCD(&services);
    }
    releaseDisplayServices(&services);
    return status;
}
