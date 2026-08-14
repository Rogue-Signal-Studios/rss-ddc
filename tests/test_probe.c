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

typedef struct {
  RSSDDCVCPResult replies[6];
  const char *mccs;
  size_t reads;
  size_t mccs_reads;
} LiveShapeTransport;

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

static RSSDDCError live_shape_get_vcp(void *opaque, uint8_t code,
                                      RSSDDCVCPResult *result) {
  LiveShapeTransport *transport = opaque;
  static const uint8_t codes[] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a};
  ++transport->reads;
  for (size_t i = 0; i < sizeof(codes); ++i)
    if (code == codes[i]) {
      *result = transport->replies[i];
      return RSS_DDC_OK;
    }
  return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}

static RSSDDCError live_shape_get_mccs(void *opaque,
                                       RSSDDCMCCSCapabilities *capabilities) {
  LiveShapeTransport *transport = opaque;
  ++transport->mccs_reads;
  return rss_ddc_parse_mccs_capabilities(transport->mccs,
                                         strlen(transport->mccs), capabilities);
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
  snprintf(value.display.edid_manufacturer,
           sizeof(value.display.edid_manufacturer), "%s", "GSM");
  value.display.edid_product_code = 0x1234;
  value.display.edid_product_code_present = true;
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

static RSSDDCProbe *run_live_shape_probe(LiveShapeTransport *transport,
                                         bool mccs) {
  RSSDDCProbeReadTransport read_transport = {
      .context = transport,
      .get_vcp = live_shape_get_vcp,
      .get_mccs_capabilities = mccs ? live_shape_get_mccs : NULL};
  RSSDDCProbe *probe = NULL;
  RSSDDCProbeTarget selected = target(mccs);
  assert(rss_ddc_probe_create(&selected, &read_transport, &probe) == RSS_DDC_OK);
  assert(rss_ddc_probe_quick(probe) == RSS_DDC_OK);
  return probe;
}

static RSSDDCMonitorKnowledgeCapability capability(
    const RSSDDCMonitorKnowledge *knowledge, const char *id) {
  RSSDDCMonitorKnowledgeCapability result = {};
  assert(rss_ddc_monitor_knowledge_find_capability(knowledge, id, &result) ==
         RSS_DDC_OK);
  return result;
}

static void assert_observed_value_and_reported_maximum(
    const RSSDDCMonitorKnowledgeCapability *control, uint64_t current,
    uint64_t maximum) {
  assert(!control->observed_range.present);
  assert(control->reported_maximum_present);
  assert(control->reported_maximum.type == RSS_DDC_RAW_UNSIGNED);
  assert(control->reported_maximum.unsigned_value == maximum);
  assert(control->value_count >= 1);
  assert(control->values[0].raw.type == RSS_DDC_RAW_UNSIGNED);
  assert(control->values[0].raw.unsigned_value == current);
  assert(control->method_count == 1 && !control->methods[0].writable);
}

static void test_live_quick_probe_shapes(void) {
  LiveShapeTransport g75 = {
      .replies = {{0x10, 50, 50}, {0x12, 50, 50}, {0x14, 4, 1},
                  {0x16, 100, 50}, {0x18, 100, 50}, {0x1a, 100, 50}}};
  RSSDDCProbe *g75_probe = run_live_shape_probe(&g75, false);
  const RSSDDCMonitorKnowledge *g75_knowledge = NULL;
  assert(rss_ddc_probe_knowledge(g75_probe, &g75_knowledge) == RSS_DDC_OK);
  RSSDDCMonitorIdentity identity = {};
  assert(rss_ddc_monitor_knowledge_identity(g75_knowledge, &identity) ==
         RSS_DDC_OK);
  assert(strcmp(identity.manufacturer, "LG") == 0 &&
         strcmp(identity.edid_manufacturer, "GSM") == 0 &&
         identity.edid_product_code_present && identity.edid_product_code == 0x1234 &&
         strcmp(identity.serial, "TEST") == 0);
  assert(rss_ddc_monitor_knowledge_capability_count(g75_knowledge) == 6);
  RSSDDCMonitorKnowledgeCapability brightness =
      capability(g75_knowledge, "display.brightness");
  RSSDDCMonitorKnowledgeCapability contrast =
      capability(g75_knowledge, "display.contrast");
  RSSDDCMonitorKnowledgeCapability preset =
      capability(g75_knowledge, "display.color_preset");
  RSSDDCMonitorKnowledgeCapability red =
      capability(g75_knowledge, "display.rgb.red_gain");
  RSSDDCMonitorKnowledgeCapability green =
      capability(g75_knowledge, "display.rgb.green_gain");
  RSSDDCMonitorKnowledgeCapability blue =
      capability(g75_knowledge, "display.rgb.blue_gain");
  assert_observed_value_and_reported_maximum(&brightness, 50, 50);
  assert_observed_value_and_reported_maximum(&contrast, 50, 50);
  assert_observed_value_and_reported_maximum(&preset, 1, 4);
  assert_observed_value_and_reported_maximum(&red, 50, 100);
  assert_observed_value_and_reported_maximum(&green, 50, 100);
  assert_observed_value_and_reported_maximum(&blue, 50, 100);
  assert(rss_ddc_monitor_knowledge_source_count(g75_knowledge) == 0);
  rss_ddc_probe_destroy(g75_probe);

  LiveShapeTransport lg = {
      .replies = {{0x10, 100, 100}, {0x12, 100, 70}, {0x14, 11, 11},
                  {0x16, 100, 50}, {0x18, 100, 50}, {0x1a, 100, 50}},
      .mccs = "vcp(10 12 14(05 08 0b))"};
  RSSDDCProbe *lg_probe = run_live_shape_probe(&lg, true);
  const RSSDDCMonitorKnowledge *lg_knowledge = NULL;
  assert(rss_ddc_probe_knowledge(lg_probe, &lg_knowledge) == RSS_DDC_OK);
  RSSDDCMonitorKnowledgeCapability lg_brightness =
      capability(lg_knowledge, "display.brightness");
  RSSDDCMonitorKnowledgeCapability lg_preset =
      capability(lg_knowledge, "display.color_preset");
  assert_observed_value_and_reported_maximum(&lg_brightness, 100, 100);
  assert_observed_value_and_reported_maximum(&lg_preset, 11, 11);
  assert(lg_preset.value_count == 4);
  assert(lg_preset.values[1].raw.unsigned_value == 5);
  assert(lg_preset.values[2].raw.unsigned_value == 8);
  assert(lg_preset.values[3].raw.unsigned_value == 11);
  assert(rss_ddc_monitor_knowledge_source_count(lg_knowledge) == 1);
  RSSDDCMonitorKnowledgeSource source = {};
  assert(rss_ddc_monitor_knowledge_source(lg_knowledge, 0, &source) ==
         RSS_DDC_OK);
  assert(strcmp(source.id, "mccs-capabilities-1") == 0 &&
         strcmp(source.type, "mccs_capabilities") == 0 &&
         strcmp(source.reference, lg.mccs) == 0);
  rss_ddc_probe_destroy(lg_probe);
}

int main(void) {
  test_live_quick_probe_shapes();
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
  assert_observed_value_and_reported_maximum(&brightness, 42, 100);
  assert(brightness.method_count == 1 && !brightness.methods[0].writable);
  assert(brightness.values[0].raw.unsigned_value == 42);
  assert(brightness.evidence_count == 2);
  RSSDDCMonitorKnowledgeCapability preset = {};
  assert(rss_ddc_monitor_knowledge_find_capability(
             knowledge, "display.color_preset", &preset) == RSS_DDC_OK);
  assert(preset.value_count == 2);
  assert(rss_ddc_monitor_knowledge_source_count(knowledge) == 1);
  size_t first_size = 0;
  assert(rss_ddc_monitor_knowledge_serialize_json(knowledge, NULL, 0,
                                                  &first_size) == RSS_DDC_OK);
  char *first_json = calloc(first_size, 1);
  assert(first_json);
  assert(rss_ddc_monitor_knowledge_serialize_json(
             knowledge, first_json, first_size, &first_size) == RSS_DDC_OK);
  assert(strstr(first_json, "\"observedRange\"") == NULL);
  assert(strstr(first_json, "\"reference\":\"(vcp(10 12 14(01 04)))\"") !=
         NULL);
  assert(strstr(strstr(first_json, "\"reference\":\"(vcp(10 12 14(01 04)))\"") +
                    1,
                "\"reference\":\"(vcp(10 12 14(01 04)))\"") == NULL);
  RSSDDCMonitorKnowledge *round_trip = NULL;
  assert(rss_ddc_monitor_knowledge_parse_json(first_json, strlen(first_json),
                                               &round_trip) == RSS_DDC_OK);
  size_t round_trip_size = 0;
  assert(rss_ddc_monitor_knowledge_serialize_json(round_trip, NULL, 0,
                                                   &round_trip_size) ==
         RSS_DDC_OK);
  char *round_trip_json = calloc(round_trip_size, 1);
  assert(round_trip_json);
  assert(rss_ddc_monitor_knowledge_serialize_json(
             round_trip, round_trip_json, round_trip_size, &round_trip_size) ==
         RSS_DDC_OK);
  assert(strcmp(first_json, round_trip_json) == 0);
  RSSDDCMonitorKnowledgeCapability round_trip_brightness =
      capability(round_trip, "display.brightness");
  assert_observed_value_and_reported_maximum(&round_trip_brightness, 42, 100);
  free(round_trip_json);
  rss_ddc_monitor_knowledge_destroy(round_trip);

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
