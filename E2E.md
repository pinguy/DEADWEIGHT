# DEADWEIGHT — performance update and E2E evidence

The game had avoidable CPU costs and a timing bug. This update caches wall
materials and fixed effects, skips floor/ceiling pixels hidden by walls, uses
integer blending, clips sprite loops, and computes core rotation once per
frame. It retains the 640×360 framebuffer.

Desktop presentation now asks SDL for acceleration and falls back to software.
The raycaster itself still runs on the CPU. Simulation catches up in fixed
steps when drawing is slow; audio is filled according to device consumption.
The soundtrack and game rules are unchanged.

## Matched host performance

These are uncapped SDL software-renderer measurements on the same hosted
machine, after identical input-based scene setup, with 240 timed frames each.
They are not measurements from the user's workstation. The benchmarked renderer
and timing code are the release implementation; the final follow-up only
separated death and timeout capture filenames.

| Scene | Packed FPS before → after | Gain | Native FPS before → after | Gain |
| --- | ---: | ---: | ---: | ---: |
| Room | 67 → 242 | 3.6× | 110 → 311 | 2.8× |
| Carrying a core | 56 → 240 | 4.3× | 89 → 302 | 3.4× |
| Facing a bulkhead | 54 → 436 | 8.1× | 94 → 619 | 6.6× |

With an artificial 40 ms drawing delay, the normal packed runtime drew about
22 frames per second while advancing simulation at 60 steps per second.
Four seconds of wall time produced approximately four seconds of mission time;
no empty software audio queue was observed. This checks the actual real-time
loop, separately from the deterministic replay mode.

## End-to-end checks

The release packed executable passed the full victory suite: title/start,
pause/resume, pull/drop/re-grab, projectile hits, all three drone kills,
collision, all three deliveries, victory and restart. The failure suite passed
mute controls, contact damage/death, restart, station timeout with hull left,
and a clean exit. Death and timeout now have distinct screenshots; the test
rejects an accidental overwrite.

The same complete replay produced byte-identical 48 kHz stereo PCM and the
same final game state as the original release. Captured PCM has no clipped
samples. A carried-core image was inspected after optimization; measured
mean RGB differences across selected images were below 0.2 of 255 levels.

## Reproduce and inspect

Run `sh run.sh --benchmark 5` for a short check on the actual desktop.
Run `python3 tests/performance.py ./deadweight-native --out performance.json`
for the scene and slow-frame checks. The optional `--baseline` argument compares
an older executable on the same host.

`evidence/victory` and `evidence/failure` contain release inputs, telemetry,
assertion results and captures. The victory folder includes the updated video.
`evidence/performance-packed.json` and `performance-native.json` contain full
measurements; `regression.json` and `audio-check.json` contain the audio/state
checks. `SHA256SUMS` identifies the packaged files.

Tests here use SDL's dummy display/audio drivers. GPU presentation, physical
input, fullscreen and speakers on the user's machine still need a local run.
An empty SDL queue counter is software telemetry, not a microphone measurement.

## Release files

- `deadweight`: 15,132 bytes; SHA-256 `3e46b73a8ae5d60fbeb32a6def3f9f5c7d8a3c025cde3fd1fe94e06969b217e1`.
- `deadweight-native`: 58,144 bytes; SHA-256 `7396fa909d0a2a6b68659ce656f4b1e2a7b7afe498172bb88cbb2b22518cdd0f`.

Shared system libraries are excluded from these executable sizes.

## Video export correction

The performance-update MP4 was incomplete: it lacked the container's `moov`
index. It was rebuilt from the intact video capture and PCM audio. The repaired
file has its index before the media data, and all 717 video frames plus its AAC
audio decode successfully. Its duration is 35.95 seconds. The decoded ending
was visually checked against the victory state.

The exporter now writes a temporary file, flushes it, checks duration/stream
metadata and frame count, and decodes both complete streams before publishing
the final filename. This verifier was checked against the actual broken file
and correctly rejected it. `evidence/victory/video-check.json` records the
repair validation. Game source and executables are unchanged by this fix.
