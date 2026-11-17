#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "protocol.h"
#include "add_client_csv.h"
#include "appstore_settings.h"
#include "status.h"

static int cancelled(void *context)
{
    GfxClient *c = context;
    int stop;
    SDL_LockMutex(c->mutex);
    stop = c->stop || c->disconnect;
    SDL_UnlockMutex(c->mutex);
    return stop;
}
static void publish(GfxClient *c, const GfxState *s)
{
    SDL_LockMutex(c->mutex);
    c->state = *s;
    SDL_UnlockMutex(c->mutex);
}
static void publish_diagnostics(void *context, const GfxDiagnostics *d)
{
    GfxClient *c=context;
    SDL_LockMutex(c->mutex);
    c->diagnostics=*d;
    SDL_UnlockMutex(c->mutex);
}
static void api_begin(GfxClient *c, GfxDiagnostics *d, const char *operation)
{
    d->command[0]=0;
    ++d->other_calls;
    gfx_diag_begin(d,operation); publish_diagnostics(c,d);
}
static void api_end(GfxClient *c, GfxDiagnostics *d, unsigned result)
{
    gfx_diag_end(d); d->last_code=result;
    if (result==FN_OK) ++d->other_ok;
    else {
        if (result==FN_ERR_TIMEOUT) ++d->nio_timeouts;
        gfx_diag_error(d,"NIO operation failed",result);
    }
    publish_diagnostics(c,d);
}
static void load_setting(GfxClient *c, GfxDiagnostics *d, char *buffer, uint8_t n, const char *key)
{
    api_begin(c,d,"load settings");
    appstore_read_setting(buffer,n,key);
    api_end(c,d,appstore_last_error());
}
static int save_setting(GfxClient *c, GfxDiagnostics *d, const char *value, const char *key)
{
    int ok;
    api_begin(c,d,"save settings");
    ok=appstore_write_setting(value,key);
    api_end(c,d,appstore_last_error());
    return ok;
}
static int reply(GfxWire *w, const char *command, int expected)
{
    int n;
    if (gfx_wire_send(w, command) < 0) return -1;
    n = gfx_wire_reply(w);
    if (n >= 0 && expected >= 0 && n != expected) {
        char reason[120];
        snprintf(reason,sizeof(reason),"Unexpected reply length: got %d, expected %d",n,expected);
        return gfx_wire_invalid(w,reason);
    }
    return n;
}
static unsigned le16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void printable(char *dst, const uint8_t *src, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) dst[i] = src[i] >= 32 && src[i] <= 126 ? (char)src[i] : ' ';
    dst[n] = 0;
}
static int world(GfxWire *w, GfxState *s)
{
    const uint8_t *p = w->packet + 2;
    if (reply(w, "x-ws", 14) < 0) return -1;
    s->width = le16(p); s->height = le16(p + 2); s->bodies = le16(p + 4);
    memcpy(s->sizes, p + 6, 5);
    s->clients = p[11]; s->frozen = p[12]; s->wrapped = p[13];
    return 0;
}
static int who(GfxWire *w, GfxState *s)
{
    int n = reply(w, "x-who", -1);
    unsigned i;
    if (n < 0) return -1;
    s->names_count = (unsigned)n / 8;
    if (s->names_count > 20) s->names_count = 20;
    for (i = 0; i < s->names_count; ++i) printable(s->names[i], w->packet + 2 + i * 8, 8);
    return 0;
}
static int server_commands(GfxWire *w, GfxState *s, unsigned id)
{
    char command[32];
    int n, i, fetch_message = 0;
    snprintf(command, sizeof(command), "x-cmd-get %u", id);
    n = reply(w, command, -1);
    if (n < 0) return -1;
    for (i = 0; i < n; ++i) {
        switch (w->packet[i + 2]) {
            case 1: s->dark = 1; break;
            case 2: s->dark = 0; break;
            case 3: s->who = 1; break;
            case 4: s->who = 0; break;
            case 5: s->broadcast = 1; fetch_message = 1; break;
            case 6: s->broadcast = 0; break;
            case 7: s->info = 1; break;
            case 8: s->info = 0; break;
            default: break;
        }
    }
    ++s->controls_serial;
    if (fetch_message) {
        n = reply(w, "x-msg", -1);
        if (n < 0) return -1;
        if (n > 119) n = 119;
        printable(s->message, w->packet + 2, (unsigned)n);
    }
    return 0;
}
static char next_command(GfxClient *c)
{
    char key = 0;
    SDL_LockMutex(c->mutex);
    if (c->length) {
        key = c->commands[c->head];
        c->head = (c->head + 1) % sizeof(c->commands);
        --c->length;
    }
    SDL_UnlockMutex(c->mutex);
    return key;
}
static int user_command(GfxWire *w, GfxState *s, char key)
{
    char command[32];
    const char *cmd = NULL;
    switch (key) {
        case '+': cmd = "x-inc"; break;
        case '-': cmd = "x-dec"; break;
        case 'f': cmd = "x-freeze"; break;
        case 'r': cmd = "x-reset"; break;
        default:
            if (key < '1' || key > '5') return 0;
            snprintf(command, sizeof(command), "x-add-body %c", key);
            cmd = command;
    }
    if (reply(w, cmd, 1) < 0) return -1;
    return world(w, s);
}
static int session(GfxClient *c, GfxWire *w, GfxState *s)
{
    char csv[100], command[128], url[80];
    unsigned id;
    uint8_t result;
    if (!gfx_settings_valid(&s->settings)) {
        return gfx_wire_invalid(w,"Invalid destination, pilot or fetch interval");
    }
    snprintf(url, sizeof(url), "%s%s", strstr(s->settings.endpoint, "://") ? "" : "tcp://", s->settings.endpoint);
    if (cancelled(c)) return -1;
    api_begin(c,w->diagnostics,"fn_open");
    result = fn_open(&w->handle, 0, url, FN_OPEN_STREAM_NO_PROBE);
    api_end(c,w->diagnostics,result);
    if (result != FN_OK) {
        snprintf(w->error, sizeof(w->error), "Connect failed (NIO %u). Check FN_PORT and destination.", result);
        return -1;
    }
    if (!bwc_build_add_client_csv(csv, sizeof(csv), s->settings.name, 3,
                                  GFX_WIDTH, GFX_HEIGHT, 40, 24, GFX_CAPS)) return gfx_wire_invalid(w,"Registration CSV overflow");
    snprintf(command, sizeof(command), "x-add-client %s", csv);
    if (reply(w, command, 1) < 0) return -1;
    id = w->packet[2];
    if (!id) return gfx_wire_invalid(w,"Server rejected registration");
    if (world(w, s) < 0 || who(w, s) < 0) return -1;
    s->connecting = 0; s->connected = 1;
    ++w->diagnostics->connections;
    snprintf(w->diagnostics->end_reason,sizeof(w->diagnostics->end_reason),"Connected");
    gfx_wire_notify(w); publish(c, s);
    while (!cancelled(c)) {
        int n;
        unsigned i;
        uint8_t status;
        uint32_t start = SDL_GetTicks();
        char key;
        while ((key = next_command(c)) != 0) {
            if (user_command(w, s, key) < 0) return -1;
        }
        snprintf(command, sizeof(command), "x-w %u", id);
        n = reply(w, command, -1);
        if (n < 0) return -1;
        /* Header: step, status, count; WIDE_COORDS + BODY_ID is 9 bytes. */
        if (n < 3 || n != 3 + w->packet[4] * 9) {
            char reason[120];
            snprintf(reason,sizeof(reason),"Malformed world snapshot: bytes=%d count=%u",n,n>=3 ? w->packet[4] : 0);
            return gfx_wire_invalid(w,reason);
        }
        status = w->packet[3];
        /* Decode before auxiliary requests overwrite the packet buffer. */
        if (!s->snapshot.serial || s->snapshot.step != w->packet[2] ||
            !w->packet[4] || (status & (OBJECT_CHANGE | FROZEN_TOGGLE))) {
            s->snapshot.count = bwc_decode_shapes(w->packet + 5, (uint16_t)(n - 3),
                w->packet[4], GFX_CAPS, s->snapshot.shapes, GFX_MAX_SHAPES);
            for (i = 0; i < s->snapshot.count; ++i) {
                if (s->snapshot.shapes[i].shape_id >= 19 || !s->snapshot.shapes[i].body_id) {
                    char reason[120];
                    snprintf(reason,sizeof(reason),"Unknown shape or missing body ID: record=%u shape=%u body=%u",
                        i,s->snapshot.shapes[i].shape_id,s->snapshot.shapes[i].body_id);
                    return gfx_wire_invalid(w,reason);
                }
            }
            s->snapshot.step = w->packet[2];
            s->snapshot.status = status;
            s->snapshot.time = SDL_GetTicks();
            ++s->snapshot.serial;
            ++w->diagnostics->steps;
        }
        ++w->diagnostics->worlds_ok;
        w->diagnostics->last_world_at=SDL_GetTicks64();
        gfx_wire_notify(w);
        if (status & COLLISION) ++s->collision_serial;
        if ((status & (CLIENT_CHANGE | OBJECT_CHANGE | FROZEN_TOGGLE)) && world(w, s) < 0) return -1;
        if ((status & CLIENT_CHANGE) && who(w, s) < 0) return -1;
        if ((status & CLIENT_CMD) && server_commands(w, s, id) < 0) return -1;
        publish(c, s);
        snprintf(w->diagnostics->operation,sizeof(w->diagnostics->operation),"fetch pacing");
        gfx_wire_notify(w);
        while (!cancelled(c) && SDL_GetTicks() - start < s->settings.interval) SDL_Delay(5);
    }
    /* Best effort deregistration after a clean stop. Do not wait for a reply. */
    w->cancelled = NULL;
    snprintf(command, sizeof(command), "close %u", id);
    (void)gfx_wire_send(w, command);
    return 0;
}
static int worker(void *context)
{
    GfxClient *c = context;
    GfxState s;
    GfxDiagnostics d;
    int initialized;
    uint8_t result;
    memset(&s, 0, sizeof(s));
    memset(&d,0,sizeof(d)); d.started=SDL_GetTicks64();
    s.settings.interval = 100;
    api_begin(c,&d,"fn_init");
    result = fn_init();
    api_end(c,&d,result);
    initialized = result == FN_OK;
    if (initialized) {
        char interval[8];
        load_setting(c,&d,s.settings.endpoint,sizeof(s.settings.endpoint),APPSTORE_KEY_ENDPOINT);
        if (cancelled(c)) goto done;
        load_setting(c,&d,s.settings.name,sizeof(s.settings.name),APPSTORE_KEY_NAME);
        if (cancelled(c)) goto done;
        interval[0]=0;
        load_setting(c,&d,interval,sizeof(interval),APPSTORE_KEY_FETCH_INTERVAL);
        if (interval[0]) {
            char *end;
            unsigned long n = strtoul(interval, &end, 10);
            if (!*end && n >= 10 && n <= 1000) s.settings.interval = (unsigned)n;
        }
    } else snprintf(s.error, sizeof(s.error), "NIO unavailable (error %u). Set FN_PORT; edit destination to retry.", result);
    s.ready = 1;
    publish(c, &s);
    for (;;) {
        int connect, stop;
        SDL_LockMutex(c->mutex);
        stop = c->stop; connect = c->connect;
        if (connect) {
            s.settings = c->requested;
            c->connect = 0;
            c->length = c->head = 0;
        }
        SDL_UnlockMutex(c->mutex);
        if (stop) break;
        if (!connect) { SDL_Delay(10); continue; }
        memset(&s.snapshot, 0, sizeof(s.snapshot));
        s.error[0] = s.warning[0] = s.message[0] = 0;
        ++s.attempt; ++d.attempts;
        snprintf(d.end_reason,sizeof(d.end_reason),"Connecting");
        publish_diagnostics(c,&d);
        s.connecting = 1; s.info = s.dark = 1; s.who = s.broadcast = 0;
        s.collision_serial = s.controls_serial = 0;
        publish(c, &s);
        if (!initialized) {
            fn_shutdown(); api_begin(c,&d,"fn_init");
            result=fn_init(); api_end(c,&d,result); initialized=result==FN_OK;
        }
        if (initialized) {
            GfxWire wire;
            char interval[8];
            int session_result;
            memset(&wire, 0, sizeof(wire));
            wire.handle = FN_INVALID_HANDLE;
            wire.cancelled = cancelled; wire.context = c;
            wire.diagnostics=&d; wire.notify=publish_diagnostics;
            snprintf(interval, sizeof(interval), "%u", s.settings.interval);
            if (!cancelled(c) &&
                (!save_setting(c,&d,s.settings.endpoint,APPSTORE_KEY_ENDPOINT) ||
                 (!cancelled(c) && !save_setting(c,&d,s.settings.name,APPSTORE_KEY_NAME)) ||
                 (!cancelled(c) && !save_setting(c,&d,interval,APPSTORE_KEY_FETCH_INTERVAL))))
                snprintf(s.warning, sizeof(s.warning), "Settings could not be saved (NIO %u)", appstore_last_error());
            session_result = session(c, &wire, &s);
            if (session_result < 0 && !cancelled(c)) {
                ++d.drops; d.drop_at=d.failure_at;
                snprintf(d.drop_error,sizeof(d.drop_error),"%s",d.last_error);
                snprintf(d.drop_detail,sizeof(d.drop_detail),"%s",d.failure_detail);
                snprintf(s.error,sizeof(s.error),"%s",wire.error);
                snprintf(d.end_reason,sizeof(d.end_reason),"%s",wire.error);
            } else {
                ++d.cancellations;
                snprintf(d.end_reason,sizeof(d.end_reason),"User requested disconnect/exit");
            }
            d.pending=0;
            /* Publish immediately: cleanup can itself block for seconds. */
            s.connected=s.connecting=0;
            memset(&s.snapshot,0,sizeof(s.snapshot));
            publish_diagnostics(c,&d); publish(c,&s);
            if (wire.handle != FN_INVALID_HANDLE) {
                api_begin(c,&d,"fn_close"); result=fn_close(wire.handle); api_end(c,&d,result);
            }
            /* Reset transport after any session, including malformed framing. */
            fn_shutdown(); initialized = 0;
        } else {
            ++d.drops; d.drop_at=d.failure_at;
            snprintf(d.drop_error,sizeof(d.drop_error),"%s",d.last_error);
            snprintf(d.drop_detail,sizeof(d.drop_detail),"%s",d.failure_detail);
            snprintf(s.error,sizeof(s.error),"Cannot open FujiNet (%s/%u). Check FN_PORT.",gfx_nio_result(result),result);
            snprintf(d.end_reason,sizeof(d.end_reason),"%s",s.error);
        }
        snprintf(d.operation,sizeof(d.operation),"idle"); d.active=0;
        publish_diagnostics(c,&d);
        fprintf(stderr,"[bwc-gfx] session ended: %s; requests=%" PRIu64 " frames=%" PRIu64
            " tx=%" PRIu64 " rx=%" PRIu64 " errors=%" PRIu64 " NIO-timeouts=%" PRIu64 " deadlines=%" PRIu64 "\n",
            d.end_reason,d.requests,d.frames_ok,d.tx_bytes,d.rx_bytes,d.errors,d.nio_timeouts,d.deadlines);
        s.connected = s.connecting = 0;
        memset(&s.snapshot, 0, sizeof(s.snapshot));
        publish(c, &s);
    }
done:
    fn_shutdown();
    s.done = 1; s.connected = s.connecting = 0;
    publish(c, &s);
    return 0;
}
int gfx_client_start(GfxClient *c)
{
    memset(c, 0, sizeof(*c));
    c->mutex = SDL_CreateMutex();
    if (!c->mutex) return 0;
    c->thread = SDL_CreateThread(worker, "bwc-nio", c);
    if (!c->thread) { SDL_DestroyMutex(c->mutex); c->mutex = NULL; return 0; }
    return 1;
}
void gfx_client_copy(GfxClient *c, GfxState *out)
{ SDL_LockMutex(c->mutex); *out = c->state; out->diagnostics=c->diagnostics; SDL_UnlockMutex(c->mutex); }
void gfx_client_connect(GfxClient *c, const GfxSettings *s)
{ SDL_LockMutex(c->mutex); c->requested = *s; c->disconnect = 0; c->connect = 1; SDL_UnlockMutex(c->mutex); }
int gfx_client_command(GfxClient *c, char command)
{
    int accepted = 0;
    SDL_LockMutex(c->mutex);
    if (c->state.connected && c->length < sizeof(c->commands)) {
        c->commands[(c->head + c->length) % sizeof(c->commands)] = command;
        ++c->length; accepted = 1;
    }
    SDL_UnlockMutex(c->mutex);
    return accepted;
}
void gfx_client_disconnect(GfxClient *c)
{ SDL_LockMutex(c->mutex); c->disconnect = 1; SDL_UnlockMutex(c->mutex); }
void gfx_client_stop(GfxClient *c)
{ SDL_LockMutex(c->mutex); c->stop = 1; SDL_UnlockMutex(c->mutex); }
void gfx_client_destroy(GfxClient *c)
{ SDL_WaitThread(c->thread, NULL); SDL_DestroyMutex(c->mutex); }
