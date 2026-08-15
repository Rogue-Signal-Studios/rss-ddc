#ifndef _IOAV_LAB_SUPPORT_H
#define _IOAV_LAB_SUPPORT_H

/*
 * Research-only helpers migrated from m1ddc-rss
 * (9992bf9255189d8d57a09273d5b3646778de20f5,
 *  c0e695c4c82f482ec647374ee0ed5f68d71485a1).
 * Not part of librss-ddc.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    /** Inclusive lower bound for service-ddc-vcp-raw-framed --reads. */
    IOAV_RAW_FRAMED_READS_MIN = 1,
    /** Inclusive upper bound for service-ddc-vcp-raw-framed --reads. */
    IOAV_RAW_FRAMED_READS_MAX = 100,
};

/** Page-backed buffer with 0xa5/0x5a canaries used by IOAV lab experiments. */
typedef struct {
    void *mapping;
    size_t pageSize;
    size_t byteCount;
    uint8_t *buffer;
} IOAVGuardedBuffer;

/**
 * Allocates an exact payload region with pre/post canaries and guard pages.
 * The payload is prefilled with 0xcc.
 */
bool ioavGuardedBufferCreate(IOAVGuardedBuffer *guarded, size_t byteCount);

/** Releases a guarded buffer created by ioavGuardedBufferCreate. */
void ioavGuardedBufferDestroy(IOAVGuardedBuffer *guarded);

/** Returns whether both 32-byte canary regions remain intact. */
bool ioavGuardedBufferCanariesIntact(const IOAVGuardedBuffer *guarded);

/** Counts payload bytes that still equal `sentinel` after an IOAV read. */
size_t ioavCountSentinelBytes(const uint8_t *buffer, size_t length, uint8_t sentinel);

/**
 * Records up to `maxIndices` byte offsets still equal to `sentinel`.
 * Returns the total number of sentinel bytes present.
 */
size_t ioavCollectSentinelByteIndices(const uint8_t *buffer, size_t length, uint8_t sentinel,
                                      uint8_t *indices, size_t maxIndices);

/**
 * Selected-display identity snapshot used by the PS190 service raw-read gate.
 * Transport counts are selected-display matches, not global unique-active-DP counts.
 */
typedef struct {
    unsigned int displayIndex;
    bool displayOnline;
    char displayProduct[128];
    bool proxyExternal;
    bool proxyUnitKnown;
    int64_t proxyUnit;
    bool serviceInterfaceSupported;
    char epicName[128];
    char epicRole[128];
    char epicProviderClass[128];
    char proxyPath[1024];
    unsigned int selectedDisplayServiceProxyCount;
    bool selectedTransportActive;
    unsigned int selectedDisplayTransportCount;
    unsigned int globalActiveDisplayPortTransportCount;
    char transportClass[128];
    char transportPath[1024];
    char branchDeviceID[128];
} IOAVPS190SelectedDisplayIdentity;

/** Predicate results for the selected-display PS190 safety gate. */
typedef struct {
    bool displayIndex;
    bool displayOnline;
    bool product;
    bool uniqueSelectedTransport;
    bool transportActive;
    bool transportClass;
    bool transportPort;
    bool branch;
    bool serviceLocation;
    bool serviceUnit;
    bool serviceEpicName;
    bool serviceRole;
    bool serviceProvider;
    bool serviceUI;
    bool uniqueSelectedService;
    bool passed;
    const char *firstFailedPredicate;
} IOAVPS190SafetyGateEvaluation;

/**
 * Evaluates whether the selected display maps to the intended Odyssey/PS190
 * service and transport. Does not require a globally unique active DP transport
 * and does not hardcode DCPEXT0.
 */
void ioavEvaluatePS190SelectedDisplayGate(const IOAVPS190SelectedDisplayIdentity *identity,
                                          IOAVPS190SafetyGateEvaluation *evaluation);

/**
 * Returns true when `reads` is inside the validated raw-framed iteration bound
 * (1..100 inclusive). Zero and values above 100 fail closed.
 */
bool ioavRawFramedReadsInBounds(unsigned long reads);

#endif
