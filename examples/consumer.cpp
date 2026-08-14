// C++ inclusion/linkage check for the installed public C API. CI never runs it.
#include <rss_ddc.h>

static_assert(RSS_DDC_VERSION_MAJOR == 0, "rss-ddc remains pre-1.0");

int main() {
    return rss_ddc_provider_backend(RSS_DDC_PROVIDER_PS190) == RSS_DDC_BACKEND_PS190 ? 0 : 1;
}
