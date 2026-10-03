#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Installed QEMU input transport only; no CDK2 firmware or guest proof."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

root = Path(__file__).resolve().parent
qemu = Path(shutil.which('qemu-system-x86_64')).resolve()
inputs = (Path(__file__), qemu, Path(sys.executable).resolve())
before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
events = ('vnc_msg_client_key_event', 'vnc_key_event_map', 'input_event_key_qcode')
available = subprocess.check_output([str(qemu), '-trace', 'help'], stderr=subprocess.STDOUT).decode().splitlines()
if not set(events).issubset(available):
    raise SystemExit('required installed trace events absent')
if (root / 'trace.log').exists():
    raise SystemExit('refusing to overwrite original probe')
command = [str(qemu), '-machine', 'q35,accel=tcg', '-m', '64M', '-nodefaults',
           '-device', 'VGA,id=probe-vga', '-device', 'qemu-xhci,id=xhci',
           '-device', 'usb-kbd,bus=xhci.0,display=probe-vga', '-S',
           '-display', f'vnc=unix:{root / "vnc.sock"}',
           '-qmp', f'unix:{root / "qmp.sock"},server=on,wait=off']
for event in events:
    command += ['-trace', f'enable={event},file={root / "trace.log"}']
(root / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
transcript = []
started = time.monotonic()
deadline = started + 5


def remaining():
    value = deadline - time.monotonic()
    if value <= 0:
        raise TimeoutError('original five-second probe budget expired')
    return value


def receive(stream, count):
    data = b''
    while len(data) < count:
        stream.settimeout(remaining())
        chunk = stream.recv(count - len(data))
        if not chunk:
            raise RuntimeError('VNC closed before response')
        data += chunk
    return data


with (root / 'qemu.log').open('wb') as log:
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        while not (root / 'qmp.sock').exists() or not (root / 'vnc.sock').exists():
            remaining()
            time.sleep(0.005)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as control, \
                socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as vnc:
            control.settimeout(remaining())
            control.connect(str(root / 'qmp.sock'))
            reader = control.makefile('rb')
            greeting = json.loads(reader.readline())
            if 'QMP' not in greeting:
                raise RuntimeError('invalid QMP greeting')

            def qmp(name):
                identity = 'probe-' + str(len(transcript))
                request = {'execute': name, 'id': identity}
                control.settimeout(remaining())
                control.sendall(json.dumps(request).encode() + b'\n')
                while True:
                    reply = json.loads(reader.readline())
                    if reply.get('id') == identity:
                        transcript.append({'request': request, 'reply': reply})
                        if 'error' in reply:
                            raise RuntimeError(reply)
                        return reply['return']
                    if 'event' not in reply:
                        raise RuntimeError('unexpected QMP identity')

            qmp('qmp_capabilities')
            vnc.settimeout(remaining())
            vnc.connect(str(root / 'vnc.sock'))
            if receive(vnc, 12) != b'RFB 003.008\n':
                raise RuntimeError('unexpected VNC version')
            vnc.sendall(b'RFB 003.008\n')
            security = receive(vnc, receive(vnc, 1)[0])
            if 1 not in security:
                raise RuntimeError('VNC no-auth absent')
            vnc.sendall(b'\x01')
            if receive(vnc, 4) != b'\0' * 4:
                raise RuntimeError('VNC security failed')
            vnc.sendall(b'\x01')
            initial = receive(vnc, 24)
            receive(vnc, struct.unpack('>I', initial[20:])[0])
            pixel_bytes = initial[4] // 8
            if pixel_bytes not in (1, 2, 4):
                raise RuntimeError('unsupported VNC pixel format')
            vnc.sendall(struct.pack('>BBHi', 2, 0, 1, 0))  # raw encoding only

            def key_and_frame_barrier(down):
                vnc.sendall(struct.pack('>BBHI', 4, int(down), 0, 0xffbf))
                vnc.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, 1, 1))
                header = receive(vnc, 4)
                if header[0] != 0:
                    raise RuntimeError('unexpected VNC framebuffer reply')
                rectangles = struct.unpack('>H', header[2:])[0]
                if not 0 < rectangles <= 4096:
                    raise RuntimeError('invalid framebuffer rectangle count')
                for _ in range(rectangles):
                    x, y, width, height, encoding = struct.unpack('>HHHHi', receive(vnc, 12))
                    if encoding != 0 or not 0 < width <= 4096 or not 0 < height <= 4096:
                        raise RuntimeError('unexpected framebuffer encoding/extent')
                    receive(vnc, width * height * pixel_bytes)
                transcript.append({'vnc_f2_down': down, 'same_socket_frame_reply': True})

            if qmp('query-status')['running'] is not False:
                raise RuntimeError('probe did not begin paused')
            key_and_frame_barrier(True)
            if qmp('query-status')['running'] is not False:
                raise RuntimeError('paused probe unexpectedly ran')
            qmp('cont')
            if qmp('query-status')['running'] is not True:
                raise RuntimeError('running comparison did not resume')
            key_and_frame_barrier(False)
            key_and_frame_barrier(True)
            qmp('stop')
            qmp('quit')
            process.wait(timeout=remaining())
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        (root / 'transcript.json').write_text(json.dumps(transcript, indent=2) + '\n')
        after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        (root / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
if before != after or process.returncode != 0:
    raise SystemExit('probe input/process identity failed')
trace = (root / 'trace.log').read_text()
mapped = re.findall(r'vnc_key_event_map down ([01]), sym 0xffbf -> keycode 0x[0-9a-f]+ \[f2\]', trace)
delivered = re.findall(r'input_event_key_qcode con [0-9]+, key qcode f2, down ([01])', trace)
if mapped != ['1', '0', '1'] or delivered != ['0', '1']:
    raise SystemExit('trace did not distinguish paused mapping from running delivery')
wall = time.monotonic() - started
if not 0 < wall <= 5:
    raise SystemExit('five-second probe bound exceeded')
(root / 'result.json').write_text(json.dumps({
    'wall_seconds': wall, 'deadline_seconds': 5, 'qemu_status': process.returncode,
    'mapped_vnc_f2': mapped, 'delivered_input_f2': delivered,
    'scope': 'installed QEMU transport, not CDK2 USB/BDS/native admission'}, indent=2) + '\n')
print('Installed paused VNC event mapped but not delivered; running comparison delivered')
