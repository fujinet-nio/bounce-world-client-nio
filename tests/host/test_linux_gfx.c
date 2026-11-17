#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

static int key(GfxUI *u, SDL_Keycode k)
{
    SDL_Event e; memset(&e,0,sizeof(e)); e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    return gfx_ui_event(u,&e);
}
static void type_text(GfxUI *u, const char *text)
{
    SDL_Event e; memset(&e,0,sizeof(e)); e.type = SDL_TEXTINPUT;
    snprintf(e.text.text,sizeof(e.text.text),"%s",text);
    assert(gfx_ui_event(u,&e)==UI_NONE);
}
static void editing(GfxUI *u)
{
    GfxState saved = {0};
    saved.ready = 1; saved.settings.interval = 100;
    strcpy(saved.settings.endpoint,"localhost:9003"); strcpy(saved.settings.name,"PILOT");
    gfx_ui_update(u,&saved);
    assert(u->caret[0]==14 && u->caret[1]==5 && u->caret[2]==3);
    key(u,SDLK_HOME); key(u,SDLK_LEFT); key(u,SDLK_BACKSPACE);
    assert(!strcmp(u->settings.endpoint,"localhost:9003") && u->caret[0]==0);
    type_text(u,"tcp://");
    assert(!strcmp(u->settings.endpoint,"tcp://localhost:9003") && u->caret[0]==6);
    key(u,SDLK_DELETE); type_text(u,"L");
    assert(!strcmp(u->settings.endpoint,"tcp://Localhost:9003"));
    key(u,SDLK_BACKSPACE); type_text(u,"l");
    key(u,SDLK_END); key(u,SDLK_RIGHT); key(u,SDLK_DELETE);
    assert(u->caret[0]==strlen(u->settings.endpoint));
    key(u,SDLK_LEFT); key(u,SDLK_BACKSPACE); type_text(u,"8");
    assert(!strcmp(u->settings.endpoint,"tcp://localhost:9083"));
    { SDL_Event e; memset(&e,0,sizeof(e)); e.type=SDL_KEYDOWN; e.key.keysym.sym=SDLK_LEFT; e.key.repeat=1;
      size_t before=u->caret[0]; gfx_ui_event(u,&e); assert(u->caret[0]==before-1);
      e.key.keysym.sym=SDLK_RETURN; gfx_ui_event(u,&e); assert(u->phase==UI_SETUP); }
    { size_t before=u->caret[0];
      key(u,SDLK_TAB); key(u,SDLK_HOME); type_text(u,"A,");
      assert(!strcmp(u->settings.name,"APILOT") && u->caret[1]==1);
      key(u,SDLK_TAB); key(u,SDLK_HOME); key(u,SDLK_DELETE); type_text(u,"2x");
      assert(!strcmp(u->interval,"200") && u->caret[2]==1);
      key(u,SDLK_TAB); assert(u->field==0 && u->caret[0]==before); }
    /* Limit insertion without losing the existing suffix or terminator. */
    memset(u->settings.endpoint,'W',60); u->settings.endpoint[60]=0;
    key(u,SDLK_HOME); type_text(u,"X"); assert(strlen(u->settings.endpoint)==60 && u->caret[0]==0);
    key(u,SDLK_DELETE); type_text(u,"XY");
    assert(strlen(u->settings.endpoint)==60 && u->settings.endpoint[0]=='X' && u->settings.endpoint[1]=='W');
    key(u,SDLK_END); gfx_ui_draw(u,1000); assert(u->scroll[0]>0);
    key(u,SDLK_HOME); gfx_ui_draw(u,1000); assert(u->scroll[0]==0);
    { SDL_Event e; memset(&e,0,sizeof(e)); e.type=SDL_MOUSEBUTTONDOWN; e.button.button=SDL_BUTTON_LEFT;
      e.button.x=224; e.button.y=300; gfx_ui_event(u,&e); assert(u->caret[0]==0);
      int width,height; TTF_SizeUTF8(u->font,"XW",&width,&height);
      e.button.x=224+width; gfx_ui_event(u,&e); assert(u->caret[0]==2); }
    { SDL_Event e; memset(&e,0,sizeof(e)); e.type=SDL_KEYDOWN; e.key.keysym.sym=SDLK_a; e.key.keysym.mod=KMOD_CTRL;
      gfx_ui_event(u,&e); assert(!*u->settings.endpoint && u->caret[0]==0 && u->scroll[0]==0); }
    type_text(u,"localhost:9003");
    strcpy(u->settings.name,"PILOT"); strcpy(u->interval,"100");
    u->notice[0]=0;
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
    editing(&u);
    u.settings.endpoint[0]=0;
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
    { GfxState failed=u.state; GfxDiagnostics *d=&failed.diagnostics;
      failed.connected=0; d->drops=1; d->failure_serial=1; d->errors=2;
      d->nio_timeouts=1; d->attempts=1; d->connections=1;
      d->requests=142; d->writes_ok=142; d->frames_ok=141; d->worlds_ok=130;
      d->tx_bytes=910; d->rx_bytes=84720; d->read_calls=390; d->not_ready=210;
      d->failure_at=d->drop_at=SDL_GetTicks64();
      strcpy(d->operation,"idle");
      strcpy(d->end_reason,"Read failed (TIMEOUT/6)");
      strcpy(d->drop_error,"x-w / fn_read: Read failed [TIMEOUT/6]");
      strcpy(d->drop_detail,"op=2040ms request=2050ms frame=0/0 RX/TX offset=84720/910 flags=0x00 bytes=0/2304");
      gfx_ui_update(&u,&failed);
      assert(u.phase==UI_SETUP && u.diagnostics_visible);
      assert(key(&u,SDLK_F2)==UI_NONE && !u.diagnostics_visible);
      assert(key(&u,SDLK_o)==UI_NONE && !u.diagnostics_visible); /* setup text key */
      assert(key(&u,SDLK_F2)==UI_NONE && u.diagnostics_visible);
      gfx_ui_update(&u,&failed); assert(strstr(u.state.diagnostics.drop_error,"TIMEOUT"));
      SDL_SetWindowSize(w,960,720); SDL_RenderSetLogicalSize(r,960,720);
      capture(&u,b);
      SDL_Surface *shot=SDL_CreateRGBSurfaceWithFormatFrom(b,960,720,32,960*4,SDL_PIXELFORMAT_ARGB8888);
      assert(shot && SDL_SaveBMP(shot,"build/test_linux_diagnostics.bmp")==0); SDL_FreeSurface(shot);
      key(&u,SDLK_F2); capture(&u,a); assert(memcmp(a,b,960*720*4));
      u.phase=UI_PLAY; key(&u,SDLK_o); assert(u.diagnostics_visible);
    }
    { SDL_Event e; memset(&e,0,sizeof(e)); e.type=SDL_QUIT; assert(gfx_ui_event(&u,&e)==UI_QUIT); }
    gfx_ui_destroy(&u); SDL_DestroyRenderer(r); SDL_DestroyWindow(w); TTF_Quit(); SDL_Quit();
    free(a); free(b);
    puts("SDL events, clipping, empty frames, resize and body-ID interpolation: PASS");
    return 0;
}
