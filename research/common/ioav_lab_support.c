#include "ioav_lab_support.h"

#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/** Returns true when a --reads value is inside the validated 1..100 bound. */
bool ioavRawFramedReadsInBounds(unsigned long reads) {
    return reads >= IOAV_RAW_FRAMED_READS_MIN && reads <= IOAV_RAW_FRAMED_READS_MAX;
}

static const char *kPS190ExpectedProduct = "Odyssey G75F";
static const char *kPS190ExpectedProvider = "AppleDCPPS190";
static const char *kPS190ExpectedBranch = "pHDMIg";
static const char *kPS190ExpectedEpicName = "dcpav-service-epic";
static const char *kPS190ExpectedTransportClass = "IOPortTransportStateDisplayPort";
static const char *kPS190ExpectedTransportPort = "Port-HDMI@1";

static bool ioavStringEquals(const char *value, const char *expected) {
    return value != NULL && expected != NULL && strcmp(value, expected) == 0;
}

static bool ioavStringContains(const char *value, const char *needle) {
    return value != NULL && needle != NULL && needle[0] != '\0' && strstr(value, needle) != NULL;
}

/** Accepts the selected display's resolved DCPEXTn role only when it appears in that display's proxy path. */
static bool ioavRoleMatchesSelectedPath(const char *role, const char *proxyPath) {
    return role != NULL && strncmp(role, "DCPEXT", 6) == 0 && role[6] != '\0' &&
        ioavStringContains(proxyPath, role);
}

bool ioavGuardedBufferCreate(IOAVGuardedBuffer *guarded, size_t byteCount) {
    *guarded = (IOAVGuardedBuffer){};
    long systemPageSize = sysconf(_SC_PAGESIZE);
    if (systemPageSize <= 0 || byteCount == 0 || byteCount + 64 > (size_t)systemPageSize) {
        return false;
    }
    guarded->pageSize = (size_t)systemPageSize;
    guarded->mapping = mmap(NULL, guarded->pageSize * 3, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (guarded->mapping == MAP_FAILED) {
        guarded->mapping = NULL;
        return false;
    }
    uint8_t *middle = (uint8_t *)guarded->mapping + guarded->pageSize;
    if (mprotect(middle, guarded->pageSize, PROT_READ | PROT_WRITE) != 0) {
        munmap(guarded->mapping, guarded->pageSize * 3);
        *guarded = (IOAVGuardedBuffer){};
        return false;
    }
    guarded->byteCount = byteCount;
    guarded->buffer = middle + guarded->pageSize - 32 - byteCount;
    memset(guarded->buffer - 32, 0xa5, 32);
    memset(guarded->buffer, 0xcc, byteCount);
    memset(guarded->buffer + byteCount, 0x5a, 32);
    return true;
}

void ioavGuardedBufferDestroy(IOAVGuardedBuffer *guarded) {
    if (guarded->mapping != NULL) {
        munmap(guarded->mapping, guarded->pageSize * 3);
    }
    *guarded = (IOAVGuardedBuffer){};
}

bool ioavGuardedBufferCanariesIntact(const IOAVGuardedBuffer *guarded) {
    for (size_t index = 0; index < 32; ++index) {
        if (guarded->buffer[-32 + (ptrdiff_t)index] != 0xa5 ||
            guarded->buffer[guarded->byteCount + index] != 0x5a) {
            return false;
        }
    }
    return true;
}

size_t ioavCountSentinelBytes(const uint8_t *buffer, size_t length, uint8_t sentinel) {
    size_t count = 0;
    for (size_t index = 0; index < length; ++index) {
        if (buffer[index] == sentinel) {
            ++count;
        }
    }
    return count;
}

size_t ioavCollectSentinelByteIndices(const uint8_t *buffer, size_t length, uint8_t sentinel,
                                      uint8_t *indices, size_t maxIndices) {
    size_t total = 0;
    for (size_t index = 0; index < length; ++index) {
        if (buffer[index] != sentinel) {
            continue;
        }
        ++total;
        if (indices != NULL && total <= maxIndices) {
            indices[total - 1] = (uint8_t)index;
        }
    }
    return total;
}

void ioavEvaluatePS190SelectedDisplayGate(const IOAVPS190SelectedDisplayIdentity *identity,
                                          IOAVPS190SafetyGateEvaluation *evaluation) {
    if (evaluation == NULL) {
        return;
    }
    *evaluation = (IOAVPS190SafetyGateEvaluation){};
    evaluation->firstFailedPredicate = "<none>";
    if (identity == NULL) {
        evaluation->firstFailedPredicate = "identity";
        return;
    }

    evaluation->displayIndex = identity->displayIndex == 1;
    evaluation->displayOnline = identity->displayOnline;
    evaluation->product = ioavStringEquals(identity->displayProduct, kPS190ExpectedProduct);
    evaluation->uniqueSelectedTransport = identity->selectedDisplayTransportCount == 1;
    evaluation->transportActive = identity->selectedTransportActive && identity->selectedDisplayTransportCount == 1;
    evaluation->transportClass = ioavStringEquals(identity->transportClass, kPS190ExpectedTransportClass);
    evaluation->transportPort = ioavStringContains(identity->transportPath, kPS190ExpectedTransportPort);
    evaluation->branch = ioavStringEquals(identity->branchDeviceID, kPS190ExpectedBranch);
    evaluation->serviceLocation = identity->proxyExternal;
    evaluation->serviceUnit = identity->proxyUnitKnown && identity->proxyUnit == 0;
    evaluation->serviceEpicName = ioavStringEquals(identity->epicName, kPS190ExpectedEpicName);
    evaluation->serviceRole = ioavRoleMatchesSelectedPath(identity->epicRole, identity->proxyPath);
    evaluation->serviceProvider = ioavStringEquals(identity->epicProviderClass, kPS190ExpectedProvider);
    evaluation->serviceUI = identity->serviceInterfaceSupported;
    evaluation->uniqueSelectedService = identity->selectedDisplayServiceProxyCount == 1;
    evaluation->passed = evaluation->displayIndex && evaluation->displayOnline && evaluation->product &&
        evaluation->uniqueSelectedTransport && evaluation->transportActive && evaluation->transportClass &&
        evaluation->transportPort && evaluation->branch && evaluation->serviceLocation &&
        evaluation->serviceUnit && evaluation->serviceEpicName && evaluation->serviceRole &&
        evaluation->serviceProvider && evaluation->serviceUI && evaluation->uniqueSelectedService;

    if (!evaluation->displayIndex) evaluation->firstFailedPredicate = "display_index";
    else if (!evaluation->displayOnline) evaluation->firstFailedPredicate = "display_online";
    else if (!evaluation->product) evaluation->firstFailedPredicate = "product";
    else if (!evaluation->uniqueSelectedTransport) evaluation->firstFailedPredicate = "unique_selected_display_transport";
    else if (!evaluation->transportActive) evaluation->firstFailedPredicate = "transport_active";
    else if (!evaluation->transportClass) evaluation->firstFailedPredicate = "transport_class";
    else if (!evaluation->transportPort) evaluation->firstFailedPredicate = "transport_port";
    else if (!evaluation->branch) evaluation->firstFailedPredicate = "branch_device_id";
    else if (!evaluation->serviceLocation) evaluation->firstFailedPredicate = "service_location";
    else if (!evaluation->serviceUnit) evaluation->firstFailedPredicate = "service_unit";
    else if (!evaluation->serviceEpicName) evaluation->firstFailedPredicate = "service_epic_name";
    else if (!evaluation->serviceRole) evaluation->firstFailedPredicate = "service_role";
    else if (!evaluation->serviceProvider) evaluation->firstFailedPredicate = "service_provider";
    else if (!evaluation->serviceUI) evaluation->firstFailedPredicate = "service_ui_supported";
    else if (!evaluation->uniqueSelectedService) evaluation->firstFailedPredicate = "unique_selected_display_service";
}
