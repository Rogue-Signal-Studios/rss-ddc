#include "characterize.h"

#include "rss_ddc.h"

RSSDDCError rss_ddc_characterization_prepare(RSSDDCCharacterization *characterization,
                                             uint32_t list_index, const RSSDDCProfileStore *store) {
    if (characterization == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    RSSDDCDisplay display = {0};
    RSSDDCError error = rss_ddc_get_display(list_index, &display);
    if (error != RSS_DDC_OK) {
        return error;
    }
    RSSDDCEDID edid = {0};
    RSSDDCEDIDInfo info = {0};
    const RSSDDCEDIDInfo *edid_info = NULL;
    if (rss_ddc_read_edid(list_index, &edid) == RSS_DDC_OK && rss_ddc_parse_edid(&edid, &info) == RSS_DDC_OK) {
        edid_info = &info;
    }
    return rss_ddc_characterization_assemble(characterization, &display, edid_info, store);
}

RSSDDCError rss_ddc_characterization_collect_passive(RSSDDCCharacterization *characterization) {
    const RSSDDCDisplay *display = rss_ddc_characterization_display(characterization);
    if (characterization == NULL || display == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!rss_ddc_characterization_mccs_supported(characterization)) {
        return rss_ddc_characterization_collect_passive_mccs_failed(
            characterization, RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    }
    RSSDDCMCCSCapabilities parsed = {0};
    RSSDDCError error = rss_ddc_get_mccs_capabilities(display->list_index, &parsed);
    if (error != RSS_DDC_OK) {
        return rss_ddc_characterization_collect_passive_mccs_failed(characterization, error);
    }
    return rss_ddc_characterization_collect_passive_mccs(characterization, &parsed);
}
