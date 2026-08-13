#ifndef RSS_DDC_RESEARCH_DISCOVERY_H
#define RSS_DDC_RESEARCH_DISCOVERY_H

/* Internal research tooling. This header is deliberately not part of include/. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "rss_ddc.h"

enum {
    RSS_DDC_RESEARCH_MAX_CANDIDATES = 64,
    RSS_DDC_RESEARCH_MAX_READS = 10,
    RSS_DDC_RESEARCH_MAX_VALUES = 32,
};

typedef enum {
    RSS_DDC_RESEARCH_CATEGORY_ALL = 0,
    RSS_DDC_RESEARCH_CATEGORY_PICTURE,
} RSSDDCResearchCategory;

typedef enum {
    RSS_DDC_RESEARCH_CLASS_NUMERIC = 0,
    RSS_DDC_RESEARCH_CLASS_ENUM_ADVERTISED,
    RSS_DDC_RESEARCH_CLASS_READABLE_UNKNOWN,
    RSS_DDC_RESEARCH_CLASS_UNSUPPORTED,
    RSS_DDC_RESEARCH_CLASS_MALFORMED,
    RSS_DDC_RESEARCH_CLASS_UNSTABLE,
    RSS_DDC_RESEARCH_CLASS_TRANSPORT_ERROR,
} RSSDDCResearchClassification;

typedef struct {
    RSSDDCResearchCategory category;
    unsigned int reads;
    bool allow_set;
    bool restore;
    uint32_t settle_ms;
    uint8_t explicit_vcps[RSS_DDC_RESEARCH_MAX_CANDIDATES];
    size_t explicit_vcp_count;
    uint16_t mutation_values[RSS_DDC_RESEARCH_MAX_VALUES];
    size_t mutation_value_count;
} RSSDDCResearchOptions;

typedef struct {
    RSSDDCError (*get_vcp)(void *context, uint8_t vcp, RSSDDCVCPResult *result);
    RSSDDCError (*set_vcp)(void *context, uint8_t vcp, uint16_t value);
    void (*settle)(void *context, uint32_t milliseconds);
    void *context;
} RSSDDCResearchTransport;

typedef struct {
    RSSDDCError status;
    RSSDDCVCPResult result;
} RSSDDCResearchSample;

typedef struct {
    uint8_t vcp;
    uint8_t advertised_values[RSS_DDC_RESEARCH_MAX_VALUES];
    size_t advertised_value_count;
    RSSDDCResearchSample samples[RSS_DDC_RESEARCH_MAX_READS];
    size_t sample_count;
    RSSDDCResearchClassification classification;
} RSSDDCResearchRead;

typedef struct {
    uint8_t vcp;
    uint16_t candidate;
    bool attempted;
    bool original_read;
    uint16_t original;
    RSSDDCError set_status;
    RSSDDCError observed_status;
    uint16_t observed;
    bool changed;
    bool restore_attempted;
    RSSDDCError restore_status;
    RSSDDCError restored_status;
    bool restored;
} RSSDDCResearchMutation;

typedef struct {
    RSSDDCDisplay display;
    RSSDDCMCCSCapabilities capabilities;
    RSSDDCError capabilities_status;
    RSSDDCResearchRead reads[RSS_DDC_RESEARCH_MAX_CANDIDATES];
    size_t read_count;
    RSSDDCResearchMutation mutations[RSS_DDC_RESEARCH_MAX_CANDIDATES * RSS_DDC_RESEARCH_MAX_VALUES];
    size_t mutation_count;
    char warnings[16][160];
    size_t warning_count;
    char timestamp[32];
    char label[RSS_DDC_TEXT_MAX];
} RSSDDCResearchReport;

bool rss_ddc_research_parse_unsigned(const char *text, unsigned long maximum, unsigned long *value);
bool rss_ddc_research_parse_category(const char *text, RSSDDCResearchCategory *category);
const char *rss_ddc_research_category_name(RSSDDCResearchCategory category);
const char *rss_ddc_research_classification_name(RSSDDCResearchClassification classification);
bool rss_ddc_research_is_picture_candidate(uint8_t vcp);
bool rss_ddc_research_is_mutation_denied(uint8_t vcp);
bool rss_ddc_research_mutation_authorized(const RSSDDCResearchOptions *options, uint8_t vcp);
RSSDDCError rss_ddc_research_validate_options(const RSSDDCResearchOptions *options);
RSSDDCError rss_ddc_research_select_candidates(const RSSDDCMCCSCapabilities *capabilities,
                                                const RSSDDCResearchOptions *options,
                                                uint8_t *candidates, size_t capacity, size_t *count);
RSSDDCResearchClassification rss_ddc_research_classify(const RSSDDCResearchSample *samples, size_t sample_count,
                                                        size_t advertised_value_count);
RSSDDCError rss_ddc_research_run(RSSDDCResearchReport *report, const RSSDDCResearchOptions *options,
                                 const RSSDDCResearchTransport *transport);
bool rss_ddc_research_write_json(FILE *output, const RSSDDCResearchReport *report);
void rss_ddc_research_print_summary(FILE *output, const RSSDDCResearchReport *report,
                                    const char *report_path);

#endif
