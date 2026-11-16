#include <math.h>
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
        vo_trace(u->cells[id], (uint8_t)w, &u->outlines[id]);
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
            pts[i] = (SDL_FPoint){x-half+p.x*cell,y-half+p.y*cell};
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
static void field(GfxUI *u, unsigned index, int y, const char *label, const char *value)
{
    const char *visible = value;
    int width, height;
    text(u,218,y,label,muted,0);
    box(u,210,y+25,540,43,(SDL_Color){12,26,40,255},1);
    box(u,210,y+25,540,43,(int)index == u->field ? cyan : muted,0);
    while (*visible) {
        TTF_SizeUTF8(u->font,visible,&width,&height);
        if (width < 495) break;
        ++visible;
    }
    text(u,224,y+35,visible,ink,0);
    if ((int)index == u->field && (SDL_GetTicks()/500)%2 == 0) {
        TTF_SizeUTF8(u->font,visible,&width,&height);
        box(u,226+width,y+36,2,21,cyan,1);
    }
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
    text(u,212,594,"Tab / click: choose field     Ctrl+A: clear     Esc: quit",muted,0);
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
    text(u,24,700,"D theme   L collision flash   Q / Esc leave",muted,0);
    if (u->notice[0]) text(u,440,700,u->notice,(SDL_Color){255,180,95,255},0);
}
void gfx_ui_draw(GfxUI *u, uint32_t now)
{
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
    text(u,600,23,u->demo ? "DEMO  /  NO NETWORK" : "FUJINET NIO   /   LINUX GFX",cyan,0);
}
void gfx_ui_update(GfxUI *u, const GfxState *s)
{
    u->state = *s;
    if (!u->loaded && s->ready) {
        u->settings = s->settings;
        snprintf(u->interval,sizeof(u->interval),"%u",s->settings.interval);
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
    if (u->phase == UI_SETUP && e->type == SDL_MOUSEBUTTONDOWN) {
        /* RenderSetLogicalSize already translates SDL mouse events. */
        float x = (float)e->button.x, y = (float)e->button.y;
        if (x >= 210 && x <= 750) {
            if (y >= 281 && y < 324) u->field = 0;
            if (y >= 368 && y < 411) u->field = 1;
            if (y >= 455 && y < 498) u->field = 2;
            if (y >= 530 && y < 574) {
                SDL_Event enter = {0}; enter.type = SDL_KEYDOWN; enter.key.keysym.sym = SDLK_RETURN;
                return gfx_ui_event(u,&enter);
            }
        }
    }
    if (u->phase == UI_SETUP && e->type == SDL_TEXTINPUT) {
        char *s = u->field == 0 ? u->settings.endpoint : u->field == 1 ? u->settings.name : u->interval;
        size_t limit = u->field == 0 ? 60 : u->field == 1 ? 8 : 4, n = strlen(s), i;
        if (!u->loaded) return UI_NONE;
        for (i = 0; e->text.text[i] && n < limit; ++i) {
            unsigned char ch = (unsigned char)e->text.text[i];
            if (ch < 33 || ch > 126 || (u->field == 1 && ch == ',') ||
                (u->field == 2 && (ch < '0' || ch > '9'))) continue;
            s[n++] = (char)ch;
        }
        s[n] = 0; u->notice[0] = 0;
    }
    if (e->type != SDL_KEYDOWN || e->key.repeat) return UI_NONE;
    key = e->key.keysym.sym;
    if (key == SDLK_ESCAPE) {
        if (u->phase == UI_GALLERY) { u->phase = UI_SETUP; return UI_NONE; }
        if (u->phase == UI_PLAY || u->phase == UI_CONNECTING) return UI_DISCONNECT;
        return UI_QUIT;
    }
    if (u->phase == UI_SETUP) {
        char *s = u->field == 0 ? u->settings.endpoint : u->field == 1 ? u->settings.name : u->interval;
        if (!u->loaded) return UI_NONE;
        if (key == SDLK_TAB) u->field = (u->field + (e->key.keysym.mod & KMOD_SHIFT ? 2 : 1)) % 3;
        if (key == SDLK_BACKSPACE && *s) s[strlen(s)-1] = 0;
        if (key == SDLK_a && (e->key.keysym.mod & KMOD_CTRL)) *s = 0;
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
