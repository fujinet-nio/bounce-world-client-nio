/* Fake only the NIO boundary. Exercise the real worker, queue and framing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "client.h"
#include "protocol.h"
#include "appstore_settings.h"
#include "status.h"

static SDL_threadID owner;
static SDL_atomic_t stalled, snapshots, scenario, block_read, read_entered, read_release;
static uint8_t input[GFX_PACKET_MAX];
static unsigned size, position, offset_in, offset_out, step;
static char log_commands[128][160];
static unsigned logged;
static void owned(void) { assert(SDL_ThreadID()==owner); }
static void response(const uint8_t *data,unsigned n)
{
    assert(position==size); size=n+2; position=0;
    input[0]=(uint8_t)size; input[1]=(uint8_t)(size>>8);
    memcpy(input+2,data,n);
}
uint8_t fn_init(void) { owner=SDL_ThreadID(); return FN_OK; }
void fn_shutdown(void) { owned(); }
uint8_t fn_open(fn_handle_t *h,uint8_t mode,const char *url,uint8_t flags)
{
    owned(); assert(mode==0 && flags==FN_OPEN_STREAM_NO_PROBE && !strcmp(url,"tcp://test:9003"));
    *h=1; size=position=offset_in=offset_out=step=0; return FN_OK;
}
uint8_t fn_close(fn_handle_t h) { owned(); assert(h==1); return SDL_AtomicGet(&scenario)==1 ? FN_ERR_TIMEOUT : FN_OK; }
bool appstore_read_setting(char *s,uint8_t n,const char *key)
{ owned(); snprintf(s,n,"%s",!strcmp(key,APPSTORE_KEY_ENDPOINT)?"test:9003":!strcmp(key,APPSTORE_KEY_NAME)?"PILOT":"10"); return true; }
bool appstore_write_setting(const char *s,const char *key) { owned(); assert(*s && *key); return true; }
uint8_t appstore_last_error(void) { return FN_OK; }
void fn_write_get_last_diagnostics(fn_write_diagnostics_t *d)
{ memset(d,0,sizeof(*d)); d->exchanges=1; }
uint8_t fn_write(fn_handle_t h,uint32_t o,const uint8_t *data,uint16_t n,uint16_t *written)
{
    char cmd[160]; uint8_t payload[GFX_PACKET_MAX-2]={0};
    owned(); assert(h==1 && o==offset_out && n<sizeof(cmd)); offset_out+=n; *written=n;
    memcpy(cmd,data,n); cmd[n-1]=0;
    if (logged<128) snprintf(log_commands[logged++],160,"%s",cmd);
    if (!strncmp(cmd,"close ",6)) return FN_OK;
    if (!strncmp(cmd,"x-add-client ",13)) {
        assert(strstr(cmd,"PILOT,3,960,576,40,24,0x5")); payload[0]=7; response(payload,1);
    } else if (!strcmp(cmd,"x-ws")) {
        payload[0]=40; payload[2]=24; payload[4]=1; payload[6]=1; payload[11]=1; response(payload,14);
    } else if (!strcmp(cmd,"x-who")) response((const uint8_t *)"PILOT   ",8);
    else if (!strncmp(cmd,"x-w ",4)) {
        assert(!strcmp(cmd,"x-w 7"));
        payload[0]=(uint8_t)++step;
        if (SDL_AtomicGet(&scenario)==1) { payload[2]=1; response(payload,3); } /* malformed */
        else if (SDL_AtomicGet(&scenario)==2) {
            unsigned i;
            payload[2]=255;
            for (i=0;i<255;++i) { payload[3+i*9]=5; payload[8+i*9]=(uint8_t)(i+1); }
            response(payload,3+255*9);
        } else {
            payload[1]=step==1 ? CLIENT_CHANGE|COLLISION|CLIENT_CMD : 0;
            payload[2]=step>=2 ? 0 : 1;
            payload[3]=5; payload[4]=100; payload[6]=120; payload[8]=42;
            response(payload,payload[2] ? 12 : 3);
        }
        SDL_AtomicAdd(&snapshots,1);
    } else if (!strcmp(cmd,"x-cmd-get 7")) {
        uint8_t controls[]={3,5,7}; response(controls,3);
    } else if (!strcmp(cmd,"x-msg")) response((const uint8_t *)"HELLO",5);
    else { payload[0]=1; response(payload,1); }
    return FN_OK;
}
uint8_t fn_read(fn_handle_t h,uint32_t o,uint8_t *p,uint16_t max,uint16_t *got,uint8_t *flags)
{
    unsigned n;
    owned(); assert(h==1 && o==offset_in); *got=0; *flags=0;
    if (SDL_AtomicGet(&block_read)) {
        SDL_AtomicSet(&read_entered,1);
        while (!SDL_AtomicGet(&read_release)) SDL_Delay(1);
        SDL_AtomicSet(&block_read,0);
    }
    if (SDL_AtomicGet(&stalled)) return FN_ERR_NOT_READY;
    n=size-position; if (n>2) n=2; if (n>max) n=max;
    assert(n); memcpy(p,input+position,n); position+=n; offset_in+=n; *got=(uint16_t)n;
    return FN_OK;
}
static GfxState wait_state(GfxClient *c,int stage)
{
    uint32_t start=SDL_GetTicks(); GfxState s;
    do {
        gfx_client_copy(c,&s);
        if ((stage==0 && s.ready) || (stage==1 && s.snapshot.serial) ||
            (stage==2 && s.error[0]) || (stage==3 && s.done) ||
            (stage==4 && !s.connected && !s.connecting)) return s;
        SDL_Delay(1);
    } while (SDL_GetTicks()-start<3000);
    assert(!"worker timed out"); return s;
}
int main(void)
{
    GfxClient c; GfxState s; unsigned i; int found=0, add_after_freeze=0;
    SDL_Init(SDL_INIT_TIMER);
    assert(gfx_client_start(&c)); s=wait_state(&c,0);
    assert(!strcmp(s.settings.name,"PILOT"));
    gfx_client_connect(&c,&s.settings); s=wait_state(&c,1);
    assert(s.snapshot.count==1 && s.snapshot.shapes[0].body_id==42);
    assert(s.who && s.info && s.broadcast && !strcmp(s.message,"HELLO") && s.collision_serial==1);
    /* Stats are published BEFORE entering a genuinely blocked library call. */
    SDL_AtomicSet(&block_read,1);
    { uint32_t start=SDL_GetTicks();
      while (!SDL_AtomicGet(&read_entered) && SDL_GetTicks()-start<1000) SDL_Delay(1); }
    assert(SDL_AtomicGet(&read_entered));
    gfx_client_copy(&c,&s);
    assert(s.diagnostics.active && !strcmp(s.diagnostics.operation,"fn_read"));
    SDL_Delay(30);
    assert(SDL_GetTicks64()-s.diagnostics.operation_at>=30);
    SDL_AtomicSet(&read_release,1);
    /* Keep foreground access responsive while a worker read stalls. */
    SDL_AtomicSet(&stalled,1);
    assert(gfx_client_command(&c,'f')); assert(gfx_client_command(&c,'3'));
    for (i=0;i<100;++i) { gfx_client_copy(&c,&s); SDL_Delay(1); }
    assert(s.diagnostics.read_calls>0 && s.diagnostics.not_ready>0 && s.diagnostics.errors==0);
    assert(s.diagnostics.connections==1 && s.diagnostics.worlds_ok>0);
    SDL_AtomicSet(&stalled,0);
    { uint32_t start=SDL_GetTicks(); do { gfx_client_copy(&c,&s); SDL_Delay(1); }
      while ((s.snapshot.serial<2 || s.snapshot.count!=0) && SDL_GetTicks()-start<3000); }
    if (!(s.snapshot.serial>=2 && s.snapshot.count==0)) fprintf(stderr,"serial=%u count=%u error=%s logged=%u\n",s.snapshot.serial,s.snapshot.count,s.error,logged);
    assert(s.snapshot.serial>=2 && s.snapshot.count==0);
    /* Cancellation interrupts repeated NOT_READY; no UI or NIO races. */
    SDL_AtomicSet(&stalled,1); gfx_client_stop(&c); wait_state(&c,3); gfx_client_destroy(&c);
    for(i=0;i<logged;++i) {
        if(!strcmp(log_commands[i],"x-freeze")) found=1;
        if(!strcmp(log_commands[i],"x-add-body 3") && found) add_after_freeze=1;
    }
    assert(found && add_after_freeze);
    SDL_AtomicSet(&stalled,0); SDL_AtomicSet(&scenario,1);
    assert(gfx_client_start(&c)); s=wait_state(&c,0); gfx_client_connect(&c,&s.settings);
    s=wait_state(&c,2); assert(strstr(s.error,"Malformed") && s.snapshot.count==0 && !s.connected);
    { uint32_t start=SDL_GetTicks();
      do { gfx_client_copy(&c,&s); SDL_Delay(1); }
      while (s.diagnostics.nio_timeouts==0 && SDL_GetTicks()-start<1000); }
    assert(s.diagnostics.drops==1 && s.diagnostics.malformed==1 && s.diagnostics.nio_timeouts==1);
    assert(strstr(s.diagnostics.drop_error,"Malformed") && strstr(s.diagnostics.last_error,"fn_close"));
    SDL_AtomicSet(&scenario,2);
    gfx_client_connect(&c,&s.settings); s=wait_state(&c,1);
    assert(s.connected && !s.error[0] && s.attempt==2 && s.snapshot.count==255);
    assert(s.snapshot.shapes[254].body_id==255);
    assert(s.diagnostics.attempts==2 && s.diagnostics.connections==2 && s.diagnostics.drops==1);
    assert(strstr(s.diagnostics.drop_error,"Malformed"));
    gfx_client_stop(&c); wait_state(&c,3); gfx_client_destroy(&c);
    SDL_Quit(); puts("Network worker ownership, commands, status events, empty/malformed snapshots and cancellation: PASS");
    return 0;
}
