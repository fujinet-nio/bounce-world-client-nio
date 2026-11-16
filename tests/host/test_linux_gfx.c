#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

static int key(GfxUI *u, SDL_Keycode k)
{
    SDL_Event e = {0}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    return gfx_ui_event(u,&e);
}
static void capture(GfxUI *u, uint32_t *pixels)
{
    gfx_ui_draw(u,1000);
    assert(SDL_RenderReadPixels(u->renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,960*4)==0);
}
int main(void)
{
    GfxUI u;
    GfxSnapshot p = {0}, c = {0};
    ShapePos out[GFX_MAX_SHAPES];
    SDL_Window *w;
    SDL_Renderer *r;
    uint32_t *a = malloc(960*720*4), *b = malloc(960*720*4);
    assert(a && b);
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0 && TTF_Init()==0);
    w=SDL_CreateWindow("test",0,0,960,720,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
    assert(w); r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE); assert(r);
    SDL_RenderSetLogicalSize(r,960,720);
    assert(gfx_ui_init(&u,r));
    u.loaded=1;
    assert(key(&u,SDLK_RETURN)==UI_NONE && u.phase==UI_SETUP);
    strcpy(u.settings.endpoint,"localhost:9003"); strcpy(u.settings.name,"PILOT");
    assert(gfx_settings_valid(&u.settings));
    strcpy(u.settings.name,"bad,name"); assert(!gfx_settings_valid(&u.settings));
    strcpy(u.settings.name,"PILOT");
    assert(key(&u,SDLK_RETURN)==UI_NONE && u.phase==UI_GALLERY);
    gfx_ui_draw(&u,1000);
    assert(key(&u,SDLK_RETURN)==UI_CONNECT && u.phase==UI_CONNECTING);
    { GfxState s={0}; s.attempt=1; s.connected=1; gfx_ui_update(&u,&s); }
    assert(u.phase==UI_PLAY);
    assert(key(&u,SDLK_i)==UI_NONE && !u.info); key(&u,SDLK_i);
    assert(key(&u,SDLK_v)==UI_NONE && u.blocks);
    assert(key(&u,SDLK_f)==UI_COMMAND && u.command=='f');
    u.blocks=0;
    capture(&u,a);
    u.current.count=1;
    u.current.shapes[0]=(ShapePos){.shape_id=0,.x=-300,.y=0,.body_id=1};
    capture(&u,b); assert(!memcmp(a,b,960*720*4)); /* fully clipped */
    u.current.shapes[0].x=100;
    capture(&u,b); assert(!memcmp(a,b,960*64*4)); /* HUD protected */
    assert(memcmp(a,b,960*720*4));
    u.current.count=0; capture(&u,b); assert(!memcmp(a,b,960*720*4));
    p.serial=1; p.time=100; p.count=2;
    p.shapes[0]=(ShapePos){.shape_id=0,.x=200,.y=200,.body_id=22};
    p.shapes[1]=(ShapePos){.shape_id=0,.x=100,.y=100,.body_id=11};
    c.serial=2; c.time=200; c.count=2;
    c.shapes[0]=(ShapePos){.shape_id=0,.x=80,.y=80,.body_id=11};
    c.shapes[1]=(ShapePos){.shape_id=0,.x=220,.y=220,.body_id=22};
    gfx_interpolate(&p,&c,250,1,0,out);
    assert(out[0].x==90 && out[0].y==90 && out[1].x==210);
    gfx_interpolate(&p,&c,5000,1,0,out); assert(out[0].x==80);
    gfx_interpolate(&p,&c,250,0,0,out); assert(out[0].x==80);
    c.shapes[0].body_id=99; gfx_interpolate(&p,&c,250,1,0,out); assert(out[0].x==80);
    c.count=1; c.shapes[0]=(ShapePos){.shape_id=0,.x=2,.y=100,.body_id=11};
    p.shapes[1].x=958;
    gfx_interpolate(&p,&c,250,1,1,out); assert(out[0].x==0); /* nearest wrap image */
    SDL_SetWindowSize(w,1280,720);
    SDL_RenderSetLogicalSize(r,960,720);
    { float x,y; SDL_RenderWindowToLogical(r,640,360,&x,&y); assert(x>479 && x<481 && y>359 && y<361); }
    assert(key(&u,SDLK_q)==UI_DISCONNECT);
    { SDL_Event e={0}; e.type=SDL_QUIT; assert(gfx_ui_event(&u,&e)==UI_QUIT); }
    gfx_ui_destroy(&u); SDL_DestroyRenderer(r); SDL_DestroyWindow(w); TTF_Quit(); SDL_Quit();
    free(a); free(b);
    puts("SDL events, clipping, empty frames, resize and body-ID interpolation: PASS");
    return 0;
}
