# Linux clients

The original terminal client and the graphical client are separate executables.
Both use FujiNet NIO over `FN_PORT`, and share the saved destination, pilot name
and fetch interval in the `bounce-world-client` app-store namespace.

## Build and run

In the workspace, source `scripts/env.sh` first. Outside the workspace, set
`FUJINET_NIO_LIB` to the library checkout. The graphical build needs a C99
compiler, `pkg-config`, SDL2 >= 2.0.18, SDL2_ttf and DejaVu Sans. On Debian-family
systems the development packages are `libsdl2-dev` and `libsdl2-ttf-dev`; on Arch
they are provided by `sdl2`/`sdl2-compat` and `sdl2_ttf`. A different TrueType font
can be selected with `BWC_FONT=/absolute/path/font.ttf`.

```sh
make linux
make linux-gfx
FN_PORT=/path/to/pty ./build/bwcn.linux
FN_PORT=/path/to/pty ./build/bwcn.linux-gfx
```

`linux-gfx` is opt-in: the existing aggregate/retro builds and terminal target do
not require SDL. Graphics objects live in `obj/linux-gfx/` and cannot overwrite
terminal objects. Each process needs its own transport endpoint/session; do not
open the same serial/PTY endpoint in both clients simultaneously.

The graphical window loads saved settings in the background. Tab or click to
select destination, pilot or interval. Left/Right and Home/End move the cursor;
typing inserts at that position. Backspace erases before the cursor and Delete
erases after it. Clicking a field positions the cursor at the clicked character;
Ctrl+A clears the field. Each field remembers its cursor position, and long
destinations scroll to keep the cursor visible. Navigation and deletion keys
repeat when held. Enter
opens the shape gallery; Enter again connects. Escape goes back. Use the framed
server port, normally `tcp://localhost:9003`; the `tcp://` prefix is optional.
Pilot names are 1–8 printable non-space characters, excluding commas. The fetch
interval is 10–1000 ms, independent of the approximately 60 Hz presentation loop.

Connection errors appear in the setup window so settings can be edited and the
connection retried. Closing the window stops the worker and releases the NIO
session; a currently executing library exchange must finish or time out first.
The window continues drawing during that wait. `FN_PORT` and `FN_BAUD` retain
the library's normal meanings.

## Controls

| Key | Action |
| --- | --- |
| F | Freeze/unfreeze the world |
| R | Reset the world |
| + / - | Increase/decrease world size |
| 1–5 | Add a body of that size |
| I | Toggle the information row |
| W | Show/hide connected pilots |
| V | Switch shape outlines / block footprints (also in gallery) |
| P | Toggle intermediate rendering |
| O / F2 | Toggle network diagnostics (F2 also works on setup) |
| D | Toggle the playfield theme |
| L | Enable/disable a brief collision flash (off by default) |
| Q / Escape | Disconnect and return to setup |
| Window close | Exit |

The keyboard legend remains visible with Info off. World resizing uses the
server command; resizing the desktop window letterboxes the same logical
viewport and does not change the server's registered region.

Offline previews do not open FujiNet or change saved settings:

```sh
./build/bwcn.linux-gfx --demo
./build/bwcn.linux-gfx --gallery
# Deterministic-duration capture; exits after the requested number of frames.
SDL_VIDEODRIVER=dummy ./build/bwcn.linux-gfx --demo --frames 30 --screenshot /tmp/world.bmp
```

Preview motion is synthetic and explicitly labelled DEMO. It is not evidence of
a working live server connection. World commands only operate in a live session.

## What was reused and learned

The text Info failure was a platform contract mismatch: shared drawing refreshes
Info for two frames, then clears only the playfield. Linux erased the entire
screen. It now preserves the bottom two rows, composes frames in memory and
emits only changed cells. Prompts remain immediate. Use an 80x24 or larger
terminal. Empty polls no longer skip keyboard handling/pacing. Normal return and
interruption while processing keyboard input restore termios and cursor state.

The graphical frontend reuses the existing app-store helpers, registration CSV
builder, coordinate decoder, portable interpolation math, embedded shape table
and Amiga silhouette tracer. Atari inspired the destination/pilot setup and
animated gallery. No library, server, Amiga or Atari implementation was changed.

A single SDL worker owns all library calls, framed TCP buffers and stream offsets.
The main thread owns SDL drawing/input. Mutex-protected state copies and a bounded
command queue connect them. The desktop frontend has its own bounded/cancellable
framing wrapper rather than calling the terminal helpers that can exit the process
or write directly to the console on errors. Partial command writes fail the
session instead of replaying potentially accepted commands.

Registration requests WIDE_COORDS and BODY_ID (`0x5`), a 960x576 pixel screen and a
40x24 logical region. Up to 255 visible records fit in the 2304-byte response
buffer. Interpolation pairs logical body IDs and their nearest wrap image, never
packet positions. It adds one snapshot of visual delay; new bodies and large
jumps snap, and stale data is never extrapolated indefinitely. Empty snapshots
clear the old world. Network waits do not block rendering or keyboard input.

`fn_read()` already chunks host reads and maintains library session offsets.
No speculative library rewrite or extra readiness round trip was needed.
`fn_read_available()` is a useful optional hint, not required for correct framed
request/reply handling here.

## Shape limitation

Shape definitions have deliberately not been corrected in this change. Amiga's
example hand-authored overrides are not copied. Linux traces the embedded
occupied-cell silhouettes; every non-space glyph counts as a whole cell, so
partial-cell glyphs and several small shapes are not yet faithful smooth vector
geometry. Block mode shows bounding footprints. Rotation is not requested.
The gallery makes every current shape inspectable; authoring canonical vector
shapes remains separate work.

## Verification

After sourcing the workspace environment, from this repository:

```sh
make linux test-linux-text
make linux-gfx test-linux-gfx test-linux-network
make test-host
```

These passed on 2026-11-16. Text tests execute the real shared renderer across
repeated Info frames/toggles and check termios/cursor restoration under a PTY.
Graphics tests use SDL dummy video and inspect rendered pixels, input events,
clipping, empty frames, letterbox mapping and interpolation. Network tests fake
only the NIO boundary and exercise the real worker, fragmented responses, status
commands/broadcasts, queued controls, empty/malformed snapshots, cancellation and
reconnection. Existing host tests cover shared decode/CSV/vector/interpolation/
pacing and Atari transport behavior.

The graphical tests also passed AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make -f makefiles/linux-gfx.mk test-linux-gfx test-linux-network \
  CFLAGS='-Wall -Wextra -Werror -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Dummy-video world/gallery screenshots were inspected, and an actual desktop
window rendered a 30-frame offline smoke run and exited successfully. No live
FujiNet/Bouncy World server session was exercised for this change.

### Graphical field editing — 2026-11-17

Added cursor movement, insertion, forward/backward deletion, click positioning,
per-field cursor positions and caret-aware horizontal scrolling. Regression tests
exercise saved values, mid-string edits, boundaries, length limits, filtering,
held keys and scrolling through the real SDL event handler. The shared legacy
`get_line` and Atari `title_edit` remain end-only editors; their platform-specific
keyboard mappings were inspected but not changed in this graphical fix.

Verification: `make linux-gfx test-linux-gfx` passed after sourcing the workspace
environment. Tests use SDL dummy video; no desktop window was opened.

## Live diagnostics and Pico soak testing

Press **O** during play or **F2** on any screen to toggle the diagnostics overlay.
On the setup screen, O remains an ordinary input character. An unexpected session
failure opens the overlay automatically. F2 hides it so fields can be edited.
Counters and the last disconnect cause survive returning to setup and reconnecting;
restart the process to reset totals. The original disconnect cause is preserved
even if `fn_close` subsequently fails. This instrumentation is Linux-gfx-only.

For a soak run, keep the terminal error output:

```sh
FN_PORT=/path/to/port ./build/bwcn.linux-gfx 2>bwcn-gfx.log
```

The log records failures with elapsed time since launch, operation/command, symbolic
and numeric error code, partial frame progress, stream offsets, flags and byte
counts. It also records a summary after each session. It does not log every poll
or reset counters on reconnect. No log file is created unless output is redirected.

The overlay reports:

- Attempts, successful connections, failed sessions and intentional exits.
- Commands attempted, complete writes, complete framed replies, valid world replies
  and published updates. A complete frame can still fail semantic validation, so
  “frames OK” is not the same as a successful world snapshot. `close` is sent without
  waiting for a reply, so successful write/frame totals need not match.
- Read calls and successes, `BUSY`, `NOT_READY` and empty successful reads. Temporary
  read statuses are counted separately from errors; they retain the current frame
  and retry after 5 ms. A successful read call may deliver only part of a frame.
- Application TCP-stream bytes sent/received, including command newlines and the
  two-byte response prefix. These are bytes reported by `fn_write`/`fn_read`, including
  reported partial progress on failure, **not** serial/FujiBus traffic. App-store and
  transport framing overhead are excluded. “Other NIO” counts init/open/settings/close
  API operations separately.
- Recent TX/RX bytes per second, commands/second, valid world replies/second and
  render FPS. These use roughly one-second wall-clock samples, including stalls;
  rates fall to zero during inactivity. Totals and timestamps use 64-bit values.
- Last/mean/max completed request latency (write through full reply), slow completed
  replies (at least one second), and last/max read and write call durations.
- Current operation and elapsed time while inside a library call, pending request
  time, age of the last complete reply/world response, partial frame size and flags.
  The worker publishes before entering a blocking call, so its elapsed time can
  keep increasing in the foreground even when no network call returns.
- Errors, NIO-returned timeouts, client reply-deadline expirations, malformed replies,
  EOF flags and incomplete writes. A short write with `OK/0` means the library returned
  success but fewer bytes than requested; the client rejects it. EOF can accompany
  a complete successful frame and therefore is not necessarily a failed read.
- Existing library write diagnostics: exchanges, transport retries, service retries,
  zero-byte acceptances, and the last write-response length/device/command. These are
  read from `fn_write_get_last_diagnostics()` without extra network traffic. Internal
  read transport exchanges/retries are not exposed by these application counters.

### Interpreting a pause or return to setup

| Observation | What it establishes |
| --- | --- |
| FPS steady, `IN FLIGHT: fn_read` time rising | Rendering is alive; the library call has not returned |
| `NOT_READY`/`BUSY` rising, pending request growing | Calls are returning temporarily unavailable; the client is retrying |
| World replies continue but updates stop | Repeated server steps; check freeze state/server progress |
| NIO timeout increases | A library API returned `FN_ERR_TIMEOUT`; this alone does not prove the Pico is at fault |
| Client deadline increases | The client stopped waiting for the framed reply after its five-second budget |
| Malformed increases | Frame length, expected response size, shape ID or body-ID validation failed |
| Last disconnect cause | The exact failure that ended the session, retained through cleanup/reconnect |

The five-second reply budget is checked between library calls and cannot interrupt
a blocking call, so a session can take longer to end. Timeouts, I/O errors, EOF
mid-frame, incomplete writes and invalid replies still end the session; this change
makes that behavior observable rather than introducing automatic replay/reconnect.
The library already retries transient writes. A timeout can leave command acceptance
or stream offsets uncertain, so blindly resending `x-add-body`, `x-reset`, etc. would
risk duplicate actions. The next recovery change should follow measured evidence.

Validation on 2026-11-17: `make linux-gfx test-linux-gfx test-linux-network` passed
after sourcing the workspace environment. Fault-injection tests cover exact traffic
and result counts, temporary/empty reads, distinct timeout classes, partial writes,
blocked-call publication, zero throughput while stalled, malformed input, retained
root cause after a cleanup timeout, and reconnect persistence. SDL dummy-video tests
cover toggling, automatic display on failure and retained display data; an off-screen
screenshot was inspected. No live Pico soak run was performed by the agent.

### Authored graphical shapes

Both graphical renderers now use the same fixed vectors in
`src/amiga/vector_outline.c` (`vo_authored`), indexed 0–18. Coordinates use
four units per world cell, preserving the server's footprints while placing
box-drawing strokes through character centres. Runtime bitmap silhouette
tracing and the Amiga example overrides are no longer used for rendering.

Disconnected dots, crossing strokes, diamonds, stars and the WW/MM and hash
patterns retain their original structure. The seven single-cell shapes use
small scalable symbols (star, ring, hash, X, stylized @, V, caret), not font
glyphs. Two-point contours are independent strokes; longer contours close.
Text shapes and network shape data remain unchanged. Block mode remains
available as a separate presentation option.

Validation: Linux graphical build and SDL tests, host vector/bounds tests,
Amiga wb31 and wb32 builds, and an inspected dummy-video gallery capture.
Amiga on-device visual acceptance and the ongoing Pico soak remain pending.
