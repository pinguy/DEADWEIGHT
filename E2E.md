# DEADWEIGHT — end-to-end handover

Tested 7 September 2026, in the hosted Ubuntu 24.04 environment.

**Both end-to-end suites passed on the delivered packed executable.**
The recorded controller completed the contract in 30.38 seconds of
mission time, banked all three cores, destroyed all three drones with thrown
plates, and finished with 100 hull. The recording is an automated
playthrough; this is not a claim about difficulty for a first-time player.

| Check | Result |
| --- | --- |
| Title → Enter → playable room | Passed |
| Pause freezes movement and mission time; resume | Passed |
| Pull, hold, drop and re-grab scrap | Passed |
| Thrown objects hit and destroy drones | Passed for all three drones |
| Wall collision | Player stopped at the bulkhead; 48 blocked movement steps |
| Collect and deliver all three cores | Passed |
| Victory screen and frozen terminal timer | Passed; screenshot inspected |
| Restart after victory | Fresh health, cargo, drones and score |
| Enemy contact damage → death | Passed |
| Restart after death | Passed |
| Station timeout | Death at 180 seconds, with 82 hull still remaining |
| Escape from results | Clean exit |
| Mute / unmute | Actual recorded PCM became exactly silent, then audible again |
| Sound recording | 48 kHz stereo; peak 0.705; zero clipped samples |
| Rendering performance | 116 FPS over 301 uncapped frames on this hosted CPU |

## Evidence

- `evidence/victory/playthrough.mp4`: 35.95-second capture with stereo sound.
- `evidence/victory/`: title, magnetic hold, cargo, combat and victory captures;
  the complete input command stream, read-only telemetry and assertion results.
- `evidence/failure/`: damage death, timeout captures, inputs, trace and results.
- `evidence/audio-check.json`, `mute-check.json`, `performance.json`:
  measured audio and renderer checks.
- `tests/e2e.py`: the controller and assertions used for these runs.

The source was also rebuilt after formatting cleanup. Both executable hashes
were byte-identical to the versions already exercised. Both builds compile
without warnings under their supplied build commands.

| Delivered binary | Bytes | SHA-256 |
| --- | ---: | --- |
| `deadweight` — C Optimizer packed runner | 13762 | `e990b248713753257a203318b7c7017fc8e3d7d05ec96a0a39e892775e231c64` |
| `deadweight-native` — regular executable | 53320 | `5cafe8acf4eb8f82ca3989080aece576276a63c0dc73623b662b1bde8a77b1ff` |

The byte counts exclude shared system libraries. Both binaries use SDL2, libc
and libm at runtime. The C Optimizer runner also uses the system shell and xz.

## What this verifies

The controller operates the actual executable through SDL keyboard and mouse
events. Physics, enemy damage, delivery rules and rendering are the game's
normal implementations. Fixed simulation timing makes the checks reproducible;
telemetry lets the controller choose its next input. There are no forced wins,
teleports, invulnerability switches or alternate collision rules.

Screenshots and video were read from the SDL software renderer. Title,
gameplay, carried cargo, victory, timeout, and a decoded video frame were
visually inspected. Audio measurements verify signal, stereo content, mute
and clipping; listening quality remains a subjective judgment.

The display and sound drivers in the tests were SDL's headless/dummy drivers.
These checks do not establish physical keyboard/mouse capture, speakers,
fullscreen behavior or desktop compatibility on the user's CachyOS box. The
regular desktop launch is provided for that final local check.
