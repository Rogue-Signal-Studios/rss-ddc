#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "discovery.h"

/* The research core is deliberately testable without linking the macOS runtime. */
const char *rss_ddc_error_string(RSSDDCError error) {
    return error == RSS_DDC_OK ? "ok" : "fake error";
}

const char *rss_ddc_provider_string(RSSDDCProvider provider) {
    (void)provider;
    return "fake provider";
}

typedef struct {
    unsigned int gets;
    unsigned int sets;
    uint16_t value;
    bool unstable;
    bool fail_restore;
} FakeTransport;

static RSSDDCError fake_get(void *context, uint8_t vcp, RSSDDCVCPResult *result) {
    FakeTransport *fake = context;
    ++fake->gets;
    if (vcp == 0xee) return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    *result = (RSSDDCVCPResult){.vcp_code = vcp, .maximum_value = 100,
                                 .current_value = (uint16_t)(fake->unstable && fake->gets % 2 == 0 ? 41 : fake->value)};
    return RSS_DDC_OK;
}

static RSSDDCError fake_set(void *context, uint8_t vcp, uint16_t value) {
    (void)vcp;
    FakeTransport *fake = context;
    ++fake->sets;
    if (fake->fail_restore && value == 50) return RSS_DDC_ERROR_WRITE;
    fake->value = value;
    return RSS_DDC_OK;
}

static RSSDDCMCCSCapabilities capabilities(void) {
    RSSDDCMCCSCapabilities parsed = {};
    const char raw[] = "vcp(10 14(05 08 0B) 60(0F 11))";
    assert(rss_ddc_parse_mccs_capabilities(raw, strlen(raw), &parsed) == RSS_DDC_OK);
    return parsed;
}

int main(void) {
    unsigned long parsed = 0;
    assert(rss_ddc_research_parse_unsigned("0x14", 255, &parsed) && parsed == 20);
    assert(!rss_ddc_research_parse_unsigned("-1", 255, &parsed));
    assert(!rss_ddc_research_parse_unsigned("20junk", 255, &parsed));
    assert(!rss_ddc_research_parse_unsigned("256", 255, &parsed));
    assert(rss_ddc_research_is_picture_candidate(0x14));
    assert(!rss_ddc_research_is_picture_candidate(0x60));
    assert(rss_ddc_research_is_mutation_denied(0x60));
    assert(rss_ddc_research_is_mutation_denied(0xd6));
    assert(rss_ddc_research_is_mutation_denied(0x04));
    assert(!rss_ddc_research_is_mutation_denied(0x10));

    RSSDDCResearchOptions options = {.category = RSS_DDC_RESEARCH_CATEGORY_ALL, .reads = 2, .restore = true};
    assert(rss_ddc_research_validate_options(&options) == RSS_DDC_OK);
    assert(!rss_ddc_research_mutation_authorized(&options, 0x10));
    options.allow_set = true;
    assert(rss_ddc_research_validate_options(&options) == RSS_DDC_ERROR_SAFETY_GATE);
    options.explicit_vcps[options.explicit_vcp_count++] = 0x60;
    options.mutation_values[options.mutation_value_count++] = 1;
    assert(rss_ddc_research_validate_options(&options) == RSS_DDC_ERROR_SAFETY_GATE);
    options.explicit_vcps[0] = 0x10;
    assert(rss_ddc_research_validate_options(&options) == RSS_DDC_OK);
    assert(rss_ddc_research_mutation_authorized(&options, 0x10));

    RSSDDCMCCSCapabilities parsed_capabilities = capabilities();
    uint8_t candidates[RSS_DDC_RESEARCH_MAX_CANDIDATES] = {};
    size_t count = 0;
    options.allow_set = false;
    options.explicit_vcp_count = 0;
    options.category = RSS_DDC_RESEARCH_CATEGORY_PICTURE;
    assert(rss_ddc_research_select_candidates(&parsed_capabilities, &options, candidates, sizeof(candidates), &count) == RSS_DDC_OK);
    assert(count == 9 && candidates[0] == 0x10 && candidates[1] == 0x14);

    RSSDDCResearchSample stable[] = {{.status = RSS_DDC_OK, .result = {.current_value = 50, .maximum_value = 100}},
                                     {.status = RSS_DDC_OK, .result = {.current_value = 50, .maximum_value = 100}}};
    assert(rss_ddc_research_classify(stable, 2, 3) == RSS_DDC_RESEARCH_CLASS_ENUM_ADVERTISED);
    stable[1].result.current_value = 51;
    assert(rss_ddc_research_classify(stable, 2, 0) == RSS_DDC_RESEARCH_CLASS_UNSTABLE);
    stable[0].status = stable[1].status = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    assert(rss_ddc_research_classify(stable, 2, 0) == RSS_DDC_RESEARCH_CLASS_UNSUPPORTED);
    stable[0].status = stable[1].status = RSS_DDC_ERROR_REPLY_CHECKSUM;
    assert(rss_ddc_research_classify(stable, 2, 0) == RSS_DDC_RESEARCH_CLASS_MALFORMED);

    RSSDDCResearchReport report = {.capabilities = parsed_capabilities, .capabilities_status = RSS_DDC_OK};
    report.display.list_index = 1;
    snprintf(report.display.product_name, sizeof(report.display.product_name), "Fake \"Display\"");
    snprintf(report.timestamp, sizeof(report.timestamp), "2026-08-13T00:00:00Z");
    options = (RSSDDCResearchOptions){.category = RSS_DDC_RESEARCH_CATEGORY_ALL, .reads = 2, .restore = true};
    options.explicit_vcps[options.explicit_vcp_count++] = 0x10;
    FakeTransport fake = {.value = 50};
    RSSDDCResearchTransport transport = {.get_vcp = fake_get, .set_vcp = fake_set, .context = &fake};
    assert(rss_ddc_research_run(&report, &options, &transport) == RSS_DDC_OK);
    assert(fake.sets == 0 && report.mutation_count == 0); /* read-only default */
    assert(report.read_count == 3); /* advertised candidates plus explicit code, de-duplicated */

    RSSDDCResearchReport mutation_report = {.capabilities_status = RSS_DDC_OK};
    mutation_report.capabilities = capabilities();
    options = (RSSDDCResearchOptions){.reads = 1, .allow_set = true, .restore = true};
    options.explicit_vcps[options.explicit_vcp_count++] = 0x10;
    options.mutation_values[options.mutation_value_count++] = 49;
    fake = (FakeTransport){.value = 50};
    transport.context = &fake;
    assert(rss_ddc_research_run(&mutation_report, &options, &transport) == RSS_DDC_OK);
    assert(mutation_report.mutation_count == 1 && mutation_report.mutations[0].restored && fake.value == 50);

    char first[8192] = {}, second[8192] = {};
    FILE *output = tmpfile();
    assert(output != NULL && rss_ddc_research_write_json(output, &mutation_report));
    rewind(output); assert(fread(first, 1, sizeof(first) - 1, output) != 0); fclose(output);
    output = tmpfile(); assert(output != NULL && rss_ddc_research_write_json(output, &mutation_report));
    rewind(output); assert(fread(second, 1, sizeof(second) - 1, output) != 0); fclose(output);
    assert(strcmp(first, second) == 0);
    assert(strstr(first, "\"schemaVersion\": 1") != NULL && strstr(first, "\"reads\"") != NULL &&
           strstr(first, "\"semantic\"") != NULL && strstr(first, "Fake") == NULL);
    puts("test_research: passed");
    return 0;
}
