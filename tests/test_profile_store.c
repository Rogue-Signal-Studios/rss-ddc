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
    rss_ddc_profile_store_destroy(round); rss_ddc_profile_store_destroy(s); free(e);free(sm->out);free(sm); puts("test_profile_store: passed"); return 0;
}
