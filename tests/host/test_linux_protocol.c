#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "protocol.h"

static uint8_t input[16];
static unsigned size, position, mode, reads, notifications;
static GfxDiagnostics stats;
static void notify(void *context, const GfxDiagnostics *d)
{ (void)context; ++notifications; assert(d==&stats); }
void fn_write_get_last_diagnostics(fn_write_diagnostics_t *d)
{ memset(d,0,sizeof(*d)); d->exchanges=1; }
uint8_t fn_write(fn_handle_t h,uint32_t o,const uint8_t *p,uint16_t n,uint16_t *written)
{ (void)h; assert(o==0 && p[n-1]=='\n'); *written=mode==4 ? n-1 : n; return FN_OK; }
uint8_t fn_read(fn_handle_t h,uint32_t o,uint8_t *p,uint16_t n,uint16_t *got,uint8_t *flags)
{
    (void)h;
    assert(o==position); ++reads; *got=0; *flags=0;
    if (mode==1 && reads<3) return reads==1 ? FN_ERR_BUSY : FN_ERR_NOT_READY;
    if (mode==5) return FN_ERR_TIMEOUT;
    if (mode==6) return FN_ERR_NOT_READY;
    if (mode==7 && reads==1) return FN_OK; /* empty successful poll */
    if (mode==8) { assert(stats.active && !strcmp(stats.operation,"fn_read")); SDL_Delay(20); }
    if (mode==2) { *flags=FN_READ_EOF; return FN_OK; }
    if (position<size) { assert(n); *p=input[position++]; *got=1; }
    if (position==size) *flags=FN_READ_EOF;
    return FN_OK;
}
static int cancel(void *unused) { (void)unused; return 1; }
static void reset(GfxWire *w,unsigned m)
{
    memset(w,0,sizeof(*w)); mode=m; position=reads=notifications=0; size=5;
    memset(&stats,0,sizeof(stats)); stats.started=stats.request_at=SDL_GetTicks64();
    w->diagnostics=&stats; w->notify=notify;
    memcpy(input,"\5\0abc",5);
}
int main(void)
{
    GfxWire w;
    SDL_Init(SDL_INIT_TIMER);
    reset(&w,1); assert(gfx_wire_reply(&w)==3 && w.read_offset==5 && !memcmp(w.packet+2,"abc",3));
    assert(stats.frames_ok==1 && stats.reads_ok==5 && stats.read_calls==7);
    assert(stats.busy==1 && stats.not_ready==1 && stats.errors==0 && stats.rx_bytes==5 && notifications>7);
    reset(&w,2); assert(gfx_wire_reply(&w)<0); assert(stats.eof==1 && stats.errors==1);
    reset(&w,0); input[0]=1; assert(gfx_wire_reply(&w)<0); assert(stats.malformed==1);
    reset(&w,0); input[1]=100; assert(gfx_wire_reply(&w)<0);
    reset(&w,0); size=4; assert(gfx_wire_reply(&w)<0); /* truncated frame */
    reset(&w,0); w.cancelled=cancel; assert(gfx_wire_reply(&w)<0 && reads==0 && stats.errors==0);
    reset(&w,4); assert(gfx_wire_send(&w,"x-w 1")<0 && w.write_offset==0);
    assert(stats.tx_bytes==5 && stats.partial_writes==1 && stats.writes_ok==0 && stats.errors==1);
    reset(&w,0); assert(gfx_wire_send(&w,"x-w 1")==0 && w.write_offset==6);
    assert(stats.requests==1 && stats.writes_ok==1 && stats.tx_bytes==6 && stats.write_exchanges==1);
    assert(!strcmp(stats.command,"x-w"));
    reset(&w,0); assert(gfx_wire_send(&w,"x-w 1\nx-reset")<0);
    reset(&w,5); assert(gfx_wire_reply(&w)<0);
    assert(stats.nio_timeouts==1 && stats.deadlines==0 && stats.errors==1);
    reset(&w,6); w.reply_timeout_ms=12; assert(gfx_wire_reply(&w)<0);
    assert(stats.deadlines==1 && stats.nio_timeouts==0 && stats.not_ready>0 && stats.errors==1);
    reset(&w,7); assert(gfx_wire_reply(&w)==3 && stats.empty_reads==1 && stats.errors==0);
    reset(&w,8); assert(gfx_wire_reply(&w)==3 && stats.read_max_ms>=20 && stats.request_ms>=100);
    { GfxRates rates={0}; GfxDiagnostics d={0};
      gfx_rates_sample(&rates,&d,1000,0);
      d.tx_bytes=100; d.rx_bytes=300; d.requests=5; d.worlds_ok=3;
      gfx_rates_sample(&rates,&d,2000,60);
      assert(rates.tx_per_second==100 && rates.rx_per_second==300 && rates.requests_per_second==5 && rates.fps==60);
      gfx_rates_sample(&rates,&d,3000,120);
      assert(rates.rx_per_second==0 && rates.worlds_per_second==0 && rates.fps==60); }
    SDL_Quit(); puts("Framing, temporary reads, EOF, cancellation and partial writes: PASS");
    return 0;
}
