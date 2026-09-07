#!/usr/bin/env python3
"""Matched scene timings plus a deliberately slow real-time presentation test.

--baseline optionally compares an earlier executable on the same host. The
scene setup consists solely of the recorded SDL input commands. Timing starts
after setup. Results are host measurements, not a prediction for another PC.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def measure(binary, prefix, frames):
    p = subprocess.Popen([str(binary), '--test-io', '--render-every', '1'],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, env=ENV)
    def command(line):
        p.stdin.write(line+'\n'); p.stdin.flush()
        line = p.stdout.readline()
        if not line:
            raise RuntimeError(p.stderr.read())
        return json.loads(line)
    try:
        assert json.loads(p.stdout.readline())['state'] == 0
        for line in prefix:
            command(line)
        command('step 3 0 0 0')
        start = time.perf_counter()
        result = command(f'step {frames} 0 0 0')
        seconds = time.perf_counter()-start
    finally:
        p.stdin.close()
        code = p.wait(timeout=10)
    assert code == 0
    return dict(seconds=seconds, fps=frames/seconds, end_state=result['state'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('--baseline', type=Path)
    ap.add_argument('--out', type=Path, default=Path('performance.json'))
    ap.add_argument('--frames', type=int, default=240)
    args = ap.parse_args()
    commands = (ROOT/'evidence/victory/inputs.txt').read_text().splitlines()
    def until(marker):
        return commands[:commands.index('shot '+marker)+1]
    scenes = {'room': ['step 1 128 0 0', 'step 1 0 0 0'],
              'carrying_core': until('carrying_core'),
              'at_wall': until('wall_collision')}
    result = {'method': 'Matched input, 640x360, SDL software renderer; timed after setup',
              'frames_per_scene': args.frames, 'scenes': {}}
    for name, prefix in scenes.items():
        values = {}
        if args.baseline:
            values['before'] = measure(args.baseline.resolve(), prefix, args.frames)
        values['after'] = measure(args.binary.resolve(), prefix, args.frames)
        if args.baseline:
            values['speedup'] = values['before']['seconds']/values['after']['seconds']
        result['scenes'][name] = values
    result['realtime'] = []
    for delay in (0,40):
        p = subprocess.run([str(args.binary.resolve()), '--benchmark', '4',
                            '--frame-delay-ms', str(delay)], env=ENV,
                           capture_output=True, text=True, check=True, timeout=15)
        value = json.loads(p.stdout)
        assert abs(value['simulation_seconds']-value['wall_seconds']) < .12, value
        assert abs(value['mission_seconds']-value['simulation_seconds']) < .01, value
        assert value['audio_queue_empty_events'] == 0, value
        assert 0 <= value['audio_seconds']-value['wall_seconds'] < .3, value
        result['realtime'].append(value)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
