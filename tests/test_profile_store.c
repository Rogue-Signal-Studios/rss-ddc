#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rss_ddc.h"

static const char *pack = "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\",\"profiles\":[{\"id\":\"one\",\"identity\":{\"productName\":\"Test\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
static RSSDDCProfileIdentity identity(void) { RSSDDCProfileIdentity x={.external=true,.provider=RSS_DDC_PROVIDER_DCPDP13}; snprintf(x.product_name,sizeof(x.product_name),"Test");snprintf(x.transport,sizeof(x.transport),"DCPEXT0");return x; }
typedef struct { RSSDDCProfileIdentity id; RSSDDCEffectiveProfile *out; RSSDDCError error; } Small;
static void *small(void *p) { Small *s=p; RSSDDCProfileStore *store=rss_ddc_profile_store_create(); if(!store){s->error=RSS_DDC_ERROR_SYSTEM;return NULL;} s->error=rss_ddc_profile_store_load_builtin(store); if(!s->error)s->error=rss_ddc_profile_store_resolve(store,&s->id,s->out); rss_ddc_profile_store_destroy(store); return NULL; }
static char *export_json(const RSSDDCProfileStore *s, RSSDDCError (*exporter)(const RSSDDCProfileStore *, char *, size_t, size_t *)) {
    size_t n = 0;
    char *json = NULL;
    assert(exporter(s, NULL, 0, &n) == RSS_DDC_OK);
    json = malloc(n);
    assert(json != NULL);
    assert(exporter(s, json, n, &n) == RSS_DDC_OK);
    return json;
}
static void test_local_export_omits_builtin_and_uses_local_metadata(void) {
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCProfileStore *reload = rss_ddc_profile_store_create();
    RSSDDCProfileIdentity identity = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    RSSDDCProfileControl input = {.id = RSS_DDC_PROFILE_CONTROL_INPUT,
                                  .method = RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT,
                                  .address = 0xf4,
                                  .readable = false,
                                  .writable = true,
                                  .confidence = RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED,
                                  .enum_value_count = 3};
    RSSDDCEffectiveProfile effective = {0};
    RSSDDCProfileControl picture = {0};
    RSSDDCProfileControl resolved_input = {0};
    RSSDDCProfilePackInfo info = {0};
    char path[] = "/private/tmp/rss-ddc-local-export-XXXXXX";
    char *all = NULL;
    char *local = NULL;
    int fd = -1;
    assert(store != NULL && reload != NULL);
    snprintf(identity.product_name, sizeof(identity.product_name), "%s", "LG HDR QHD");
    snprintf(identity.transport, sizeof(identity.transport), "%s", "DCPEXT0");
    input.enum_values[0] = (RSSDDCProfileEnumValue){.raw_value = 0x90};
    input.enum_values[1] = (RSSDDCProfileEnumValue){.raw_value = 0x91};
    input.enum_values[2] = (RSSDDCProfileEnumValue){.raw_value = 0xd0};
    snprintf(input.enum_values[0].id, sizeof(input.enum_values[0].id), "%s", "hdmi-1");
    snprintf(input.enum_values[0].name, sizeof(input.enum_values[0].name), "%s", "HDMI 1");
    snprintf(input.enum_values[1].id, sizeof(input.enum_values[1].id), "%s", "hdmi-2");
    snprintf(input.enum_values[1].name, sizeof(input.enum_values[1].name), "%s", "HDMI 2");
    snprintf(input.enum_values[2].id, sizeof(input.enum_values[2].id), "%s", "dp-1");
    snprintf(input.enum_values[2].name, sizeof(input.enum_values[2].name), "%s", "DisplayPort 1");
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_put_local_profile(store, "lg-local-input", &identity,
                                                   RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, &input,
                                                   1) == RSS_DDC_OK);
    all = export_json(store, rss_ddc_profile_store_export_json);
    local = export_json(store, rss_ddc_profile_store_export_local_json);
    assert(strstr(all, "rogue-builtin") != NULL);
    assert(strstr(all, "picture-mode") != NULL);
    assert(strstr(local, "rogue-builtin") == NULL);
    assert(strstr(local, "picture-mode") == NULL);
    assert(strstr(local, "\"packId\":\"local-export\"") != NULL);
    assert(strstr(local, "\"databaseVersion\":\"local-export\"") != NULL);
    assert(strstr(local, "lg-alt-input") != NULL);
    assert(strstr(local, "\"address\":244") != NULL);
    assert(strstr(local, "\"value\":144") != NULL);
    assert(strstr(local, "\"value\":145") != NULL);
    assert(strstr(local, "\"value\":208") != NULL);
    assert(rss_ddc_profile_validate_pack_data(local, strlen(local), RSS_DDC_PROFILE_SOURCE_LOCAL, &info) ==
           RSS_DDC_OK);
    assert(info.schema_version == 1);
    assert(strcmp(info.pack_id, "local-export") == 0);
    fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(rss_ddc_profile_store_save_local_file(store, path) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_builtin(reload) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_local_file(reload, path) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_resolve(reload, &identity, &effective) == RSS_DDC_OK);
    for (size_t index = 0; index < rss_ddc_effective_profile_control_count(&effective); ++index) {
        RSSDDCProfileControl control = {0};
        assert(rss_ddc_effective_profile_control(&effective, index, &control) == RSS_DDC_OK);
        if (control.id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE) {
            picture = control;
        } else if (control.id == RSS_DDC_PROFILE_CONTROL_INPUT) {
            resolved_input = control;
        }
    }
    assert(picture.method == RSS_DDC_PROFILE_METHOD_VCP && picture.address == 0x15);
    assert(picture.source == RSS_DDC_PROFILE_SOURCE_BUILTIN);
    assert(resolved_input.method == RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT && resolved_input.address == 0xf4);
    assert(resolved_input.source == RSS_DDC_PROFILE_SOURCE_LOCAL);
    unlink(path);
    rss_ddc_profile_store_destroy(reload);
    rss_ddc_profile_store_destroy(store);
    free(all);
    free(local);
}
int main(void) {
    RSSDDCProfileStore *s=rss_ddc_profile_store_create(); assert(s);
    RSSDDCEffectiveProfile *e=calloc(1,sizeof(*e)); assert(e);
    RSSDDCProfileIdentity id=identity(); assert(rss_ddc_profile_store_resolve(s,&id,e)==RSS_DDC_ERROR_NOT_FOUND);
    assert(rss_ddc_profile_store_load_pack_data(s,pack,strlen(pack))==RSS_DDC_OK);
    assert(rss_ddc_profile_store_resolve(s,&id,e)==RSS_DDC_OK && e->control_count==1);
    RSSDDCProfileControl c={}; assert(rss_ddc_effective_profile_control(e,0,&c)==RSS_DDC_OK && c.address==0x10);
    RSSDDCEffectiveProfile before=*e; assert(rss_ddc_profile_store_load_pack_data(s,"{",1)==RSS_DDC_ERROR_PROFILE_MALFORMED); assert(!memcmp(e,&before,sizeof(*e)));
    const char *unknown="{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\",\"future\":{\"x\":true},\"profiles\":[]}";
    assert(rss_ddc_profile_validate_pack_data(unknown,strlen(unknown),RSS_DDC_PROFILE_SOURCE_LOCAL,NULL)==RSS_DDC_OK);
    char path[]="/private/tmp/rss-ddc-profile-XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd); assert(rss_ddc_profile_store_save_file(s,path)==RSS_DDC_OK);
    RSSDDCProfileStore *round=rss_ddc_profile_store_create(); assert(rss_ddc_profile_store_load_local_file(round,path)==RSS_DDC_OK); assert(rss_ddc_profile_store_resolve(round,&id,e)==RSS_DDC_OK); unlink(path);
    for(int i=0;i<8;i++){RSSDDCProfileStore *cycle=rss_ddc_profile_store_create();assert(rss_ddc_profile_store_load_pack_data(cycle,pack,strlen(pack))==RSS_DDC_OK);rss_ddc_profile_store_destroy(cycle);}
    /* fc48972 regression: builtin parse/store/resolution stays heap-backed on a 512 KiB thread stack. */
    Small *sm=calloc(1,sizeof(*sm)); sm->id.product_name[0]='\0'; snprintf(sm->id.product_name,sizeof(sm->id.product_name),"LG HDR QHD");snprintf(sm->id.transport,sizeof(sm->id.transport),"DCPEXT0");sm->id.external=true;sm->id.provider=RSS_DDC_PROVIDER_DCPDP13;sm->out=calloc(1,sizeof(*sm->out)); pthread_attr_t a; pthread_t t; assert(!pthread_attr_init(&a));assert(!pthread_attr_setstacksize(&a,512*1024));assert(!pthread_create(&t,&a,small,sm));assert(!pthread_join(t,NULL));assert(sm->error==RSS_DDC_OK);
    rss_ddc_profile_store_destroy(round); rss_ddc_profile_store_destroy(s); free(e);free(sm->out);free(sm);
    test_local_export_omits_builtin_and_uses_local_metadata();
    puts("test_profile_store: passed"); return 0;
}
