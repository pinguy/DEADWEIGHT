#!/usr/bin/env python3
"""Exercise the actual executable using SDL keyboard and mouse events.

Telemetry is read-only. No teleportation, HP changes, forced wins, or alternate
physics. The --test-io protocol changes timing/display, not game rules.
The exact commands are saved so the playthrough can be reproduced.
"""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

FORWARD, BACK, LEFT, RIGHT, Q, E, F, ENTER, ESC, M, SHIFT = [1 << n for n in range(11)]


class Game:
    def __init__(self, binary, out, record=False):
        self.out = Path(out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.commands = (self.out / 'inputs.txt').open('w')
        self.trace = (self.out / 'trace.jsonl').open('w')
        self.errors = (self.out / 'game-stderr.log').open('w')
        self.encoder = None
        self.record = record
        self.s = None
        args = [str(Path(binary).resolve()), '--test-io', '--seed', '123',
                '--shots', str(self.out), '--render-every', '12']
        fds = ()
        if record:
            self.encoder = subprocess.Popen([
                'ffmpeg', '-nostdin', '-y', '-loglevel', 'error',
                '-f', 'rawvideo', '-pixel_format', 'bgra', '-video_size', '640x360',
                '-framerate', '20', '-i', 'pipe:0', '-an', '-c:v', 'libx264',
                '-preset', 'fast', '-crf', '19', '-pix_fmt', 'yuv420p',
                str(self.out / 'silent.mp4')], stdin=subprocess.PIPE)
            fd = self.encoder.stdin.fileno()
            args += ['--video', f'/dev/fd/{fd}', '--audio', str(self.out / 'audio.s16le')]
            fds = (fd,)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=self.errors, text=True, bufsize=1,
                                  env=env, pass_fds=fds)
        self.read()

    def read(self):
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError(f'Game stopped unexpectedly. Inspect {self.out}/game-stderr.log')
        self.s = json.loads(line)
        self.trace.write(line)
        return self.s

    def command(self, line):
        self.commands.write(line + '\n')
        self.commands.flush()
        self.p.stdin.write(line + '\n')
        self.p.stdin.flush()
        return self.read()

    def step(self, frames=1, keys=0, dx=0, buttons=0):
        return self.command(f'step {frames} {keys} {dx} {buttons}')

    def shot(self, name):
        return self.command(f'shot {name}')

    def press(self, key=0, button=0):
        self.step()
        self.step(keys=key, buttons=button)
        return self.step() if self.s['running'] else self.s

    def aim(self, x, y):
        delta = (math.atan2(y - self.s['y'], x - self.s['x']) - self.s['yaw'] + math.pi) % (2*math.pi) - math.pi
        total = round(delta / .003)
        steps = min(18, max(1, abs(total)//55)) if self.record else 1
        for i in range(steps):
            dx = round(total * (i+1)/steps) - round(total * i/steps)
            self.step(dx=dx)

    def go(self, x, y, sprint=False):
        start = self.s['frame']
        while math.hypot(x-self.s['x'], y-self.s['y']) > .13:
            assert self.s['state'] == 1, f'Not playing: {self.s}'
            assert self.s['frame'] - start < 1200, f'Stuck en route to {(x,y)}: {self.s}'
            delta = (math.atan2(y-self.s['y'], x-self.s['x']) - self.s['yaw'] + math.pi) % (2*math.pi) - math.pi
            self.step(dx=round(delta/.003))
            distance = math.hypot(x-self.s['x'], y-self.s['y'])
            speed = (4.25 if sprint else 3.2) * (.79 if self.s['held'] >= 0 else 1)
            n = min(12, max(1, int((distance-.08)/speed*60)))
            self.step(n, FORWARD | (SHIFT if sprint else 0))
        self.step()

    def pull(self, index):
        x, y, live = self.s['cargo'][index]
        assert live
        self.aim(x, y)
        self.press(button=1)
        assert self.s['held'] == index, f'Expected cargo {index}, got {self.s}'
        self.step(20)

    def shoot(self, drone):
        assert self.s['held'] >= 3, 'Use a plate for the combat check'
        self.aim(*self.s['drones'][drone][:2])
        self.press(button=1)
        self.step(45)
        assert self.s['drones'][drone][2] <= 0, f'Drone survived: {self.s}'

    def close(self):
        self.p.stdin.close()
        try:
            code = self.p.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.p.terminate()
            self.p.wait(timeout=5)
            raise
        finally:
            self.commands.close(); self.trace.close(); self.errors.close()
            if self.encoder:
                self.encoder.stdin.close()
                self.encoder.wait(timeout=30)
        if code:
            raise RuntimeError(f'Game exited with {code}')
        if self.encoder and self.encoder.returncode:
            raise RuntimeError('Video encoding failed')


def victory(g):
    checks = []
    assert g.s['state'] == 0
    g.step(90)
    assert g.s['state'] == 0 and g.s['time'] == 180
    checks.append('Title waits for input')
    g.press(ENTER)
    assert g.s['state'] == 1
    checks.append('Enter starts the actual game')
    g.press(ESC)
    frozen = dict(g.s)
    g.step(90, FORWARD)
    assert g.s['state'] == 2 and g.s['x'] == frozen['x'] and g.s['time'] == frozen['time']
    g.press(ESC)
    assert g.s['state'] == 1
    checks.append('Pause freezes movement and mission time; Escape resumes')
    g.pull(3)
    g.shot('magnet_locked')
    g.press(F)
    assert g.s['held'] == -1
    g.pull(3)
    checks.append('Pull, hold, drop, and re-grab a plate')
    g.shoot(0)
    g.shot('drone_scrapped')
    checks.append('Thrown scrap collides with and destroys a drone')
    g.go(3.5, 5.6)
    g.pull(0)
    g.shot('carrying_core')
    g.go(3.5, 10.5)
    assert g.s['cores'] == 1
    checks.append('First core delivered to the lift')
    # Cargo and player collision: drive into the west bulkhead, then return.
    g.aim(-10, g.s['y'])
    g.step(90, FORWARD)
    assert 1.21 <= g.s['x'] <= 1.30 and g.s['bumps'] > 0
    g.shot('wall_collision')
    checks.append('Bulkhead collision stops forward movement')
    g.go(3.5, 10.5)
    # The south aisle gives a clear shot at the second roaming drone.
    g.pull(4)
    g.shoot(2)
    g.go(8.5, 10.5)
    g.go(13.5, 10.5)
    g.pull(5)
    g.shoot(1)
    g.shot('security_cleared')
    assert g.s['kills'] == 3
    checks.append('All three security drones destroyed using physics projectiles')
    g.pull(2)
    g.go(8.5, 10.5)
    g.go(3.5, 10.5)
    assert g.s['cores'] == 2
    checks.append('Second core delivered')
    g.go(3.5, 5.5)
    g.go(8.5, 5.5)
    g.go(14.5, 5.5)
    g.pull(1)
    g.go(14.5, 10.5)
    g.go(8.5, 10.5)
    # Winning may happen while approaching the final waypoint.
    g.aim(3.5, 10.5)
    for _ in range(160):
        g.step(3, FORWARD)
        if g.s['state'] == 4:
            break
    assert g.s['state'] == 4 and g.s['cores'] == 3 and g.s['hp'] > 0
    g.shot('win')
    final = dict(g.s)
    g.step(150)
    assert g.s['time'] == final['time']
    checks.append('Third core triggers victory; the terminal state freezes the timer')
    g.press(ENTER)
    assert g.s['state'] == 1 and g.s['cores'] == 0 and g.s['hp'] == 100 and g.s['kills'] == 0
    checks.append('Restart resets cargo, health, drones and score')
    return checks, final


def failure_paths(g):
    checks = []
    g.press(ENTER)
    g.press(M)
    assert g.s['muted'] == 1
    g.step(60)
    g.press(M)
    assert g.s['muted'] == 0
    checks.append('Mute toggles both ways')
    g.step(950)
    assert g.s['state'] == 3 and g.s['hp'] == 0
    g.shot('dead')
    checks.append('Live enemy contact reduces hull and causes death')
    g.press(ENTER)
    assert g.s['state'] == 1 and g.s['hp'] == 100 and g.s['cores'] == 0
    checks.append('Death screen restart restores a fresh run')
    # Destroy the south drone and shelter behind the north columns. The
    # other two drones have no line of sight; normal mission time still runs.
    g.pull(4)
    g.shoot(2)
    g.go(3.5, 5.5, sprint=True)
    g.go(8.5, 5.5, sprint=True)
    g.go(8.5, 2.5, sprint=True)
    g.step(11000)
    assert g.s['state'] == 3 and g.s['time'] <= 0 and g.s['hp'] > 0, g.s
    g.shot('collapse')
    checks.append('The 180-second station timer causes death even with hull remaining')
    g.press(ESC)
    assert g.s['running'] == 0
    checks.append('Escape exits from the death screen')
    return checks


def convert_shots(out):
    dead, collapse = Path(out)/'dead.ppm', Path(out)/'collapse.ppm'
    if dead.exists() and collapse.exists():
        assert dead.read_bytes() != collapse.read_bytes(), 'Death evidence was overwritten by the timeout capture'
    try:
        from PIL import Image
    except ImportError:
        return
    for path in Path(out).glob('*.ppm'):
        im = Image.open(path)
        assert im.size == (640, 360)
        assert len(im.getcolors(640*360) or []) > 12, f'Empty capture: {path}'
        im.save(path.with_suffix('.png'))


def verify_recording(path, expected_seconds):
    """Reject unfinished containers and decode every video/audio packet."""
    probe = subprocess.run([
        'ffprobe', '-v', 'error', '-count_frames', '-show_streams',
        '-show_format', '-of', 'json', str(path)],
        capture_output=True, text=True, check=True)
    info = json.loads(probe.stdout)
    video = next(s for s in info['streams'] if s['codec_type'] == 'video')
    audio = next(s for s in info['streams'] if s['codec_type'] == 'audio')
    assert video['codec_name'] == 'h264' and video['pix_fmt'] == 'yuv420p'
    assert (video['width'], video['height']) == (640, 360)
    assert audio['codec_name'] == 'aac' and audio['channels'] == 2
    assert abs(float(info['format']['duration']) - expected_seconds) < .15
    # Stream-copy trimming can omit up to two trailing reordered H.264 frames.
    assert int(video['nb_read_frames']) >= int(expected_seconds * 20) - 2
    assert float(audio['duration']) >= expected_seconds - .1
    subprocess.run([
        'ffmpeg', '-nostdin', '-v', 'error', '-xerror', '-i', str(path),
        '-map', '0:v:0', '-map', '0:a:0', '-f', 'null', '-'],
        capture_output=True, check=True)
    return dict(passed=True, duration=float(info['format']['duration']),
                decoded_video_frames=int(video['nb_read_frames']),
                video_codec=video['codec_name'], pixel_format=video['pix_fmt'],
                audio_codec=audio['codec_name'], channels=audio['channels'])


def main():
    p = argparse.ArgumentParser()
    p.add_argument('binary')
    p.add_argument('--out', default='evidence')
    p.add_argument('--record', action='store_true')
    p.add_argument('--failure-only', action='store_true')
    args = p.parse_args()
    started = time.monotonic()
    g = Game(args.binary, args.out, args.record)
    try:
        if args.failure_only:
            checks, final = failure_paths(g), dict(g.s)
        else:
            checks, final = victory(g)
    finally:
        g.close()
    convert_shots(args.out)
    if args.record:
        duration = (final['frame']+150)//3/20
        pending = g.out/'playthrough.pending.mp4'
        subprocess.run(['ffmpeg','-nostdin','-y','-loglevel','error',
            '-i',str(g.out/'silent.mp4'),'-f','s16le','-ar','48000','-ac','2',
            '-i',str(g.out/'audio.s16le'),'-c:v','copy','-c:a','aac','-b:a','160k',
            '-t',str(duration),'-shortest','-movflags','+faststart',str(pending)],check=True)
        with pending.open('rb') as f:
            os.fsync(f.fileno())
        video_check = verify_recording(pending, duration)
        pending.replace(g.out/'playthrough.mp4')
        (g.out/'video-check.json').write_text(json.dumps(video_check,indent=2)+'\n')
    result = dict(passed=True,binary=str(Path(args.binary).resolve()),
                  seed=123,method='SDL_PushEvent keyboard and mouse; real event loop, physics, SDL software renderer and PCM audio',
                  checks=checks,final=final,wall_seconds=round(time.monotonic()-started,2))
    (g.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
