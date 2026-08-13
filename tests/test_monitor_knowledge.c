#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

static const char fixture[] =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"ignored\":true,\"identity\":{\"manufacturer\":\"LG\",\"model\":"
    "\"HDR QHD\",\"provider\":\"DCPDP13Service\",\"confidence\":\"observed\","
    "\"evidence\":[{\"type\":\"edid_derived\",\"sourceId\":\"EDID\",\"scope\":"
    "\"virtual\"}]},\"capabilities\":["
    "{\"id\":\"display.brightness\",\"availability\":\"conditional\","
    "\"conditions\":\"legacy HDR note\","
    "\"conditionGroups\":[{\"type\":\"all_of\",\"conditions\":[{\"semanticId\":"
    "\"display.hdr\","
    "\"valueId\":\"off\",\"op\":\"equals\",\"comparison\":{\"type\":"
    "\"unsigned\",\"value\":0},"
    "\"confidence\":\"validated\",\"validation\":\"read_validated\","
    "\"evidence\":[{\"type\":\"stable_get\"}]}]}],"
    "\"advertisedRange\":{\"min\":0,\"max\":100,\"step\":1,\"units\":"
    "\"percent\"},\"confidence\":\"validated\",\"methods\":[{"
    "\"id\":\"vcp-10\",\"type\":\"mccs_vcp\",\"vcpCode\":16,\"readable\":true,"
    "\"writable\":true,\"risk\":\"validate_safe_set\",\"evidence\":[{\"type\":"
    "\"standard_defined\",\"sourceId\":\"MCCS\"}]}],\"values\":[]},"
    "{\"id\":\"inputs.switching\",\"confidence\":\"hardware_validated\","
    "\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"vcp-"
    "60\",\"type\":\"mccs_vcp\",\"vcpCode\":96,\"readable\":true,\"writable\":"
    "false,\"risk\":\"read_standard\"},{\"id\":\"lg-input\",\"type\":\"vendor_"
    "protocol\",\"protocolId\":\"lg-alt\",\"address\":\"input\",\"readable\":"
    "false,\"writable\":true,\"risk\":\"validate_safe_set\"}],\"values\":[{"
    "\"id\":\"hdmi_1\",\"label\":\"HDMI "
    "1\",\"raw\":{\"type\":\"unsigned\",\"value\":17},\"rawAliases\":[{"
    "\"type\":\"string\",\"value\":\"HDMI1\"}],\"readable\":true,"
    "\"writable\":true}] }],"
    "\"inputRoutes\":[{\"id\":\"hdmi-1\",\"connector\":\"hdmi\",\"port\":\"1\","
    "\"switchingSupported\":true,\"currentReadable\":true,\"ddcPathMayChange\":"
    "true,\"readValue\":{\"type\":\"unsigned\",\"value\":17},\"switchValue\":{"
    "\"type\":\"string\",\"value\":\"HDMI1\"}}],"
    "\"relationships\":[{\"sourceId\":\"inputs.switching\",\"targetId\":"
    "\"display.brightness\",\"type\":\"secondary_effect\"}]}";

int main(void) {
  RSSDDCMonitorKnowledge *knowledge = NULL;
  assert(rss_ddc_monitor_knowledge_parse_json(fixture, strlen(fixture),
                                              &knowledge) == RSS_DDC_OK);
  assert(strcmp(rss_ddc_monitor_knowledge_schema_version(knowledge),
                RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA) == 0);
  assert(rss_ddc_monitor_knowledge_capability_count(knowledge) == 2);
  RSSDDCMonitorKnowledgeCapability capability = {};
  assert(rss_ddc_monitor_knowledge_find_capability(
             knowledge, "inputs.switching", &capability) == RSS_DDC_OK);
  assert(capability.method_count == 2 && capability.value_count == 1);
  assert(capability.values[0].raw.type == RSS_DDC_RAW_UNSIGNED &&
         capability.values[0].raw.unsigned_value == 17);
  assert(capability.values[0].raw_alias_count == 1);
  assert(strcmp((const char *)capability.values[0].raw_aliases[0].data,
                "HDMI1") == 0);
  assert(rss_ddc_monitor_knowledge_input_route_count(knowledge) == 1);
  assert(rss_ddc_monitor_knowledge_relationship_count(knowledge) == 1);
  size_t json_size = 0;
  assert(rss_ddc_monitor_knowledge_serialize_json(knowledge, NULL, 0,
                                                  &json_size) == RSS_DDC_OK);
  char json[4096] = {};
  assert(json_size <= sizeof(json));
  assert(rss_ddc_monitor_knowledge_serialize_json(knowledge, json, sizeof(json),
                                                  &json_size) == RSS_DDC_OK);
  RSSDDCMonitorKnowledge *round_trip = NULL;
  RSSDDCError round_trip_error =
      rss_ddc_monitor_knowledge_parse_json(json, strlen(json), &round_trip);
  assert(round_trip_error == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_capability_count(round_trip) == 2);
  assert(strstr(json, "conditionGroups") != NULL);
  assert(strstr(json, "rawAliases") != NULL);
  assert(strstr(json, "inputRoutes") != NULL);
  assert(strstr(json, "relationships") != NULL);
  char canonical[4096] = {};
  size_t canonical_size = 0;
  assert(rss_ddc_monitor_knowledge_serialize_json(
             round_trip, canonical, sizeof(canonical), &canonical_size) ==
         RSS_DDC_OK);
  assert(strcmp(json, canonical) == 0);
  for (size_t iteration = 0; iteration < 64; ++iteration) {
    RSSDDCMonitorKnowledge *repeat = NULL;
    assert(rss_ddc_monitor_knowledge_parse_json(json, strlen(json), &repeat) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_serialize_json(
               repeat, canonical, sizeof(canonical), &canonical_size) ==
           RSS_DDC_OK);
    assert(strcmp(json, canonical) == 0);
    rss_ddc_monitor_knowledge_destroy(repeat);
  }
  rss_ddc_monitor_knowledge_destroy(round_trip);
  RSSDDCMonitorKnowledge *merged = NULL;
  assert(rss_ddc_monitor_knowledge_merge(knowledge, knowledge, &merged) ==
         RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_capability_count(merged) == 2);
  assert(rss_ddc_monitor_knowledge_input_route_count(merged) == 1);
  assert(rss_ddc_monitor_knowledge_relationship_count(merged) == 1);
  assert(rss_ddc_monitor_knowledge_validate(merged) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_serialize_json(merged, NULL, 0,
                                                  &json_size) == RSS_DDC_OK);
  assert(json_size <= sizeof(json));
  assert(rss_ddc_monitor_knowledge_serialize_json(merged, json, sizeof(json),
                                                  &json_size) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_parse_json(json, strlen(json),
                                              &round_trip) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_capability_count(round_trip) == 2);
  assert(rss_ddc_monitor_knowledge_input_route_count(round_trip) == 1);
  assert(rss_ddc_monitor_knowledge_relationship_count(round_trip) == 1);
  rss_ddc_monitor_knowledge_destroy(round_trip);
  rss_ddc_monitor_knowledge_destroy(merged);
  assert(rss_ddc_semantic_registry_lookup_vcp(0x10) != NULL);
  assert(rss_ddc_semantic_registry_lookup("display.picture_mode") == NULL);
  RSSDDCConfidence confidence = RSS_DDC_CONFIDENCE_UNKNOWN;
  assert(rss_ddc_confidence_parse("hardware_validated", &confidence) ==
         RSS_DDC_OK);
  assert(confidence == RSS_DDC_CONFIDENCE_HARDWARE_VALIDATED);
  RSSDDCMonitorKnowledge *bad = NULL;
  const char *wrong_schema = "{\"schemaVersion\":\"monitor-knowledge/v9\"}";
  assert(rss_ddc_monitor_knowledge_parse_json(wrong_schema,
                                              strlen(wrong_schema), &bad) ==
         RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA);
  const char *unsafe =
      "{\"schemaVersion\":\"monitor-knowledge/"
      "v0.1\",\"identity\":{},\"capabilities\":[{\"id\":\"vendor.test\","
      "\"methods\":[{\"id\":\"x\",\"type\":\"vendor_protocol\",\"readable\":"
      "true,\"writable\":true,\"risk\":\"high_risk_denied\"}]}]}";
  assert(rss_ddc_monitor_knowledge_parse_json(unsafe, strlen(unsafe), &bad) ==
         RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED);
  rss_ddc_monitor_knowledge_destroy(knowledge);
  puts("test_monitor_knowledge: passed");
  return 0;
}
