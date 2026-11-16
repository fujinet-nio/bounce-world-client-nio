#include <assert.h>
#include "data.h"
#include "run_simulation.h"
static int keys, pauses, closed;
void init_screen(void) {}
void cursor(uint8_t on) { (void)on; }
void get_world_clients(void) {}
int16_t fetch_client_state(void) { return 0; }
void handle_kb(void) { ++keys; is_running_sim = false; }
void network_retry_pause(void) { ++pauses; }
void pace_client_fetch(void) {}
void handle_app_status(void) {}
void show_screen(void) { assert(0); }
void disconnect_service(void) { ++closed; }
void bwc_latency_trace_report(void) {}
uint8_t is_alt_screen;
int main(void) { run_simulation(); assert(keys == 1 && pauses == 1 && closed == 1); return 0; }
