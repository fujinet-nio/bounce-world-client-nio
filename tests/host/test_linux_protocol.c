#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "protocol.h"

static uint8_t input[16];
static unsigned size, position, mode, reads;
uint8_t fn_write(fn_handle_t h,uint32_t o,const uint8_t *p,uint16_t n,uint16_t *written)
{ (void)h; assert(o==0 && p[n-1]=='\n'); *written=mode==4 ? n-1 : n; return FN_OK; }
uint8_t fn_read(fn_handle_t h,uint32_t o,uint8_t *p,uint16_t n,uint16_t *got,uint8_t *flags)
{
    (void)h;
    assert(o==position); ++reads; *got=0; *flags=0;
    if (mode==1 && reads<3) return reads==1 ? FN_ERR_BUSY : FN_ERR_NOT_READY;
    if (mode==2) { *flags=FN_READ_EOF; return FN_OK; }
    if (position<size) { assert(n); *p=input[position++]; *got=1; }
    if (position==size) *flags=FN_READ_EOF;
    return FN_OK;
}
static int cancel(void *unused) { (void)unused; return 1; }
static void reset(GfxWire *w,unsigned m)
{
    memset(w,0,sizeof(*w)); mode=m; position=reads=0; size=5;
    memcpy(input,"\5\0abc",5);
}
int main(void)
{
    GfxWire w;
    SDL_Init(SDL_INIT_TIMER);
    reset(&w,1); assert(gfx_wire_reply(&w)==3 && w.read_offset==5 && !memcmp(w.packet+2,"abc",3));
    reset(&w,2); assert(gfx_wire_reply(&w)<0);
    reset(&w,0); input[0]=1; assert(gfx_wire_reply(&w)<0);
    reset(&w,0); input[1]=100; assert(gfx_wire_reply(&w)<0);
    reset(&w,0); size=4; assert(gfx_wire_reply(&w)<0); /* truncated frame */
    reset(&w,0); w.cancelled=cancel; assert(gfx_wire_reply(&w)<0 && reads==0);
    reset(&w,4); assert(gfx_wire_send(&w,"x-w 1")<0 && w.write_offset==0);
    reset(&w,0); assert(gfx_wire_send(&w,"x-w 1")==0 && w.write_offset==6);
    reset(&w,0); assert(gfx_wire_send(&w,"x-w 1\nx-reset")<0);
    SDL_Quit(); puts("Framing, temporary reads, EOF, cancellation and partial writes: PASS");
    return 0;
}
