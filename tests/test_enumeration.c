#include <assert.h>
#include <stdio.h>

#include "enumeration.h"
#include "rss_ddc.h"

int main(void) {
    /* A NULL/zero-capacity public query writes no snapshots but reports the total. */
    assert(rss_ddc_enumeration_write_count(0, 0) == 0);
    assert(rss_ddc_enumeration_write_count(3, 0) == 0);
    assert(rss_ddc_enumeration_write_count(3, 3) == 3);
    assert(rss_ddc_enumeration_write_count(3, 1) == 1);
    assert(RSS_DDC_VERSION_MAJOR == 0 && RSS_DDC_VERSION_MINOR == 1 && RSS_DDC_VERSION_PATCH == 0);
    puts("test_enumeration: passed");
    return 0;
}
