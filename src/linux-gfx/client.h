#ifndef BWC_GFX_CLIENT_H
#define BWC_GFX_CLIENT_H
#include <SDL.h>
#include "shape_decode.h"

#define GFX_WIDTH 960
#define GFX_HEIGHT 576
#define GFX_WINDOW_HEIGHT 720
#define GFX_CAPS (BWC_CAP_WIDE_COORDS | BWC_CAP_BODY_ID)
#define GFX_MAX_SHAPES 255

typedef struct {
    char endpoint[61], name[9];
    unsigned interval;
} GfxSettings;
typedef struct {
    ShapePos shapes[GFX_MAX_SHAPES];
    uint8_t count, step, status;
    uint32_t time, serial;
} GfxSnapshot;
typedef struct {
    GfxSettings settings;
    GfxSnapshot snapshot;
    unsigned width, height, bodies, clients;
    uint8_t sizes[5], frozen, wrapped;
    char names[20][9], message[120], error[160], warning[100];
    unsigned names_count;
    uint32_t collision_serial, controls_serial, attempt;
    uint8_t info, who, broadcast, dark;
    int ready, connecting, connected, done;
} GfxState;
typedef struct {
    SDL_mutex *mutex;
    SDL_Thread *thread;
    GfxState state;
    GfxSettings requested;
    int stop, connect, disconnect;
    char commands[32];
    unsigned head, length;
} GfxClient;

int gfx_client_start(GfxClient *client);
void gfx_client_copy(GfxClient *client, GfxState *out);
void gfx_client_connect(GfxClient *client, const GfxSettings *settings);
int gfx_client_command(GfxClient *client, char command);
void gfx_client_disconnect(GfxClient *client);
void gfx_client_stop(GfxClient *client);
void gfx_client_destroy(GfxClient *client); /* only after state.done */
int gfx_settings_valid(const GfxSettings *settings);
void gfx_interpolate(const GfxSnapshot *previous, const GfxSnapshot *current,
                     uint32_t now, int enabled, int wrapped, ShapePos *out);
#endif
