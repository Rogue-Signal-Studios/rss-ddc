#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile_store.h"

static RSSDDCProfileIdentity lg_identity(void) {
    RSSDDCProfileIdentity identity = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(identity.product_name, sizeof(identity.product_name), "LG HDR QHD");
    snprintf(identity.transport, sizeof(identity.transport), "DCPEXT0");
    return identity;
}

typedef struct {
    RSSDDCProfileIdentity identity;
    RSSDDCEffectiveProfile effective;
    RSSDDCError error;
} BuiltinResolution;

static void *resolve_builtin_on_small_stack(void *context) {
    BuiltinResolution *resolution = context;
    resolution->error = rss_ddc_profile_store_resolve_builtin(&resolution->identity, &resolution->effective);
    return NULL;
}

static void test_builtin_resolution_on_small_stack(void) {
    BuiltinResolution *resolution = calloc(1, sizeof(*resolution));
    assert(resolution != NULL);
    resolution->identity = lg_identity();

    pthread_attr_t attributes;
    pthread_t thread;
    assert(pthread_attr_init(&attributes) == 0);
    assert(pthread_attr_setstacksize(&attributes, 512 * 1024) == 0);
    assert(pthread_create(&thread, &attributes, resolve_builtin_on_small_stack, resolution) == 0);
    assert(pthread_attr_destroy(&attributes) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(resolution->error == RSS_DDC_OK);
    assert(resolution->effective.control_count == 1);
    assert(resolution->effective.controls[0].id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE);
    free(resolution);
}

static const char *local_response_pack =
    "{\"schemaVersion\":1,\"databaseVersion\":\"2026.08.13.2\",\"minimumRSSDDCVersion\":\"0.3.0\",\"packId\":\"local\",\"profiles\":[{\"id\":\"lg-local\",\"identity\":{\"productName\":\"LG HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"response-time\",\"method\":\"vcp\",\"address\":170,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";

static const char *serial_pack =
    "{\"schemaVersion\":1,\"databaseVersion\":\"2026.08.13.2\",\"minimumRSSDDCVersion\":\"0.3.0\",\"packId\":\"serial\",\"profiles\":[{\"id\":\"serial-only\",\"identity\":{\"productName\":\"LG HDR QHD\",\"serial\":\"ABC\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"gamma\",\"method\":\"vcp\",\"address\":114,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";

static const char *candidate_writable_pack =
    "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.3.0\",\"profiles\":[{\"id\":\"unsafe\",\"identity\":{\"productName\":\"LG HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"candidate\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,\"writable\":true,\"confidence\":\"candidate\",\"enums\":[]}]}]}";

static const char *same_authority_conflict_a =
    "{\"schemaVersion\":1,\"databaseVersion\":\"a\",\"minimumRSSDDCVersion\":\"0.3.0\",\"profiles\":[{\"id\":\"a\",\"identity\":{\"productName\":\"Conflict\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
static const char *same_authority_conflict_b =
    "{\"schemaVersion\":1,\"databaseVersion\":\"b\",\"minimumRSSDDCVersion\":\"0.3.0\",\"profiles\":[{\"id\":\"b\",\"identity\":{\"productName\":\"Conflict\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":18,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";

int main(void) {
    test_builtin_resolution_on_small_stack();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    RSSDDCProfilePackInfo info = {};
    assert(rss_ddc_profile_store_pack_info(store, &info) == RSS_DDC_OK);
    assert(info.schema_version == 1 && strcmp(info.database_version, "2026.08.13.1") == 0);

    RSSDDCProfileIdentity lg = lg_identity();
    RSSDDCEffectiveProfile effective = {};
    assert(rss_ddc_profile_store_resolve(store, &lg, &effective) == RSS_DDC_OK);
    assert(rss_ddc_effective_profile_control_count(&effective) == 1);
    RSSDDCProfileControl control = {};
    assert(rss_ddc_effective_profile_control(&effective, 0, &control) == RSS_DDC_OK);
    assert(control.id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE && control.address == 0x15 && control.write_authorized);
    assert(control.enum_value_count == 8);
    RSSDDCProfileEnumValue choice = {};
    assert(rss_ddc_profile_control_enum_value(&control, 1, &choice) == RSS_DDC_OK);
    assert(strcmp(choice.id, "vivid") == 0 && choice.raw_value == 0x31);
    assert(rss_ddc_profile_picture_mode_raw(&effective, RSS_DDC_PICTURE_MODE_READER, &choice.raw_value) == RSS_DDC_OK && choice.raw_value == 1);
    assert(rss_ddc_profile_picture_mode_from_raw(&effective, 0x48) == RSS_DDC_PICTURE_MODE_UNKNOWN);
    size_t export_size = 0;
    assert(rss_ddc_profile_store_export_json(store, NULL, 0, &export_size) == RSS_DDC_OK && export_size > 32);
    char export_data[8192] = {};
    assert(export_size <= sizeof(export_data));
    assert(rss_ddc_profile_store_export_json(store, export_data, sizeof(export_data), &export_size) == RSS_DDC_OK);
    assert(rss_ddc_profile_validate_pack_data(export_data, strlen(export_data), RSS_DDC_PROFILE_SOURCE_LOCAL, NULL) == RSS_DDC_OK);

    RSSDDCProfileIdentity g75 = lg; snprintf(g75.product_name, sizeof(g75.product_name), "Odyssey G75F");
    assert(rss_ddc_profile_store_resolve(store, &g75, &effective) == RSS_DDC_ERROR_NOT_FOUND);
    g75 = lg; g75.provider = RSS_DDC_PROVIDER_DCPDP_SERVICE;
    assert(rss_ddc_profile_store_resolve(store, &g75, &effective) == RSS_DDC_ERROR_NOT_FOUND);
    g75 = lg; snprintf(g75.transport, sizeof(g75.transport), "DCPEXT1");
    assert(rss_ddc_profile_store_resolve(store, &g75, &effective) == RSS_DDC_ERROR_NOT_FOUND);

    assert(rss_ddc_profile_store_load_local_data(store, local_response_pack, strlen(local_response_pack)) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_resolve(store, &lg, &effective) == RSS_DDC_OK);
    assert(effective.control_count == 2);
    bool response_found = false;
    for (size_t index = 0; index < effective.control_count; ++index) if (effective.controls[index].id == RSS_DDC_PROFILE_CONTROL_RESPONSE_TIME) { response_found = true; assert(effective.controls[index].source == RSS_DDC_PROFILE_SOURCE_LOCAL && effective.controls[index].write_authorized); }
    assert(response_found);

    size_t before = effective.control_count;
    assert(rss_ddc_profile_store_load_pack_data(store, "{", 1) == RSS_DDC_ERROR_PROFILE_MALFORMED);
    assert(rss_ddc_profile_store_resolve(store, &lg, &effective) == RSS_DDC_OK && effective.control_count == before);
    assert(rss_ddc_profile_store_load_pack_data(store, candidate_writable_pack, strlen(candidate_writable_pack)) == RSS_DDC_ERROR_PROFILE_UNSAFE);
    assert(rss_ddc_profile_validate_pack_data(candidate_writable_pack, strlen(candidate_writable_pack), RSS_DDC_PROFILE_SOURCE_RESEARCH, NULL) == RSS_DDC_ERROR_PROFILE_UNSAFE);
    const char *unknown_optional = "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.3.0\",\"futureMetadata\":{\"ignored\":true},\"profiles\":[]}";
    assert(rss_ddc_profile_validate_pack_data(unknown_optional, strlen(unknown_optional), RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK, NULL) == RSS_DDC_OK);
    const char *unsupported_schema = "{\"schemaVersion\":2,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.3.0\",\"profiles\":[]}";
    const char *unsupported_version = "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"9.0.0\",\"profiles\":[]}";
    assert(rss_ddc_profile_validate_pack_data(unsupported_schema, strlen(unsupported_schema), RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK, NULL) == RSS_DDC_ERROR_PROFILE_SCHEMA);
    assert(rss_ddc_profile_validate_pack_data(unsupported_version, strlen(unsupported_version), RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK, NULL) == RSS_DDC_ERROR_PROFILE_VERSION);

    RSSDDCProfileStore *serial_store = rss_ddc_profile_store_create();
    assert(rss_ddc_profile_store_load_pack_data(serial_store, serial_pack, strlen(serial_pack)) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_resolve(serial_store, &lg, &effective) == RSS_DDC_ERROR_NOT_FOUND);
    snprintf(lg.serial, sizeof(lg.serial), "ABC");
    assert(rss_ddc_profile_store_resolve(serial_store, &lg, &effective) == RSS_DDC_OK && effective.controls[0].id == RSS_DDC_PROFILE_CONTROL_GAMMA);

    RSSDDCProfileStore *conflict_store = rss_ddc_profile_store_create();
    assert(rss_ddc_profile_store_load_pack_data(conflict_store, same_authority_conflict_a, strlen(same_authority_conflict_a)) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_pack_data(conflict_store, same_authority_conflict_b, strlen(same_authority_conflict_b)) == RSS_DDC_OK);
    RSSDDCProfileIdentity conflict = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(conflict.product_name, sizeof(conflict.product_name), "Conflict"); snprintf(conflict.transport, sizeof(conflict.transport), "DCPEXT0");
    assert(rss_ddc_profile_store_resolve(conflict_store, &conflict, &effective) == RSS_DDC_ERROR_PROFILE_CONFLICT);

    rss_ddc_profile_store_destroy(conflict_store);
    rss_ddc_profile_store_destroy(serial_store);
    rss_ddc_profile_store_destroy(store);
    puts("test_profile_store: passed");
    return 0;
}
