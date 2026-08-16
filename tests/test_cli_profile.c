#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "characterize.h"
#include "output_settings.h"
#include "profile_update.h"
#include "rss_ddc.h"

RSSDDCError rss_ddc_get_display(uint32_t index, RSSDDCDisplay *display) {
    (void)index;
    (void)display;
    return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_vcp(uint32_t index, uint8_t code, RSSDDCVCPResult *result) {
    (void)index;
    (void)code;
    (void)result;
    return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_mccs_capabilities(uint32_t index, RSSDDCMCCSCapabilities *capabilities) {
    (void)index;
    (void)capabilities;
    return RSS_DDC_ERROR_DISCOVERY;
}

static RSSDDCDisplay lg_display(void) {
    RSSDDCDisplay display = {
        .list_index = 2,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_DCPDP13,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "LG HDR QHD");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT0");
    return display;
}

static RSSDDCDisplay odyssey_display(void) {
    RSSDDCDisplay display = {
        .list_index = 1,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_PS190,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Odyssey G75F");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT1");
    return display;
}

static const char *lg_vcp_input_pack(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"lg-vcp-input\",\"profiles\":[{\"id\":\"lg-vcp\",\"identity\":{"
           "\"productName\":\"LG HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static char *read_path(const char *path) {
    FILE *file = fopen(path, "rb");
    char *data = NULL;
    long length = 0;
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    data = calloc(1, (size_t)length + 1);
    assert(data != NULL);
    assert(fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    return data;
}

static char *capture_update_render(const RSSDDCCharacterization *characterization,
                                   const RSSDDCCharacterizationProfileUpdateResult *update,
                                   const RSSDDCEffectiveProfile *effective, const char *saved_path) {
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&buffer, &length);
    RSSDDCCliEffectiveOutput output = {.color = false, .table = false, .unicode = false};
    assert(stream != NULL);
    rss_ddc_cli_render_profile_update(stream, characterization, update, effective, saved_path, &output);
    fclose(stream);
    return buffer;
}

static void test_save_policy(void) {
    assert(rss_ddc_cli_profile_update_should_save(RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED));
    assert(rss_ddc_cli_profile_update_should_save(RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED));
    assert(!rss_ddc_cli_profile_update_should_save(RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNCHANGED));
    assert(!rss_ddc_cli_profile_update_should_save(RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED));
    assert(!rss_ddc_cli_profile_update_should_save(RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT));
}

static void test_lg_local_overlay_roundtrip(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCProfileStore *reload = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCEffectiveProfile effective = {0};
    RSSDDCEffectiveProfile reloaded = {0};
    RSSDDCProfileControl picture = {0};
    RSSDDCProfileControl input = {0};
    char path[] = "/private/tmp/rss-ddc-cli-profile-lg-XXXXXX";
    char *json = NULL;
    char *report = NULL;
    bool written = false;
    int fd = mkstemp(path);
    assert(characterization != NULL && store != NULL && reload != NULL && fd >= 0);
    close(fd);
    unlink(path);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    {
        RSSDDCDisplay display = lg_display();
        assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    }
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(characterization, store, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED);
    assert(rss_ddc_profile_store_resolve(store, rss_ddc_characterization_profile_identity(characterization),
                                         &effective) == RSS_DDC_OK);
    assert(rss_ddc_cli_profile_update_save_if_needed(store, update.status, path, &written) == RSS_DDC_OK);
    assert(written);
    json = read_path(path);
    assert(strstr(json, "lg-alt-input") != NULL);
    assert(strstr(json, "\"address\":244") != NULL);
    assert(strstr(json, "\"value\":144") != NULL);
    assert(strstr(json, "\"value\":145") != NULL);
    assert(strstr(json, "\"value\":208") != NULL);
    assert(strstr(json, "\"value\":17") == NULL);
    assert(strstr(json, "\"value\":18") == NULL);
    assert(strstr(json, "\"value\":15") == NULL);
    assert(strstr(json, "picture-mode") == NULL);
    assert(strstr(json, "rogue-builtin") == NULL);
    assert(strstr(json, "\"packId\":\"local-export\"") != NULL);
    assert(strstr(json, "current") == NULL);
    assert(rss_ddc_profile_store_load_builtin(reload) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_local_file(reload, path) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_resolve(reload, rss_ddc_characterization_profile_identity(characterization),
                                         &reloaded) == RSS_DDC_OK);
    for (size_t index = 0; index < rss_ddc_effective_profile_control_count(&reloaded); ++index) {
        RSSDDCProfileControl control = {0};
        assert(rss_ddc_effective_profile_control(&reloaded, index, &control) == RSS_DDC_OK);
        if (control.id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE) {
            picture = control;
        } else if (control.id == RSS_DDC_PROFILE_CONTROL_INPUT) {
            input = control;
        }
    }
    assert(picture.source == RSS_DDC_PROFILE_SOURCE_BUILTIN);
    assert(picture.method == RSS_DDC_PROFILE_METHOD_VCP);
    assert(picture.address == 0x15);
    assert(input.source == RSS_DDC_PROFILE_SOURCE_LOCAL);
    assert(input.method == RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT);
    assert(input.address == 0xf4);
    report = capture_update_render(characterization, &update, &effective, path);
    assert(strstr(report, "Display: LG HDR QHD") != NULL);
    assert(strstr(report, "Profile update: UPDATED") != NULL);
    assert(strstr(report, "method: LG_ALT") != NULL);
    assert(strstr(report, "address: 0xf4") != NULL);
    assert(strstr(report, "HDMI 1") != NULL);
    assert(strstr(report, "Saved:") != NULL);
    unlink(path);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_profile_store_destroy(reload);
    free(json);
    free(report);
}

static void test_odyssey_unsupported_writes_no_file(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCDisplay display = odyssey_display();
    char path[128];
    char *report = NULL;
    bool written = true;
    assert(characterization != NULL && store != NULL);
    (void)snprintf(path, sizeof(path), "/private/tmp/rss-ddc-cli-profile-odyssey-%d.json", (int)getpid());
    unlink(path);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(characterization, store, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED);
    assert(rss_ddc_cli_profile_update_save_if_needed(store, update.status, path, &written) == RSS_DDC_OK);
    assert(!written);
    assert(access(path, F_OK) != 0);
    report = capture_update_render(characterization, &update, NULL, NULL);
    assert(strstr(report, "Display: Odyssey G75F") != NULL);
    assert(strstr(report, "Profile update: UNSUPPORTED") != NULL);
    assert(strstr(report, "No file written") != NULL);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
    free(report);
}

static void test_conflict_leaves_file_unchanged(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCDisplay display = lg_display();
    const char *sentinel = "do-not-overwrite";
    char path[] = "/private/tmp/rss-ddc-cli-profile-conflict-XXXXXX";
    char *after = NULL;
    bool written = true;
    int fd = mkstemp(path);
    FILE *file = NULL;
    assert(characterization != NULL && store != NULL && fd >= 0);
    close(fd);
    file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(sentinel, 1, strlen(sentinel), file) == strlen(sentinel));
    fclose(file);
    assert(rss_ddc_profile_store_load_pack_data(store, lg_vcp_input_pack(), strlen(lg_vcp_input_pack())) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(characterization, store, &update) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT);
    assert(rss_ddc_cli_profile_update_save_if_needed(store, update.status, path, &written) == RSS_DDC_OK);
    assert(!written);
    after = read_path(path);
    assert(strcmp(after, sentinel) == 0);
    unlink(path);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
    free(after);
}

static void test_unchanged_does_not_rewrite(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCDisplay display = lg_display();
    char path[] = "/private/tmp/rss-ddc-cli-profile-unchanged-XXXXXX";
    const char *sentinel = "leave-this-file";
    char *after = NULL;
    bool written = true;
    int fd = mkstemp(path);
    FILE *file = NULL;
    assert(characterization != NULL && store != NULL && fd >= 0);
    close(fd);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(characterization, store, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED);
    update = (RSSDDCCharacterizationProfileUpdateResult){0};
    assert(rss_ddc_characterization_update_profile(characterization, store, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNCHANGED);
    file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(sentinel, 1, strlen(sentinel), file) == strlen(sentinel));
    fclose(file);
    assert(rss_ddc_cli_profile_update_save_if_needed(store, update.status, path, &written) == RSS_DDC_OK);
    assert(!written);
    after = read_path(path);
    assert(strcmp(after, sentinel) == 0);
    unlink(path);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
    free(after);
}

static void test_save_argument_is_fatal(void) {
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    bool written = true;
    assert(store != NULL);
    assert(rss_ddc_cli_profile_update_save_if_needed(store, RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED,
                                                     NULL, &written) == RSS_DDC_ERROR_ARGUMENT);
    assert(!written);
    rss_ddc_profile_store_destroy(store);
}

int main(void) {
    test_save_policy();
    test_lg_local_overlay_roundtrip();
    test_odyssey_unsupported_writes_no_file();
    test_conflict_leaves_file_unchanged();
    test_unchanged_does_not_rewrite();
    test_save_argument_is_fatal();
    puts("test_cli_profile: passed");
    return 0;
}
