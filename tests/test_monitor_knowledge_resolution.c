#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

static const char *standard =
    "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR QHD\",\"provider\":\"DCPDP13Service\"},\"capabilities\":[{\"id\":\"inputs.switching\",\"confidence\":\"observed\",\"methods\":[{\"id\":\"vcp-60\",\"type\":\"mccs_vcp\",\"vcpCode\":96,\"readable\":true,\"writable\":false,\"risk\":\"read_standard\",\"evidence\":[{\"type\":\"mccs_advertised\"}]}]}]}";
static const char *validated =
    "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\"},\"capabilities\":[{\"id\":\"inputs.switching\",\"confidence\":\"hardware_validated\",\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"lg-alt\",\"type\":\"vendor_protocol\",\"protocolId\":\"lg-alt-input\",\"address\":\"input\",\"readable\":false,\"writable\":true,\"risk\":\"validate_safe_set\"}]}]}";
static const char *conflict_a =
    "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR QHD\"},\"capabilities\":[{\"id\":\"display.picture_mode\",\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"vcp-15\",\"type\":\"mccs_vcp\",\"vcpCode\":21,\"readable\":true,\"writable\":true,\"risk\":\"validate_safe_set\"}]}]}";
static const char *conflict_b =
    "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR QHD\"},\"capabilities\":[{\"id\":\"display.picture_mode\",\"evidence\":[{\"type\":\"set_confirmed\"}],\"methods\":[{\"id\":\"vendor-mode\",\"type\":\"vendor_protocol\",\"protocolId\":\"vendor\",\"address\":\"mode\",\"readable\":false,\"writable\":true,\"risk\":\"validate_safe_set\"}]}]}";
static const char *external =
    "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{\"manufacturer\":\"LG\",\"model\":\"HDR QHD\"},\"capabilities\":[{\"id\":\"gaming.response_time\",\"methods\":[{\"id\":\"candidate\",\"type\":\"mccs_vcp\",\"vcpCode\":170,\"readable\":true,\"writable\":false,\"risk\":\"read_extended\",\"evidence\":[{\"type\":\"external_candidate\"}]}]}]}";

static RSSDDCMonitorKnowledge *parse(const char *json) {
    RSSDDCMonitorKnowledge *knowledge = NULL;
    assert(rss_ddc_monitor_knowledge_parse_json(json, strlen(json), &knowledge) == RSS_DDC_OK);
    return knowledge;
}

int main(void) {
    RSSDDCMonitorKnowledge *first = parse(standard), *second = parse(validated);
    const RSSDDCMonitorKnowledge *sources[] = {first, second};
    RSSDDCMonitorKnowledgeResolution *resolved = NULL;
    assert(rss_ddc_monitor_knowledge_resolve_capability(sources, 2, "inputs.switching", &resolved) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_method_count(resolved) == 2);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolved)->vcp_code == 0x60);
    assert(strcmp(rss_ddc_monitor_knowledge_resolution_preferred_write(resolved)->id, "lg-alt") == 0);
    assert(rss_ddc_monitor_knowledge_resolution_write_authorized(resolved));
    rss_ddc_monitor_knowledge_resolution_destroy(resolved);
    const RSSDDCMonitorKnowledge *reversed[] = {second, first};
    assert(rss_ddc_monitor_knowledge_resolve_capability(reversed, 2, "inputs.switching", &resolved) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolved)->vcp_code == 0x60);
    assert(strcmp(rss_ddc_monitor_knowledge_resolution_preferred_write(resolved)->id, "lg-alt") == 0);
    rss_ddc_monitor_knowledge_resolution_destroy(resolved);

    RSSDDCMonitorKnowledge *third = parse(conflict_a), *fourth = parse(conflict_b);
    const RSSDDCMonitorKnowledge *conflicts[] = {third, fourth};
    assert(rss_ddc_monitor_knowledge_resolve_capability(conflicts, 2, "display.picture_mode", &resolved) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_has_conflict(resolved));
    assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolved));
    assert(rss_ddc_monitor_knowledge_resolution_reason(resolved) == RSS_DDC_RESOLUTION_REASON_EQUAL_AUTHORITY_CONFLICT);
    rss_ddc_monitor_knowledge_resolution_destroy(resolved);

    RSSDDCMonitorKnowledge *candidate = parse(external);
    const RSSDDCMonitorKnowledge *candidate_source[] = {candidate};
    assert(rss_ddc_monitor_knowledge_resolve_capability(candidate_source, 1, "gaming.response_time", &resolved) == RSS_DDC_OK);
    assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolved));
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolved)->vcp_code == 0xaa);
    rss_ddc_monitor_knowledge_resolution_destroy(resolved);

    rss_ddc_monitor_knowledge_destroy(candidate);
    rss_ddc_monitor_knowledge_destroy(fourth);
    rss_ddc_monitor_knowledge_destroy(third);
    rss_ddc_monitor_knowledge_destroy(second);
    rss_ddc_monitor_knowledge_destroy(first);
    puts("test_monitor_knowledge_resolution: passed");
    return 0;
}
