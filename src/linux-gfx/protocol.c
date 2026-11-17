#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"

void gfx_wire_notify(GfxWire *w)
{
    if (w->diagnostics && w->notify) w->notify(w->context,w->diagnostics);
}
static int cancelled(GfxWire *w)
{
    if (!w->cancelled || !w->cancelled(w->context)) return 0;
    snprintf(w->error, sizeof(w->error), "Cancelled");
    return 1;
}
static int fail(GfxWire *w, const char *what, unsigned error)
{
    snprintf(w->error, sizeof(w->error), "%s (%s/%u)",what,gfx_nio_result(error),error);
    gfx_diag_error(w->diagnostics,what,error);
    gfx_wire_notify(w);
    return -1;
}
int gfx_wire_invalid(GfxWire *w, const char *reason)
{
    if (w->diagnostics) ++w->diagnostics->malformed;
    return fail(w,reason,FN_ERR_INVALID);
}
int gfx_wire_send(GfxWire *w, const char *command)
{
    GfxDiagnostics *d=w->diagnostics;
    char line[160];
    uint16_t written = 0;
    uint8_t result;
    size_t n = strlen(command);
    if (cancelled(w)) return -1;
    if (d) {
        /* The verb is enough to identify a request; do not log pilot/URL data. */
        size_t verb=strcspn(command," \r\n");
        if (verb>=sizeof(d->command)) verb=sizeof(d->command)-1;
        memcpy(d->command,command,verb); d->command[verb]=0;
        d->request_at=SDL_GetTicks64(); d->pending=1; d->received=d->expected=0;
        d->read_offset=w->read_offset; d->write_offset=w->write_offset;
    }
    if (n + 1 >= sizeof(line) || strchr(command, '\n') || strchr(command, '\r'))
        return gfx_wire_invalid(w,"Invalid command");
    memcpy(line, command, n);
    line[n++] = '\n';
    if (d) { ++d->requests; d->requested_bytes=(unsigned)n; d->last_bytes=0; }
    gfx_diag_begin(d,"fn_write"); gfx_wire_notify(w);
    result = fn_write(w->handle, w->write_offset, (const uint8_t *)line, (uint16_t)n, &written);
    gfx_diag_end(d);
    if (d) {
        fn_write_diagnostics_t detail;
        memset(&detail,0,sizeof(detail)); fn_write_get_last_diagnostics(&detail);
        d->write_exchanges+=detail.exchanges; d->transport_retries+=detail.transport_retry;
        d->service_retries+=detail.service_retry; d->zero_accepted+=detail.zero_accepted;
        d->response_length=detail.response_length; d->response_device=detail.response_device;
        d->response_command=detail.response_command;
        d->write_ms=d->operation_last_ms;
        if (d->write_ms>d->write_max_ms) d->write_max_ms=d->write_ms;
        d->last_code=result; d->last_bytes=written; d->tx_bytes+=written;
        if (result==FN_ERR_TIMEOUT) ++d->nio_timeouts;
        if (written!=n) ++d->partial_writes;
        if (result==FN_OK && written==n) ++d->writes_ok;
    }
    gfx_wire_notify(w);
    /* Never blindly replay a partially accepted command. Drop the session. */
    if (result != FN_OK || written != n) return fail(w, "Write failed or incomplete", result);
    w->write_offset += written;
    if (d) d->write_offset=w->write_offset;
    return 0;
}
int gfx_wire_reply(GfxWire *w)
{
    GfxDiagnostics *d=w->diagnostics;
    unsigned used = 0, total = 0;
    uint64_t start = SDL_GetTicks64();
    uint32_t timeout=w->reply_timeout_ms ? w->reply_timeout_ms : 5000;
    while (!total || used < total) {
        uint16_t got = 0;
        uint8_t flags = 0;
        unsigned capacity = (total ? total : sizeof(w->packet)) - used;
        uint8_t result;
        if (cancelled(w)) return -1;
        if (SDL_GetTicks64() - start >= timeout) {
            if (d) ++d->deadlines;
            return fail(w, "Client reply deadline exceeded", FN_ERR_TIMEOUT);
        }
        if (d) { ++d->read_calls; d->requested_bytes=capacity; d->last_bytes=0; }
        gfx_diag_begin(d,"fn_read"); gfx_wire_notify(w);
        result = fn_read(w->handle, w->read_offset, w->packet + used, (uint16_t)capacity, &got, &flags);
        gfx_diag_end(d);
        if (d) {
            d->read_ms=d->operation_last_ms;
            if (d->read_ms>d->read_max_ms) d->read_max_ms=d->read_ms;
            d->last_code=result; d->last_bytes=got; d->last_flags=flags; d->rx_bytes+=got;
            if (result==FN_OK) ++d->reads_ok;
            if (result==FN_ERR_NOT_READY) ++d->not_ready;
            if (result==FN_ERR_BUSY) ++d->busy;
            if (result==FN_ERR_TIMEOUT) ++d->nio_timeouts;
            if (result==FN_OK && !got && !(flags & FN_READ_EOF)) ++d->empty_reads;
            if (flags & FN_READ_EOF) ++d->eof;
        }
        gfx_wire_notify(w);
        if (result == FN_ERR_NOT_READY || result == FN_ERR_BUSY) { SDL_Delay(5); continue; }
        if (result != FN_OK) return fail(w, "Read failed", result);
        if (got > capacity) return gfx_wire_invalid(w,"Oversized read");
        used += got;
        w->read_offset += got;
        if (used >= 2) total = w->packet[0] | ((unsigned)w->packet[1] << 8);
        if (d) { d->received=used; d->expected=total; d->read_offset=w->read_offset; }
        if (used >= 2) {
            if (total < 2 || total > sizeof(w->packet) || used > total)
                return gfx_wire_invalid(w,"Invalid frame length");
            if (used == total) {
                if (d) {
                    ++d->frames_ok; d->pending=0; d->last_reply_at=SDL_GetTicks64();
                    d->request_ms=d->last_reply_at-d->request_at;
                    d->request_total_ms+=d->request_ms;
                    if (d->request_ms>d->request_max_ms) d->request_max_ms=d->request_ms;
                    if (d->request_ms>=1000) ++d->slow_requests;
                }
                gfx_wire_notify(w);
                return (int)total - 2;
            }
        }
        if (flags & FN_READ_EOF) return fail(w, "Connection closed mid-frame", FN_ERR_IO);
        gfx_wire_notify(w);
        if (!got) SDL_Delay(5);
    }
    return -1;
}
