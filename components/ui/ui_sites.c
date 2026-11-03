#include <string.h>
#include <strings.h>
#include "ui_sites.h"

/* rp_id is domain itself or one of its subdomains. */
static bool matches(const char *rp_id, size_t rp_len, const char *domain, size_t len)
{
    if (rp_len < len || strcasecmp(rp_id + rp_len - len, domain) != 0) {
        return false;
    }
    return rp_len == len || rp_id[rp_len - len - 1] == '.';
}

const ui_site_t *ui_site_find(const char *rp_id)
{
    if (!rp_id || !rp_id[0]) {
        return NULL;
    }
    size_t rp_len = strlen(rp_id);
    const ui_site_t *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < ui_site_count; i++) {
        for (const char *const *d = ui_sites[i].domains; *d; d++) {
            size_t len = strlen(*d);
            if (len > best_len && matches(rp_id, rp_len, *d, len)) {
                best = &ui_sites[i];
                best_len = len;
            }
        }
    }
    return best;
}
