#include "profile_update.h"

#include <stdio.h>
#include <string.h>

#include "color.h"

const char *rss_ddc_cli_profile_update_status_name(RSSDDCCharacterizationProfileUpdateStatus status) {
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED) {
        return "CREATED";
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED) {
        return "UPDATED";
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNCHANGED) {
        return "UNCHANGED";
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT) {
        return "CONFLICT";
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED) {
        return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

bool rss_ddc_cli_profile_update_should_save(RSSDDCCharacterizationProfileUpdateStatus status) {
    return status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED ||
           status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED;
}

RSSDDCError rss_ddc_cli_profile_update_save_if_needed(const RSSDDCProfileStore *store,
                                                      RSSDDCCharacterizationProfileUpdateStatus status,
                                                      const char *path, bool *written) {
    if (written != NULL) {
        *written = false;
    }
    if (!rss_ddc_cli_profile_update_should_save(status)) {
        return RSS_DDC_OK;
    }
    if (store == NULL || path == NULL || path[0] == '\0') {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    RSSDDCError error = rss_ddc_profile_store_save_local_file(store, path);
    if (error == RSS_DDC_OK && written != NULL) {
        *written = true;
    }
    return error;
}

static const char *sufficiency_name(RSSDDCCharacterizationSufficiency status) {
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT) {
        return "SUFFICIENT";
    }
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT) {
        return "INSUFFICIENT";
    }
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT) {
        return "CONFLICT";
    }
    return "UNAVAILABLE";
}

static const char *control_label(RSSDDCProfileControlID id) {
    if (id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE) {
        return "Picture Mode";
    }
    if (id == RSS_DDC_PROFILE_CONTROL_INPUT) {
        return "Input";
    }
    if (id == RSS_DDC_PROFILE_CONTROL_BRIGHTNESS) {
        return "Brightness";
    }
    if (id == RSS_DDC_PROFILE_CONTROL_CONTRAST) {
        return "Contrast";
    }
    if (id == RSS_DDC_PROFILE_CONTROL_COLOR_PRESET) {
        return "Color Preset";
    }
    return rss_ddc_profile_control_name(id);
}

static const char *method_label(RSSDDCProfileMethod method) {
    if (method == RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT) {
        return "LG_ALT";
    }
    if (method == RSS_DDC_PROFILE_METHOD_VCP) {
        return "VCP";
    }
    return "unknown";
}

static RSSDDCCliColorRole update_status_color(RSSDDCCharacterizationProfileUpdateStatus status) {
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED ||
        status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED) {
        return RSS_DDC_CLI_COLOR_GREEN;
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNCHANGED ||
        status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED) {
        return RSS_DDC_CLI_COLOR_YELLOW;
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT) {
        return RSS_DDC_CLI_COLOR_RED;
    }
    return RSS_DDC_CLI_COLOR_DEFAULT;
}

static void format_confidence(char *buffer, size_t capacity, RSSDDCProfileConfidence confidence) {
    const char *name = rss_ddc_profile_confidence_name(confidence);
    size_t index = 0;
    if (buffer == NULL || capacity == 0) {
        return;
    }
    for (; name[index] != '\0' && index + 1 < capacity; ++index) {
        buffer[index] = name[index] == '-' ? ' ' : name[index];
    }
    buffer[index] = '\0';
}

void rss_ddc_cli_render_profile_update(FILE *stream, const RSSDDCCharacterization *characterization,
                                       const RSSDDCCharacterizationProfileUpdateResult *update,
                                       const RSSDDCEffectiveProfile *effective, const char *saved_path,
                                       const RSSDDCCliEffectiveOutput *output) {
    const RSSDDCDisplay *display = rss_ddc_characterization_display(characterization);
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    const char *product = (display != NULL && display->product_name[0] != '\0') ? display->product_name : "(unknown)";
    const char *sufficiency_text = "UNAVAILABLE";
    if (stream == NULL || update == NULL) {
        return;
    }
    if (characterization != NULL &&
        rss_ddc_characterization_sufficiency(characterization, &sufficiency) == RSS_DDC_OK) {
        sufficiency_text = sufficiency_name(sufficiency.status);
    }
    fprintf(stream, "Display: %s\n", product);
    fprintf(stream, "Characterization: %s\n", sufficiency_text);
    fprintf(stream, "Profile update: ");
    rss_ddc_cli_color_begin(stream, output, update_status_color(update->status));
    fprintf(stream, "%s", rss_ddc_cli_profile_update_status_name(update->status));
    rss_ddc_cli_color_reset(stream, output);
    fprintf(stream, "\n");
    if (update->status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED) {
        fprintf(stream, "Reason: no safely persistable authoritative controls\n");
    } else if (update->status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT) {
        fprintf(stream, "Reason: equal-authority profile conflict\n");
    }
    if ((update->status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED ||
         update->status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED) &&
        effective != NULL) {
        bool heading = false;
        size_t count = rss_ddc_effective_profile_control_count(effective);
        for (size_t index = 0; index < count; ++index) {
            RSSDDCProfileControl control = {0};
            char confidence[64];
            if (rss_ddc_effective_profile_control(effective, index, &control) != RSS_DDC_OK) {
                continue;
            }
            if (control.source != RSS_DDC_PROFILE_SOURCE_LOCAL) {
                continue;
            }
            if (!heading) {
                fprintf(stream, "Added:\n");
                heading = true;
            }
            format_confidence(confidence, sizeof(confidence), control.confidence);
            fprintf(stream, "  %s\n", control_label(control.id));
            fprintf(stream, "    method: %s\n", method_label(control.method));
            fprintf(stream, "    address: 0x%x\n", control.address);
            fprintf(stream, "    values:");
            if (control.enum_value_count == 0) {
                fprintf(stream, " (none)\n");
            } else {
                for (size_t enum_index = 0; enum_index < control.enum_value_count; ++enum_index) {
                    RSSDDCProfileEnumValue value = {0};
                    if (rss_ddc_profile_control_enum_value(&control, enum_index, &value) != RSS_DDC_OK) {
                        continue;
                    }
                    fprintf(stream, "%s %s", enum_index == 0 ? "" : ",", value.name[0] != '\0' ? value.name : value.id);
                }
                fprintf(stream, "\n");
            }
            fprintf(stream, "    confidence: %s\n", confidence);
        }
    }
    if (saved_path != NULL && saved_path[0] != '\0') {
        fprintf(stream, "Saved: %s\n", saved_path);
    } else {
        fprintf(stream, "No file written\n");
    }
}
