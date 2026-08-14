#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rss_ddc.h"

typedef struct {
  size_t reads;
  size_t mccs_reads;
  size_t writes;
  unsigned int brightness_reads;
  unsigned int contrast_reads;
  RSSDDCError mccs_error;
} MockProbeTransport;

static RSSDDCError mock_get_vcp(void *opaque, uint8_t code,
                                RSSDDCVCPResult *result) {
  MockProbeTransport *mock = opaque;
  ++mock->reads;
  if (code == 0x10) {
    ++mock->brightness_reads;
    *result = (RSSDDCVCPResult){
        .vcp_code = code, .current_value = 42, .maximum_value = 100};
    return RSS_DDC_OK;
  }
  if (code == 0x12) {
    ++mock->contrast_reads;
    *result =
        (RSSDDCVCPResult){.vcp_code = code,
                          .current_value = mock->contrast_reads == 1 ? 50 : 51,
                          .maximum_value = 100};
    return RSS_DDC_OK;
  }
  return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}

static RSSDDCError mock_get_mccs(void *opaque,
                                 RSSDDCMCCSCapabilities *capabilities) {
  MockProbeTransport *mock = opaque;
  ++mock->mccs_reads;
  if (mock->mccs_error != RSS_DDC_OK)
    return mock->mccs_error;
  return rss_ddc_parse_mccs_capabilities(
      "(vcp(10 12 14(01 04)))", strlen("(vcp(10 12 14(01 04)))"), capabilities);
}

/* Probe's system convenience entry point references these public hardware
 * APIs. Unit tests use the injected transport and never call these stubs. */
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display) {
  (void)list_index;
  (void)display;
  return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t code,
                            RSSDDCVCPResult *result) {
  (void)list_index;
  (void)code;
  (void)result;
  return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError
rss_ddc_get_mccs_capabilities(uint32_t list_index,
                              RSSDDCMCCSCapabilities *capabilities) {
  (void)list_index;
  (void)capabilities;
  return RSS_DDC_ERROR_DISCOVERY;
}

static RSSDDCProbeTarget target(bool mccs) {
  RSSDDCProbeTarget value = {};
  value.correlation = RSS_DDC_PROBE_CORRELATION_EXACT;
  value.display = (RSSDDCDisplay){
      .list_index = 7,
      .online = true,
      .external = true,
      .provider = RSS_DDC_PROVIDER_DCPDP13,
      .capabilities = mccs ? RSS_DDC_CAP_MCCS_CAPABILITIES : RSS_DDC_CAP_NONE};
  snprintf(value.display.product_name, sizeof(value.display.product_name), "%s",
           "LG HDR QHD");
  snprintf(value.display.manufacturer, sizeof(value.display.manufacturer), "%s",
           "LG");
  snprintf(value.display.serial, sizeof(value.display.serial), "%s", "TEST");
  snprintf(value.display.transport, sizeof(value.display.transport), "%s",
           "DCPEXT1");
  snprintf(value.display.branch_device_id,
           sizeof(value.display.branch_device_id), "%s", "branch-7");
  return value;
}

static RSSDDCProbe *run_mock_probe(MockProbeTransport *mock, bool mccs) {
  RSSDDCProbeReadTransport transport = {.context = mock,
                                        .get_vcp = mock_get_vcp,
                                        .get_mccs_capabilities = mock_get_mccs};
  RSSDDCProbe *probe = NULL;
  RSSDDCProbeTarget selected = target(mccs);
  assert(rss_ddc_probe_create(&selected, &transport, &probe) == RSS_DDC_OK);
  assert(rss_ddc_probe_quick(probe) == RSS_DDC_OK);
  return probe;
}

int main(void) {
  RSSDDCProbeReadTransport transport = {.get_vcp = mock_get_vcp};
  RSSDDCProbe *probe = NULL;
  RSSDDCProbeTarget ambiguous = target(false);
  ambiguous.correlation = RSS_DDC_PROBE_CORRELATION_AMBIGUOUS;
  assert(rss_ddc_probe_create(&ambiguous, &transport, &probe) ==
         RSS_DDC_ERROR_DISCOVERY);

  MockProbeTransport mock = {};
  RSSDDCProbe *first = run_mock_probe(&mock, true);
  RSSDDCProbeDiagnostics diagnostics = {};
  assert(rss_ddc_probe_diagnostics(first, &diagnostics) == RSS_DDC_OK);
  assert(diagnostics.controls_attempted == 6);
  assert(diagnostics.controls_readable == 2);
  assert(diagnostics.controls_stable == 1);
  assert(diagnostics.controls_variable == 1);
  assert(diagnostics.controls_failed == 4);
  assert(diagnostics.mccs_error == RSS_DDC_OK && mock.mccs_reads == 1);
  assert(mock.reads == 8 && mock.writes == 0);
  assert(diagnostics.controls[0].stable);
  assert(!diagnostics.controls[1].stable);

  const RSSDDCMonitorKnowledge *knowledge = NULL;
  assert(rss_ddc_probe_knowledge(first, &knowledge) == RSS_DDC_OK);
  RSSDDCMonitorKnowledgeCapability brightness = {};
  assert(rss_ddc_monitor_knowledge_find_capability(
             knowledge, "display.brightness", &brightness) == RSS_DDC_OK);
  assert(brightness.confidence == RSS_DDC_CONFIDENCE_OBSERVED);
  assert(brightness.observed_range.present &&
         brightness.observed_range.maximum == 100);
  assert(brightness.method_count == 1 && !brightness.methods[0].writable);
  assert(brightness.values[0].raw.unsigned_value == 42);
  assert(brightness.evidence_count == 2);
  RSSDDCMonitorKnowledgeCapability preset = {};
  assert(rss_ddc_monitor_knowledge_find_capability(
             knowledge, "display.color_preset", &preset) == RSS_DDC_OK);
  assert(preset.value_count == 2);
  size_t first_size = 0;
  assert(rss_ddc_monitor_knowledge_serialize_json(knowledge, NULL, 0,
                                                  &first_size) == RSS_DDC_OK);
  char *first_json = calloc(first_size, 1);
  assert(first_json);
  assert(rss_ddc_monitor_knowledge_serialize_json(
             knowledge, first_json, first_size, &first_size) == RSS_DDC_OK);

  MockProbeTransport repeated = {};
  RSSDDCProbe *second = run_mock_probe(&repeated, true);
  const RSSDDCMonitorKnowledge *second_knowledge = NULL;
  assert(rss_ddc_probe_knowledge(second, &second_knowledge) == RSS_DDC_OK);
  size_t second_size = 0;
  assert(rss_ddc_monitor_knowledge_serialize_json(second_knowledge, NULL, 0,
                                                  &second_size) == RSS_DDC_OK);
  char *second_json = calloc(second_size, 1);
  assert(second_json);
  assert(rss_ddc_monitor_knowledge_serialize_json(second_knowledge, second_json,
                                                  second_size,
                                                  &second_size) == RSS_DDC_OK);
  assert(strcmp(first_json, second_json) == 0);
  const char profile_json[] =
      "{\"schemaVersion\":\"monitor-knowledge/"
      "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"LG HDR "
      "QHD\"},\"capabilities\":[{\"id\":\"display.picture_mode\","
      "\"confidence\":\"validated\",\"evidence\":[{\"type\":\"rogue_validated_"
      "profile\",\"sourceId\":\"builtin-lg\"}],\"methods\":[],\"values\":[]}]}";
  RSSDDCMonitorKnowledge *profile = NULL;
  RSSDDCMonitorKnowledge *composed = NULL;
  assert(rss_ddc_monitor_knowledge_parse_json(
             profile_json, strlen(profile_json), &profile) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_merge(knowledge, profile, &composed) ==
         RSS_DDC_OK);
  RSSDDCMonitorKnowledgeCapability picture_mode = {};
  assert(rss_ddc_monitor_knowledge_find_capability(
             composed, "display.picture_mode", &picture_mode) == RSS_DDC_OK);
  assert(picture_mode.evidence_count == 1 &&
         picture_mode.evidence[0].type ==
             RSS_DDC_EVIDENCE_ROGUE_VALIDATED_PROFILE);
  rss_ddc_monitor_knowledge_destroy(composed);
  rss_ddc_monitor_knowledge_destroy(profile);
  free(first_json);
  free(second_json);
  rss_ddc_probe_destroy(first);
  rss_ddc_probe_destroy(second);

  MockProbeTransport unsupported = {.mccs_error = RSS_DDC_ERROR_READ};
  RSSDDCProbe *without_mccs = run_mock_probe(&unsupported, false);
  assert(rss_ddc_probe_diagnostics(without_mccs, &diagnostics) == RSS_DDC_OK);
  assert(diagnostics.mccs_error == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
  assert(unsupported.mccs_reads == 0 && unsupported.writes == 0);
  rss_ddc_probe_destroy(without_mccs);
  puts("test_probe: passed");
  return 0;
}
