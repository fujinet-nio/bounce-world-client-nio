#!/usr/bin/env python3
"""Exercise the real renderer and terminal lifecycle, without a FujiNet."""
import os
import pty
import re
import select
import signal
import subprocess
import termios
import time

binary = 'build/test_linux_display.host'
frames = subprocess.check_output([binary]).decode().split(chr(30))[:-1]
cells = [[' '] * 80 for _ in range(24)]
x = y = 0
for index, frame in enumerate(frames):
    for token in re.findall(r'\x1b\[[0-9;?]*[A-Za-z]|[^\x1b]', frame):
        if token.startswith('\x1b['):
            args, op = token[2:-1], token[-1]
            if op == 'H':
                coords = [int(n) if n else 1 for n in args.split(';')]
                y, x = (coords + [1])[:2]
                y -= 1
                x -= 1
            elif op == 'J':
                cells = [[' '] * 80 for _ in range(24)]
            elif op == 'K':
                cells[y] = [' '] * 80
        else:
            if 0 <= y < 24 and 0 <= x < 80:
                cells[y][x] = token
            x += 1
    if index == 12:
        assert ''.join(cells[22]).strip() == ''
        assert ''.join(cells[23]).strip() == ''
    else:
        assert ''.join(cells[22]).startswith('PILOT'), (index, cells[22])
        assert 'Quit' in ''.join(cells[23]), index
# Unchanged frames must not redraw the screen or status text.
assert all('PILOT' not in f and '\x1b[2J' not in f for f in frames[2:12])

for sig in (None, signal.SIGINT, signal.SIGTERM):
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    proc = subprocess.Popen([binary, '--input'], stdin=slave, stdout=slave)
    try:
        deadline = time.monotonic() + 3
        while termios.tcgetattr(slave) == before and time.monotonic() < deadline:
            time.sleep(.01)
        assert termios.tcgetattr(slave) != before, 'raw mode never entered'
        if sig:
            proc.send_signal(sig)
        else:
            os.write(master, b'q')
        proc.wait(timeout=3)
        assert termios.tcgetattr(slave) == before, 'terminal not restored'
        output = b''
        while select.select([master], [], [], .05)[0]:
            output += os.read(master, 4096)
        assert b'\x1b[?25h' in output
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        os.close(master)
        os.close(slave)
print('Linux Info persistence, differential frames and PTY cleanup: PASS')
