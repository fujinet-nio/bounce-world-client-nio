#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"

static int cancelled(GfxWire *w)
{
    if (!w->cancelled || !w->cancelled(w->context)) return 0;
    snprintf(w->error, sizeof(w->error), "Cancelled");
    return 1;
}
static int fail(GfxWire *w, const char *what, unsigned error)
{
    snprintf(w->error, sizeof(w->error), "%s (NIO %u)", what, error);
    return -1;
}
int gfx_wire_send(GfxWire *w, const char *command)
{
    char line[160];
    uint16_t written = 0;
    uint8_t result;
    size_t n = strlen(command);
    if (n + 1 >= sizeof(line) || strchr(command, '\n') || strchr(command, '\r'))
        return fail(w, "Invalid command", FN_ERR_INVALID);
    if (cancelled(w)) return -1;
    memcpy(line, command, n);
    line[n++] = '\n';
    result = fn_write(w->handle, w->write_offset, (const uint8_t *)line, (uint16_t)n, &written);
    /* Never blindly replay a partially accepted command. Drop the session. */
    if (result != FN_OK || written != n) return fail(w, "Write failed or incomplete", result);
    w->write_offset += written;
    return 0;
}
int gfx_wire_reply(GfxWire *w)
{
    unsigned used = 0, total = 0;
    uint32_t start = SDL_GetTicks();
    while (!total || used < total) {
        uint16_t got = 0;
        uint8_t flags = 0;
        unsigned capacity = (total ? total : sizeof(w->packet)) - used;
        uint8_t result;
        if (cancelled(w)) return -1;
        if (SDL_GetTicks() - start >= 5000) return fail(w, "Server response timed out", FN_ERR_TIMEOUT);
        result = fn_read(w->handle, w->read_offset, w->packet + used, (uint16_t)capacity, &got, &flags);
        if (result == FN_ERR_NOT_READY || result == FN_ERR_BUSY) { SDL_Delay(5); continue; }
        if (result != FN_OK) return fail(w, "Read failed", result);
        if (got > capacity) return fail(w, "Oversized read", FN_ERR_INVALID);
        used += got;
        w->read_offset += got;
        if (used >= 2) {
            total = w->packet[0] | ((unsigned)w->packet[1] << 8);
            if (total < 2 || total > sizeof(w->packet) || used > total)
                return fail(w, "Invalid frame length", FN_ERR_INVALID);
            if (used == total) return (int)total - 2;
        }
        if (flags & FN_READ_EOF) return fail(w, "Connection closed mid-frame", FN_ERR_IO);
        if (!got) SDL_Delay(5);
    }
    return -1;
}
