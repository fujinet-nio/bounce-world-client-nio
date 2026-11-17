#ifndef BWC_GFX_DIAGNOSTICS_H
#define BWC_GFX_DIAGNOSTICS_H
#include <stdint.h>
/* Process-lifetime counters. Copied under the client mutex, never shared live. */
typedef struct {
    uint64_t started, operation_at, request_at, last_reply_at, last_world_at;
    uint64_t requests, writes_ok, frames_ok, worlds_ok, steps;
    uint64_t read_calls, reads_ok, tx_bytes, rx_bytes, other_calls, other_ok;
    uint64_t busy, not_ready, empty_reads, errors, nio_timeouts, deadlines;
    uint64_t eof, malformed, partial_writes, attempts, connections, drops, cancellations;
    uint64_t write_exchanges, transport_retries, service_retries, zero_accepted;
    uint64_t read_ms, read_max_ms, write_ms, write_max_ms;
    uint64_t request_ms, request_max_ms, request_total_ms, slow_requests;
    uint64_t operation_last_ms, operation_max_ms;
    uint64_t failure_serial, failure_at, drop_at;
    uint32_t read_offset, write_offset;
    unsigned received, expected, last_bytes, requested_bytes, last_code, last_flags;
    unsigned response_length, response_device, response_command;
    int active, pending;
    char operation[32], command[32], last_error[240], failure_detail[240], end_reason[160];
    char drop_error[240], drop_detail[240]; /* first cause, before cleanup */
} GfxDiagnostics;
typedef struct {
    uint64_t at, tx, rx, requests, worlds, frames;
    double tx_per_second, rx_per_second, requests_per_second, worlds_per_second, fps;
    int initialized;
} GfxRates;
void gfx_rates_sample(GfxRates *r, const GfxDiagnostics *d, uint64_t now, uint64_t frames);
const char *gfx_nio_result(unsigned code);
void gfx_diag_begin(GfxDiagnostics *d, const char *operation);
void gfx_diag_end(GfxDiagnostics *d);
void gfx_diag_error(GfxDiagnostics *d, const char *reason, unsigned code);
#endif
