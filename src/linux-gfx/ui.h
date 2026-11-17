#ifndef BWC_GFX_UI_H
#define BWC_GFX_UI_H
#include <SDL_ttf.h>
#include "client.h"
#include "vector_outline.h"
enum { UI_SETUP, UI_GALLERY, UI_CONNECTING, UI_PLAY, UI_CLOSING };
enum { UI_NONE, UI_CONNECT, UI_DISCONNECT, UI_QUIT, UI_COMMAND };
typedef struct {
    SDL_Renderer *renderer;
    TTF_Font *font, *large;
    vo_outline outlines[19];
    const uint8_t *cells[19];
    uint8_t widths[19];
    GfxSettings settings;
    GfxState state;
    GfxSnapshot previous, current;
    int phase, field, loaded, info, who, dark, interpolate, blocks, flash, demo;
    uint32_t request_attempt, controls_serial, collision_serial, flash_until;
    char interval[5], notice[160], command;
    size_t caret[3], scroll[3]; /* ASCII fields; independent editing positions */
} GfxUI;
int gfx_ui_init(GfxUI *ui, SDL_Renderer *renderer);
void gfx_ui_destroy(GfxUI *ui);
int gfx_ui_event(GfxUI *ui, const SDL_Event *event);
void gfx_ui_update(GfxUI *ui, const GfxState *state);
void gfx_ui_draw(GfxUI *ui, uint32_t now);
void gfx_ui_demo(GfxUI *ui, uint32_t now);
#endif
