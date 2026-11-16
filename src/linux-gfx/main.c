#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "ui.h"

static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number) { interrupted = signal_number; }
static int screenshot(SDL_Renderer *renderer, const char *path)
{
    int width,height,ok;
    SDL_Surface *surface;
    SDL_GetRendererOutputSize(renderer,&width,&height);
    surface = SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return 0;
    ok = SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch) == 0;
    if (ok) ok = SDL_SaveBMP(surface,path) == 0;
    SDL_FreeSurface(surface);
    return ok;
}
int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    GfxUI ui;
    GfxClient client;
    GfxState state;
    int demo = 0, gallery = 0, frame_limit = 0, frames = 0, closing = 0, running = 1, exit_code = 1, started = 0, ui_ready = 0;
    const char *capture = NULL;
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--demo")) demo = 1;
        else if (!strcmp(argv[i],"--gallery")) { demo = 1; gallery = 1; }
        else if (!strcmp(argv[i],"--frames") && i+1<argc) frame_limit = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--screenshot") && i+1<argc) capture = argv[++i];
        else { fprintf(stderr,"Usage: %s [--demo | --gallery] [--frames N] [--screenshot file.bmp]\n",argv[0]); return !strcmp(argv[i],"--help") ? 0 : 1; }
    }
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) < 0 || TTF_Init() < 0) goto fail;
    window = SDL_CreateWindow("Bouncy World / FujiNet NIO",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        GFX_WIDTH,GFX_WINDOW_HEIGHT,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) goto fail;
    SDL_SetWindowMinimumSize(window,640,480);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if (!renderer) goto fail;
    SDL_RenderSetLogicalSize(renderer,GFX_WIDTH,GFX_WINDOW_HEIGHT);
    if (!gfx_ui_init(&ui,renderer)) { fprintf(stderr,"Cannot load font. Install DejaVu Sans or set BWC_FONT to a TTF file.\n"); goto fail; }
    ui_ready = 1;
    if (!demo) {
        if (!gfx_client_start(&client)) goto fail;
        started = 1;
    }
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    SDL_StartTextInput();
    exit_code = 0;
    while (running) {
        uint32_t start = SDL_GetTicks();
        SDL_Event event;
        if (demo) {
            gfx_ui_demo(&ui,start);
            if (gallery) ui.phase = UI_GALLERY;
        } else {
            gfx_client_copy(&client,&state);
            gfx_ui_update(&ui,&state);
            if (closing && state.done) running = 0;
        }
        while (SDL_PollEvent(&event)) {
            int action = gfx_ui_event(&ui,&event);
            if (closing) continue;
            if (action == UI_QUIT || (demo && action == UI_DISCONNECT)) closing = 1;
            if (!demo && action == UI_CONNECT) gfx_client_connect(&client,&ui.settings);
            if (!demo && action == UI_DISCONNECT) gfx_client_disconnect(&client);
            if (!demo && action == UI_COMMAND && !gfx_client_command(&client,ui.command))
                snprintf(ui.notice,sizeof(ui.notice),"Command queue full; try again.");
        }
        if (interrupted) closing = 1;
        gfx_ui_draw(&ui,start);
        ++frames;
        if (frame_limit > 0 && frames == frame_limit) {
            if (capture && !screenshot(renderer,capture)) { fprintf(stderr,"Screenshot: %s\n",SDL_GetError()); exit_code = 1; }
            closing = 1;
        }
        SDL_RenderPresent(renderer);
        if (closing) {
            ui.phase = UI_CLOSING;
            if (demo) running = 0; else gfx_client_stop(&client);
        }
        /* Also cap software/dummy rendering and minimized windows. */
        if (SDL_GetTicks()-start < 16) SDL_Delay(16-(SDL_GetTicks()-start));
    }
    goto cleanup;
fail:
    fprintf(stderr,"Linux graphics: %s\n",SDL_GetError());
cleanup:
    if (started) { gfx_client_stop(&client); gfx_client_destroy(&client); }
    if (ui_ready) gfx_ui_destroy(&ui);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit(); SDL_Quit();
    return exit_code;
}
