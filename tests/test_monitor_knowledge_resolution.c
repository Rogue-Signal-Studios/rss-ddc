#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

static const char *standard =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR "
    "QHD\",\"provider\":\"DCPDP13Service\"},\"capabilities\":[{\"id\":\"inputs."
    "switching\",\"confidence\":\"observed\",\"methods\":[{\"id\":\"vcp-60\","
    "\"type\":\"mccs_vcp\",\"vcpCode\":96,\"readable\":true,\"writable\":false,"
    "\"risk\":\"read_standard\",\"evidence\":[{\"type\":\"mccs_advertised\"}]}]"
    "}]}";
static const char *validated =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR "
    "QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\"},"
    "\"capabilities\":[{\"id\":\"inputs.switching\",\"confidence\":\"hardware_"
    "validated\",\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{"
    "\"id\":\"lg-alt\",\"type\":\"vendor_protocol\",\"protocolId\":\"lg-alt-"
    "input\",\"address\":\"input\",\"readable\":false,\"writable\":true,"
    "\"risk\":\"validate_safe_set\"}]}]}";
static const char *conflict_a =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR "
    "QHD\"},\"capabilities\":[{\"id\":\"display.picture_mode\",\"evidence\":[{"
    "\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"vcp-15\",\"type\":"
    "\"mccs_vcp\",\"vcpCode\":21,\"readable\":true,\"writable\":true,\"risk\":"
    "\"validate_safe_set\"}]}]}";
static const char *conflict_b =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR "
    "QHD\"},\"capabilities\":[{\"id\":\"display.picture_mode\",\"evidence\":[{"
    "\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"vendor-mode\","
    "\"type\":\"vendor_protocol\",\"protocolId\":\"vendor\",\"address\":"
    "\"mode\",\"readable\":false,\"writable\":true,\"risk\":\"validate_safe_"
    "set\"}]}]}";
static const char *external =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR "
    "QHD\"},\"capabilities\":[{\"id\":\"gaming.response_time\",\"methods\":[{"
    "\"id\":\"candidate\",\"type\":\"mccs_vcp\",\"vcpCode\":170,\"readable\":"
    "true,\"writable\":false,\"risk\":\"read_extended\",\"evidence\":[{"
    "\"type\":\"external_candidate\"}]}]}]}";
static const char *values_and_routes =
    "{\"schemaVersion\":\"monitor-knowledge/"
    "v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR "
    "QHD\"},\"capabilities\":["
    "{\"id\":\"display.picture_mode\",\"confidence\":\"hardware_validated\","
    "\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"mode\","
    "\"type\":\"mccs_vcp\",\"vcpCode\":21,\"readable\":true,\"writable\":true,"
    "\"risk\":\"validate_safe_set\"}],\"values\":[{\"id\":\"fps\",\"raw\":{"
    "\"type\":\"unsigned\",\"value\":30},\"readable\":true,\"writable\":true,"
    "\"validation\":\"hardware_validated\"},{\"id\":\"unknown-external\","
    "\"raw\":{\"type\":\"unsigned\",\"value\":99},\"readable\":true,"
    "\"writable\":true}]},"
    "{\"id\":\"display.brightness\",\"advertisedRange\":{\"min\":0,\"max\":100,"
    "\"step\":1,\"units\":\"percent\"},\"observedRange\":{\"min\":5,\"max\":95,"
    "\"step\":1,\"units\":\"percent\"},\"validatedRange\":{\"min\":10,\"max\":"
    "90,\"step\":1,\"units\":\"percent\"},\"methods\":[]},"
    "{\"id\":\"inputs.switching\",\"confidence\":\"hardware_validated\","
    "\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"lg-"
    "alt\",\"type\":\"vendor_protocol\",\"readable\":false,\"writable\":true,"
    "\"risk\":\"validate_safe_set\"}]}],"
    "\"inputRoutes\":[{\"id\":\"hdmi-1\",\"connector\":\"hdmi\",\"port\":\"1\","
    "\"switchingSupported\":true,\"currentReadable\":true,\"switchValue\":{"
    "\"type\":\"unsigned\",\"value\":144},\"confidence\":\"hardware_"
    "validated\",\"evidence\":[{\"type\":\"set_confirmed\"}]}]}";

static RSSDDCMonitorKnowledge *parse(const char *json) {
  RSSDDCMonitorKnowledge *knowledge = NULL;
  assert(rss_ddc_monitor_knowledge_parse_json(json, strlen(json), &knowledge) ==
         RSS_DDC_OK);
  return knowledge;
}

int main(void) {
  RSSDDCMonitorKnowledge *first = parse(standard), *second = parse(validated);
  const RSSDDCMonitorKnowledge *sources[] = {first, second};
  RSSDDCMonitorKnowledgeResolution *resolved = NULL;
  assert(rss_ddc_monitor_knowledge_resolve_capability(
             sources, 2, "inputs.switching", &resolved) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_resolution_method_count(resolved) == 2);
  assert(
      rss_ddc_monitor_knowledge_resolution_preferred_read(resolved)->vcp_code ==
      0x60);
  assert(
      strcmp(rss_ddc_monitor_knowledge_resolution_preferred_write(resolved)->id,
             "lg-alt") == 0);
  assert(rss_ddc_monitor_knowledge_resolution_write_authorized(resolved));
  rss_ddc_monitor_knowledge_resolution_destroy(resolved);

  RSSDDCMonitorKnowledge *rich = parse(values_and_routes);
  const RSSDDCMonitorKnowledge *rich_sources[] = {rich};
  RSSDDCMonitorKnowledgeValueResolution *value = NULL;
  assert(rss_ddc_monitor_knowledge_resolve_value(rich_sources, 1,
                                                 "display.picture_mode", "fps",
                                                 &value) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_value_resolution_write_authorized(value));
  assert(rss_ddc_monitor_knowledge_value_resolution_preferred_write(value)
             ->raw.unsigned_value == 30);
  rss_ddc_monitor_knowledge_value_resolution_destroy(value);
  assert(rss_ddc_monitor_knowledge_resolve_value(
             rich_sources, 1, "display.picture_mode", "unknown-external",
             &value) == RSS_DDC_OK);
  assert(!rss_ddc_monitor_knowledge_value_resolution_write_authorized(value));
  rss_ddc_monitor_knowledge_value_resolution_destroy(value);
  RSSDDCMonitorKnowledgeRangeResolution *range = NULL;
  RSSDDCRange selected = {};
  assert(rss_ddc_monitor_knowledge_resolve_range(
             rich_sources, 1, "display.brightness", &range) == RSS_DDC_OK);
  assert(
      rss_ddc_monitor_knowledge_range_resolution_advertised(range, &selected) &&
      selected.minimum == 0 && selected.maximum == 100);
  assert(rss_ddc_monitor_knowledge_range_resolution_write_range(range,
                                                                &selected) &&
         selected.minimum == 10 && selected.maximum == 90);
  rss_ddc_monitor_knowledge_range_resolution_destroy(range);
  RSSDDCInputRouteResolution *route = NULL;
  assert(rss_ddc_monitor_knowledge_resolve_input_route(
             rich_sources, 1, "hdmi-1", &route) == RSS_DDC_OK);
  assert(rss_ddc_input_route_resolution_switch_authorized(route));
  assert(rss_ddc_input_route_resolution_preferred_switch(route)
             ->switch_value.unsigned_value == 144);
  rss_ddc_input_route_resolution_destroy(route);
  rss_ddc_monitor_knowledge_destroy(rich);
  const RSSDDCMonitorKnowledge *reversed[] = {second, first};
  assert(rss_ddc_monitor_knowledge_resolve_capability(
             reversed, 2, "inputs.switching", &resolved) == RSS_DDC_OK);
  assert(
      rss_ddc_monitor_knowledge_resolution_preferred_read(resolved)->vcp_code ==
      0x60);
  assert(
      strcmp(rss_ddc_monitor_knowledge_resolution_preferred_write(resolved)->id,
             "lg-alt") == 0);
  rss_ddc_monitor_knowledge_resolution_destroy(resolved);

  RSSDDCMonitorKnowledge *third = parse(conflict_a),
                         *fourth = parse(conflict_b);
  const RSSDDCMonitorKnowledge *conflicts[] = {third, fourth};
  assert(rss_ddc_monitor_knowledge_resolve_capability(
             conflicts, 2, "display.picture_mode", &resolved) == RSS_DDC_OK);
  assert(rss_ddc_monitor_knowledge_resolution_has_conflict(resolved));
  assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolved));
  assert(rss_ddc_monitor_knowledge_resolution_reason(resolved) ==
         RSS_DDC_RESOLUTION_REASON_EQUAL_AUTHORITY_CONFLICT);
  rss_ddc_monitor_knowledge_resolution_destroy(resolved);

  RSSDDCMonitorKnowledge *candidate = parse(external);
  const RSSDDCMonitorKnowledge *candidate_source[] = {candidate};
  assert(rss_ddc_monitor_knowledge_resolve_capability(candidate_source, 1,
                                                      "gaming.response_time",
                                                      &resolved) == RSS_DDC_OK);
  assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolved));
  assert(
      rss_ddc_monitor_knowledge_resolution_preferred_read(resolved)->vcp_code ==
      0xaa);
  rss_ddc_monitor_knowledge_resolution_destroy(resolved);

  rss_ddc_monitor_knowledge_destroy(candidate);
  rss_ddc_monitor_knowledge_destroy(fourth);
  rss_ddc_monitor_knowledge_destroy(third);
  rss_ddc_monitor_knowledge_destroy(second);
  rss_ddc_monitor_knowledge_destroy(first);
  puts("test_monitor_knowledge_resolution: passed");
  return 0;
}
