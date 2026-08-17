// C++ inclusion/linkage check for the installed public C API. CI never runs it.
#include <rss_ddc.h>

static_assert(RSS_DDC_VERSION_MAJOR == 0, "rss-ddc remains pre-1.0");
static_assert(RSS_DDC_CHARACTERIZATION_ACTION_COMPLETE == 5,
              "COMPLETE remains stable; WAIT_FOR_INTERACTION is appended");
static_assert(RSS_DDC_CHARACTERIZATION_ACTION_WAIT_FOR_INTERACTION == 6,
              "WAIT_FOR_INTERACTION is appended after COMPLETE");
static_assert(RSS_DDC_CHARACTERIZATION_STAGE_BLOCKED == 6, "BLOCKED remains stable");
static_assert(RSS_DDC_CHARACTERIZATION_STAGE_INTERACTION == 7, "INTERACTION is appended after BLOCKED");
static_assert(RSS_DDC_CHARACTERIZATION_INTERACTION_NONE == 0, "NONE is the empty interaction");

int main() {
    return rss_ddc_provider_backend(RSS_DDC_PROVIDER_PS190) == RSS_DDC_BACKEND_PS190 ? 0 : 1;
}
