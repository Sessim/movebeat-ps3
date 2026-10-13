# MoveBeat PS3 target

The game itself: it initializes the RSX, the PlayStation Eye, SPURS and
`libgem` for two PS Move controllers, and runs the track library, the
three-pose body calibration, gameplay, pause, playlists and results. Player
controls are described in the top-level [README](../README.md#controls); this
file is about the code.

Imported Ogg/Vorbis audio is decoded with libvorbis, resampled to the PS3's
48 kHz stereo output and used as the gameplay clock. The importer checks the
data signature, not the file name (`audio.egg` is usually plain Ogg/Vorbis);
encrypted EGG and other codecs are not supported. After the last object of a
level, play continues until the decoder reaches the end of the song.

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

From a PS3DEV/PSL1GHT shell (Linux or WSL2), from the repository root:

```sh
bash tools/build_ps3.sh
```

It builds in a temporary directory and writes `out/ps3/movebeat.pkg` and the
PPU test package `movebeat-tests.pkg` (title `MOVEBTST1`, which only prints
the results of the core tests). The build needs the NVIDIA Cg Toolkit (`nvidia-cg-toolkit` on Ubuntu):
PSL1GHT's `cgcomp` uses its compiler for the RSX shaders in `shaders/`.

## Rendering

The RSX draws every frame. `Framebuffer` keeps its drawing calls, but on the
RSX path each call is recorded into a `DrawBatch` (`draw_batch.cpp`) as
clip-space triangles, and `RsxBatchRenderer` (`rsx_batch_renderer.cpp`) draws
the batch at `present()`: one clear, then one draw per run of equal state.
Runs are flat (painter's order), note cube faces (depth LESS with write),
note glyphs (depth LEQUAL, no write) and textured library cards. Note bodies
come from `forEachRoundedNoteTriangle`, the same generator the software
rasterizer uses, and depth is the software inverse depth mapped linearly to
[0, 1], so both paths occlude alike. A note body is a superellipsoid
(exponent 6) on a 7x7 grid per visible face: corners round off more than
edges, the RSX shades it per vertex (key light plus a soft highlight) and
the PPU fallback per triangle. Each face center stays on the cube's front
plane and the rest of the body bulges behind it. Glyphs take their depth
from that plane per vertex (RSX) or per pixel (PPU): the plane is tilted
toward the pitched camera, and a single center depth used to hide the
upper half of arrows and dots. `tests/draw_batch_tests.cpp` replays the
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

`tools/build_ps3.sh` uses `/opt/movebeat-sdk/ps3dev` unless `PS3DEV`/`PSL1GHT`
are set. In a checkout whose path contains no spaces, `make` also works:

```sh
cd ps3
make pkg
make core-tests
```

The package carries original XMB artwork, `ICON0.PNG` (320x176) and
`PIC1.PNG` (1920x1080); their SVG sources are in `assets/`.

`tools/rpcs3_ppu_tests.ps1` installs `movebeat-tests.pkg` into a fresh RPCS3
profile and waits for the test program's success or failure line. The GitHub
Actions workflow cross-compiles the game and the test package with the
current PS3DEV release and the Cg Toolkit and publishes them as the
`movebeat-ps3` artifact.

PS3DEV's newlib orders its file-table constructor before its malloc mutex
constructor; `newlib_malloc_lock.cpp` supplies a lazy recursive malloc lock so
stream-based level parsing cannot abort before `main`.

## Levels

The package includes First Steps, an original 18-second, 120 BPM tutorial
(four count-in beats, 32 alternating-hand notes). Its generated audio and
chart are in `ps3/package`; `tools/generate_tutorial.py` regenerates them
with FFmpeg.

At startup the game reads the `level.ini` of up to 256 level folders and 32
playlist folders in `/dev_hdd0/game/MOVEBEAT1/USRDIR/levels/`, then in
`MOVEBEAT/levels/` on USB drives `/dev_usb000`–`/dev_usb007` (a folder name
already on the hard disk wins). Charts are parsed when a level starts.
Invalid or incomplete folders are skipped; with none, the tutorial plays.

USB bundles are copied into the hard-disk library (`usb_import.hpp`) a piece
per frame while the menus are open: never during calibration or play, when
the PS Eye needs the USB bus. Files go through `<name>.part`, and the manifest
is copied last, so an interrupted folder is never listed and is copied again
next time. A drive plugged in while the track list is open is noticed within
two seconds and the library is reloaded after its copy. The music of a level
still on a USB drive is copied to the hard disk during calibration
(`audio_cache.hpp`) and plays from there.

`bash tools/build_ps3.sh --include-imported` builds a personal package with
every bundle in `out/levels` (or `MOVEBEAT_LEVELS_DIR`). This is opt-in, so an
ordinary build never redistributes downloaded music; it rejects symlinks,
missing manifests, unsafe names and collisions with bundled levels.

## Gameplay notes

- **Hand tracking.** Each Move's orientation comes from its IMU quaternion;
  the camera corrects position. A sphere lost for a moment is held from the
  IMU for up to 3 s, and partial camera fixes (sphere visible, no position)
  keep the last depth. Depth follows the camera radius with adaptive
  smoothing (as psmoveapi does) and a jump filter, and is limited to 30 cm
  around the forward pose (`depth_range_cm` in `renderer.ini`).
- **Body frame.** The three calibration poses give the play-area origin, the
  turn towards the camera, the arm-span scale, which hand is which and
  whether depth must be flipped. Hits are measured 45 cm along the blade
  from the forward pose; hands are kept within reach and blades in the
  forward hemisphere.
- **Cuts.** Collision checks the whole blade swept between its previous and
  current poses against the moving note centre (28 cm tolerance). The tip's
  motion gives the swing speed and direction. Untracked or non-finite poses
  cannot cut, and a gap over 100 ms between samples resets swing continuity.
- **Walls** are purple volumes; both blade tips must stay out of them while
  they cross the play plane. Touching one breaks the combo once and rumbles
  both controllers for as long as a blade stays inside. **Chains** are
  smaller ring-marked targets along the swing; **arcs** are guides the
  matching blade should follow for 60% of their length.
- **Energy.** Failing on energy is switched off for now
  (`GameConfig::energy_failure`): energy is still tracked but not shown, and
  every song plays to its end.
- **Tracking pause.** Losing a Move for a second during play pauses audio and
  chart time; play resumes 500 ms after the camera sees both spheres again.
  Swing history is discarded on pause and recovery so a pose jump cannot
  register as a cut.
- Opening the XMB pauses audio and gameplay time; exiting through the XMB
  shuts down in order.

## Files in USRDIR

- `settings.ini`: timing offset (−500…+500 ms), blade length (50–140 cm),
  hand inversion.
- `scores.log`: best score and max combo per difficulty, appended when
  improved; malformed rows are ignored.
- `renderer.ini` (optional, one `key=value` per line): `renderer=software`,
  `effects=off`, `background=off`, `hand_colors=on|off`, `camera_exposure`,
  `camera_quality`, `left_hue`, `right_hue`, `gem_filter=off`,
  `camera_depth=off`, `depth_range_cm`.
- `run_status.ini`: rewritten every 120 frames and at each state change, with
  the phase, tracking, calibration, audio, frame timing and library counters.
- `startup_error.txt`: the stage and code of a startup failure (also shown on
  a red screen; error `-1003` means the camera is not a PlayStation Eye).
- `gem_capture.csv`: 30 s of raw `gemState` and image data, recorded by
  holding both T triggers and pressing Start in the track list. Analyse it
  with `python tools/analyze_gem_capture.py gem_capture.csv`.

## Emulator tests

`demo_mode.ini` in USRDIR turns on scripted Move input for RPCS3
(`enabled=1`, plus `menu_test=1`, `playlist_test=1`, `pause_test=1`,
`tracking_loss_test=1`, `usb_import=1`, `level_id=`, `chart_index=`,
`capture_at_ms=`). The scripts in `tools/` set it up:
`rpcs3_pkg_install_smoke.ps1` installs a package into an isolated RPCS3
profile and plays levels (`-TrackingRecovery` injects a tracking loss), and
`rpcs3_menu_smoke.ps1` drives the library, a playlist, the pause menu or a
USB import. None of this replaces a real PlayStation Eye and Move, and
`demo_mode.ini` must never be placed on a real PS3. The demo writes
`first_frame.ppm`, `demo_frame.ppm` and `demo_scored_frame.ppm` beside it;
`tools/ppm_to_bmp.py` converts them for viewing.

## Library performance

The library scans only `level.ini` files at startup (charts are parsed when
a level starts, hidden behind the one-second calibration), keeps the cards
within three entries of the selection loaded (one disk read per frame at
most), converts cards to display format once and lets the RSX keep their
textures resident. To measure with a large library:

```sh
python tools/make_bench_library.py --count 30 --output out/levels-bench
cp -r out/levels/* out/levels-bench/   # optional: add real bundles
MOVEBEAT_LEVELS_DIR=out/levels-bench bash tools/build_ps3.sh --include-imported
```

Install the package (for example with `tools/rpcs3_pkg_install_smoke.ps1`)
and run `tools/rpcs3_menu_smoke.ps1 -AppPath <run>/app`. It sets
`menu_test=1` in `demo_mode.ini`: the game scrolls through every track one
press at a time, holds a button for auto-repeat, changes difficulties and
reports `library_scan_ms`, `menu_step_avg_us`, `menu_step_max_us` and
`menu_idle_max_us`.
