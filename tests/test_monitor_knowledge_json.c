#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rss_ddc.h"

static RSSDDCKnowledgeRoute make_observed(const char *semantic, const char *route_id, uint16_t address,
                                          uint16_t current, uint16_t maximum, bool stable) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
        .address = address,
        .readable = true,
        .writable = true,
        .write_authorized = true,
        .reported_maximum_present = true,
        .reported_maximum = maximum,
        .value = {.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, .unsigned_value = current},
        .provenance = {.source = RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       .confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
                       .fact_kind = RSS_DDC_KNOWLEDGE_FACT_OBSERVED},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", semantic);
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", route_id);
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                   "alien-probe-live-read");
    (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                   stable ? "stable-get" : "variable-get");
    return route;
}

static char *serialize_all(const RSSDDCMonitorKnowledge *knowledge,
                           const RSSDDCMonitorKnowledgeIdentity *identity) {
    size_t required = 0;
    char *json = NULL;
    assert(rss_ddc_monitor_knowledge_serialize_json(knowledge, identity, NULL, 0, &required) == RSS_DDC_OK);
    json = malloc(required);
    assert(json != NULL);
    assert(rss_ddc_monitor_knowledge_serialize_json(knowledge, identity, json, required, &required) ==
           RSS_DDC_OK);
    return json;
}

static void test_schema_and_empty_capabilities(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledgeIdentity identity = {0};
    char *json = NULL;
    assert(knowledge != NULL);
    (void)snprintf(identity.model, sizeof(identity.model), "%s", "Example");
    (void)snprintf(identity.provider, sizeof(identity.provider), "%s", "DCPDP13Service");
    json = serialize_all(knowledge, &identity);
    assert(strstr(json, "\"schemaVersion\":\"monitor-knowledge/v0.1\"") != NULL);
    assert(strstr(json, "\"model\":\"Example\"") != NULL);
    assert(strstr(json, "\"capabilities\":[]") != NULL);
    assert(strstr(json, "\"inputRoutes\":[]") != NULL);
    assert(strstr(json, "\"relationships\":[]") != NULL);
    assert(strstr(json, "list_index") == NULL);
    assert(strstr(json, "cg_display_id") == NULL);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_observed_current_max_and_no_write_authority(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute brightness = make_observed("display.brightness", "mccs-vcp-10", 0x10, 100, 100, true);
    RSSDDCKnowledgeRoute input = make_observed("inputs.switching", "mccs-vcp-60", 0x60, 17, 18, true);
    RSSDDCKnowledgeRoute picture = make_observed("display.picture_mode", "mccs-vcp-15", 0x15, 49, 255, true);
    char *json = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &brightness) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &input) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &picture) == RSS_DDC_OK);
    json = serialize_all(knowledge, NULL);
    assert(strstr(json, "\"id\":\"display.brightness\"") != NULL);
    assert(strstr(json, "\"type\":\"unsigned\",\"value\":100") != NULL);
    assert(strstr(json, "\"observedRange\":{\"min\":100,\"max\":100}") != NULL);
    assert(strstr(json, "\"vcpCode\":96") != NULL);
    assert(strstr(json, "\"vcpCode\":21") != NULL);
    assert(strstr(json, "\"writable\":true") == NULL);
    assert(strstr(json, "\"writable\":false") != NULL);
    assert(strstr(json, "lg-alt") == NULL);
    assert(strstr(json, "profile_known") == NULL);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_variable_distinct_from_stable(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute stable = make_observed("display.brightness", "mccs-vcp-10", 0x10, 42, 100, true);
    RSSDDCKnowledgeRoute variable = make_observed("vendor.unknown.vcp.ee", "mccs-vcp-ee", 0xee, 7, 255, false);
    char *json = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &stable) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &variable) == RSS_DDC_OK);
    json = serialize_all(knowledge, NULL);
    assert(strstr(json, "\"reference\":\"stable\"") != NULL);
    assert(strstr(json, "\"reference\":\"variable\"") != NULL);
    assert(strstr(json, "\"id\":\"vendor.unknown.vcp.ee\"") != NULL);
    assert(strstr(json, "\"risk\":\"read_extended\"") != NULL);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_round_trip(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *parsed = NULL;
    RSSDDCMonitorKnowledgeIdentity identity = {0};
    RSSDDCMonitorKnowledgeIdentity parsed_identity = {0};
    RSSDDCKnowledgeRoute brightness = make_observed("display.brightness", "mccs-vcp-10", 0x10, 80, 100, true);
    RSSDDCKnowledgeRoute unknown = make_observed("vendor.unknown.vcp.ef", "mccs-vcp-ef", 0xef, 1, 255, false);
    char *first = NULL;
    char *second = NULL;
    const RSSDDCKnowledgeRoute *route = NULL;
    assert(knowledge != NULL);
    (void)snprintf(identity.model, sizeof(identity.model), "%s", "Round Trip");
    (void)snprintf(identity.edid_manufacturer, sizeof(identity.edid_manufacturer), "%s", "GSM");
    identity.edid_product_code = 123;
    identity.edid_product_code_present = true;
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &brightness) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &unknown) == RSS_DDC_OK);
    first = serialize_all(knowledge, &identity);
    assert(rss_ddc_monitor_knowledge_parse_json(first, strlen(first), &parsed, &parsed_identity) == RSS_DDC_OK);
    assert(parsed != NULL);
    assert(strcmp(parsed_identity.model, "Round Trip") == 0);
    assert(strcmp(parsed_identity.edid_manufacturer, "GSM") == 0);
    assert(parsed_identity.edid_product_code_present && parsed_identity.edid_product_code == 123);
    route = rss_ddc_monitor_knowledge_route_at(parsed, 0);
    assert(route != NULL);
    assert(!route->writable && !route->write_authorized);
    second = serialize_all(parsed, &parsed_identity);
    assert(strcmp(first, second) == 0);
    rss_ddc_monitor_knowledge_destroy(parsed);
    parsed = (RSSDDCMonitorKnowledge *)0x1;
    const char *bad_schema =
        "{\"schemaVersion\":\"nope\",\"identity\":{},\"capabilities\":[]}";
    assert(rss_ddc_monitor_knowledge_parse_json(bad_schema, strlen(bad_schema), &parsed, NULL) ==
           RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA);
    assert(parsed == NULL);
    parsed = (RSSDDCMonitorKnowledge *)0x1;
    assert(rss_ddc_monitor_knowledge_parse_json("{", 1, &parsed, NULL) ==
           RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED);
    assert(parsed == NULL);
    free(first);
    free(second);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_bounds_and_file(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *parsed = NULL;
    char *json = NULL;
    char path[] = "/tmp/rss-ddc-mk-v01-XXXXXX";
    int fd = -1;
    assert(knowledge != NULL);
    json = serialize_all(knowledge, NULL);
    assert(rss_ddc_monitor_knowledge_parse_json(json, RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_BYTES + 1, &parsed,
                                                NULL) == RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE);
    assert(parsed == NULL);
    fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(rss_ddc_monitor_knowledge_write_json_file(knowledge, NULL, path) == RSS_DDC_OK);
    FILE *file = fopen(path, "rb");
    char buffer[256] = {0};
    assert(file != NULL);
    assert(fread(buffer, 1, sizeof(buffer) - 1, file) > 0);
    fclose(file);
    assert(strstr(buffer, "monitor-knowledge/v0.1") != NULL);
    unlink(path);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_skips_unknown_keys(void) {
    const char *text =
        "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{\"ignored\":true,\"model\":\"X\"},"
        "\"capabilities\":[{\"id\":\"display.brightness\",\"label\":\"Brightness\",\"methods\":["
        "{\"id\":\"mccs-vcp-10\",\"type\":\"mccs_vcp\",\"readable\":true,\"writable\":true,\"risk\":"
        "\"read_standard\",\"vcpCode\":16,\"evidence\":[{\"type\":\"stable_get\",\"sourceId\":"
        "\"alien-probe-live-read\",\"reference\":\"stable\"}]}],\"values\":[{\"id\":\"mccs-vcp-10\","
        "\"raw\":{\"type\":\"unsigned\",\"value\":50}}]}],\"inputRoutes\":[{\"id\":\"hdmi\"}],"
        "\"relationships\":[]}";
    RSSDDCMonitorKnowledge *parsed = NULL;
    RSSDDCMonitorKnowledgeIdentity identity = {0};
    const RSSDDCKnowledgeRoute *route = NULL;
    assert(rss_ddc_monitor_knowledge_parse_json(text, strlen(text), &parsed, &identity) == RSS_DDC_OK);
    assert(strcmp(identity.model, "X") == 0);
    assert(rss_ddc_monitor_knowledge_route_count(parsed) == 1);
    route = rss_ddc_monitor_knowledge_route_at(parsed, 0);
    assert(route != NULL && route->address == 0x10);
    assert(!route->writable && !route->write_authorized);
    assert(route->value.unsigned_value == 50);
    rss_ddc_monitor_knowledge_destroy(parsed);
}

int main(void) {
    test_schema_and_empty_capabilities();
    test_observed_current_max_and_no_write_authority();
    test_variable_distinct_from_stable();
    test_round_trip();
    test_bounds_and_file();
    test_skips_unknown_keys();
    puts("test_monitor_knowledge_json: passed");
    return 0;
}
