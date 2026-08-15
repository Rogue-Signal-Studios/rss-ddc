#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ddc_parser.h"
#include "ddc_request.h"
#include "ioav_lab_support.h"

static void makeReply(uint8_t *reply, uint8_t vcpCode, uint8_t resultCode, uint8_t vcpType,
                      uint16_t maximumValue, uint16_t currentValue) {
    const uint8_t header[] = {0x6e, 0x88, 0x02};
    for (size_t index = 0; index < sizeof(header); ++index) {
        reply[index] = header[index];
    }
    reply[3] = resultCode;
    reply[4] = vcpCode;
    reply[5] = vcpType;
    reply[6] = (uint8_t)(maximumValue >> 8);
    reply[7] = (uint8_t)maximumValue;
    reply[8] = (uint8_t)(currentValue >> 8);
    reply[9] = (uint8_t)currentValue;
    reply[10] = ddcGetVCPReplyChecksum(reply, DDC_GET_VCP_REPLY_SIZE - 1);
}

static void assertRawFramedRequest(uint8_t vcpCode, uint8_t expectedChecksum) {
    uint8_t request[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE] = {0xff, 0xff, 0xff, 0xff, 0xff};
    buildRawFramedDDCGetVCPRequest(vcpCode, request);
    assert(request[0] == 0x51);
    assert(request[1] == 0x82);
    assert(request[2] == 0x01);
    assert(request[3] == vcpCode);
    assert(request[4] == expectedChecksum);
    assert(ddcRawFramedRequestChecksum(request, sizeof(request) - 1) == expectedChecksum);
}

static void assertIndependentRequestBuilds(void) {
    uint8_t first[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE];
    uint8_t second[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE];

    buildRawFramedDDCGetVCPRequest(0x10, first);
    buildRawFramedDDCGetVCPRequest(0xe2, second);
    assert(first[3] == 0x10 && first[4] == 0xac);
    assert(second[3] == 0xe2 && second[4] == 0x5e);

    buildRawFramedDDCGetVCPRequest(0x60, first);
    buildRawFramedDDCGetVCPRequest(0xfa, second);
    assert(first[3] == 0x60 && first[4] == 0xdc);
    assert(second[3] == 0xfa && second[4] == 0x46);
}

int main(void) {
    const uint8_t expectedRawLuminanceRequest[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE] = {
        0x51, 0x82, 0x01, 0x10, 0xac,
    };
    const uint8_t expectedRawInputRequest[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE] = {
        0x51, 0x82, 0x01, 0x60, 0xdc,
    };
    uint8_t rawRequest[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE] = {0};
    buildRawFramedDDCGetVCPRequest(0x10, rawRequest);
    assert(memcmp(rawRequest, expectedRawLuminanceRequest, sizeof(rawRequest)) == 0);
    buildRawFramedDDCGetVCPRequest(0x60, rawRequest);
    assert(memcmp(rawRequest, expectedRawInputRequest, sizeof(rawRequest)) == 0);

    assertRawFramedRequest(0x10, 0xac);
    assertRawFramedRequest(0x60, 0xdc);
    assertRawFramedRequest(0x61, 0xdd);
    assertRawFramedRequest(0xe2, 0x5e);
    assertRawFramedRequest(0xee, 0x52);
    assertRawFramedRequest(0xef, 0x53);
    assertRawFramedRequest(0xf2, 0x4e);
    assertRawFramedRequest(0xf3, 0x4f);
    assertRawFramedRequest(0xfa, 0x46);
    assertIndependentRequestBuilds();

    uint8_t reply[DDC_GET_VCP_REPLY_SIZE];
    DDCGetVCPResponse parsed;
    const uint8_t validatedLuminanceReply[DDC_GET_VCP_REPLY_SIZE] = {
        0x6e, 0x88, 0x02, 0x00, 0x10, 0x00, 0x00, 0x32, 0x00, 0x32, 0xa4,
    };
    const uint8_t validatedInputReply[DDC_GET_VCP_REPLY_SIZE] = {
        0x6e, 0x88, 0x02, 0x00, 0x60, 0x00, 0x00, 0x12, 0x00, 0x12, 0xd4,
    };
    assert(parseDDCGetVCPReply(validatedLuminanceReply, sizeof(validatedLuminanceReply), 0x10, &parsed) ==
           DDC_GET_VCP_PARSE_OK);
    assert(parsed.maximumValue == 50 && parsed.currentValue == 50 && parsed.vcpType == 0x00);
    assert(parseDDCGetVCPReply(validatedInputReply, sizeof(validatedInputReply), 0x60, &parsed) ==
           DDC_GET_VCP_PARSE_OK);
    assert(parsed.maximumValue == 18 && parsed.currentValue == 18 && parsed.vcpType == 0x00);

    makeReply(reply, 0xe2, 0x00, 0x00, 127, 12800);
    assert(parseDDCGetVCPReply(reply, sizeof(reply), 0xe2, &parsed) == DDC_GET_VCP_PARSE_OK);
    assert(parsed.maximumValue == 127);
    assert(parsed.currentValue == 12800);
    assert(parsed.vcpType == 0x00);

    makeReply(reply, 0xee, 0x00, 0x03, 3, 1734);
    assert(parseDDCGetVCPReply(reply, sizeof(reply), 0xee, &parsed) == DDC_GET_VCP_PARSE_OK);
    assert(parsed.vcpType == 0x03);
    assert(parsed.maximumValue == 3);
    assert(parsed.currentValue == 1734);

    uint8_t mixedReply[DDC_GET_VCP_REPLY_SIZE];
    memset(mixedReply, 0xcc, sizeof(mixedReply));
    mixedReply[0] = 0x6e;
    mixedReply[1] = 0x88;
    mixedReply[4] = 0x10;
    assert(ioavCountSentinelBytes(mixedReply, sizeof(mixedReply), 0xcc) == 8);
    uint8_t indices[DDC_GET_VCP_REPLY_SIZE];
    assert(ioavCollectSentinelByteIndices(mixedReply, sizeof(mixedReply), 0xcc, indices, sizeof(indices)) == 8);
    assert(indices[0] == 2);
    assert(indices[1] == 3);
    assert(indices[2] == 5);

    IOAVGuardedBuffer guarded = {};
    assert(ioavGuardedBufferCreate(&guarded, DDC_GET_VCP_REPLY_SIZE));
    assert(ioavGuardedBufferCanariesIntact(&guarded));
    guarded.buffer[-1] = 0x00;
    assert(!ioavGuardedBufferCanariesIntact(&guarded));
    ioavGuardedBufferDestroy(&guarded);

    assert(ioavGuardedBufferCreate(&guarded, DDC_GET_VCP_REPLY_SIZE));
    guarded.buffer[guarded.byteCount - 1] = 0x00;
    assert(ioavGuardedBufferCanariesIntact(&guarded));
    guarded.buffer[guarded.byteCount] = 0x00;
    assert(!ioavGuardedBufferCanariesIntact(&guarded));
    ioavGuardedBufferDestroy(&guarded);

    IOAVPS190SelectedDisplayIdentity odyssey = {
        .displayIndex = 1,
        .displayOnline = true,
        .proxyExternal = true,
        .proxyUnitKnown = true,
        .proxyUnit = 0,
        .serviceInterfaceSupported = true,
        .selectedDisplayServiceProxyCount = 1,
        .selectedTransportActive = true,
        .selectedDisplayTransportCount = 1,
        .globalActiveDisplayPortTransportCount = 2,
    };
    strcpy(odyssey.displayProduct, "Odyssey G75F");
    strcpy(odyssey.epicName, "dcpav-service-epic");
    strcpy(odyssey.epicRole, "DCPEXT1");
    strcpy(odyssey.epicProviderClass, "AppleDCPPS190");
    strcpy(odyssey.proxyPath,
           "IOService:/AppleARMPE/arm-io@10F00000/AppleH16GFamilyIO/dcpext1@2E00000/"
           "AppleASCWrapV6/iop-dcpext1-nub/RTBuddy(DCPEXT1)/DCPEXT1Endpoint11/"
           "DCPAVServiceProxy");
    strcpy(odyssey.transportClass, "IOPortTransportStateDisplayPort");
    strcpy(odyssey.transportPath, "IOService:/AppleARMPE/arm-io/Port-HDMI@1/DisplayPort");
    strcpy(odyssey.branchDeviceID, "pHDMIg");

    IOAVPS190SafetyGateEvaluation evaluation = {};
    ioavEvaluatePS190SelectedDisplayGate(&odyssey, &evaluation);
    assert(evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "<none>") == 0);

    IOAVPS190SelectedDisplayIdentity twoActive = odyssey;
    twoActive.globalActiveDisplayPortTransportCount = 2;
    twoActive.selectedDisplayTransportCount = 1;
    ioavEvaluatePS190SelectedDisplayGate(&twoActive, &evaluation);
    assert(evaluation.passed);

    IOAVPS190SelectedDisplayIdentity lgAlsoActive = odyssey;
    lgAlsoActive.globalActiveDisplayPortTransportCount = 2;
    ioavEvaluatePS190SelectedDisplayGate(&lgAlsoActive, &evaluation);
    assert(evaluation.passed);
    assert(evaluation.transportPort);
    assert(evaluation.serviceRole);

    IOAVPS190SelectedDisplayIdentity wrongBranch = odyssey;
    strcpy(wrongBranch.branchDeviceID, "Dp1.2");
    ioavEvaluatePS190SelectedDisplayGate(&wrongBranch, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "branch_device_id") == 0);

    IOAVPS190SelectedDisplayIdentity missingBranch = odyssey;
    missingBranch.branchDeviceID[0] = '\0';
    ioavEvaluatePS190SelectedDisplayGate(&missingBranch, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "branch_device_id") == 0);

    IOAVPS190SelectedDisplayIdentity wrongProvider = odyssey;
    strcpy(wrongProvider.epicProviderClass, "AppleDCPMCDP29XX");
    ioavEvaluatePS190SelectedDisplayGate(&wrongProvider, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "service_provider") == 0);

    IOAVPS190SelectedDisplayIdentity wrongPath = odyssey;
    strcpy(wrongPath.transportPath, "IOService:/AppleARMPE/arm-io/Port-USB-C@2/DisplayPort");
    ioavEvaluatePS190SelectedDisplayGate(&wrongPath, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "transport_port") == 0);

    IOAVPS190SelectedDisplayIdentity ambiguousService = odyssey;
    ambiguousService.selectedDisplayServiceProxyCount = 2;
    ioavEvaluatePS190SelectedDisplayGate(&ambiguousService, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "unique_selected_display_service") == 0);

    IOAVPS190SelectedDisplayIdentity ambiguousTransport = odyssey;
    ambiguousTransport.selectedDisplayTransportCount = 2;
    ioavEvaluatePS190SelectedDisplayGate(&ambiguousTransport, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "unique_selected_display_transport") == 0);

    IOAVPS190SelectedDisplayIdentity rolePathMismatch = odyssey;
    strcpy(rolePathMismatch.epicRole, "DCPEXT1");
    strcpy(rolePathMismatch.proxyPath,
           "IOService:/AppleARMPE/arm-io@10F00000/AppleH16GFamilyIO/dcpext0@2E00000/"
           "RTBuddy(DCPEXT0)/DCPEXT0Endpoint11/DCPAVServiceProxy");
    ioavEvaluatePS190SelectedDisplayGate(&rolePathMismatch, &evaluation);
    assert(!evaluation.passed);
    assert(strcmp(evaluation.firstFailedPredicate, "service_role") == 0);

    IOAVPS190SelectedDisplayIdentity dcpext0Mapped = odyssey;
    strcpy(dcpext0Mapped.epicRole, "DCPEXT0");
    strcpy(dcpext0Mapped.proxyPath,
           "IOService:/AppleARMPE/arm-io@10F00000/AppleH16GFamilyIO/dcpext0@2E00000/"
           "RTBuddy(DCPEXT0)/DCPEXT0Endpoint11/DCPAVServiceProxy");
    ioavEvaluatePS190SelectedDisplayGate(&dcpext0Mapped, &evaluation);
    assert(evaluation.passed);

    assert(!ioavRawFramedReadsInBounds(0));
    assert(ioavRawFramedReadsInBounds(1));
    assert(ioavRawFramedReadsInBounds(100));
    assert(!ioavRawFramedReadsInBounds(101));

    puts("test_research_lab_support: passed");
    return 0;
}
