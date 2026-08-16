#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rss_ddc.h"

static RSSDDCKnowledgeRoute make_observed_from(const char *semantic, const char *route_id, uint16_t address,
                                               uint16_t current, uint16_t maximum, bool stable,
                                               const char *source_id) {
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
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s", source_id);
    (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                   stable ? "stable-get" : "variable-get");
    return route;
}

static RSSDDCKnowledgeRoute make_observed(const char *semantic, const char *route_id, uint16_t address,
                                          uint16_t current, uint16_t maximum, bool stable) {
    return make_observed_from(semantic, route_id, address, current, maximum, stable, "alien-probe-quick");
}

static RSSDDCKnowledgeRoute make_declared(const char *semantic, const char *route_id, uint16_t address) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
        .address = address,
        .readable = false,
        .writable = false,
        .write_authorized = false,
        .value = {.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN},
        .provenance = {.source = RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       .confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
                       .fact_kind = RSS_DDC_KNOWLEDGE_FACT_DECLARED},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", semantic);
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", route_id);
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                   "mccs-capabilities");
    (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                   "mccs-advertised");
    return route;
}

static RSSDDCKnowledgeRoute make_profile(const char *semantic, const char *route_id, uint16_t address,
                                         RSSDDCProfileConfidence confidence) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
        .address = address,
        .readable = true,
        .writable = true,
        .write_authorized = true,
        .value = {.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN},
        .provenance = {.source = RSS_DDC_PROFILE_SOURCE_BUILTIN,
                       .confidence = confidence,
                       .fact_kind = RSS_DDC_KNOWLEDGE_FACT_PROFILE},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", semantic);
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", route_id);
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                   "builtin-profile");
    (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                   "profile-known");
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
    RSSDDCKnowledgeRoute input =
        make_observed_from("inputs.switching", "mccs-vcp-60", 0x60, 17, 18, true, "alien-probe-extended");
    RSSDDCKnowledgeRoute picture =
        make_observed_from("display.picture_mode", "mccs-vcp-15", 0x15, 49, 255, true,
                           "alien-probe-extended");
    char *json = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &brightness) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &input) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &picture) == RSS_DDC_OK);
    json = serialize_all(knowledge, NULL);
    assert(strstr(json, "\"id\":\"display.brightness\"") != NULL);
    assert(strstr(json, "\"type\":\"unsigned\",\"value\":100") != NULL);
    assert(strstr(json, "\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":100}") != NULL);
    assert(strstr(json, "observedRange") == NULL);
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
    RSSDDCKnowledgeRoute variable =
        make_observed_from("vendor.unknown.vcp.ee", "mccs-vcp-ee", 0xee, 7, 255, false,
                           "alien-probe-extended");
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
    RSSDDCKnowledgeRoute unknown =
        make_observed_from("vendor.unknown.vcp.ef", "mccs-vcp-ef", 0xef, 1, 255, false,
                           "alien-probe-extended");
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
    assert(route->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED);
    assert(route->value.unsigned_value == 80);
    assert(route->reported_maximum_present && route->reported_maximum == 100);
    assert(strstr(first, "observedRange") == NULL);
    assert(strstr(first, "\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":100}") != NULL);
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

static void test_current_is_not_observed_minimum(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute brightness = make_observed("display.brightness", "mccs-vcp-10", 0x10, 50, 100, true);
    char *json = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &brightness) == RSS_DDC_OK);
    json = serialize_all(knowledge, NULL);
    assert(strstr(json, "\"type\":\"unsigned\",\"value\":50") != NULL);
    assert(strstr(json, "\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":100}") != NULL);
    assert(strstr(json, "observedRange") == NULL);
    assert(strstr(json, "\"min\":50") == NULL);
    assert(strstr(json, "\"writable\":true") == NULL);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_equal_current_and_max_is_not_a_range(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute brightness = make_observed("display.brightness", "mccs-vcp-10", 0x10, 100, 100, true);
    char *json = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &brightness) == RSS_DDC_OK);
    json = serialize_all(knowledge, NULL);
    assert(strstr(json, "\"type\":\"unsigned\",\"value\":100") != NULL);
    assert(strstr(json, "\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":100}") != NULL);
    assert(strstr(json, "observedRange") == NULL);
    assert(strstr(json, "{\"min\":100,\"max\":100}") == NULL);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_variable_and_unknown_follow_the_same_range_rule(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute variable =
        make_observed_from("vendor.unknown.vcp.ee", "mccs-vcp-ee", 0xee, 7, 255, false,
                           "alien-probe-extended");
    RSSDDCMonitorKnowledge *parsed = NULL;
    char *json = NULL;
    const RSSDDCKnowledgeRoute *route = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &variable) == RSS_DDC_OK);
    json = serialize_all(knowledge, NULL);
    assert(strstr(json, "\"id\":\"vendor.unknown.vcp.ee\"") != NULL);
    assert(strstr(json, "\"type\":\"unsigned\",\"value\":7") != NULL);
    assert(strstr(json, "\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":255}") != NULL);
    assert(strstr(json, "\"reference\":\"variable\"") != NULL);
    assert(strstr(json, "observedRange") == NULL);
    assert(strstr(json, "\"min\":7") == NULL);
    assert(strstr(json, "\"writable\":true") == NULL);
    assert(rss_ddc_monitor_knowledge_parse_json(json, strlen(json), &parsed, NULL) == RSS_DDC_OK);
    route = rss_ddc_monitor_knowledge_route_at(parsed, 0);
    assert(route != NULL);
    assert(route->value.unsigned_value == 7);
    assert(route->reported_maximum_present && route->reported_maximum == 255);
    assert(!route->writable && !route->write_authorized);
    rss_ddc_monitor_knowledge_destroy(parsed);
    free(json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_observed_range_in_input_is_not_reported_maximum(void) {
    const char *text =
        "{\"schemaVersion\":\"monitor-knowledge/v0.1\",\"identity\":{},\"capabilities\":[{"
        "\"id\":\"display.brightness\",\"observedRange\":{\"min\":50,\"max\":100},\"methods\":[{"
        "\"id\":\"mccs-vcp-10\",\"type\":\"mccs_vcp\",\"readable\":true,\"writable\":false,"
        "\"risk\":\"read_standard\",\"vcpCode\":16,\"evidence\":[{\"type\":\"stable_get\","
        "\"reference\":\"stable\"}]}],\"values\":[{\"id\":\"mccs-vcp-10\",\"raw\":{\"type\":"
        "\"unsigned\",\"value\":50}}]}]}";
    RSSDDCMonitorKnowledge *parsed = NULL;
    const RSSDDCKnowledgeRoute *route = NULL;
    assert(rss_ddc_monitor_knowledge_parse_json(text, strlen(text), &parsed, NULL) == RSS_DDC_OK);
    route = rss_ddc_monitor_knowledge_route_at(parsed, 0);
    assert(route != NULL);
    assert(route->value.unsigned_value == 50);
    assert(!route->reported_maximum_present);
    rss_ddc_monitor_knowledge_destroy(parsed);
}

static char *serialize_routes(RSSDDCKnowledgeRoute *routes, size_t count) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    char *json = NULL;
    assert(knowledge != NULL);
    for (size_t index = 0; index < count; ++index) {
        assert(rss_ddc_monitor_knowledge_add_route(knowledge, &routes[index]) == RSS_DDC_OK);
    }
    json = serialize_all(knowledge, NULL);
    rss_ddc_monitor_knowledge_destroy(knowledge);
    return json;
}

static void test_capability_aggregation_is_order_independent(void) {
    RSSDDCKnowledgeRoute declared = make_declared("display.brightness", "mccs-vcp-10", 0x10);
    RSSDDCKnowledgeRoute observed = make_observed("display.brightness", "mccs-vcp-10", 0x10, 42, 100, true);
    RSSDDCKnowledgeRoute candidate = make_observed("display.brightness", "mccs-vcp-10-b", 0x10, 42, 100, true);
    RSSDDCKnowledgeRoute stronger = make_observed("display.brightness", "mccs-vcp-10-c", 0x10, 42, 100, true);
    RSSDDCKnowledgeRoute forward[2];
    RSSDDCKnowledgeRoute reverse[2];
    RSSDDCKnowledgeRoute observed_pair[2];
    RSSDDCKnowledgeRoute observed_pair_reverse[2];
    char *declared_only = NULL;
    char *observed_only = NULL;
    char *mixed_a = NULL;
    char *mixed_b = NULL;
    char *pair_a = NULL;
    char *pair_b = NULL;
    candidate.provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE;
    stronger.provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    forward[0] = declared;
    forward[1] = observed;
    reverse[0] = observed;
    reverse[1] = declared;
    observed_pair[0] = candidate;
    observed_pair[1] = stronger;
    observed_pair_reverse[0] = stronger;
    observed_pair_reverse[1] = candidate;
    declared_only = serialize_routes(&declared, 1);
    observed_only = serialize_routes(&observed, 1);
    mixed_a = serialize_routes(forward, 2);
    mixed_b = serialize_routes(reverse, 2);
    pair_a = serialize_routes(observed_pair, 2);
    pair_b = serialize_routes(observed_pair_reverse, 2);
    assert(strstr(declared_only, "\"confidence\":\"observed\"") != NULL);
    assert(strstr(declared_only, "\"validation\":\"read_validated\"") == NULL);
    assert(strstr(declared_only, "mccs_advertised") != NULL);
    assert(strstr(observed_only, "\"confidence\":\"observed\",\"validation\":\"read_validated\"") != NULL);
    assert(strcmp(mixed_a, mixed_b) == 0);
    assert(strstr(mixed_a, "\"confidence\":\"observed\",\"validation\":\"read_validated\"") != NULL);
    assert(strstr(mixed_a, "mccs_advertised") != NULL);
    assert(strstr(mixed_a, "stable_get") != NULL);
    assert(strcmp(pair_a, pair_b) == 0);
    assert(strstr(pair_a, "\"confidence\":\"observed\",\"validation\":\"read_validated\"") != NULL);
    assert(strstr(pair_a, "\"confidence\":\"candidate\"") != NULL);
    free(declared_only);
    free(observed_only);
    free(mixed_a);
    free(mixed_b);
    free(pair_a);
    free(pair_b);
}

static void test_read_evidence_does_not_become_set_validation(void) {
    RSSDDCKnowledgeRoute observed = make_observed("display.brightness", "mccs-vcp-10", 0x10, 42, 100, true);
    RSSDDCKnowledgeRoute profile =
        make_profile("display.brightness", "profile-brightness", 0x10,
                     RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED);
    RSSDDCKnowledgeRoute routes[2] = {profile, observed};
    RSSDDCKnowledgeRoute reversed[2] = {observed, profile};
    char *json = serialize_routes(routes, 2);
    char *json_reverse = serialize_routes(reversed, 2);
    assert(strcmp(json, json_reverse) == 0);
    assert(strstr(json, "\"confidence\":\"hardware_validated\",\"validation\"") == NULL);
    assert(strstr(json, "\"validation\":\"set_confirmed\"") == NULL);
    assert(strstr(json, "\"writable\":true") != NULL);
    assert(strstr(json, "profile_known") != NULL);
    assert(strstr(json, "stable_get") != NULL);
    free(json);
    free(json_reverse);
}

static void test_quick_and_extended_same_vcp_keep_distinct_stage(void) {
    RSSDDCKnowledgeRoute quick =
        make_observed_from("display.brightness", "mccs-vcp-10", 0x10, 50, 100, true, "alien-probe-quick");
    RSSDDCKnowledgeRoute extended =
        make_observed_from("display.brightness", "mccs-vcp-10", 0x10, 50, 100, true, "alien-probe-extended");
    char *quick_json = serialize_routes(&quick, 1);
    char *extended_json = serialize_routes(&extended, 1);
    char *both = NULL;
    RSSDDCKnowledgeRoute both_routes[2] = {quick, extended};
    assert(strstr(quick_json, "\"type\":\"stable_get\"") != NULL);
    assert(strstr(quick_json, "\"sourceId\":\"alien-probe-quick\"") != NULL);
    assert(strstr(quick_json, "extended_discovery") == NULL);
    assert(strstr(quick_json, "\"risk\":\"read_standard\"") != NULL);
    assert(strstr(extended_json, "\"type\":\"extended_discovery\"") != NULL);
    assert(strstr(extended_json, "\"sourceId\":\"alien-probe-extended\"") != NULL);
    assert(strstr(extended_json, "\"risk\":\"read_extended\"") != NULL);
    both = serialize_routes(both_routes, 2);
    assert(strstr(both, "alien-probe-quick") != NULL);
    assert(strstr(both, "alien-probe-extended") != NULL);
    assert(strstr(both, "stable_get") != NULL);
    assert(strstr(both, "extended_discovery") != NULL);
    free(quick_json);
    free(extended_json);
    free(both);
}

static void test_extended_provenance_is_not_address_inference(void) {
    RSSDDCKnowledgeRoute extended_60 =
        make_observed_from("inputs.switching", "mccs-vcp-60", 0x60, 17, 18, true, "alien-probe-extended");
    RSSDDCKnowledgeRoute targeted_60 =
        make_observed_from("inputs.switching", "mccs-vcp-60", 0x60, 17, 18, true, "alien-probe-quick");
    char *extended_json = serialize_routes(&extended_60, 1);
    char *targeted_json = serialize_routes(&targeted_60, 1);
    assert(strstr(extended_json, "\"type\":\"extended_discovery\"") != NULL);
    assert(strstr(extended_json, "\"sourceId\":\"alien-probe-extended\"") != NULL);
    assert(strstr(targeted_json, "\"type\":\"stable_get\"") != NULL);
    assert(strstr(targeted_json, "\"sourceId\":\"alien-probe-quick\"") != NULL);
    assert(strstr(targeted_json, "extended_discovery") == NULL);
    free(extended_json);
    free(targeted_json);
}

static void test_variable_extended_keeps_stage_and_variability(void) {
    RSSDDCKnowledgeRoute variable =
        make_observed_from("vendor.unknown.vcp.ee", "mccs-vcp-ee", 0xee, 7, 255, false,
                           "alien-probe-extended");
    RSSDDCKnowledgeRoute quick_variable =
        make_observed_from("display.brightness", "mccs-vcp-10", 0x10, 7, 100, false, "alien-probe-quick");
    char *extended_json = serialize_routes(&variable, 1);
    char *quick_json = serialize_routes(&quick_variable, 1);
    assert(strstr(extended_json, "\"type\":\"extended_discovery\"") != NULL);
    assert(strstr(extended_json, "\"reference\":\"variable\"") != NULL);
    assert(strstr(extended_json, "\"sourceId\":\"alien-probe-extended\"") != NULL);
    assert(strstr(quick_json, "\"type\":\"stable_get\"") != NULL);
    assert(strstr(quick_json, "\"reference\":\"variable\"") != NULL);
    assert(strstr(quick_json, "extended_discovery") == NULL);
    free(extended_json);
    free(quick_json);
}

static void test_declared_mccs_stays_advertised(void) {
    RSSDDCKnowledgeRoute declared = make_declared("display.brightness", "mccs-vcp-10", 0x10);
    char *json = serialize_routes(&declared, 1);
    assert(strstr(json, "\"type\":\"mccs_advertised\"") != NULL);
    assert(strstr(json, "\"sourceId\":\"mccs-capabilities\"") != NULL);
    assert(strstr(json, "stable_get") == NULL);
    assert(strstr(json, "extended_discovery") == NULL);
    assert(strstr(json, "\"readable\":false") != NULL);
    assert(strstr(json, "\"writable\":true") == NULL);
    free(json);
}

static void test_observed_is_readable_only_for_successful_get(void) {
    RSSDDCKnowledgeRoute observed = make_observed("display.brightness", "mccs-vcp-10", 0x10, 42, 100, true);
    RSSDDCKnowledgeRoute declared = make_declared("display.contrast", "mccs-vcp-12", 0x12);
    RSSDDCKnowledgeRoute unmarked = make_observed("display.color_preset", "mccs-vcp-14", 0x14, 1, 4, true);
    char *json = NULL;
    RSSDDCKnowledgeRoute routes[3];
    unmarked.readable = false;
    routes[0] = observed;
    routes[1] = declared;
    routes[2] = unmarked;
    json = serialize_routes(routes, 3);
    assert(strstr(json, "\"id\":\"mccs-vcp-10\",\"type\":\"mccs_vcp\",\"readable\":true,\"writable\":false") !=
           NULL);
    assert(strstr(json, "\"id\":\"mccs-vcp-12\",\"type\":\"mccs_vcp\",\"readable\":false,\"writable\":false") !=
           NULL);
    assert(strstr(json, "\"id\":\"mccs-vcp-14\",\"type\":\"mccs_vcp\",\"readable\":true,\"writable\":false") !=
           NULL);
    free(json);
}

static void test_edid_evidence_is_not_applied_to_connection_fields(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledgeIdentity connection = {0};
    RSSDDCMonitorKnowledgeIdentity with_edid = {0};
    char *connection_json = NULL;
    char *edid_json = NULL;
    assert(knowledge != NULL);
    (void)snprintf(connection.provider, sizeof(connection.provider), "%s", "DCPDP13Service");
    (void)snprintf(connection.transport, sizeof(connection.transport), "%s", "DCPAVServiceProxy");
    (void)snprintf(connection.branch, sizeof(connection.branch), "%s", "branch-1");
    (void)snprintf(connection.model, sizeof(connection.model), "%s", "From Display");
    connection_json = serialize_all(knowledge, &connection);
    assert(strstr(connection_json, "\"provider\":\"DCPDP13Service\"") != NULL);
    assert(strstr(connection_json, "\"transport\":\"DCPAVServiceProxy\"") != NULL);
    assert(strstr(connection_json, "\"branch\":\"branch-1\"") != NULL);
    assert(strstr(connection_json, "edid_derived") == NULL);
    assert(strstr(connection_json, "edidManufacturer") == NULL);
    assert(strstr(connection_json, "edidProductCode") == NULL);
    with_edid = connection;
    (void)snprintf(with_edid.edid_manufacturer, sizeof(with_edid.edid_manufacturer), "%s", "GSM");
    with_edid.edid_product_code = 123;
    with_edid.edid_product_code_present = true;
    edid_json = serialize_all(knowledge, &with_edid);
    assert(strstr(edid_json, "\"edidManufacturer\":\"GSM\"") != NULL);
    assert(strstr(edid_json, "\"edidProductCode\":123") != NULL);
    assert(strstr(edid_json, "\"evidence\":[{\"type\":\"edid_derived\"}]") != NULL);
    assert(strstr(edid_json, "\"provider\":\"DCPDP13Service\"") != NULL);
    free(connection_json);
    free(edid_json);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

int main(void) {
    test_schema_and_empty_capabilities();
    test_observed_current_max_and_no_write_authority();
    test_variable_distinct_from_stable();
    test_current_is_not_observed_minimum();
    test_equal_current_and_max_is_not_a_range();
    test_variable_and_unknown_follow_the_same_range_rule();
    test_observed_range_in_input_is_not_reported_maximum();
    test_round_trip();
    test_bounds_and_file();
    test_skips_unknown_keys();
    test_capability_aggregation_is_order_independent();
    test_read_evidence_does_not_become_set_validation();
    test_quick_and_extended_same_vcp_keep_distinct_stage();
    test_extended_provenance_is_not_address_inference();
    test_variable_extended_keeps_stage_and_variability();
    test_declared_mccs_stays_advertised();
    test_observed_is_readable_only_for_successful_get();
    test_edid_evidence_is_not_applied_to_connection_fields();
    puts("test_monitor_knowledge_json: passed");
    return 0;
}
