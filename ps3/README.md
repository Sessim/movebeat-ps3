# MoveBeat PS3 target

This target is the hardware proof and playable game shell. It initializes
the RSX framebuffer, PlayStation Eye, SPURS, and `libgem` for two PS Move
controllers. The screen shows a looping tutorial chart, perspective grid,
connection indicators, and both tracked virtual blades. The shared game core
already handles timing, cuts, misses, combo, score, and bombs; a full level
library scanner and Move-button selector are connected to the shell. Imported
Ogg/Vorbis audio is decoded with libvorbis, resampled to the PS3's 48 kHz stereo
output, and used as the gameplay clock. The importer checks the data signature,
not the filename suffix: `54851` is named `audio.egg` but is plain Ogg/Vorbis.
Encrypted EGG and other codecs are not supported.
After the last gameplay object is settled, imported levels remain in the play
state until the Ogg decoder reaches EOF, so a mapped song's outro is not cut
off. A failed level still stops immediately.

## Source layout

- `main.cpp` only creates `App`.
- `app.cpp` owns startup, shutdown and the per-frame state machine. A frame
  runs, in order: pause/audio checks, input (`readInput`), library actions,
  tracking-loss recovery, calibration, Start handling, gameplay update,
  rendering and the `run_status.ini` heartbeat.
- `level_library.cpp` scans `USRDIR/levels`, manages library cards and holds
  the built-in fallback chart; `demo_options.cpp` reads `demo_mode.ini`.
- `playfield_draw.cpp` draws the 3D play scene (grid, walls, arcs, notes,
  cut bursts, sabers, HUD); `screens_draw.cpp` draws menus and overlays;
  `text_draw.cpp` holds the bitmap text and digit helpers.
- `framebuffer.cpp` receives every draw call and forwards it to the RSX batch
  (`draw_batch.cpp`, `rsx_batch_renderer.cpp`) or to the software fallback
  (`software_note_renderer.hpp`); `app_paths.hpp` lists the USRDIR files.

## Build

From a PS3DEV/PSL1GHT shell:

The build needs the NVIDIA Cg Toolkit (`nvidia-cg-toolkit` on Ubuntu):
PSL1GHT's `cgcomp` uses its compiler for the RSX shaders in `shaders/`.

## Rendering

The RSX draws every frame. `Framebuffer` keeps its drawing calls, but on the
RSX path each call is recorded into a `DrawBatch` (`draw_batch.cpp`) as
clip-space triangles, and `RsxBatchRenderer` (`rsx_batch_renderer.cpp`) draws
the batch at `present()`: one clear, then one draw per run of equal state.
Runs are flat (painter's order), note cube faces (depth LESS with write),
note glyphs (depth LEQUAL, no write) and textured library cards. Cube faces
come from `forEachNoteCubeTriangle`, the same generator the software
rasterizer uses, and depth is the software inverse depth mapped linearly to
[0, 1], so both paths occlude alike. `tests/draw_batch_tests.cpp` replays the
batch pixel by pixel and compares it with the software primitives.

Effects are geometry, not post-processing, because the RSX is fill-rate
bound at 720p: an additive floor glow and rails, translucent wall faces with
glowing near edges, saber halos with a white core, swing trails and soft cut
flashes. Runs carry a blend mode (opaque, alpha, additive) and vertices carry
their own colors. A heavy effects frame adds under 1200 vertices and at most
two translucent layers per wall. Saber trails (`saber_trail.hpp`) last 120 ms
regardless of frame rate, hold at most 12 samples and restart on tracking
loss or on tip jumps faster than 18 m/s. Effects exist only on the RSX path;
`effects=off` in `USRDIR/renderer.ini` disables them for A/B checks.

The stage behind the track (`stage_layout.hpp`, effects only) is a sky
gradient, seven neon frames drifting toward the player at 1.2 m/s and a
portal in the far wall; the frames and the portal pulse on the level's beat.
Notes emerge small from the portal's center and settle into their cells
during the first 35% of their flight (`note_spawn.hpp`): the upper row
rises, the lower rows drop. This is drawing only; a note closer than 1.5 m
is always drawn at its gameplay position, even for maps with a very short
jump distance. Sabers (`saber_model.hpp`) have a black cylindrical hilt for
the Move in the hand and a sphere in the saber's color where the Move's
light is; cuts still use the game's handle-to-tip segment.

If the RSX resources cannot be created the app falls back to the PPU
software rasterizer. The fallback can also be forced for A/B checks by
writing `renderer=software` to `USRDIR/renderer.ini`. `run_status.ini`
reports `renderer=rsx|software`, `batch_max_vertices` and
`batch_dropped_primitives`.

`savePpm` on the RSX path renders the pending frame, waits for the RSX and
reads video memory back. RPCS3 only mirrors GPU output into emulated memory
with "Write Color Buffers" enabled; `tools/rpcs3_pkg_install_smoke.ps1`
turns it on in its isolated profile.

On the configured Ubuntu WSL instance, run `bash tools/build_ps3.sh` from the
project root. It uses `/opt/movebeat-sdk/ps3dev`, builds in a fresh Linux path
without spaces, and copies artifacts to `out/ps3`. `PS3DEV`/`PSL1GHT` may be
overridden for another installation. This build was completed on 2026-09-20
with the official `nightly-2026-07-26` SDK and PPU GCC 7.2.0. The packaged PPU
tests pass under RPCS3 with PS3 firmware 4.93. The application and tests have
not yet been run on a physical PS3.

Alternatively, in a checkout whose path contains no spaces:

```sh
cd ps3
make
make pkg
make core-tests
```

`movebeat.self` can be sent with `ps3load`; `movebeat.pkg` is the installable
diagnostic package. Neither artifact is considered hardware-verified until it
has run on a real CFW/HEN console.
For an end-to-end emulator install check, run
`powershell -NoProfile -ExecutionPolicy Bypass -File
tools/rpcs3_pkg_install_smoke.ps1 -LevelIds 132ce -DurationSeconds 12`.
It copies RPCS3 and firmware into a new isolated profile, installs the actual
PKG, checks EBOOT/XMB assets/level directories, then boots that installed
EBOOT and applies the usual audio/scoring/frame smoke checks. In this local
RPCS3 build the command-line installer can return `-1073740791` after writing
the package; the script reports that anomaly and still requires a successful
installed-file check and game boot. It does not touch the manual preview.

The installable package also contains original MoveBeat XMB artwork:
`ICON0.PNG` (320x176) and `PIC1.PNG` (1920x1080). Their editable SVG sources
are kept in `assets/`.

To execute the separately packaged portable tests on RPCS3's PPU, run
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/rpcs3_ppu_tests.ps1`.
The runner installs `movebeat-tests.pkg` into a fresh emulator profile, waits
for the test program's explicit success/failure line in the TTY log, and then
closes only the emulator instance it started. Its profile and logs remain under
`out/rpcs3-ppu-test-runs/`.

The repository's GitHub Actions workflow also cross-compiles the SELF with the
current official PS3DEV prerelease and the Cg Toolkit, in addition to
running desktop tests. A
successful run publishes both `movebeat.self` and installable `movebeat.pkg` in
the `movebeat-ps3` workflow artifact. The workflow also retains the upstream
finalized `movebeat.gnpdrm.pkg` variant and the ELF, SELF, and installable PKG
forms of the portable PPU tests. Building the test package checks PPU
compilation, linking, SPRX processing, SELF generation, and NPDRM packaging.
`make -j2 pkg` orders compilation before packaging.

The current PS3DEV newlib orders its global file-table constructor before its
malloc mutex constructor. `newlib_malloc_lock.cpp` supplies a lazy recursive
malloc lock so stream-based level parsing cannot abort before `main`; the same
fix is exercised by the RPCS3 PPU test package. With RPCS3 0.0.42-19675 and
Vulkan, the game reaches RSX rendering. A first-frame RSX flip deadlock was
fixed by waiting only after a flip has actually been submitted. On this PC,
ordinary startup then reports stage 13, error `0x80140807` from
`cameraGetType`: no PlayStation Eye is available to the emulator. Eye, GEM,
Move input, and audio still require real-console validation. The newer local
RPCS3 0.0.42-20028 build also hits an emulator-side `vm::writer_lock`
assertion in `_cellGcmInitBody`; it is not suitable for this graphics check.
With RPCS3's fake PS Eye and Mouse Move handler, normal initialization instead
reaches the selection screen and `gemGetInfo` reports one connected virtual
Move. The fake camera returns zero bytes on each `cameraReadEx` in this local
build, so no tracked pose is produced. `run_status.ini` distinguishes this
with `gem_connected_count=1`, `gem_update_stage=2`, and
`camera_read_count=0`. This emulator result does not validate physical Eye
tracking or two-controller calibration.

For a graphics-only RPCS3 preview, put `demo_mode.ini` containing `enabled=1`
in `/dev_hdd0/game/MOVEBEAT1/USRDIR/` in the emulator's virtual disk. To
refresh the isolated `out/rpcs3-19675/app` profile with the latest built SELF,
use `powershell -NoProfile -ExecutionPolicy Bypass -File
tools/prepare_rpcs3_preview.ps1 -LevelId 132ce` while RPCS3 is closed. It
backs up the prior EBOOT and demo marker, verifies the copied EBOOT hash, and
enables the synthetic demo. The automated smoke test restores its temporary
EBOOT afterward, so it does not permanently refresh a profile for manual
visual inspection. To
exercise a specific imported map, add a second line such as `level_id=132ce`;
an unknown ID is a reported startup error, not a silent fallback. Restart the
game. This explicitly skips Eye/GEM initialization, displays two scripted
synthetic sabers, and runs the selected chart in a loop,
and writes `first_frame.ppm`, `demo_frame.ppm`, and a scored frame after three
hits (`demo_scored_frame.ppm`) beside the marker. It exercises
the normal cut/scoring path, starts the bundled audio when available, and
records current and last completed cycle scores, hits, misses, `demo_cycles`,
`audio_active`, `audio_time_ms`, `level_id`, and `chart_difficulty` in
`run_status.ini`. It does not test
optical tracking, calibration, physical audio output, or score persistence and
is not enabled or packaged by default. Remove `demo_mode.ini` to return to the
hardware input path. Do not place this marker on a real PS3.

The Windows RPCS3 smoke runner copies both PPM captures into its report folder
and, when Python is available, converts them to `demo_frame_preview.bmp` and
`demo_scored_frame.bmp` for direct visual inspection. The standard-library
converter is `tools/ppm_to_bmp.py`; its padding, channel order and malformed
input handling are covered by CTest.

## Imported levels

The package includes First Steps, an original 18-second, 120 BPM tutorial with
music and 32 alternating-hand notes. Four count-in beats precede the first note.
Its assets are staged from `ps3/package` through PSL1GHT's `PKGFILES` mechanism.
The generated audio and chart are included in the repository; ordinary package
builds do not require FFmpeg. To regenerate them, run
`python3 tools/generate_tutorial.py` with FFmpeg available.

At startup the shell scans up to 64 direct child directories under:

```text
/dev_hdd0/game/MOVEBEAT1/USRDIR/levels/
```

Copy the directories produced in `out/levels` there using FTP or a file
manager, preserving one complete directory per map (for example
`levels/547fb/level.ini`). Invalid or incomplete directories are skipped. If
none are valid, the built-in tutorial is used. A legacy `levels/current`
directory remains valid and is selected first when present.

To make a personal PKG containing every imported bundle in `out/levels`, run
`bash tools/build_ps3.sh --include-imported`. This is opt-in: the ordinary build
never redistributes downloaded music. The build rejects symlinks, missing
manifests, unsafe directory names, and collisions with bundled levels.
For a reviewed staging directory, set `MOVEBEAT_LEVELS_DIR` to its path before
the command; the default remains `out/levels`, and builds never rewrite bundles.

## On-console flow

1. Connect the PlayStation Eye and pair both Move controllers before launch.
2. Stand where both illuminated spheres are visible.
3. Keep each sphere steady and visible while the application automatically
   requests `gemCalibrate` for uncalibrated controllers. The Move button
   manually restarts calibration after a controller is producing readable
   samples.
4. If notes appear consistently early or late, hold either T trigger and press
   Circle/Square to change the timing offset by +10/-10 ms (range -500 to
   +500 ms). A positive offset advances the chart relative to the audio. The
   bottom number is green for positive, red for negative, and white for zero.
5. Hold both T triggers and press Circle/Square to adjust virtual blade reach
   by +5/-5 cm (range 50-140 cm). While both triggers are held, the cyan bottom
   number shows the selected reach instead of the timing offset.
6. For hardware diagnostics, hold both T triggers and press Start on the
   selection screen. A magenta square means raw GEM capture is active. Press
   the chord again to stop it, or let it stop automatically after 30 seconds.
7. Before calibration, use Cross/Triangle to move forward/backward through
   imported maps. Holding one for 450 ms begins controlled scrolling every
   150 ms; a button already held when a Move connects does not start it.
   With the PlayStation Eye tracking a sphere, its small colored reticle can
   point to the previous, current or next card: press T over a neighboring
   card to change tracks, or over the current card to begin pose calibration.
   Triangle/Cross remain available. Camera coordinates have not yet been
   checked on physical hardware, so verify horizontal mirroring there.
   The center card is selected; its cover, title and artist
   and mapper are pre-rendered by the PC importer into `library.mblc`. Only
   the selected and adjacent cards are kept in PS3 memory; the current card
   count and byte total appear in `run_status.ini` for diagnostics.
8. For an imported bundle with multiple charts, use Circle/Square to move
   forward/backward through the difficulty indicators below the center card;
   the selected characteristic and difficulty appear to its right.
   Each Move has independent button edges, so the second controller can use
   the same button even while it is held on the first. A missing optical frame
   does not create a fresh press when the button sample returns. In the
   diagnostic status, `button_ready_mask` shows which slots supplied an actual
   GEM or inertial button sample in the latest frame (bit 0 left, bit 1 right).
9. Face the camera and hold the Move controllers comfortably in front of you,
   separated left/right by at least 15 cm, with both blades pointing forward. Press Start and
   remain still while the green calibration bar fills for one second. If a
   sphere is lost, the screen says `TRACK BOTH SPHERES` and resets progress;
   if the handles overlap horizontally, it says `SEPARATE MOVE HANDS` and
   waits. The left/right hand assignment is determined from the camera-space
   handle positions, not the order in which the controllers connected; it
   stays fixed for the session and also determines which Move receives rumble.
   `hand_slots_swapped=1` in `run_status.ini` means GEM slot 1 was assigned to
   the left hand. Camera X-axis orientation still needs physical verification.
   If blue and red hands are reversed on your setup, press Select on the track
   screen to toggle `HANDS AUTO` / `HANDS FLIPPED`; this correction is saved in
   `settings.ini` and applied to the next calibration. The diagnostic
   `invert_hands_setting` shows the saved correction separately from the
   effective `hand_slots_swapped` value. After calibration, the physical Move
   spheres are assigned the hand colors (left blue, right red); changing the
   hand inversion updates the sphere LEDs as well as gameplay mapping.
   Press Triangle to cancel without changing the selected track. Other library
   and setting buttons are ignored during calibration. The
   average midpoint of the tips defines the center of the middle target row;
   the chart and audio start only after calibration completes. Moving either
   controller more than 4 cm, rotating more than 5 degrees, losing tracking,
   or a sample gap over 100 ms restarts the measurement. Press Start again to
   restart deliberately. These thresholds await real-camera validation.
10. The top-left squares are red when absent, amber when connected but not
   tracked, and blue/red when fully tracked.
    During play, losing either tracked Move for 250 ms pauses both audio and
    chart time. The game shows `TRACKING LOST`; once both spheres are visible,
    press Start and hold the hands still for one second to recalibrate and
    resume. `run_status.ini` reports `tracking_paused` and
    `tracking_recalibrating`. The old swing history is discarded on pause and
    recovery so a pose jump cannot register as a cut. This recovery path still
    needs physical Eye/Move validation; RPCS3's scripted demo does not simulate
    optical loss.
11. At the result screen the large center number is the current score, the
   purple upper number is the saved best score, and the cyan/red lower numbers
   are hits/misses. Press Start to replay with the same play-space origin, or
   Triangle to return to level selection.
12. Opening the XMB pauses both audio and gameplay time. Exit through the XMB;
   the registered callback performs orderly cleanup.

Best scores and maximum combos are appended only when improved to
`/dev_hdd0/game/MOVEBEAT1/USRDIR/scores.log`. Each difficulty has an independent
stable key; malformed log rows are ignored.

A regular cut is worth up to 115 base points: 70 from up to 100 degrees of
pre-swing, 30 from up to 60 degrees of follow-through, and 15 from crossing
near the note center. Direction and timing decide whether the cut is accepted
but do not add score. Blade-orientation changes are accumulated over 200 ms.
Tracking loss clears the angle history so reacquisition cannot create a false
bonus.
Collision checks the complete virtual blade swept between its previous and
current tracked poses against the moving note center, rather than checking only
the tip's path. Both ends of the blade are evaluated relative to the note's
travel during that input interval. The tip's motion still determines slash
speed and direction; the 28 cm center tolerance limits spatial forgiveness.
Non-finite GEM coordinates or an undefined handle-to-sphere direction are
treated as untracked. A gap over 100 ms between gameplay samples resets blade
motion continuity, so a delayed sample cannot cut a note by teleporting across
it. An already earned cut retains its follow-through window.

Regular notes and chain links are axis-aligned solids in track space. Their
front faces are perpendicular to the travel direction, and width, height and
depth are equal. The track camera is centered on the middle of the three
vertical rows, with finite perspective depth. Upper notes project above
the sightline and move upward toward the player; lower notes project below it
and move downward. All three rows follow parallel paths that compensate for
camera pitch, preserving their vertical spacing while the center row stays
level and upper/lower rows fan away from it in perspective. Regression tests
check every 50 ms step for row direction and separation. Lanes converge
toward the centerline and widen toward the player. Notes use this
same camera rather than a separate three-quarter camera: their front faces
therefore face the player instead of turning sideways, and visible top/side
planes come only from the real viewing angle. The differently shaded faces
have no inset front border. The RSX draws the 3D mesh triangles with the
grid and HUD in one pass per frame (see Rendering); the software fallback
rasterizes them into the same PPU color buffer with a per-frame
inverse-depth buffer for occlusion. The cut arrow is
depth-tested against nearby cubes on the front face, and bombs use the same
depth test. A dense BeatSaver chart had shown far arrows through near cubes
before this fix. Bombs remain round. An RPCS3 capture confirms
distinct front, top and side faces, intact HUD/grid, and no old-frame trail. The
real-PS3 frame time and appearance still need physical validation.
The software rasterizer steps triangle edge and inverse-depth values across
each scanline; on the same 12-second RPCS3 demo the reported note pass fell
from 2766 to 1783 microseconds on average. This emulator timing is not a
substitute for profiling on a real Cell PPU.
`run_status.ini` reports `software_note_draw_calls`,
`software_note_max_cubes`, and `gpu_note_fallback_frames` for the default
path. If depth-buffer allocation fails, a visible `3D RENDER UNAVAILABLE`
warning accompanies the flat fallback.
The isolated smoke test also requires a fresh scored-frame capture for every
level, in addition to audio, scoring, note draws, and clean shutdown.
The installed-package RPCS3 smoke test can inject one optical-tracking loss with
`tools/rpcs3_pkg_install_smoke.ps1 -LevelIds 132ce -TrackingRecovery`.
It verifies one pause, one stable recalibration, continued scoring, and no
advance of the audio clock during the pause. It also compares score, hits, and
misses at pause and resume to catch false gameplay events while tracking is
unavailable. This synthetic test does not validate a real PlayStation Eye or
Move.
Successful cuts produce a short hand-colored burst at each hit position; two
notes cut in one swing produce two bursts. The effect expires after 180 ms and
does not affect hit detection or scoring.

Notes become visible at a finite depth of five track units (about 1.25 seconds
before the strike plane at the current note speed). The lane floor and spawn
frame use the same pinhole camera as the notes. Its downward and oblique view
now exposes real top and side surfaces without rotating the cubes away from the
movement axis. The latest RPCS3 frame has been captured; screen composition and
lane perspective still need feedback on a television.

The timing offset and blade reach are persisted in
`/dev_hdd0/game/MOVEBEAT1/USRDIR/settings.ini` and restored on the next launch.
An imported level whose Ogg/Vorbis stream cannot start shows a visible
`AUDIO ERROR` code instead of silently playing against the wall clock;
Triangle returns to track selection. The same code is written as
`audio_error_code` in `run_status.ini`.

BeatSaver walls are rendered as purple volumes. Because this is a television
adaptation without head tracking, both saber tips must remain outside the wall
while it crosses the play plane; contact breaks the combo and drains energy.
BeatSaver v3/v4 chains are rendered as smaller ring-marked targets interpolated
between their head and tail. Each link must continue the swing with the matching
hand and has reduced score and miss penalty.

BeatSaver v2/v3/v4 arcs are rendered as red or blue curved guides between their
head and tail grid cells. The importer preserves both endpoint cut directions,
control-point multipliers and the mid-anchor mode. Modes 1/2 now offset the
curve clockwise/counter-clockwise around its midpoint when both ends use the
same X lane and parallel or opposite cut directions; other combinations keep
the ordinary endpoint-driven cubic path. Endpoints, tangent continuity and
time-based travel depth are preserved. This follows the mode's documented
constraints in the [community map-format reference](https://github.com/qqrz997/bsmg-wiki/blob/master/wiki/mapping/map-format/beatmap.md#arcs-mid-anchor-mode);
the exact visual scale still needs comparison on the target hardware. During play, the assigned
saber must follow the guide for at least 60% of its duration; covered time is
measured independently of frame rate. A successful arc adds up to 50 base
points and a combo hit; a missed arc applies a reduced energy penalty. The
current follow check allows 36 cm around the moving guide and still needs
physical PS Move validation.

Raw diagnostic samples are written to
`/dev_hdd0/game/MOVEBEAT1/USRDIR/gem_capture.csv`. The file contains camera and
GEM timestamps, return/tracking flags, buttons, trigger, sphere/handle position,
velocity, acceleration, quaternion, and `gemGetImageState` pixel position,
radius, distance, and visibility for both controllers. The image fields are
diagnostic only; no unmeasured pointer mapping is enabled in the menu.
Starting another
capture replaces the previous file; copy it off the console before repeating.
Analyze it on the PC with:

```sh
python tools/analyze_gem_capture.py path/to/gem_capture.csv
```

The report shows measured camera rate, frame spacing, tracking success and
dropouts, return-code counts, and raw sphere/handle coordinate ranges.
For captures from this build it also reports observed `image_u`/`image_v`
ranges when the sphere is visible, so the future Move pointer can be mapped
from measured camera coordinates.
For an axis check on real hardware, make three separate short captures without
moving the camera: hold both spheres still at center, then about 20 cm to the
right, then about 20 cm higher. Stop and copy each CSV before starting the
next capture (a new capture overwrites the old one). Run:

```sh
python tools/analyze_gem_capture.py center.csv --right right.csv --up up.csv
```

The additional report uses medians of fully tracked samples and shows the raw
X/Y/Z shifts for each controller, warning if X-right or Y-up has the opposite
sign. It does not prove the exact metric scale without a measured displacement.
RPCS3's [cellGem implementation](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellGem.cpp)
models x/y/z in millimetres and Y upward; the real console must still be
measured independently.

Initialization failures are printed to the debug console and shown as a red
screen. Error `-1003` means the attached camera is not a PlayStation Eye.
