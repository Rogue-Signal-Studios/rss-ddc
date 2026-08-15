@import Darwin;
@import Foundation;
@import IOKit;
@import CoreGraphics;

/* Migrated from m1ddc-rss tools/ioav-ddc-lab (9992bf9, c0e695c). Research-only. */

#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ddc_parser.h"
#include "ddc_request.h"
#include "ioav_lab_support.h"

/* Private CoreDisplay/IOKit declarations required solely by this diagnostic. */
typedef CFTypeRef IOAVServiceRef;
extern CFDictionaryRef CoreDisplay_DisplayCreateInfoDictionary(CGDirectDisplayID);
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceReadI2C(IOAVServiceRef, uint32_t chipAddress,
                                   uint32_t dataAddress, void *buffer, uint32_t length);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef, uint32_t chipAddress,
                                    uint32_t dataAddress, void *buffer, uint32_t length);

#define DDC_CHIP_ADDRESS 0x37
#define MCDP_DDC_CHIP_ADDRESS 0xb7
#define DDC_DATA_ADDRESS 0x51
#define DDC_REPLY_CAPACITY 256
#define DDC_NO_OFFSET UINT32_MAX

typedef enum {
    LAB_MODE_ONE,
    LAB_MODE_STREAM,
    LAB_MODE_REPEAT,
    LAB_MODE_READ_ONLY,
    LAB_MODE_EDID,
    LAB_MODE_TOPOLOGY,
    LAB_MODE_TOPOLOGY_DETAIL,
    LAB_MODE_MCDP,
    LAB_MODE_SENTINEL_VCP,
    LAB_MODE_SERVICE_DDC_VCP_RAW,
    LAB_MODE_SERVICE_DDC_VCP_RAW_FRAMED,
    LAB_MODE_SERVICE_DDC_INPUT_RAW_FRAMED,
} LabMode;

typedef struct {
    unsigned int displayIndex;
    uint8_t vcpCode;
    uint32_t delayMicroseconds;
    uint32_t readDataAddress;
    uint32_t replyLength;
    unsigned int reads;
    LabMode mode;
    bool listDisplays;
} LabOptions;

/* Registry information needed to decide whether the selected service is MCDP-routed. */
typedef struct {
    bool parentFound;
    bool mcdpDetected;
    char proxyName[128];
    char proxyPath[1024];
    char parentName[128];
    char parentPath[1024];
    char epicProviderClass[128];
    char epicName[128];
    char epicLocation[128];
    char epicRole[128];
    char transportPath[1024];
    char transportClass[128];
    int64_t proxyUnit;
    char productName[128];
    char branchDeviceID[128];
    uint64_t proxyRegistryID;
    bool proxyExternal;
    bool serviceInterfaceSupported;
    bool proxyUnitKnown;
    bool activeTransportFound;
    bool matchingBranchDeviceProxyFound;
    unsigned int matchingServiceProxyCount;
    unsigned int matchingSelectedTransportCount;
    unsigned int globalActiveDisplayPortTransportCount;
} MCDPTopology;

typedef enum {
    EXPERIMENT_VALID_DDC_REPLY,
    EXPERIMENT_IO_ERROR,
    EXPERIMENT_INVALID_DDC_REPLY,
    EXPERIMENT_MEMORY_ANOMALY,
} ExperimentResult;

/** Prints the brief command-line help for this intentionally narrow experiment. */
static void printUsage(const char *program) {
    fprintf(stderr,
            "Usage: %s [--list-displays] [--display N] [--vcp HEX] [--delay-ms N] [--read-data HEX] [--reply-length N] "
            "[--mode one|stream|repeat|read-only|edid|topology|topology-detail|mcdp|sentinel-vcp|service-ddc-vcp-raw|service-ddc-vcp-raw-framed|service-ddc-input-raw-framed] [--reads N]\n"
            "\nDefaults: display 1, VCP 0x10, delay 50 ms, read data 0x51, reply length 11, mode one.\n"
            "Normal modes use chip 0x37. MCDP mode uses 0xb7 only after registry confirmation.\n",
            program);
}

/** Parses a bounded unsigned integer option, accepting decimal or 0x-prefixed hexadecimal. */
static bool parseUnsigned(const char *text, unsigned long maximum, unsigned long *value) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

/** Parses the lab's small set of options without accepting positional arguments. */
static bool parseOptions(int argc, char **argv, LabOptions *options) {
    *options = (LabOptions){
        .displayIndex = 1,
        .vcpCode = 0x10,
        .delayMicroseconds = 50000,
        .readDataAddress = DDC_DATA_ADDRESS,
        .replyLength = 11,
        .reads = 1,
        .mode = LAB_MODE_ONE,
    };

    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            return false;
        }
        if (strcmp(argument, "--list-displays") == 0) {
            options->listDisplays = true;
            continue;
        }
        if (index + 1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", argument);
            return false;
        }

        unsigned long value = 0;
        const char *next = argv[++index];
        if (strcmp(argument, "--display") == 0) {
            if (!parseUnsigned(next, 16, &value) || value == 0) return false;
            options->displayIndex = (unsigned int)value;
        } else if (strcmp(argument, "--vcp") == 0) {
            if (!parseUnsigned(next, UINT8_MAX, &value)) return false;
            options->vcpCode = (uint8_t)value;
        } else if (strcmp(argument, "--delay-ms") == 0) {
            if (!parseUnsigned(next, 10000, &value)) return false;
            options->delayMicroseconds = (uint32_t)value * 1000;
        } else if (strcmp(argument, "--read-data") == 0) {
            if (!parseUnsigned(next, UINT8_MAX, &value)) return false;
            options->readDataAddress = (uint32_t)value;
        } else if (strcmp(argument, "--reply-length") == 0) {
            if (!parseUnsigned(next, DDC_REPLY_CAPACITY, &value) || value == 0) return false;
            options->replyLength = (uint32_t)value;
        } else if (strcmp(argument, "--reads") == 0) {
            if (!parseUnsigned(next, IOAV_RAW_FRAMED_READS_MAX, &value) ||
                !ioavRawFramedReadsInBounds(value)) return false;
            options->reads = (unsigned int)value;
        } else if (strcmp(argument, "--mode") == 0) {
            if (strcmp(next, "one") == 0) options->mode = LAB_MODE_ONE;
            else if (strcmp(next, "stream") == 0) options->mode = LAB_MODE_STREAM;
            else if (strcmp(next, "repeat") == 0) options->mode = LAB_MODE_REPEAT;
            else if (strcmp(next, "read-only") == 0) options->mode = LAB_MODE_READ_ONLY;
            else if (strcmp(next, "edid") == 0) options->mode = LAB_MODE_EDID;
            else if (strcmp(next, "topology") == 0) options->mode = LAB_MODE_TOPOLOGY;
            else if (strcmp(next, "topology-detail") == 0) options->mode = LAB_MODE_TOPOLOGY_DETAIL;
            else if (strcmp(next, "mcdp") == 0) options->mode = LAB_MODE_MCDP;
            else if (strcmp(next, "sentinel-vcp") == 0) options->mode = LAB_MODE_SENTINEL_VCP;
            else if (strcmp(next, "service-ddc-vcp-raw") == 0) options->mode = LAB_MODE_SERVICE_DDC_VCP_RAW;
            else if (strcmp(next, "service-ddc-vcp-raw-framed") == 0) options->mode = LAB_MODE_SERVICE_DDC_VCP_RAW_FRAMED;
            else if (strcmp(next, "service-ddc-input-raw-framed") == 0) options->mode = LAB_MODE_SERVICE_DDC_INPUT_RAW_FRAMED;
            else return false;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argument);
            return false;
        }
    }
    return true;
}

/** Resolves an online display-list index to a CoreGraphics display identifier. */
static bool displayIDForIndex(unsigned int displayIndex, CGDirectDisplayID *displayID) {
    CGDirectDisplayID displays[16] = {};
    CGDisplayCount displayCount = 0;
    if (CGGetOnlineDisplayList(16, displays, &displayCount) != kCGErrorSuccess ||
        displayIndex == 0 || displayIndex > displayCount) return false;
    *displayID = displays[displayIndex - 1];
    return true;
}

/** Returns the IOKit adapter backing a CoreGraphics display, if the system exposes one. */
static io_service_t adapterForDisplay(CGDirectDisplayID displayID) {
    CFDictionaryRef information = CoreDisplay_DisplayCreateInfoDictionary(displayID);
    if (information == NULL) return MACH_PORT_NULL;
    CFStringRef location = CFDictionaryGetValue(information, CFSTR("IODisplayLocation"));
    io_service_t adapter = location == NULL ? MACH_PORT_NULL :
        IORegistryEntryCopyFromPath(kIOMainPortDefault, location);
    CFRelease(information);
    return adapter;
}

/** Copies a human-readable product name from the display's IOKit attributes. */
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

/** Returns whether a DCPAVServiceProxy belongs to an external display. */
static bool serviceIsExternal(io_service_t service) {
    CFTypeRef location = IORegistryEntryCreateCFProperty(service, CFSTR("Location"),
                                                          kCFAllocatorDefault, 0);
    bool isExternal = location != NULL && CFGetTypeID(location) == CFStringGetTypeID() &&
        CFStringCompare(location, CFSTR("External"), 0) == kCFCompareEqualTo;
    if (location != NULL) CFRelease(location);
    return isExternal;
}

/**
 * Resolves the requested online display to its matching external DCPAVServiceProxy.
 * This is a self-contained copy of only the service-discovery concept needed by the lab.
 */
static void copyRegistryDescription(io_service_t service, char name[128], char path[1024]) {
    snprintf(name, 128, "<unavailable>");
    snprintf(path, 1024, "<unavailable>");
    if (IORegistryEntryGetName(service, name) != KERN_SUCCESS) snprintf(name, 128, "<unavailable>");
    if (IORegistryEntryGetPath(service, kIOServicePlane, path) != KERN_SUCCESS) {
        snprintf(path, 1024, "<unavailable>");
    }
}

/** Copies a string property from an entry or its Metadata dictionary. */
static bool copyRegistryString(io_registry_entry_t entry, CFStringRef key, char *out, size_t outSize) {
    if (outSize == 0) return false;
    out[0] = '\0';
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    bool ok = value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringGetCString(value, out, outSize, kCFStringEncodingUTF8);
    if (value != NULL) CFRelease(value);
    if (ok) return true;
    CFTypeRef metadata = IORegistryEntryCreateCFProperty(entry, CFSTR("Metadata"), kCFAllocatorDefault, 0);
    if (metadata != NULL && CFGetTypeID(metadata) == CFDictionaryGetTypeID()) {
        CFTypeRef nested = CFDictionaryGetValue((CFDictionaryRef)metadata, key);
        ok = nested != NULL && CFGetTypeID(nested) == CFStringGetTypeID() &&
            CFStringGetCString(nested, out, outSize, kCFStringEncodingUTF8);
    }
    if (metadata != NULL) CFRelease(metadata);
    return ok;
}

/**
 * Binds the selected display's active DP transport.
 * Other active IOPortTransportStateDisplayPort objects may exist and are ignored.
 */
static void inspectActiveTransport(MCDPTopology *topology, const char *selectedProduct) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return;
    }
    IOObjectRelease(root);
    io_registry_entry_t match = MACH_PORT_NULL;
    io_registry_entry_t entry = MACH_PORT_NULL;
    unsigned int selectedCount = 0;
    unsigned int globalActiveCount = 0;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        if (strcmp(className, "IOPortTransportStateDisplayPort") != 0) {
            IOObjectRelease(entry);
            continue;
        }
        CFTypeRef active = IORegistryEntryCreateCFProperty(entry, CFSTR("Active"), kCFAllocatorDefault, 0);
        bool isActive = active != NULL && CFGetTypeID(active) == CFBooleanGetTypeID() && CFBooleanGetValue(active);
        if (active != NULL) CFRelease(active);
        if (!isActive) {
            IOObjectRelease(entry);
            continue;
        }
        ++globalActiveCount;
        char product[128] = {};
        bool selected = selectedProduct != NULL && selectedProduct[0] != '\0' &&
            copyRegistryString(entry, CFSTR("ProductName"), product, sizeof(product)) &&
            strcmp(product, selectedProduct) == 0;
        if (!selected) {
            IOObjectRelease(entry);
            continue;
        }
        ++selectedCount;
        if (selectedCount == 1) {
            match = entry;
            continue;
        }
        if (match != MACH_PORT_NULL) {
            IOObjectRelease(match);
            match = MACH_PORT_NULL;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    topology->globalActiveDisplayPortTransportCount = globalActiveCount;
    topology->matchingSelectedTransportCount = selectedCount;
    if (selectedCount != 1 || match == MACH_PORT_NULL) {
        if (match != MACH_PORT_NULL) IOObjectRelease(match);
        return;
    }
    topology->activeTransportFound = true;
    IOObjectGetClass(match, topology->transportClass);
    if (IORegistryEntryGetPath(match, kIOServicePlane, topology->transportPath) != KERN_SUCCESS) {
        snprintf(topology->transportPath, sizeof(topology->transportPath), "<unavailable>");
    }
    if (strstr(topology->transportPath, "Port-HDMI@1") == NULL) {
        char description[256] = {};
        if (copyRegistryString(match, CFSTR("TransportDescription"), description, sizeof(description)) &&
            strstr(description, "Port-HDMI@1") != NULL) {
            snprintf(topology->transportPath, sizeof(topology->transportPath), "%s", description);
        }
    }
    copyRegistryString(match, CFSTR("ProductName"), topology->productName, sizeof(topology->productName));
    copyRegistryString(match, CFSTR("BranchDeviceID"), topology->branchDeviceID, sizeof(topology->branchDeviceID));
    IOObjectRelease(match);
}

/** Confirms that the active transport branch has exactly one External DCPDP device proxy. */
static void confirmBranchDeviceProxy(MCDPTopology *topology) {
    if (topology->branchDeviceID[0] == '\0') return;
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return;
    }
    IOObjectRelease(root);
    unsigned int count = 0;
    io_registry_entry_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t className = {};
        IOObjectGetClass(entry, className);
        if (strcmp(className, "DCPDPDeviceProxy") == 0 && serviceIsExternal(entry)) {
            CFTypeRef branch = IORegistryEntryCreateCFProperty(entry, CFSTR("BranchDeviceID"), kCFAllocatorDefault, 0);
            if (branch != NULL && CFGetTypeID(branch) == CFStringGetTypeID()) {
                char value[128] = {};
                if (CFStringGetCString(branch, value, sizeof(value), kCFStringEncodingUTF8) &&
                    strcmp(value, topology->branchDeviceID) == 0) ++count;
            }
            if (branch != NULL) CFRelease(branch);
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    topology->matchingBranchDeviceProxyFound = count == 1;
}

/** Reads the immediate proxy-parent marker used to distinguish MCDP-routed HDMI. */
static void inspectMCDPTopology(io_service_t proxy, MCDPTopology *topology, const char *selectedProduct) {
    *topology = (MCDPTopology){};
    copyRegistryDescription(proxy, topology->proxyName, topology->proxyPath);
    IORegistryEntryGetRegistryEntryID(proxy, &topology->proxyRegistryID);
    topology->proxyExternal = serviceIsExternal(proxy);
    CFTypeRef interfaceSupported = IORegistryEntryCreateCFProperty(proxy, CFSTR("IOAVServiceUserInterfaceSupported"),
                                                                    kCFAllocatorDefault, 0);
    topology->serviceInterfaceSupported = interfaceSupported != NULL && CFGetTypeID(interfaceSupported) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(interfaceSupported);
    if (interfaceSupported != NULL) CFRelease(interfaceSupported);

    io_registry_entry_t parent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(proxy, kIOServicePlane, &parent) != KERN_SUCCESS) return;
    topology->parentFound = true;
    copyRegistryDescription(parent, topology->parentName, topology->parentPath);

    CFTypeRef providerClass = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"),
                                                               kCFAllocatorDefault, 0);
    CFTypeRef epicName = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICName"), kCFAllocatorDefault, 0);
    CFTypeRef epicLocation = IORegistryEntryCreateCFProperty(parent, CFSTR("EPICLocation"), kCFAllocatorDefault, 0);
    CFTypeRef epicRole = IORegistryEntryCreateCFProperty(parent, CFSTR("role"), kCFAllocatorDefault, 0);
    CFTypeRef proxyUnit = IORegistryEntryCreateCFProperty(proxy, CFSTR("Unit"), kCFAllocatorDefault, 0);
    if (providerClass != NULL && CFGetTypeID(providerClass) == CFStringGetTypeID()) {
        [(NSString *)providerClass getCString:topology->epicProviderClass
                                    maxLength:sizeof(topology->epicProviderClass)
                                     encoding:NSUTF8StringEncoding];
        topology->mcdpDetected = strcmp(topology->epicProviderClass, "AppleDCPMCDP29XX") == 0;
    } else {
        snprintf(topology->epicProviderClass, sizeof(topology->epicProviderClass), "<unavailable>");
    }
    if (epicName != NULL && CFGetTypeID(epicName) == CFStringGetTypeID()) {
        CFStringGetCString(epicName, topology->epicName, sizeof(topology->epicName), kCFStringEncodingUTF8);
    }
    if (epicLocation != NULL && CFGetTypeID(epicLocation) == CFStringGetTypeID()) {
        CFStringGetCString(epicLocation, topology->epicLocation, sizeof(topology->epicLocation), kCFStringEncodingUTF8);
    }
    if (epicRole != NULL && CFGetTypeID(epicRole) == CFStringGetTypeID()) {
        CFStringGetCString(epicRole, topology->epicRole, sizeof(topology->epicRole), kCFStringEncodingUTF8);
    }
    topology->proxyUnitKnown = proxyUnit != NULL && CFGetTypeID(proxyUnit) == CFNumberGetTypeID() &&
        CFNumberGetValue(proxyUnit, kCFNumberSInt64Type, &topology->proxyUnit);
    if (proxyUnit != NULL) CFRelease(proxyUnit);
    if (epicRole != NULL) CFRelease(epicRole);
    if (epicLocation != NULL) CFRelease(epicLocation);
    if (epicName != NULL) CFRelease(epicName);
    if (providerClass != NULL) CFRelease(providerClass);
    IOObjectRelease(parent);
    inspectActiveTransport(topology, selectedProduct);
    confirmBranchDeviceProxy(topology);
}

/** Prints the exact live registry evidence used for MCDP mode's safety gate. */
static void printMCDPTopology(unsigned int displayIndex, const MCDPTopology *topology) {
    CGDirectDisplayID displayID = 0;
    char productName[128] = "Unknown Display";
    if (displayIDForIndex(displayIndex, &displayID)) productNameForDisplay(displayID, productName);
    printf("selected display: index=%u; name=%s; CG display ID=%u\n", displayIndex, productName, displayID);
    printf("DCPAVServiceProxy: name=%s; path=%s\n", topology->proxyName, topology->proxyPath);
    printf("DCPAVServiceProxy registry ID: 0x%016llx\n", (unsigned long long)topology->proxyRegistryID);
    printf("proxy parent: name=%s; path=%s\n", topology->parentFound ? topology->parentName : "<unavailable>",
           topology->parentFound ? topology->parentPath : "<unavailable>");
    printf("EPICProviderClass: %s\n", topology->epicProviderClass[0] ? topology->epicProviderClass : "<unavailable>");
    printf("active transport product: %s\n", topology->productName[0] ? topology->productName : "<unavailable>");
    printf("active transport BranchDeviceID: %s\n", topology->branchDeviceID[0] ? topology->branchDeviceID : "<unavailable>");
    printf("matching External DCPDPDeviceProxy: %s\n", topology->matchingBranchDeviceProxyFound ? "yes" : "no");
    printf("selected-display matching transports: %u (global active DP transports: %u)\n",
           topology->matchingSelectedTransportCount, topology->globalActiveDisplayPortTransportCount);
    printf("selected-display matching service proxies: %u\n", topology->matchingServiceProxyCount);
    printf("Location: %s\n", topology->proxyExternal ? "External" : "<unavailable>");
    printf("IOAVServiceUserInterfaceSupported: %s\n", topology->serviceInterfaceSupported ? "true" : "false");
    printf("MCDP detected: %s\n", topology->mcdpDetected ? "yes (AppleDCPMCDP29XX)" : "no");
}

/* This fixed property list keeps topology reports concise and diff-friendly. */
static const CFStringRef kTransportPropertyKeys[] = {
    CFSTR("EPICProviderClass"), CFSTR("EPICName"), CFSTR("EPICLocation"), CFSTR("EPICUnit"),
    CFSTR("interface-id"), CFSTR("interface-name"), CFSTR("role"), CFSTR("Location"),
    CFSTR("Unit"), CFSTR("IOProviderClass"), CFSTR("IOUserClientClass"),
    CFSTR("IOAVServiceUserInterfaceSupported"), CFSTR("IONameMatched"),
};

/** Prints an available scalar registry property without dumping arbitrary dictionaries. */
static void printRegistryProperty(io_registry_entry_t entry, CFStringRef key) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    if (value == NULL) return;

    char keyText[128] = {};
    CFStringGetCString(key, keyText, sizeof(keyText), kCFStringEncodingUTF8);
    printf("%s: ", keyText);
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char text[512] = {};
        if (CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8)) printf("%s", text);
        else printf("<non-UTF-8 string>");
    } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        int64_t number = 0;
        CFNumberGetValue(value, kCFNumberSInt64Type, &number);
        printf("%lld", number);
    } else if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
        printf("%s", CFBooleanGetValue(value) ? "true" : "false");
    } else {
        printf("<non-scalar value>");
    }
    putchar('\n');
    CFRelease(value);
}

/** Prints the stable identity fields for one registry entry. */
static void printRegistryIdentity(io_registry_entry_t entry) {
    uint64_t registryID = 0;
    io_name_t name = {};
    io_name_t className = {};
    char path[1024] = {};
    IORegistryEntryGetRegistryEntryID(entry, &registryID);
    IORegistryEntryGetName(entry, name);
    IOObjectGetClass(entry, className);
    if (IORegistryEntryGetPath(entry, kIOServicePlane, path) != KERN_SUCCESS) {
        snprintf(path, sizeof(path), "<unavailable>");
    }
    printf("registry ID: 0x%016llx\n", (unsigned long long)registryID);
    printf("class: %s\n", className[0] ? className : "<unavailable>");
    printf("name: %s\n", name[0] ? name : "<unavailable>");
    printf("path: %s\n", path);
}

/** Prints only the fixed, transport-identifying property subset for an entry. */
static void printTransportProperties(io_registry_entry_t entry) {
    for (size_t index = 0; index < sizeof(kTransportPropertyKeys) / sizeof(kTransportPropertyKeys[0]); ++index) {
        printRegistryProperty(entry, kTransportPropertyKeys[index]);
    }
}

/** Returns whether ASCII text contains a token without making locale-dependent comparisons. */
static bool textContainsIgnoreCase(const char *text, const char *token) {
    if (text == NULL || token == NULL || *token == '\0') return false;
    size_t tokenLength = strlen(token);
    for (; *text != '\0'; ++text) {
        size_t index = 0;
        while (index < tokenLength && text[index] != '\0' &&
               tolower((unsigned char)text[index]) == tolower((unsigned char)token[index])) {
            ++index;
        }
        if (index == tokenLength) return true;
    }
    return false;
}

/** Tests an entry identity and its selected scalar properties against transport keywords. */
static bool entryMatchesTransportKeywords(io_registry_entry_t entry) {
    static const char *const keywords[] = {"dptx", "dp", "av", "hdmi", "ps190", "aux", "i2c", "display", "external"};
    char identity[1024] = {};
    io_name_t name = {};
    io_name_t className = {};
    IORegistryEntryGetName(entry, name);
    IOObjectGetClass(entry, className);
    snprintf(identity, sizeof(identity), "%s %s", name, className);
    for (size_t keyIndex = 0; keyIndex < sizeof(keywords) / sizeof(keywords[0]); ++keyIndex) {
        if (textContainsIgnoreCase(identity, keywords[keyIndex])) return true;
    }

    for (size_t propertyIndex = 0; propertyIndex < sizeof(kTransportPropertyKeys) / sizeof(kTransportPropertyKeys[0]); ++propertyIndex) {
        CFTypeRef value = IORegistryEntryCreateCFProperty(entry, kTransportPropertyKeys[propertyIndex],
                                                          kCFAllocatorDefault, 0);
        if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
            char text[512] = {};
            CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8);
            for (size_t keyIndex = 0; keyIndex < sizeof(keywords) / sizeof(keywords[0]); ++keyIndex) {
                if (textContainsIgnoreCase(text, keywords[keyIndex])) {
                    CFRelease(value);
                    return true;
                }
            }
        }
        if (value != NULL) CFRelease(value);
    }
    return false;
}

/** Searches only a local property collection for a PS190 string, with a bounded traversal depth. */
static bool propertyListContainsPS190(CFTypeRef value, unsigned int depth) {
    if (value == NULL || depth > 4) return false;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char text[512] = {};
        return CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8) &&
            textContainsIgnoreCase(text, "ps190");
    }
    if (CFGetTypeID(value) == CFArrayGetTypeID()) {
        CFArrayRef array = value;
        for (CFIndex index = 0; index < CFArrayGetCount(array); ++index) {
            if (propertyListContainsPS190(CFArrayGetValueAtIndex(array, index), depth + 1)) return true;
        }
    }
    if (CFGetTypeID(value) == CFDictionaryGetTypeID()) {
        CFDictionaryRef dictionary = value;
        CFIndex count = CFDictionaryGetCount(dictionary);
        const void **keys = calloc((size_t)count, sizeof(*keys));
        const void **values = calloc((size_t)count, sizeof(*values));
        if (keys == NULL || values == NULL) {
            free(keys);
            free(values);
            return false;
        }
        CFDictionaryGetKeysAndValues(dictionary, keys, values);
        bool found = false;
        for (CFIndex index = 0; index < count && !found; ++index) {
            found = propertyListContainsPS190(values[index], depth + 1);
        }
        free(keys);
        free(values);
        return found;
    }
    return false;
}

/** Finds PS190 text in an entry's identity or its local registry property values. */
static bool entryMentionsPS190(io_registry_entry_t entry) {
    io_name_t name = {};
    io_name_t className = {};
    IORegistryEntryGetName(entry, name);
    IOObjectGetClass(entry, className);
    if (textContainsIgnoreCase(name, "ps190") || textContainsIgnoreCase(className, "ps190")) return true;

    CFMutableDictionaryRef properties = NULL;
    if (IORegistryEntryCreateCFProperties(entry, &properties, kCFAllocatorDefault, 0) != KERN_SUCCESS) return false;
    bool found = propertyListContainsPS190(properties, 0);
    CFRelease(properties);
    return found;
}

/**
 * Falls back only when CoreGraphics does not enumerate an online display for
 * this process. The fallback requires one External, user-callable DCPAV
 * service whose immediate EPIC provider is AppleDCPPS190; later gating also
 * requires the unique active transport and matching DCPDP branch proxy.
 */
static io_service_t resolveUniquePS190ServiceProxy(MCDPTopology *topology) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);
    io_service_t match = MACH_PORT_NULL;
    io_service_t entry = MACH_PORT_NULL;
    while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        io_name_t name = {};
        IORegistryEntryGetName(entry, name);
        bool candidate = strcmp(name, "DCPAVServiceProxy") == 0 && serviceIsExternal(entry);
        io_registry_entry_t parent = MACH_PORT_NULL;
        CFTypeRef provider = candidate && IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) == KERN_SUCCESS ?
            IORegistryEntryCreateCFProperty(parent, CFSTR("EPICProviderClass"), kCFAllocatorDefault, 0) : NULL;
        bool isPS190 = provider != NULL && CFGetTypeID(provider) == CFStringGetTypeID() &&
            CFStringCompare(provider, CFSTR("AppleDCPPS190"), 0) == kCFCompareEqualTo;
        if (provider != NULL) CFRelease(provider);
        if (parent != MACH_PORT_NULL) IOObjectRelease(parent);
        if (!isPS190 || match != MACH_PORT_NULL) {
            if (match != MACH_PORT_NULL && isPS190) {
                IOObjectRelease(match);
                match = MACH_PORT_NULL;
            }
            IOObjectRelease(entry);
            continue;
        }
        match = entry;
    }
    IOObjectRelease(iterator);
    if (match != MACH_PORT_NULL && topology != NULL) inspectMCDPTopology(match, topology, "");
    return match;
}

/** Resolves the selected display to its retained DCPAVServiceProxy registry entry. */
static io_service_t resolveDisplayProxy(unsigned int displayIndex, MCDPTopology *topology) {
    CGDirectDisplayID displayID = 0;
    if (!displayIDForIndex(displayIndex, &displayID)) {
        fprintf(stderr, "Display index %u is unavailable through CoreGraphics; using PS190 registry correlation fallback.\n",
                displayIndex);
        return resolveUniquePS190ServiceProxy(topology);
    }

    io_service_t adapter = adapterForDisplay(displayID);
    if (adapter == MACH_PORT_NULL) return MACH_PORT_NULL;

    uint64_t adapterID = 0;
    if (IORegistryEntryGetRegistryEntryID(adapter, &adapterID) != KERN_SUCCESS) {
        IOObjectRelease(adapter);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(adapter);

    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    io_iterator_t iterator = MACH_PORT_NULL;
    if (root == MACH_PORT_NULL || IORegistryEntryCreateIterator(root, kIOServicePlane,
                                                                 kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
        if (root != MACH_PORT_NULL) IOObjectRelease(root);
        return MACH_PORT_NULL;
    }
    IOObjectRelease(root);

    char selectedProduct[128] = {};
    productNameForDisplay(displayID, selectedProduct);

    bool matchingFramebufferSeen = false;
    unsigned int selectedProxyCount = 0;
    io_service_t firstProxy = MACH_PORT_NULL;
    io_service_t service = MACH_PORT_NULL;
    while ((service = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
        if (IOObjectConformsTo(service, "IOMobileFramebuffer")) {
            uint64_t registryID = 0;
            matchingFramebufferSeen = IORegistryEntryGetRegistryEntryID(service, &registryID) == KERN_SUCCESS &&
                registryID == adapterID;
            IOObjectRelease(service);
            continue;
        }

        io_name_t name = {};
        IORegistryEntryGetName(service, name);
        if (matchingFramebufferSeen && strcmp(name, "DCPAVServiceProxy") == 0 && serviceIsExternal(service)) {
            ++selectedProxyCount;
            if (firstProxy == MACH_PORT_NULL) {
                firstProxy = service;
                continue;
            }
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    if (firstProxy != MACH_PORT_NULL && topology != NULL) {
        inspectMCDPTopology(firstProxy, topology, selectedProduct);
        topology->matchingServiceProxyCount = selectedProxyCount;
    }
    return firstProxy;
}

/** Creates the private IOAV service only for modes that perform an IOAV transaction. */
static IOAVServiceRef resolveDisplayService(unsigned int displayIndex, MCDPTopology *topology) {
    io_service_t proxy = resolveDisplayProxy(displayIndex, topology);
    if (proxy == MACH_PORT_NULL) return NULL;
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, proxy);
    IOObjectRelease(proxy);
    return service;
}

/** Copies a string-valued CoreDisplay information field using a small set of known aliases. */
static bool copyDisplayInfoString(CFDictionaryRef information, const CFStringRef *keys, size_t keyCount,
                                  char text[256]) {
    for (size_t index = 0; index < keyCount; ++index) {
        CFTypeRef value = CFDictionaryGetValue(information, keys[index]);
        if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
            CFStringGetCString(value, text, 256, kCFStringEncodingUTF8)) return true;
    }
    return false;
}

/** Prints immediate parent and child identities for a PS190-related local entry. */
static void printImmediateRelationships(io_registry_entry_t entry) {
    io_registry_entry_t parent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) == KERN_SUCCESS) {
        uint64_t parentID = 0;
        io_name_t parentName = {};
        IORegistryEntryGetRegistryEntryID(parent, &parentID);
        IORegistryEntryGetName(parent, parentName);
        printf("immediate parent: 0x%016llx %s\n", (unsigned long long)parentID, parentName);
        IOObjectRelease(parent);
    } else {
        printf("immediate parent: <unavailable>\n");
    }

    io_iterator_t children = MACH_PORT_NULL;
    if (IORegistryEntryGetChildIterator(entry, kIOServicePlane, &children) != KERN_SUCCESS) return;
    io_registry_entry_t child = MACH_PORT_NULL;
    bool foundChild = false;
    while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
        uint64_t childID = 0;
        io_name_t childName = {};
        IORegistryEntryGetRegistryEntryID(child, &childID);
        IORegistryEntryGetName(child, childName);
        printf("immediate child: 0x%016llx %s\n", (unsigned long long)childID, childName);
        foundChild = true;
        IOObjectRelease(child);
    }
    if (!foundChild) printf("immediate child: <none>\n");
    IOObjectRelease(children);
}

/** Prints the selected display's identity and its existing CoreDisplay/IORegistry metadata. */
static void printTopologyDisplay(unsigned int displayIndex) {
    static const CFStringRef displayUUIDKeys[] = {CFSTR("DisplayUUID"), CFSTR("Display UUID")};
    static const CFStringRef edidUUIDKeys[] = {CFSTR("EDIDUUID"), CFSTR("EDID UUID")};
    CGDirectDisplayID displayID = 0;
    char productName[128] = "Unknown Display";
    char displayUUID[256] = "<unavailable>";
    char edidUUID[256] = "<unavailable>";
    char location[256] = "<unavailable>";

    displayIDForIndex(displayIndex, &displayID);
    productNameForDisplay(displayID, productName);
    CFDictionaryRef information = CoreDisplay_DisplayCreateInfoDictionary(displayID);
    if (information != NULL) {
        copyDisplayInfoString(information, displayUUIDKeys,
                              sizeof(displayUUIDKeys) / sizeof(displayUUIDKeys[0]), displayUUID);
        copyDisplayInfoString(information, edidUUIDKeys,
                              sizeof(edidUUIDKeys) / sizeof(edidUUIDKeys[0]), edidUUID);
        const CFStringRef locationKey = CFSTR("IODisplayLocation");
        copyDisplayInfoString(information, &locationKey, 1, location);
        CFRelease(information);
    }

    printf("=== Display ===\n");
    printf("display index: %u\n", displayIndex);
    printf("product name: %s\n", productName);
    printf("CG display ID: %u\n", displayID);
    printf("display UUID: %s\n", displayUUID);
    printf("EDID UUID: %s\n", edidUUID);
    printf("IODisplayLocation: %s\n", location);

    io_service_t adapter = adapterForDisplay(displayID);
    if (adapter == MACH_PORT_NULL) {
        printf("registry ID: <unavailable>\n");
        return;
    }
    uint64_t registryID = 0;
    if (IORegistryEntryGetRegistryEntryID(adapter, &registryID) == KERN_SUCCESS) {
        printf("registry ID: 0x%016llx\n", (unsigned long long)registryID);
    } else {
        printf("registry ID: <unavailable>\n");
    }
    IOObjectRelease(adapter);
}

/** Prints the bounded parent chain from the EPIC service toward the DCP external endpoint. */
static void printTransportAncestry(io_registry_entry_t epicParent) {
    printf("=== Transport Ancestry ===\n");
    io_registry_entry_t current = epicParent;
    IOObjectRetain(current);
    for (unsigned int depth = 0; depth < 6 && current != MACH_PORT_NULL; ++depth) {
        printf("--- ancestor %u ---\n", depth);
        printRegistryIdentity(current);
        printTransportProperties(current);
        io_registry_entry_t next = MACH_PORT_NULL;
        if (IORegistryEntryGetParentEntry(current, kIOServicePlane, &next) != KERN_SUCCESS) next = MACH_PORT_NULL;
        IOObjectRelease(current);
        current = next;
    }
}

/** Enumerates only direct, transport-keyword siblings of the selected EPIC parent. */
static void printRelevantSiblings(io_registry_entry_t epicParent) {
    printf("=== Relevant Sibling EPIC Services ===\n");
    uint64_t parentID = 0;
    IORegistryEntryGetRegistryEntryID(epicParent, &parentID);
    io_registry_entry_t interface = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(epicParent, kIOServicePlane, &interface) != KERN_SUCCESS) {
        printf("<interface parent unavailable>\n");
        return;
    }

    io_iterator_t children = MACH_PORT_NULL;
    if (IORegistryEntryGetChildIterator(interface, kIOServicePlane, &children) != KERN_SUCCESS) {
        printf("<interface children unavailable>\n");
        IOObjectRelease(interface);
        return;
    }
    bool foundSibling = false;
    io_registry_entry_t child = MACH_PORT_NULL;
    while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
        uint64_t childID = 0;
        IORegistryEntryGetRegistryEntryID(child, &childID);
        if (childID != parentID && entryMatchesTransportKeywords(child)) {
            printf("--- sibling ---\n");
            printRegistryIdentity(child);
            printTransportProperties(child);
            foundSibling = true;
        }
        IOObjectRelease(child);
    }
    if (!foundSibling) printf("<none>\n");
    IOObjectRelease(children);
    IOObjectRelease(interface);
}

/** Searches the immediate EPIC-interface subtree, never the whole registry, for PS190 markers. */
static void printPS190Entries(io_registry_entry_t epicParent) {
    printf("=== PS190-Related Entries ===\n");
    io_registry_entry_t interface = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(epicParent, kIOServicePlane, &interface) != KERN_SUCCESS) {
        printf("<interface parent unavailable>\n");
        return;
    }

    bool found = false;
    if (entryMentionsPS190(interface)) {
        printf("--- PS190 entry ---\n");
        printRegistryIdentity(interface);
        printTransportProperties(interface);
        printImmediateRelationships(interface);
        found = true;
    }

    io_iterator_t iterator = MACH_PORT_NULL;
    if (IORegistryEntryCreateIterator(interface, kIOServicePlane, kIORegistryIterateRecursively,
                                      &iterator) == KERN_SUCCESS) {
        io_registry_entry_t entry = MACH_PORT_NULL;
        while ((entry = IOIteratorNext(iterator)) != MACH_PORT_NULL) {
            if (entryMentionsPS190(entry)) {
                printf("--- PS190 entry ---\n");
                printRegistryIdentity(entry);
                printTransportProperties(entry);
                printImmediateRelationships(entry);
                found = true;
            }
            IOObjectRelease(entry);
        }
        IOObjectRelease(iterator);
    }
    if (!found) printf("<none>\n");
    IOObjectRelease(interface);
}

/** Runs the read-only registry capture; this function never creates or calls an IOAV I2C API. */
static int runTopologyDetail(unsigned int displayIndex, io_service_t proxy) {
    printTopologyDisplay(displayIndex);
    printf("=== DCPAVServiceProxy ===\n");
    printRegistryIdentity(proxy);
    printTransportProperties(proxy);

    io_registry_entry_t epicParent = MACH_PORT_NULL;
    if (IORegistryEntryGetParentEntry(proxy, kIOServicePlane, &epicParent) != KERN_SUCCESS) {
        printf("=== Immediate EPIC Parent ===\n<unavailable>\n");
        return EXIT_FAILURE;
    }
    printf("=== Immediate EPIC Parent ===\n");
    printRegistryIdentity(epicParent);
    printTransportProperties(epicParent);
    printTransportAncestry(epicParent);
    printRelevantSiblings(epicParent);
    printPS190Entries(epicParent);
    IOObjectRelease(epicParent);
    return EXIT_SUCCESS;
}

/** Lists externally connected online displays and whether each resolves to an IOAV service. */
static int listDisplays(void) {
    CGDirectDisplayID displays[16] = {};
    CGDisplayCount displayCount = 0;
    if (CGGetOnlineDisplayList(16, displays, &displayCount) != kCGErrorSuccess) return EXIT_FAILURE;

    bool foundExternalDisplay = false;
    for (CGDisplayCount index = 0; index < displayCount; ++index) {
        if (CGDisplayIsBuiltin(displays[index])) continue;
        foundExternalDisplay = true;
        char productName[128];
        productNameForDisplay(displays[index], productName);
        IOAVServiceRef service = resolveDisplayService((unsigned int)(index + 1), NULL);
        printf("[%u] %s; CG display ID: %u; IOAV/DCP service: %s\n", (unsigned int)(index + 1),
               productName, displays[index], service == NULL ? "not resolved" : "resolved");
        if (service != NULL) CFRelease(service);
    }
    if (!foundExternalDisplay) printf("No external displays found.\n");
    return EXIT_SUCCESS;
}

/** Constructs the four-byte DDC/CI Get VCP packet without depending on external code. */
static void buildGetVCPRequest(uint8_t vcpCode, uint8_t request[4]) {
    request[0] = 0x82;
    request[1] = 0x01;
    request[2] = vcpCode;
    request[3] = 0x6e ^ request[0] ^ request[1] ^ request[2];
}

/** Prints bytes exactly as supplied by the IOAV read/write call. */
static void printBytes(const char *label, const uint8_t *bytes, uint32_t length) {
    printf("%s (%u bytes):", label, length);
    for (uint32_t index = 0; index < length; ++index) printf(" %02x", bytes[index]);
    putchar('\n');
}

static const char *experimentResultString(ExperimentResult result) {
    switch (result) {
        case EXPERIMENT_VALID_DDC_REPLY: return "VALID_DDC_REPLY";
        case EXPERIMENT_IO_ERROR: return "IO_ERROR";
        case EXPERIMENT_INVALID_DDC_REPLY: return "INVALID_DDC_REPLY";
        case EXPERIMENT_MEMORY_ANOMALY: return "MEMORY_ANOMALY";
    }
    return "UNKNOWN";
}

/** Performs one raw IOAV read after resetting the complete caller-owned reply buffer to 0xcc. */
static IOReturn performRead(IOAVServiceRef service, uint32_t chipAddress, uint32_t dataAddress,
                            uint32_t replyLength, unsigned int readNumber,
                            uint8_t reply[DDC_REPLY_CAPACITY]) {
    memset(reply, 0xcc, DDC_REPLY_CAPACITY);
    printf("read %u: chip=0x%02x data=0x%02x length=%u\n", readNumber, chipAddress,
           dataAddress, replyLength);
    IOReturn result = IOAVServiceReadI2C(service, chipAddress, dataAddress, reply, replyLength);
    printf("read %u IOReturn: 0x%08x (%s)\n", readNumber, (unsigned int)result,
           mach_error_string(result));
    printBytes("raw reply", reply, replyLength);
    return result;
}

/** Performs one exactly-once Get VCP write and reports its full IOAV call inputs. */
static IOReturn performWrite(IOAVServiceRef service, uint32_t chipAddress, uint32_t dataAddress, uint8_t vcpCode) {
    uint8_t request[4];
    buildGetVCPRequest(vcpCode, request);
    printf("write: chip=0x%02x data=0x%02x length=4\n", chipAddress, dataAddress);
    printBytes("request", request, sizeof(request));
    IOReturn result = IOAVServiceWriteI2C(service, chipAddress, dataAddress,
                                          request, sizeof(request));
    printf("write IOReturn: 0x%08x (%s)\n", (unsigned int)result, mach_error_string(result));
    return result;
}

/** Runs ten DDC reads without calling IOAVServiceWriteI2C at any point. */
static int runReadOnlyExperiment(IOAVServiceRef service) {
    const unsigned int readCount = 10;
    uint8_t reply[DDC_REPLY_CAPACITY];

    printf("read-only mode: no IOAVServiceWriteI2C call will be made\n");
    for (unsigned int read = 1; read <= readCount; ++read) {
        if (read > 1) {
            printf("delay: 50000 us\n");
            usleep(50000);
        }
        if (performRead(service, DDC_CHIP_ADDRESS, DDC_DATA_ADDRESS, 11, read, reply) != KERN_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

/** Checks only the eight-byte EDID header; this lab intentionally does not parse EDID fields. */
static bool hasEDIDHeader(const uint8_t *reply) {
    static const uint8_t header[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    return memcmp(reply, header, sizeof(header)) == 0;
}

/** Returns whether a 128-byte EDID block has the standard modulo-256 checksum. */
static bool hasValidEDIDChecksum(const uint8_t *reply) {
    uint8_t sum = 0;
    for (size_t index = 0; index < 128; ++index) sum = (uint8_t)(sum + reply[index]);
    return sum == 0;
}

/** Runs one independent raw EDID read without sending a DDC/CI request first. */
static int runEDIDExperiment(IOAVServiceRef service) {
    uint8_t reply[DDC_REPLY_CAPACITY];

    printf("edid mode: no IOAVServiceWriteI2C call will be made\n");
    IOReturn result = performRead(service, 0x50, 0x00, 128, 1, reply);
    if (result != KERN_SUCCESS) return EXIT_FAILURE;
    printf("EDID header valid: %s\n", hasEDIDHeader(reply) ? "yes" : "no");
    printf("EDID checksum valid: %s\n", hasValidEDIDChecksum(reply) ? "yes" : "no");
    return EXIT_SUCCESS;
}

/** Executes the one guarded built-in-MCDP Get VCP experiment without changing normal modes. */
static int runMCDPExperiment(IOAVServiceRef service, const LabOptions *options,
                             const MCDPTopology *topology) {
    if (!topology->mcdpDetected) {
        fprintf(stderr, "MCDP mode refused: EPICProviderClass is not AppleDCPMCDP29XX; no 0xb7 I2C traffic was sent.\n");
        return EXIT_FAILURE;
    }

    uint8_t reply[DDC_REPLY_CAPACITY];
    printf("mcdp mode: fixed IOAV tuples write=(chip=0xb7, data=0x51, length=4), "
           "read=(chip=0xb7, data=0x51, length=11)\n");
    if (performWrite(service, MCDP_DDC_CHIP_ADDRESS, DDC_DATA_ADDRESS, options->vcpCode) != KERN_SUCCESS) {
        return EXIT_FAILURE;
    }
    printf("delay: 50000 us\n");
    usleep(50000);
    return performRead(service, MCDP_DDC_CHIP_ADDRESS, DDC_DATA_ADDRESS, 11, 1, reply) == KERN_SUCCESS
        ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** Performs the single authorized PS190 Service-path Get VCP sentinel experiment. */
static int runSentinelVCPExperiment(IOAVServiceRef service, const LabOptions *options,
                                    const MCDPTopology *topology) {
    const uint32_t chipAddress = DDC_CHIP_ADDRESS;
    const uint32_t writeDataAddress = DDC_DATA_ADDRESS;
    const uint32_t readDataAddress = DDC_NO_OFFSET;
    const uint8_t vcpCode = 0x10;
    if (options->displayIndex != 1 || strcmp(topology->epicProviderClass, "AppleDCPPS190") != 0 ||
        strcmp(topology->productName, "Odyssey G75F") != 0 || strcmp(topology->branchDeviceID, "pHDMIg") != 0 ||
        !topology->proxyExternal || !topology->serviceInterfaceSupported || !topology->matchingBranchDeviceProxyFound) {
        fprintf(stderr, "sentinel-vcp refused: PS190 display/service correlation is incomplete; no I2C traffic was sent.\n");
        return EXIT_FAILURE;
    }

    uint8_t request[DDC_GET_VCP_REQUEST_SIZE] = {};
    buildDDCGetVCPRequest(vcpCode, request);
    printf("=== Single PS190 Service-Path DDC/CI Get VCP ===\n");
    printf("write tuple: chip=0x%02x data=0x%02x length=%u\n", chipAddress, writeDataAddress,
           DDC_GET_VCP_REQUEST_SIZE);
    printBytes("request", request, sizeof(request));
    IOReturn writeResult = IOAVServiceWriteI2C(service, chipAddress, writeDataAddress, request, sizeof(request));
    printf("write IOReturn: 0x%08x (%s)\n", (unsigned int)writeResult, mach_error_string(writeResult));
    if (writeResult != kIOReturnSuccess) {
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_IO_ERROR));
        return EXIT_FAILURE;
    }

    printf("post-write delay: 50 ms\n");
    usleep(50000);

    IOAVGuardedBuffer guarded = {};
    if (!ioavGuardedBufferCreate(&guarded, DDC_GET_VCP_REPLY_SIZE)) {
        printf("guarded response allocation: failed\n");
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }
    bool canariesBefore = ioavGuardedBufferCanariesIntact(&guarded);
    printf("read tuple: chip=0x%02x data=0x%08x length=%zu\n", chipAddress, readDataAddress, guarded.byteCount);
    printBytes("buffer before read", guarded.buffer, (uint32_t)guarded.byteCount);
    printf("canaries before read: %s\n", canariesBefore ? "intact" : "CHANGED");
    if (!canariesBefore) {
        ioavGuardedBufferDestroy(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }

    IOReturn readResult = IOAVServiceReadI2C(service, chipAddress, readDataAddress, guarded.buffer,
                                              (uint32_t)guarded.byteCount);
    printf("read IOReturn: 0x%08x (%s)\n", (unsigned int)readResult, mach_error_string(readResult));
    printBytes("reply", guarded.buffer, (uint32_t)guarded.byteCount);
    bool canariesAfter = ioavGuardedBufferCanariesIntact(&guarded);
    printf("canaries after read: %s\n", canariesAfter ? "intact" : "CHANGED");
    if (!canariesAfter) {
        ioavGuardedBufferDestroy(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }

    DDCGetVCPResponse response = {};
    DDCGetVCPParseError parserResult = parseDDCGetVCPReply(guarded.buffer, guarded.byteCount, vcpCode, &response);
    printf("strict parser: %s\n", ddcGetVCPParseErrorString(parserResult));
    printf("checksum: received=0x%02x calculated=0x%02x (%s)\n", response.receivedChecksum,
           response.calculatedChecksum, response.checksumValid ? "valid" : "invalid");
    ExperimentResult result = readResult != kIOReturnSuccess ? EXPERIMENT_IO_ERROR :
        parserResult == DDC_GET_VCP_PARSE_OK ? EXPERIMENT_VALID_DDC_REPLY : EXPERIMENT_INVALID_DDC_REPLY;
    if (result == EXPERIMENT_VALID_DDC_REPLY) {
        printf("returned VCP: 0x%02x\n", response.vcpCode);
        printf("maximum value: %u\n", response.maximumValue);
        printf("current value: %u\n", response.currentValue);
    }
    ioavGuardedBufferDestroy(&guarded);
    printf("final classification: %s\n", experimentResultString(result));
    return result == EXPERIMENT_VALID_DDC_REPLY ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** Prints one explicit service-path safety predicate before any IOAVService construction. */
static void printServiceSafetyPredicate(const char *name, const char *actual, const char *expected, bool passed) {
    printf("%s:\n  actual: %s\n  expected: %s\n  result: %s\n", name, actual, expected,
           passed ? "PASS" : "FAIL");
}

/** Expands the complete service raw-read safety gate using CoreGraphics and registry state only. */
static bool serviceRawSafetyGate(const LabOptions *options, const MCDPTopology *topology) {
    CGDirectDisplayID displayID = 0;
    char displayProduct[128] = "<unavailable>";
    bool displayOnline = displayIDForIndex(options->displayIndex, &displayID);
    if (displayOnline) productNameForDisplay(displayID, displayProduct);

    IOAVPS190SelectedDisplayIdentity identity = {
        .displayIndex = options->displayIndex,
        .displayOnline = displayOnline,
        .proxyExternal = topology->proxyExternal,
        .proxyUnitKnown = topology->proxyUnitKnown,
        .proxyUnit = topology->proxyUnit,
        .serviceInterfaceSupported = topology->serviceInterfaceSupported,
        .selectedDisplayServiceProxyCount = topology->matchingServiceProxyCount,
        .selectedTransportActive = topology->activeTransportFound,
        .selectedDisplayTransportCount = topology->matchingSelectedTransportCount,
        .globalActiveDisplayPortTransportCount = topology->globalActiveDisplayPortTransportCount,
    };
    snprintf(identity.displayProduct, sizeof(identity.displayProduct), "%s", displayProduct);
    snprintf(identity.epicName, sizeof(identity.epicName), "%s", topology->epicName);
    snprintf(identity.epicRole, sizeof(identity.epicRole), "%s", topology->epicRole);
    snprintf(identity.epicProviderClass, sizeof(identity.epicProviderClass), "%s", topology->epicProviderClass);
    snprintf(identity.proxyPath, sizeof(identity.proxyPath), "%s", topology->proxyPath);
    snprintf(identity.transportClass, sizeof(identity.transportClass), "%s", topology->transportClass);
    snprintf(identity.transportPath, sizeof(identity.transportPath), "%s", topology->transportPath);
    snprintf(identity.branchDeviceID, sizeof(identity.branchDeviceID), "%s", topology->branchDeviceID);

    IOAVPS190SafetyGateEvaluation evaluation = {};
    ioavEvaluatePS190SelectedDisplayGate(&identity, &evaluation);

    char indexText[32] = {};
    char displayText[64] = {};
    char unitText[32] = {};
    char serviceCountText[32] = {};
    char transportCountText[64] = {};
    char roleExpected[128] = {};
    snprintf(indexText, sizeof(indexText), "%u", options->displayIndex);
    snprintf(displayText, sizeof(displayText), "%s (CG display ID %u)", displayOnline ? "true" : "false", displayID);
    snprintf(unitText, sizeof(unitText), "%lld", (long long)topology->proxyUnit);
    snprintf(serviceCountText, sizeof(serviceCountText), "%u", topology->matchingServiceProxyCount);
    snprintf(transportCountText, sizeof(transportCountText), "%u selected / %u global active",
             topology->matchingSelectedTransportCount, topology->globalActiveDisplayPortTransportCount);
    snprintf(roleExpected, sizeof(roleExpected), "resolved DCPEXT* in selected proxy path");

    printf("=== Service Raw-Read Safety Predicates ===\n");
    printServiceSafetyPredicate("display_index", indexText, "1", evaluation.displayIndex);
    printServiceSafetyPredicate("display_online", displayText, "true", evaluation.displayOnline);
    printServiceSafetyPredicate("product", displayProduct, "Odyssey G75F", evaluation.product);
    printServiceSafetyPredicate("unique_selected_display_transport", transportCountText, "1 selected-display transport",
                                evaluation.uniqueSelectedTransport);
    printServiceSafetyPredicate("transport_active", topology->activeTransportFound ? "true" : "false", "true",
                                evaluation.transportActive);
    printServiceSafetyPredicate("transport_class", topology->transportClass[0] ? topology->transportClass : "<unavailable>",
                                "IOPortTransportStateDisplayPort", evaluation.transportClass);
    printServiceSafetyPredicate("transport_port", topology->transportPath[0] ? topology->transportPath : "<unavailable>",
                                "path contains Port-HDMI@1", evaluation.transportPort);
    printServiceSafetyPredicate("branch_device_id", topology->branchDeviceID[0] ? topology->branchDeviceID : "<unavailable>",
                                "pHDMIg", evaluation.branch);
    printServiceSafetyPredicate("service_location", topology->proxyExternal ? "External" : "<missing/non-External>",
                                "External", evaluation.serviceLocation);
    printServiceSafetyPredicate("service_unit", topology->proxyUnitKnown ? unitText : "<unavailable>", "0",
                                evaluation.serviceUnit);
    printServiceSafetyPredicate("service_epic_name", topology->epicName[0] ? topology->epicName : "<unavailable>",
                                "dcpav-service-epic", evaluation.serviceEpicName);
    printServiceSafetyPredicate("service_role", topology->epicRole[0] ? topology->epicRole : "<unavailable>",
                                roleExpected, evaluation.serviceRole);
    printServiceSafetyPredicate("service_provider", topology->epicProviderClass[0] ? topology->epicProviderClass : "<unavailable>",
                                "AppleDCPPS190", evaluation.serviceProvider);
    printServiceSafetyPredicate("service_ui_supported", topology->serviceInterfaceSupported ? "true" : "false", "true",
                                evaluation.serviceUI);
    printServiceSafetyPredicate("unique_selected_display_service", serviceCountText, "1",
                                evaluation.uniqueSelectedService);
    printf("FIRST FAILED PREDICATE: %s\n", evaluation.firstFailedPredicate);
    printf("service raw-read safety gate: %s\n", evaluation.passed ? "PASS" : "FAIL");
    return evaluation.passed;
}

/** Constructs and type-validates a retained service only after all registry safety predicates pass. */
static IOAVServiceRef createValidatedService(io_service_t proxy) {
    printf("=== IOAVService Construction ===\n");
    printf("call: IOAVServiceCreateWithService(kCFAllocatorDefault, selectedDCPAVServiceProxy)\n");
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, proxy);
    printf("IOAVServiceCreateWithService: %p\n", service);
    if (service == NULL) return NULL;
    CFTypeID actualType = CFGetTypeID(service);
    CFTypeID expectedType = IOAVServiceGetTypeID();
    bool typeMatches = actualType == expectedType;
    printf("CFGetTypeID: 0x%lx\n", (unsigned long)actualType);
    printf("IOAVServiceGetTypeID: 0x%lx\n", (unsigned long)expectedType);
    printf("type match: %s\n", typeMatches ? "yes" : "no");
    printf("ownership: CreateWithService returned a retained CF object; caller releases it with CFRelease.\n");
    if (!typeMatches) {
        CFRelease(service);
        return NULL;
    }
    return service;
}

/** Performs exactly one service-path Get VCP request and one UINT32_MAX raw reply read. */
static int runServiceRawVCPExperiment(IOAVServiceRef service) {
    const uint32_t chipAddress = 0x37;
    const uint32_t writeDataAddress = 0x51;
    const uint32_t readDataAddress = UINT32_MAX;
    const uint8_t vcpCode = 0x10;
    uint8_t request[4] = {0x82, 0x01, 0x10, 0xfd};
    IOAVGuardedBuffer guarded = {};
    if (!ioavGuardedBufferCreate(&guarded, DDC_GET_VCP_REPLY_SIZE)) {
        printf("guarded response allocation: failed\nfinal classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }
    bool canariesBefore = ioavGuardedBufferCanariesIntact(&guarded);
    printf("=== One Service-Level DDC/CI Get VCP Raw Read ===\n");
    printf("write tuple: chip=0x%02x data=0x%02x length=4\n", chipAddress, writeDataAddress);
    printBytes("request", request, sizeof(request));
    printf("read tuple: chip=0x%02x data=0x%08x length=11\n", chipAddress, readDataAddress);
    printBytes("buffer before read", guarded.buffer, (uint32_t)guarded.byteCount);
    printf("canaries before read: %s\n", canariesBefore ? "intact" : "CHANGED");
    if (!canariesBefore) {
        ioavGuardedBufferDestroy(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }
    IOReturn writeResult = IOAVServiceWriteI2C(service, chipAddress, writeDataAddress, request, sizeof(request));
    printf("write IOReturn: 0x%08x (%s)\n", (unsigned int)writeResult, mach_error_string(writeResult));
    if (writeResult != kIOReturnSuccess) {
        ioavGuardedBufferDestroy(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_IO_ERROR));
        return EXIT_FAILURE;
    }
    printf("post-write delay: 50 ms\n");
    usleep(50000);
    IOReturn readResult = IOAVServiceReadI2C(service, chipAddress, readDataAddress, guarded.buffer,
                                              (uint32_t)guarded.byteCount);
    printf("read IOReturn: 0x%08x (%s)\n", (unsigned int)readResult, mach_error_string(readResult));
    printBytes("reply", guarded.buffer, (uint32_t)guarded.byteCount);
    bool canariesAfter = ioavGuardedBufferCanariesIntact(&guarded);
    printf("canaries after read: %s\n", canariesAfter ? "intact" : "CHANGED");
    if (!canariesAfter) {
        ioavGuardedBufferDestroy(&guarded);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }
    DDCGetVCPResponse response = {};
    DDCGetVCPParseError parserResult = parseDDCGetVCPReply(guarded.buffer, guarded.byteCount, vcpCode, &response);
    printf("strict parser: %s\n", ddcGetVCPParseErrorString(parserResult));
    printf("checksum: received=0x%02x calculated=0x%02x (%s)\n", response.receivedChecksum,
           response.calculatedChecksum, response.checksumValid ? "valid" : "invalid");
    ExperimentResult result = readResult != kIOReturnSuccess ? EXPERIMENT_IO_ERROR :
        parserResult == DDC_GET_VCP_PARSE_OK ? EXPERIMENT_VALID_DDC_REPLY : EXPERIMENT_INVALID_DDC_REPLY;
    if (result == EXPERIMENT_VALID_DDC_REPLY) {
        printf("returned VCP: 0x%02x\nmaximum value: %u\ncurrent value: %u\n", response.vcpCode,
               response.maximumValue, response.currentValue);
    }
    ioavGuardedBufferDestroy(&guarded);
    printf("final classification: %s\n", experimentResultString(result));
    return result == EXPERIMENT_VALID_DDC_REPLY ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** Reports how many reply bytes still equal the pre-read sentinel value. */
static void printSentinelResidueReport(const uint8_t *reply, size_t length) {
    const uint8_t sentinel = 0xcc;
    size_t residueCount = ioavCountSentinelBytes(reply, length, sentinel);
    printf("reply sentinel residue count: %zu\n", residueCount);
    if (residueCount == 0) return;

    uint8_t indices[DDC_GET_VCP_REPLY_SIZE];
    size_t collected = ioavCollectSentinelByteIndices(reply, length, sentinel, indices, sizeof(indices));
    printf("reply sentinel residue indices:");
    for (size_t index = 0; index < collected && index < sizeof(indices); ++index) {
        printf(" %u", indices[index]);
    }
    if (collected > sizeof(indices)) printf(" ...");
    putchar('\n');
}

/** Prints parser diagnostics without changing parser acceptance rules. */
static void printRawFramedParseReport(uint8_t requestedVcp, DDCGetVCPParseError parserResult,
                                      const DDCGetVCPResponse *response) {
    bool requestMatchValid = response->vcpCode == requestedVcp;

    printf("parsed result: %s\n", ddcGetVCPParseErrorString(parserResult));
    printf("echoed VCP: 0x%02x\n", response->vcpCode);
    printf("byte[5] / vcpType: 0x%02x\n", response->vcpType);
    printf("maximum: %u\n", response->maximumValue);
    printf("current: %u\n", response->currentValue);
    printf("checksum valid: %s\n", response->checksumValid ? "yes" : "no");
    printf("request-match valid: %s\n", requestMatchValid ? "yes" : "no");
}

/**
 * Performs one or more separately gated raw-framed Get VCP requests with the
 * DDC source address in the raw payload, followed by UINT32_MAX raw reply reads.
 */
static int runServiceRawFramedVCPExperiment(IOAVServiceRef service, const LabOptions *options) {
    const uint32_t chipAddress = DDC_CHIP_ADDRESS;
    const uint32_t writeDataAddress = DDC_NO_OFFSET;
    const uint32_t readDataAddress = DDC_NO_OFFSET;
    const uint8_t vcpCode = options->vcpCode;
    const unsigned int iterationCount = options->reads;
    int exitStatus = EXIT_SUCCESS;

    printf("=== Service-Level Raw-Framed DDC/CI Get VCP ===\n");
    printf("requested VCP: 0x%02x\n", vcpCode);
    printf("iterations: %u\n", iterationCount);
    printf("post-write delay: %u us\n", options->delayMicroseconds);
    printf("write tuple: chip=0x%02x data=0x%08x\n", chipAddress, writeDataAddress);
    printf("read tuple: chip=0x%02x data=0x%08x length=%u\n", chipAddress, readDataAddress,
           DDC_GET_VCP_REPLY_SIZE);
    printf("write data sentinel: UINT32_MAX / 0x%08x (no offset preparation)\n", writeDataAddress);
    printf("read data sentinel: UINT32_MAX / 0x%08x (raw read)\n", readDataAddress);
    printf("DDC destination checksum seed: 0x6e\n");

    for (unsigned int iteration = 1; iteration <= iterationCount; ++iteration) {
        uint8_t requestBytes[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE];
        buildRawFramedDDCGetVCPRequest(vcpCode, requestBytes);
        IOAVGuardedBuffer request = {};
        IOAVGuardedBuffer reply = {};

        printf("--- iteration %u/%u ---\n", iteration, iterationCount);
        printf("requested VCP: 0x%02x\n", vcpCode);

        if (!ioavGuardedBufferCreate(&request, sizeof(requestBytes)) ||
            !ioavGuardedBufferCreate(&reply, DDC_GET_VCP_REPLY_SIZE)) {
            ioavGuardedBufferDestroy(&reply);
            ioavGuardedBufferDestroy(&request);
            printf("guarded buffer allocation: failed\n");
            printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
            exitStatus = EXIT_FAILURE;
            continue;
        }
        memcpy(request.buffer, requestBytes, sizeof(requestBytes));

        bool requestCanariesBefore = ioavGuardedBufferCanariesIntact(&request);
        bool replyCanariesBefore = ioavGuardedBufferCanariesIntact(&reply);
        printBytes("request", request.buffer, (uint32_t)request.byteCount);
        printBytes("buffer before read", reply.buffer, (uint32_t)reply.byteCount);
        if (!requestCanariesBefore || !replyCanariesBefore) {
            ioavGuardedBufferDestroy(&reply);
            ioavGuardedBufferDestroy(&request);
            printf("request canary intact: %s\n", requestCanariesBefore ? "yes" : "no");
            printf("reply canary intact: %s\n", replyCanariesBefore ? "yes" : "no");
            printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
            exitStatus = EXIT_FAILURE;
            continue;
        }

        IOReturn writeResult = IOAVServiceWriteI2C(service, chipAddress, writeDataAddress,
                                                    request.buffer, (uint32_t)request.byteCount);
        printf("write IOReturn: 0x%08x (%s)\n", (unsigned int)writeResult, mach_error_string(writeResult));
        bool requestCanariesAfterWrite = ioavGuardedBufferCanariesIntact(&request);
        bool replyCanariesAfterWrite = ioavGuardedBufferCanariesIntact(&reply);
        if (!requestCanariesAfterWrite || !replyCanariesAfterWrite) {
            printf("request canary intact: %s\n", requestCanariesAfterWrite ? "yes" : "no");
            printf("reply canary intact: %s\n", replyCanariesAfterWrite ? "yes" : "no");
            ioavGuardedBufferDestroy(&reply);
            ioavGuardedBufferDestroy(&request);
            printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
            exitStatus = EXIT_FAILURE;
            continue;
        }
        if (writeResult != kIOReturnSuccess) {
            printf("request canary intact: yes\n");
            printf("reply canary intact: yes\n");
            ioavGuardedBufferDestroy(&reply);
            ioavGuardedBufferDestroy(&request);
            printf("final classification: %s\n", experimentResultString(EXPERIMENT_IO_ERROR));
            exitStatus = EXIT_FAILURE;
            continue;
        }

        printf("post-write delay: %u us\n", options->delayMicroseconds);
        usleep(options->delayMicroseconds);

        IOReturn readResult = IOAVServiceReadI2C(service, chipAddress, readDataAddress, reply.buffer,
                                                  (uint32_t)reply.byteCount);
        printf("read IOReturn: 0x%08x (%s)\n", (unsigned int)readResult, mach_error_string(readResult));
        printBytes("raw reply", reply.buffer, (uint32_t)reply.byteCount);
        bool requestCanariesAfterRead = ioavGuardedBufferCanariesIntact(&request);
        bool replyCanariesAfterRead = ioavGuardedBufferCanariesIntact(&reply);
        printf("request canary intact: %s\n", requestCanariesAfterRead ? "yes" : "no");
        printf("reply canary intact: %s\n", replyCanariesAfterRead ? "yes" : "no");
        printSentinelResidueReport(reply.buffer, reply.byteCount);
        if (!requestCanariesAfterRead || !replyCanariesAfterRead) {
            ioavGuardedBufferDestroy(&reply);
            ioavGuardedBufferDestroy(&request);
            printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
            exitStatus = EXIT_FAILURE;
            continue;
        }

        DDCGetVCPResponse response = {};
        DDCGetVCPParseError parserResult = parseDDCGetVCPReply(reply.buffer, reply.byteCount, vcpCode, &response);
        printRawFramedParseReport(vcpCode, parserResult, &response);
        ExperimentResult result = readResult != kIOReturnSuccess ? EXPERIMENT_IO_ERROR :
            parserResult == DDC_GET_VCP_PARSE_OK ? EXPERIMENT_VALID_DDC_REPLY : EXPERIMENT_INVALID_DDC_REPLY;
        printf("final classification: %s\n", experimentResultString(result));
        if (result != EXPERIMENT_VALID_DDC_REPLY) exitStatus = EXIT_FAILURE;

        ioavGuardedBufferDestroy(&reply);
        ioavGuardedBufferDestroy(&request);
    }

    return exitStatus;
}

/** Performs one separately gated raw-framed Get VCP Input Source request. */
static int runServiceRawFramedInputExperiment(IOAVServiceRef service) {
    const uint32_t chipAddress = DDC_CHIP_ADDRESS;
    const uint32_t writeDataAddress = DDC_NO_OFFSET;
    const uint32_t readDataAddress = DDC_NO_OFFSET;
    const uint8_t vcpCode = 0x60;
    uint8_t requestBytes[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE];
    buildRawFramedDDCGetVCPRequest(vcpCode, requestBytes);
    IOAVGuardedBuffer request = {};
    IOAVGuardedBuffer reply = {};

    if (!ioavGuardedBufferCreate(&request, sizeof(requestBytes)) ||
        !ioavGuardedBufferCreate(&reply, DDC_GET_VCP_REPLY_SIZE)) {
        ioavGuardedBufferDestroy(&reply);
        ioavGuardedBufferDestroy(&request);
        printf("guarded buffer allocation: failed\nfinal classification: %s\n",
               experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }
    memcpy(request.buffer, requestBytes, sizeof(requestBytes));
    bool requestCanariesBefore = ioavGuardedBufferCanariesIntact(&request);
    bool replyCanariesBefore = ioavGuardedBufferCanariesIntact(&reply);
    printf("=== One Service-Level Raw-Framed DDC/CI Get Input ===\n");
    printf("write tuple: chip=0x%02x data=0x%08x length=%zu\n", chipAddress, writeDataAddress,
           request.byteCount);
    printf("write data sentinel: UINT32_MAX / 0x%08x (no offset preparation)\n", writeDataAddress);
    printf("DDC destination checksum seed: 0x6e\n");
    printBytes("raw message bytes covered", requestBytes, (uint32_t)(sizeof(requestBytes) - 1));
    printf("calculated request checksum: %02x\n", requestBytes[4]);
    printBytes("request", request.buffer, (uint32_t)request.byteCount);
    printf("write canaries before call: %s\n", requestCanariesBefore ? "intact" : "CHANGED");
    printf("read tuple: chip=0x%02x data=0x%08x length=%zu\n", chipAddress, readDataAddress,
           reply.byteCount);
    printf("read data sentinel: UINT32_MAX / 0x%08x (raw read)\n", readDataAddress);
    printBytes("buffer before read", reply.buffer, (uint32_t)reply.byteCount);
    printf("read canaries before call: %s\n", replyCanariesBefore ? "intact" : "CHANGED");
    if (!requestCanariesBefore || !replyCanariesBefore) {
        ioavGuardedBufferDestroy(&reply);
        ioavGuardedBufferDestroy(&request);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }

    IOReturn writeResult = IOAVServiceWriteI2C(service, chipAddress, writeDataAddress,
                                                request.buffer, (uint32_t)request.byteCount);
    printf("write IOReturn: 0x%08x (%s)\n", (unsigned int)writeResult, mach_error_string(writeResult));
    bool requestCanariesAfter = ioavGuardedBufferCanariesIntact(&request);
    bool replyCanariesAfterWrite = ioavGuardedBufferCanariesIntact(&reply);
    printf("write canaries after call: %s\n", requestCanariesAfter ? "intact" : "CHANGED");
    printf("read canaries before read: %s\n", replyCanariesAfterWrite ? "intact" : "CHANGED");
    if (!requestCanariesAfter || !replyCanariesAfterWrite) {
        ioavGuardedBufferDestroy(&reply);
        ioavGuardedBufferDestroy(&request);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }
    if (writeResult != kIOReturnSuccess) {
        ioavGuardedBufferDestroy(&reply);
        ioavGuardedBufferDestroy(&request);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_IO_ERROR));
        return EXIT_FAILURE;
    }

    printf("post-write delay: 50 ms\n");
    usleep(50000);

    IOReturn readResult = IOAVServiceReadI2C(service, chipAddress, readDataAddress, reply.buffer,
                                              (uint32_t)reply.byteCount);
    printf("read IOReturn: 0x%08x (%s)\n", (unsigned int)readResult, mach_error_string(readResult));
    printBytes("reply", reply.buffer, (uint32_t)reply.byteCount);
    bool replyCanariesAfter = ioavGuardedBufferCanariesIntact(&reply);
    bool requestCanariesAfterRead = ioavGuardedBufferCanariesIntact(&request);
    printf("read canaries after call: %s\n", replyCanariesAfter ? "intact" : "CHANGED");
    printf("write canaries after read: %s\n", requestCanariesAfterRead ? "intact" : "CHANGED");
    if (!replyCanariesAfter || !requestCanariesAfterRead) {
        ioavGuardedBufferDestroy(&reply);
        ioavGuardedBufferDestroy(&request);
        printf("final classification: %s\n", experimentResultString(EXPERIMENT_MEMORY_ANOMALY));
        return EXIT_FAILURE;
    }

    DDCGetVCPResponse response = {};
    DDCGetVCPParseError parserResult = parseDDCGetVCPReply(reply.buffer, reply.byteCount, vcpCode, &response);
    printf("strict parser: %s\n", ddcGetVCPParseErrorString(parserResult));
    printf("checksum: received=0x%02x calculated=0x%02x (%s)\n", response.receivedChecksum,
           response.calculatedChecksum, response.checksumValid ? "valid" : "invalid");
    ExperimentResult result = readResult != kIOReturnSuccess ? EXPERIMENT_IO_ERROR :
        parserResult == DDC_GET_VCP_PARSE_OK ? EXPERIMENT_VALID_DDC_REPLY : EXPERIMENT_INVALID_DDC_REPLY;
    if (result == EXPERIMENT_VALID_DDC_REPLY) {
        printf("returned VCP: 0x%02x\nmaximum value: %u\ncurrent value: %u\n", response.vcpCode,
               response.maximumValue, response.currentValue);
    }
    ioavGuardedBufferDestroy(&reply);
    ioavGuardedBufferDestroy(&request);
    printf("final classification: %s\n", experimentResultString(result));
    return result == EXPERIMENT_VALID_DDC_REPLY ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** Executes the selected one, stream, or independent-repeat diagnostic sequence. */
static int runExperiment(IOAVServiceRef service, const LabOptions *options, const MCDPTopology *topology) {
    if (options->mode == LAB_MODE_READ_ONLY) return runReadOnlyExperiment(service);
    if (options->mode == LAB_MODE_EDID) return runEDIDExperiment(service);
    if (options->mode == LAB_MODE_TOPOLOGY) return EXIT_SUCCESS;
    if (options->mode == LAB_MODE_MCDP) return runMCDPExperiment(service, options, topology);
    if (options->mode == LAB_MODE_SENTINEL_VCP) return runSentinelVCPExperiment(service, options, topology);

    const unsigned int transactionCount = options->mode == LAB_MODE_REPEAT ? options->reads : 1;
    const unsigned int readsPerWrite = options->mode == LAB_MODE_STREAM ? options->reads : 1;
    uint8_t reply[DDC_REPLY_CAPACITY];

    for (unsigned int transaction = 1; transaction <= transactionCount; ++transaction) {
        printf("transaction %u/%u\n", transaction, transactionCount);
        IOReturn writeResult = performWrite(service, DDC_CHIP_ADDRESS, DDC_DATA_ADDRESS, options->vcpCode);
        if (writeResult != KERN_SUCCESS) return EXIT_FAILURE;
        for (unsigned int read = 1; read <= readsPerWrite; ++read) {
            printf("delay: %u us\n", options->delayMicroseconds);
            usleep(options->delayMicroseconds);
            if (performRead(service, DDC_CHIP_ADDRESS, options->readDataAddress,
                            options->replyLength, read, reply) != KERN_SUCCESS) return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printUsage(argv[0]);
        return EXIT_SUCCESS;
    }

    LabOptions options;
    if (!parseOptions(argc, argv, &options)) {
        printUsage(argv[0]);
        return argc > 1 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    if (options.listDisplays) return listDisplays();

    printf("Resolving display index %u...\n", options.displayIndex);
    if (options.mode == LAB_MODE_TOPOLOGY_DETAIL) {
        io_service_t proxy = resolveDisplayProxy(options.displayIndex, NULL);
        if (proxy == MACH_PORT_NULL) {
            fprintf(stderr, "Could not resolve an external DCPAVServiceProxy for display %u.\n", options.displayIndex);
            return EXIT_FAILURE;
        }
        int result = runTopologyDetail(options.displayIndex, proxy);
        IOObjectRelease(proxy);
        return result;
    }

    if (options.mode == LAB_MODE_SERVICE_DDC_VCP_RAW ||
        options.mode == LAB_MODE_SERVICE_DDC_VCP_RAW_FRAMED ||
        options.mode == LAB_MODE_SERVICE_DDC_INPUT_RAW_FRAMED) {
        MCDPTopology topology;
        io_service_t proxy = resolveDisplayProxy(options.displayIndex, &topology);
        if (proxy == MACH_PORT_NULL) {
            fprintf(stderr, "Could not resolve an external DCPAVServiceProxy for display %u. No IOAV or I2C call was made.\n",
                    options.displayIndex);
            return EXIT_FAILURE;
        }
        printMCDPTopology(options.displayIndex, &topology);
        if (!serviceRawSafetyGate(&options, &topology)) {
            IOObjectRelease(proxy);
            return EXIT_FAILURE;
        }
        IOAVServiceRef service = createValidatedService(proxy);
        IOObjectRelease(proxy);
        if (service == NULL) {
            fprintf(stderr, "IOAVService construction/type validation failed; no I2C call was made.\n");
            return EXIT_FAILURE;
        }
        int result = options.mode == LAB_MODE_SERVICE_DDC_VCP_RAW
            ? runServiceRawVCPExperiment(service)
            : options.mode == LAB_MODE_SERVICE_DDC_VCP_RAW_FRAMED
                ? runServiceRawFramedVCPExperiment(service, &options)
                : runServiceRawFramedInputExperiment(service);
        CFRelease(service);
        printf("release: CFRelease completed\n");
        return result;
    }

    MCDPTopology topology;
    IOAVServiceRef service = resolveDisplayService(options.displayIndex, &topology);
    if (service == NULL) {
        fprintf(stderr, "Could not resolve an external IOAVService for display %u.\n", options.displayIndex);
        return EXIT_FAILURE;
    }
    printMCDPTopology(options.displayIndex, &topology);
    int result = runExperiment(service, &options, &topology);
    CFRelease(service);
    return result;
}
