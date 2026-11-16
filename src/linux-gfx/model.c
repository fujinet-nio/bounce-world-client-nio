#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "bwc_interpolation_math.h"

int gfx_settings_valid(const GfxSettings *s)
{
    size_t i, n = strlen(s->name), e = strlen(s->endpoint);
    if (!n || n > 8 || !e || e > 60 || s->interval < 10 || s->interval > 1000) return 0;
    for (i = 0; i < n; ++i)
        if ((unsigned char)s->name[i] < 33 || (unsigned char)s->name[i] > 126 || s->name[i] == ',') return 0;
    for (i = 0; i < e; ++i)
        if ((unsigned char)s->endpoint[i] < 33 || (unsigned char)s->endpoint[i] > 126) return 0;
    if (strstr(s->endpoint, "://") && strncmp(s->endpoint, "tcp://", 6) && strncmp(s->endpoint, "tls://", 6)) return 0;
    return strcmp(s->endpoint, "tcp://") && strcmp(s->endpoint, "tls://");
}

void gfx_interpolate(const GfxSnapshot *p, const GfxSnapshot *c,
                     uint32_t now, int enabled, int wrapped, ShapePos *out)
{
    unsigned i;
    uint32_t interval = c->time - p->time;
    for (i = 0; i < c->count; ++i) {
        ShapePos prior;
        out[i] = c->shapes[i];
        if (!enabled || !p->serial || interval == 0 || interval > 1000 ||
            !bwc_interp_match_body_image(p->shapes, p->count, &c->shapes[i],
                wrapped ? GFX_WIDTH : 0, wrapped ? GFX_HEIGHT : 0, &prior)) continue;
        /* New bodies, reset teleports, and shape substitutions snap. Never
         * extrapolate stale data beyond the newest authoritative snapshot. */
        if (prior.shape_id != c->shapes[i].shape_id ||
            abs(prior.x - c->shapes[i].x) > GFX_WIDTH / 4 ||
            abs(prior.y - c->shapes[i].y) > GFX_HEIGHT / 4) continue;
        bwc_interp_blend(&out[i], &prior, &c->shapes[i], now - c->time,
                        interval, 1000, 0, 0);
    }
}
