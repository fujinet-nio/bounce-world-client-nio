#include <math.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "embedded_shapes.h"

static SDL_Color ink = {219, 235, 247, 255};
static SDL_Color muted = {132, 161, 183, 255};
static SDL_Color cyan = {93, 231, 216, 255};
static SDL_Color palette[] = {{93,231,216,255}, {255,180,95,255}, {184,151,255,255}, {255,121,158,255}, {129,215,255,255}};
static void color(GfxUI *u, SDL_Color c) { SDL_SetRenderDrawColor(u->renderer, c.r,c.g,c.b,c.a); }
static void box(GfxUI *u, int x, int y, int w, int h, SDL_Color c, int fill)
{
    SDL_Rect r = {x,y,w,h}; color(u,c);
    if (fill) SDL_RenderFillRect(u->renderer,&r); else SDL_RenderDrawRect(u->renderer,&r);
}
static void text(GfxUI *u, int x, int y, const char *s, SDL_Color c, int large)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect dest;
    if (!*s) return;
    surface = TTF_RenderUTF8_Blended(large ? u->large : u->font, s, c);
    if (!surface) return;
    texture = SDL_CreateTextureFromSurface(u->renderer, surface);
    dest = (SDL_Rect){x,y,surface->w,surface->h};
    SDL_FreeSurface(surface);
    if (texture) { SDL_RenderCopy(u->renderer,texture,NULL,&dest); SDL_DestroyTexture(texture); }
}
static TTF_Font *open_font(int size)
{
    const char *override = getenv("BWC_FONT");
    const char *paths[] = {"/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf", NULL};
    int i;
    TTF_Font *font;
    if (override && *override) return TTF_OpenFont(override, size);
    for (i = 0; paths[i]; ++i) if ((font = TTF_OpenFont(paths[i], size))) return font;
    return NULL;
}
int gfx_ui_init(GfxUI *u, SDL_Renderer *renderer)
{
    unsigned offset = 0;
    memset(u,0,sizeof(*u));
    u->renderer = renderer;
    u->font = open_font(16); u->large = open_font(32);
    if (!u->font || !u->large) { gfx_ui_destroy(u); return 0; }
    u->info = u->dark = u->interpolate = 1;
    u->settings.interval = 100;
    strcpy(u->interval,"100");
    while (offset + 2 <= embedded_shape_data_len) {
        unsigned id = embedded_shape_data[offset++], w = embedded_shape_data[offset++];
        if (id >= 19 || !w || offset + w*w > embedded_shape_data_len) break;
        u->widths[id] = (uint8_t)w; u->cells[id] = embedded_shape_data + offset;
        vo_authored((uint8_t)id, &u->outlines[id]);
        offset += w*w;
    }
    return 1;
}
void gfx_ui_destroy(GfxUI *u)
{
    if (u->font) TTF_CloseFont(u->font);
    if (u->large) TTF_CloseFont(u->large);
    u->font = u->large = NULL;
}
static void shape(GfxUI *u, unsigned id, float x, float y, float cell)
{
    unsigned c, i;
    const vo_outline *o;
    float half;
    if (id >= 19 || !u->widths[id]) return;
    o = &u->outlines[id]; half = cell * u->widths[id] / 2;
    color(u, palette[id % 5]);
    if (u->blocks) {
        SDL_FRect rect = {x-half,y-half,half*2,half*2};
        SDL_RenderFillRectF(u->renderer,&rect);
        return;
    }
    for (c = 0; c < o->contour_count; ++c) {
        SDL_FPoint pts[VO_MAX_PTS+1];
        for (i = 0; i < o->contour_len[c]; ++i) {
            vo_point p = o->pts[o->contour_start[c]+i];
            pts[i] = (SDL_FPoint){x-half+p.x*cell/4,y-half+p.y*cell/4};
        }
        pts[i] = pts[0];
        SDL_RenderDrawLinesF(u->renderer,pts,(int)i+1);
        /* A second offset stroke gives outlines weight at desktop resolution. */
        for (i = 0; i <= o->contour_len[c]; ++i) pts[i].x += 1;
        SDL_RenderDrawLinesF(u->renderer,pts,(int)o->contour_len[c]+1);
    }
}
static void stars(GfxUI *u, uint32_t now)
{
    unsigned i;
    color(u,(SDL_Color){31,53,72,255});
    for (i = 0; i < 95; ++i) {
        int x = (int)((i*179U + now / (20 + i%5*10)) % GFX_WIDTH);
        int y = (int)((i*83U+17U) % GFX_WINDOW_HEIGHT);
        SDL_RenderDrawPoint(u->renderer,x,y);
    }
}
static char *field_value(GfxUI *u, unsigned index)
{
    return index == 0 ? u->settings.endpoint : index == 1 ? u->settings.name : u->interval;
}
static int field_width(GfxUI *u, const char *s, size_t begin, size_t end)
{
    char prefix[61];
    int width = 0, height;
    if (end <= begin) return 0;
    memcpy(prefix,s+begin,end-begin);
    prefix[end-begin] = 0;
    TTF_SizeUTF8(u->font,prefix,&width,&height);
    return width;
}
static void field_view(GfxUI *u, unsigned index)
{
    const char *s = field_value(u,index);
    size_t n = strlen(s);
    if (u->caret[index] > n) u->caret[index] = n;
    if (u->scroll[index] > u->caret[index]) u->scroll[index] = u->caret[index];
    while (field_width(u,s,u->scroll[index],u->caret[index]) > 500)
        ++u->scroll[index];
}
static void field_click(GfxUI *u, unsigned index, int x)
{
    const char *s = field_value(u,index);
    size_t n = strlen(s), at;
    field_view(u,index);
    at = u->scroll[index];
    while (at < n) {
        int left = field_width(u,s,u->scroll[index],at);
        int right = field_width(u,s,u->scroll[index],at+1);
        if (x-224 < (left+right)/2) break;
        ++at;
    }
    u->caret[index] = at;
    u->field = (int)index;
}
static void field(GfxUI *u, unsigned index, int y, const char *label, const char *value)
{
    SDL_Rect clip = {224,y+26,512,41};
    field_view(u,index);
    text(u,218,y,label,muted,0);
    box(u,210,y+25,540,43,(SDL_Color){12,26,40,255},1);
    box(u,210,y+25,540,43,(int)index == u->field ? cyan : muted,0);
    SDL_RenderSetClipRect(u->renderer,&clip);
    text(u,224,y+35,value+u->scroll[index],ink,0);
    if ((int)index == u->field && (SDL_GetTicks()/500)%2 == 0) {
        int width = field_width(u,value,u->scroll[index],u->caret[index]);
        box(u,224+width,y+36,2,21,cyan,1);
    }
    SDL_RenderSetClipRect(u->renderer,NULL);
}
static void setup(GfxUI *u, uint32_t now)
{
    unsigned i;
    for (i = 0; i < 9; ++i)
        shape(u,i,(float)((60+i*113)%960), 90+(float)sin(now*.0007+i)*30,10);
    box(u,180,148,600,494,(SDL_Color){17,33,49,255},1);
    text(u,210,170,"SET YOUR COURSE",cyan,1);
    text(u,212,217,"A shared world. A little chaos. Your corner of it.",muted,0);
    field(u,0,256,"DESTINATION  /  framed TCP server",u->settings.endpoint);
    field(u,1,343,"PILOT  /  1-8 characters",u->settings.name);
    field(u,2,430,"SNAPSHOT INTERVAL  /  10-1000 ms",u->interval);
    box(u,210,530,540,44,cyan,1);
    text(u,292,541,"ENTER  /  EXPLORE THE SHAPES",(SDL_Color){10,27,40,255},0);
    text(u,212,594,"Tab: next field   Arrows / Home / End: move   Esc: quit",muted,0);
    text(u,35,670,u->notice[0] ? u->notice : !u->loaded ? "Loading saved settings from FujiNet..." : u->state.error, (SDL_Color){255,180,95,255},0);
}
static void gallery(GfxUI *u, uint32_t now)
{
    unsigned i;
    text(u,35,84,"MEET THE LOCALS",cyan,1);
    text(u,35,128,"19 shapes from the shared library. V switches outlines / blocks.",muted,0);
    for (i = 0; i < 19; ++i) {
        int x = 35+(i%5)*180, y = 175+(i/5)*112;
        char label[40];
        box(u,x,y,166,100,(SDL_Color){17,33,49,255},1);
        shape(u,i,x+83,y+43+(float)sin(now*.002+i)*6,10);
        snprintf(label,sizeof(label),"%02u   /   %u x %u",i,u->widths[i],u->widths[i]);
        text(u,x+30,y+76,label,muted,0);
    }
    text(u,35,651,"ENTER / launch world       ESC / edit destination",cyan,0);
    text(u,35,683,"A shared cast of characters, from the smallest blocks to the largest wanderers.",muted,0);
}
static void play(GfxUI *u, uint32_t now)
{
    ShapePos positions[GFX_MAX_SHAPES];
    SDL_Rect clip = {0,64,GFX_WIDTH,GFX_HEIGHT};
    unsigned i;
    char line[180];
    if (!u->dark) box(u,0,64,GFX_WIDTH,GFX_HEIGHT,(SDL_Color){47,66,82,255},1);
    if (u->flash && (int32_t)(u->flash_until-now) > 0)
        box(u,0,64,GFX_WIDTH,GFX_HEIGHT,(SDL_Color){54,39,64,255},1);
    SDL_RenderSetClipRect(u->renderer,&clip);
    gfx_interpolate(&u->previous,&u->current,now,u->interpolate && !u->state.frozen,u->state.wrapped,positions);
    for (i = 0; i < u->current.count; ++i) shape(u,positions[i].shape_id,positions[i].x,positions[i].y+64,24);
    SDL_RenderSetClipRect(u->renderer,NULL);
    if (u->who) {
        box(u,724,82,218,48+24*(int)u->state.names_count,(SDL_Color){17,33,49,255},1);
        text(u,740,94,"WORLD PILOTS",cyan,0);
        for (i = 0; i < u->state.names_count; ++i) text(u,740,126+24*i,u->state.names[i],ink,0);
    }
    if (u->state.broadcast && u->state.message[0]) {
        box(u,20,584,920,40,(SDL_Color){17,33,49,255},1);
        /* Split the bounded ASCII message across two lines if needed. */
        char first[61];
        snprintf(first,sizeof(first),"%.60s",u->state.message);
        text(u,30,584,first,cyan,0);
        if (strlen(u->state.message)>60) text(u,30,604,u->state.message+60,cyan,0);
    }
    box(u,0,640,960,80,(SDL_Color){17,33,49,255},1);
    if (u->info) {
        snprintf(line,sizeof(line),"%s   /   %u pilots   /   %u bodies   /   %u x %u world   /   %s",
            u->settings.name,u->state.clients,u->state.bodies,u->state.width,u->state.height,
            u->state.frozen ? "FROZEN" : u->interpolate ? "SMOOTH" : "SNAPSHOTS");
        text(u,24,650,line,ink,0);
    }
    text(u,24,679,"F freeze   R reset   +/- world   1-5 add   I info   W who   V shapes   P smooth",muted,0);
    text(u,24,700,"D theme   L collision flash   O stats   Q / Esc leave",muted,0);
    if (u->notice[0]) text(u,510,700,u->notice,(SDL_Color){255,180,95,255},0);
}
static void diag_line(GfxUI *u, int *y, SDL_Color c, const char *format, ...)
{
    char line[240];
    va_list args;
    va_start(args,format); vsnprintf(line,sizeof(line),format,args); va_end(args);
    text(u,42,*y,line,c,0); *y+=23;
}
static void diag_wrapped(GfxUI *u, int *y, const char *value, SDL_Color c)
{
    /* At most three short lines for the retained failure. */
    while (*value) {
        char line[101];
        size_t n=strlen(value);
        if (n>100) n=100;
        memcpy(line,value,n); line[n]=0;
        diag_line(u,y,c,"%s",line); value+=n;
    }
}
static void diagnostics(GfxUI *u, uint64_t now)
{
    const GfxDiagnostics *d=&u->state.diagnostics;
    const GfxRates *r=&u->rates;
    int y=87;
    SDL_Color warning={255,180,95,255};
    box(u,24,76,912,555,(SDL_Color){12,26,40,255},1);
    box(u,24,76,912,555,cyan,0);
    diag_line(u,&y,cyan,"NETWORK DIAGNOSTICS   /   F2 or O to hide   /   totals since launch");
    diag_line(u,&y,ink,"Attempts %" PRIu64 "  connected %" PRIu64 "  failures %" PRIu64 "  user exits %" PRIu64 "  uptime %.1fs",
        d->attempts,d->connections,d->drops,d->cancellations,(double)(now-d->started)/1000.0);
    diag_line(u,&y,ink,"Requests %" PRIu64 "  writes OK %" PRIu64 "  frames OK %" PRIu64 "  world replies %" PRIu64 "  updates %" PRIu64,
        d->requests,d->writes_ok,d->frames_ok,d->worlds_ok,d->steps);
    diag_line(u,&y,ink,"Read calls %" PRIu64 "  OK %" PRIu64 "  BUSY %" PRIu64 "  NOT_READY %" PRIu64 "  empty %" PRIu64,
        d->read_calls,d->reads_ok,d->busy,d->not_ready,d->empty_reads);
    diag_line(u,&y,ink,"Stream bytes TX %" PRIu64 "  RX %" PRIu64 "  /  recent TX %.0f B/s  RX %.0f B/s",
        d->tx_bytes,d->rx_bytes,r->tx_per_second,r->rx_per_second);
    diag_line(u,&y,ink,"Recent: %.1f requests/s  %.1f world replies/s  %.1f render FPS  (1s samples)",
        r->requests_per_second,r->worlds_per_second,r->fps);
    diag_line(u,&y,warning,"Errors %" PRIu64 "  NIO timeouts %" PRIu64 "  client deadlines %" PRIu64 "  malformed %" PRIu64,
        d->errors,d->nio_timeouts,d->deadlines,d->malformed);
    diag_line(u,&y,ink,"EOF flags %" PRIu64 "  short writes %" PRIu64 "  slow replies (>=1s) %" PRIu64,
        d->eof,d->partial_writes,d->slow_requests);
    diag_line(u,&y,ink,"Request ms last/mean/max: %" PRIu64 " / %.1f / %" PRIu64 "   pending: %" PRIu64 "ms",
        d->request_ms,d->frames_ok ? (double)d->request_total_ms/d->frames_ok : 0,d->request_max_ms,
        d->pending ? now-d->request_at : 0);
    diag_line(u,&y,ink,"Read ms last/max: %" PRIu64 "/%" PRIu64 "   Write ms last/max: %" PRIu64 "/%" PRIu64,
        d->read_ms,d->read_max_ms,d->write_ms,d->write_max_ms);
    diag_line(u,&y,cyan,"%s: %s / %s  (%" PRIu64 "ms)  last result %s/%u",
        d->active ? "IN FLIGHT" : "Last/idle",d->command,d->operation,
        d->active ? now-d->operation_at : d->operation_last_ms,gfx_nio_result(d->last_code),d->last_code);
    diag_line(u,&y,ink,"Last reply age %.2fs  world age %.2fs  frame bytes %u/%u  flags 0x%02x",
        d->frames_ok ? (double)(now-d->last_reply_at)/1000.0 : 0,
        d->worlds_ok ? (double)(now-d->last_world_at)/1000.0 : 0,d->received,d->expected,d->last_flags);
    diag_line(u,&y,ink,"Write internals: exchanges %" PRIu64 "  transport retries %" PRIu64 "  service retries %" PRIu64 "  zero %" PRIu64,
        d->write_exchanges,d->transport_retries,d->service_retries,d->zero_accepted);
    diag_line(u,&y,muted,"Last write response: length %u device 0x%02x command 0x%02x  other NIO OK/calls %" PRIu64 "/%" PRIu64,
        d->response_length,d->response_device,d->response_command,d->other_ok,d->other_calls);
    diag_line(u,&y,warning,"Session: %.100s",d->end_reason[0] ? d->end_reason : "Not connected");
    if (d->failure_serial) {
        diag_line(u,&y,warning,"%s (%.1fs ago):",d->drop_error[0] ? "Last disconnect cause" : "Last error",
            (double)(now-(d->drop_error[0] ? d->drop_at : d->failure_at))/1000.0);
        diag_wrapped(u,&y,d->drop_error[0] ? d->drop_error : d->last_error,warning);
        diag_wrapped(u,&y,d->drop_error[0] ? d->drop_detail : d->failure_detail,muted);
    }
    diag_line(u,&y,muted,"TCP stream counts, not serial-wire bytes. Failures also go to stderr.");
}
void gfx_ui_draw(GfxUI *u, uint32_t now)
{
    uint64_t wall_now=SDL_GetTicks64();
    gfx_rates_sample(&u->rates,&u->state.diagnostics,wall_now,++u->diagnostic_frames);
    color(u,(SDL_Color){9,20,32,255}); SDL_RenderClear(u->renderer);
    stars(u,now);
    if (u->phase == UI_SETUP) setup(u,now);
    if (u->phase == UI_GALLERY) gallery(u,now);
    if (u->phase == UI_PLAY) play(u,now);
    if (u->phase == UI_CONNECTING || u->phase == UI_CLOSING) {
        text(u,220,310,u->phase == UI_CLOSING ? "CLOSING THE CONNECTION" : "CONTACTING YOUR WORLD",cyan,1);
        text(u,220,365,"The window stays responsive while FujiNet finishes its request.",muted,0);
        text(u,220,400,"Esc / cancel",ink,0);
        shape(u,5,480+(float)sin(now*.003)*180,490,16);
    }
    box(u,0,0,960,64,(SDL_Color){17,33,49,255},1);
    text(u,24,12,"BOUNCY WORLD",ink,1);
    text(u,600,23,u->demo ? "DEMO  /  NO NETWORK" : "FUJINET NIO   /   F2 STATS",cyan,0);
    if (u->diagnostics_visible) diagnostics(u,wall_now);
}
void gfx_ui_update(GfxUI *u, const GfxState *s)
{
    u->state = *s;
    if (s->diagnostics.drops>u->seen_drops) u->diagnostics_visible=1;
    u->seen_drops=s->diagnostics.drops;
    if (!u->loaded && s->ready) {
        u->settings = s->settings;
        snprintf(u->interval,sizeof(u->interval),"%u",s->settings.interval);
        for (unsigned i = 0; i < 3; ++i) u->caret[i] = strlen(field_value(u,i));
        u->loaded = 1;
    }
    if (u->phase == UI_CLOSING) return;
    if (s->attempt >= u->request_attempt && u->phase == UI_CONNECTING) {
        if (s->connected) { u->phase = UI_PLAY; u->notice[0] = 0; }
        else if (!s->connecting) { u->phase = UI_SETUP; }
    }
    if (u->phase == UI_PLAY && !u->demo && !s->connected) {
        u->phase = UI_SETUP; u->current.serial = u->previous.serial = 0;
    }
    if (u->phase == UI_PLAY && s->snapshot.serial != u->current.serial) {
        u->previous = u->current; u->current = s->snapshot;
    }
    if (s->controls_serial != u->controls_serial) {
        u->info = s->info; u->who = s->who; u->dark = s->dark;
        u->controls_serial = s->controls_serial;
    }
    if (s->collision_serial != u->collision_serial) {
        u->flash_until = SDL_GetTicks()+100; u->collision_serial = s->collision_serial;
    }
    if (s->warning[0]) snprintf(u->notice,sizeof(u->notice),"%s",s->warning);
}
int gfx_ui_event(GfxUI *u, const SDL_Event *e)
{
    SDL_Keycode key;
    if (e->type == SDL_QUIT) return UI_QUIT;
    if (u->phase == UI_SETUP && u->loaded && e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        /* RenderSetLogicalSize already translates SDL mouse events. */
        float x = (float)e->button.x, y = (float)e->button.y;
        if (x >= 210 && x <= 750) {
            if (y >= 281 && y < 324) field_click(u,0,(int)x);
            if (y >= 368 && y < 411) field_click(u,1,(int)x);
            if (y >= 455 && y < 498) field_click(u,2,(int)x);
            if (y >= 530 && y < 574) {
                SDL_Event enter; memset(&enter,0,sizeof(enter)); enter.type = SDL_KEYDOWN; enter.key.keysym.sym = SDLK_RETURN;
                return gfx_ui_event(u,&enter);
            }
        }
    }
    if (u->phase == UI_SETUP && e->type == SDL_TEXTINPUT) {
        char *s = field_value(u,(unsigned)u->field);
        size_t limit = u->field == 0 ? 60 : u->field == 1 ? 8 : 4, n = strlen(s), i;
        if (!u->loaded) return UI_NONE;
        field_view(u,(unsigned)u->field);
        for (i = 0; e->text.text[i] && n < limit; ++i) {
            unsigned char ch = (unsigned char)e->text.text[i];
            if (ch < 33 || ch > 126 || (u->field == 1 && ch == ',') ||
                (u->field == 2 && (ch < '0' || ch > '9'))) continue;
            size_t at = u->caret[u->field];
            memmove(s+at+1,s+at,n-at+1); /* includes terminator */
            s[at] = (char)ch;
            ++u->caret[u->field]; ++n;
        }
        field_view(u,(unsigned)u->field);
        u->notice[0] = 0;
    }
    if (e->type != SDL_KEYDOWN) return UI_NONE;
    key = e->key.keysym.sym;
    if (!e->key.repeat && (key==SDLK_F2 || (key==SDLK_o && u->phase!=UI_SETUP))) {
        u->diagnostics_visible=!u->diagnostics_visible;
        return UI_NONE;
    }
    /* Repeat edits/navigation, never launch or repeat world commands. */
    if (e->key.repeat && (u->phase != UI_SETUP ||
        (key != SDLK_LEFT && key != SDLK_RIGHT && key != SDLK_HOME &&
         key != SDLK_END && key != SDLK_BACKSPACE && key != SDLK_DELETE))) return UI_NONE;
    if (key == SDLK_ESCAPE) {
        if (u->phase == UI_GALLERY) { u->phase = UI_SETUP; return UI_NONE; }
        if (u->phase == UI_PLAY || u->phase == UI_CONNECTING) return UI_DISCONNECT;
        return UI_QUIT;
    }
    if (u->phase == UI_SETUP) {
        char *s = field_value(u,(unsigned)u->field);
        if (!u->loaded) return UI_NONE;
        size_t n = strlen(s), *at = &u->caret[u->field];
        field_view(u,(unsigned)u->field);
        switch (key) {
            case SDLK_LEFT: if (*at) --*at; break;
            case SDLK_RIGHT: if (*at < n) ++*at; break;
            case SDLK_HOME: *at = 0; break;
            case SDLK_END: *at = n; break;
            case SDLK_BACKSPACE:
                if (*at) { memmove(s+*at-1,s+*at,n-*at+1); --*at; }
                break;
            case SDLK_DELETE:
                if (*at < n) memmove(s+*at,s+*at+1,n-*at);
                break;
            case SDLK_a:
                if (e->key.keysym.mod & KMOD_CTRL) { *s = 0; *at = 0; }
                break;
            case SDLK_TAB:
                u->field = (u->field + (e->key.keysym.mod & KMOD_SHIFT ? 2 : 1)) % 3;
                break;
            default: break;
        }
        field_view(u,(unsigned)u->field);
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            u->settings.interval = (unsigned)strtoul(u->interval,NULL,10);
            if (gfx_settings_valid(&u->settings)) { u->phase = UI_GALLERY; u->notice[0] = 0; }
            else snprintf(u->notice,sizeof(u->notice),"Enter a TCP destination, a pilot name and an interval from 10 to 1000 ms.");
        }
    } else if (u->phase == UI_GALLERY) {
        if (key == SDLK_v) u->blocks = !u->blocks;
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            u->phase = UI_CONNECTING; u->request_attempt = u->state.attempt+1;
            u->current.serial = u->previous.serial = 0;
            u->info = u->dark = 1; u->who = 0;
            u->controls_serial = u->collision_serial = 0;
            return UI_CONNECT;
        }
    } else if (u->phase == UI_PLAY) {
        switch (key) {
            case SDLK_q: return UI_DISCONNECT;
            case SDLK_i: u->info = !u->info; break;
            case SDLK_w: u->who = !u->who; break;
            case SDLK_d: u->dark = !u->dark; break;
            case SDLK_l: u->flash = !u->flash; break;
            case SDLK_v: u->blocks = !u->blocks; break;
            case SDLK_p: u->interpolate = !u->interpolate; break;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: u->command = '+'; return UI_COMMAND;
            case SDLK_MINUS: case SDLK_KP_MINUS: u->command = '-'; return UI_COMMAND;
            case SDLK_f: case SDLK_r: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5:
                u->command = (char)key; return UI_COMMAND;
            default: break;
        }
    }
    return UI_NONE;
}
void gfx_ui_demo(GfxUI *u, uint32_t now)
{
    GfxState *s = &u->state;
    unsigned i;
    u->demo = u->loaded = 1;
    u->phase = UI_PLAY;
    strcpy(u->settings.name,"PREVIEW");
    s->clients = 3; s->bodies = 19; s->width = 40; s->height = 24;
    if (u->current.serial && now-u->current.time < 100) return;
    u->previous = u->current;
    u->current.time = now; ++u->current.serial; u->current.count = 19;
    for (i = 0; i < 19; ++i) {
        ShapePos *p = &u->current.shapes[i];
        p->shape_id = (uint8_t)i; p->body_id = i+1;
        p->x = (int16_t)(95+(i%5)*185+sin(now*.0006+i)*35);
        p->y = (int16_t)(75+(i/5)*135+cos(now*.0009+i)*25);
    }
}
