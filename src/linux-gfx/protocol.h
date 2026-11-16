#ifndef BWC_GFX_PROTOCOL_H
#define BWC_GFX_PROTOCOL_H
#include "fujinet-nio.h"
#define GFX_PACKET_MAX 2304
/* All fields and all fn_* calls belong exclusively to the network worker. */
typedef struct {
    fn_handle_t handle;
    uint32_t read_offset, write_offset;
    uint8_t packet[GFX_PACKET_MAX];
    int (*cancelled)(void *context);
    void *context;
    char error[160];
} GfxWire;
int gfx_wire_send(GfxWire *wire, const char *command);
int gfx_wire_reply(GfxWire *wire); /* payload length; payload at packet + 2 */
#endif
