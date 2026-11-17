#include <SDL.h>
#include <inttypes.h>
#include <stdio.h>
#include "fujinet-nio.h"
#include "diagnostics.h"
const char *gfx_nio_result(unsigned code)
{
    switch (code) {
        case FN_OK: return "OK";
        case FN_ERR_NOT_FOUND: return "NOT_FOUND";
        case FN_ERR_INVALID: return "INVALID";
        case FN_ERR_BUSY: return "BUSY";
        case FN_ERR_NOT_READY: return "NOT_READY";
        case FN_ERR_IO: return "IO";
        case FN_ERR_TIMEOUT: return "TIMEOUT";
        case FN_ERR_INTERNAL: return "INTERNAL";
        case FN_ERR_UNSUPPORTED: return "UNSUPPORTED";
        case FN_ERR_TRANSPORT: return "TRANSPORT";
        default: return "OTHER";
    }
}
void gfx_diag_begin(GfxDiagnostics *d, const char *operation)
{
    if (!d) return;
    d->active = 1; d->operation_at = SDL_GetTicks64();
    snprintf(d->operation,sizeof(d->operation),"%s",operation);
}
void gfx_diag_end(GfxDiagnostics *d)
{
    if (!d) return;
    d->operation_last_ms = SDL_GetTicks64()-d->operation_at;
    if (d->operation_last_ms>d->operation_max_ms) d->operation_max_ms=d->operation_last_ms;
    d->active = 0;
}
void gfx_diag_error(GfxDiagnostics *d, const char *reason, unsigned code)
{
    if (!d) return;
    ++d->errors; ++d->failure_serial;
    d->failure_at = SDL_GetTicks64(); d->last_code = code;
    snprintf(d->last_error,sizeof(d->last_error),"%s / %s: %s [%s/%u]",
        d->command[0] ? d->command : "session",d->operation,reason,gfx_nio_result(code),code);
    snprintf(d->failure_detail,sizeof(d->failure_detail),
        "op=%" PRIu64 "ms request=%" PRIu64 "ms frame=%u/%u RX/TX offset=%u/%u flags=0x%02x bytes=%u/%u",
        d->operation_last_ms,d->failure_at-d->request_at,d->received,d->expected,d->read_offset,d->write_offset,
        d->last_flags,d->last_bytes,d->requested_bytes);
    /* Keep failure evidence even when cleanup changes the current operation. */
    fprintf(stderr,"[bwc-gfx +%" PRIu64 "ms] %s; op=%" PRIu64 "ms frame=%u/%u offsets(rx/tx)=%u/%u flags=0x%02x bytes=%u/%u\n",
        d->failure_at-d->started,d->last_error,d->operation_last_ms,d->received,d->expected,
        d->read_offset,d->write_offset,d->last_flags,d->last_bytes,d->requested_bytes);
}

void gfx_rates_sample(GfxRates *r, const GfxDiagnostics *d, uint64_t now, uint64_t frames)
{
    if (r->initialized && now-r->at<1000) return;
    if (r->initialized) {
        double seconds=(double)(now-r->at)/1000.0;
        r->tx_per_second=(double)(d->tx_bytes-r->tx)/seconds;
        r->rx_per_second=(double)(d->rx_bytes-r->rx)/seconds;
        r->requests_per_second=(double)(d->requests-r->requests)/seconds;
        r->worlds_per_second=(double)(d->worlds_ok-r->worlds)/seconds;
        r->fps=(double)(frames-r->frames)/seconds;
    }
    r->initialized=1; r->at=now; r->tx=d->tx_bytes; r->rx=d->rx_bytes;
    r->requests=d->requests; r->worlds=d->worlds_ok; r->frames=frames;
}
