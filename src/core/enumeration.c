#include "enumeration.h"

size_t rss_ddc_enumeration_write_count(size_t total, size_t capacity) {
    return total < capacity ? total : capacity;
}
