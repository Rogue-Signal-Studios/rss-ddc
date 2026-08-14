#include "tri_state.h"

#include <string.h>

bool rss_ddc_cli_tri_state_parse(const char *text, RSSDDCCliTriState *out) {
    if (text == NULL || out == NULL) {
        return false;
    }
    if (strcmp(text, "yes") == 0) {
        *out = RSS_DDC_CLI_TRI_YES;
        return true;
    }
    if (strcmp(text, "no") == 0) {
        *out = RSS_DDC_CLI_TRI_NO;
        return true;
    }
    if (strcmp(text, "auto") == 0) {
        *out = RSS_DDC_CLI_TRI_AUTO;
        return true;
    }
    return false;
}

const char *rss_ddc_cli_tri_state_string(RSSDDCCliTriState state) {
    switch (state) {
    case RSS_DDC_CLI_TRI_YES: return "yes";
    case RSS_DDC_CLI_TRI_NO: return "no";
    case RSS_DDC_CLI_TRI_AUTO: return "auto";
    }
    return "auto";
}
