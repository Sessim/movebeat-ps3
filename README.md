# MoveBeat

**English** · [Русский](README_RU.md)

An experimental free rhythm game for the PlayStation 3, played with two
PlayStation Move controllers and the PlayStation Eye camera. You slash flying
blocks to the music with two blades; maps come from BeatSaver. The project
follows the general genre but **contains no code, music, maps, models, name or
other material from Beat Saber**.

[![MoveBeat on YouTube: gameplay on a real PS3](https://img.youtube.com/vi/wMVJnFPkHqQ/maxresdefault.jpg)](https://youtu.be/wMVJnFPkHqQ)

▶ [Watch the 30-second trailer](https://youtu.be/wMVJnFPkHqQ)

This is an early version: it has been played on one console (HEN, PS Eye
under the TV, about 2 m from the player) and tested in the RPCS3 emulator.
Tracking depends a lot on lighting and camera placement. Bug reports with the
`run_status.ini` from `/dev_hdd0/game/MOVEBEAT1/USRDIR/` are welcome in Issues.

## Quick start

**You need:** a PS3 with HEN or CFW (able to install unsigned PKGs), two PS
Move controllers, a PS Eye camera, a FAT32 USB stick and a Windows PC to
download songs.

1. Download `movebeat.pkg` and `MoveBeatTrackAssistant.zip` from
   [Releases](../../releases).
2. Install `movebeat.pkg` on the PS3 (Package Manager → Install Package
   Files, from a USB stick or over FTP). The package only contains the
   built-in tutorial song.
3. Run the assistant on the PC and press **Starter set**: it downloads six
   maps and the "MoveBeat Starter" playlist from BeatSaver.
   Any other map can be found by name or by its `!bsr` code.
4. Plug in the USB stick and press **All levels to USB**; the assistant
   checks that the stick is FAT32.
5. Plug the stick into the PS3 and start MoveBeat: the game copies the levels
   into its library on the hard disk ("USB COPY…" in the top right), after
   which the stick can be removed.
6. Stand about two metres from the camera. Pick a track, press Move, go
   through the calibration and play.

The music of the maps belongs to its rightsholders: download and use maps
under BeatSaver's rules and with the authors' permission, and do not publish
ready-made sets with music.

## Controls

| Screen | Button | Action |
|---|---|---|
| Track list | Triangle / Circle | previous / next track (hold to scroll) |
| | Cross | next difficulty |
| | Square | shelves: all tracks, then playlists |
| | Move (single press) or Start | start through calibration |
| | Move on both, hold | randomizer |
| | T + motion | up-down scrolls with inertia, sideways changes difficulty |
| | T + Circle / Square | timing offset +10 / −10 ms |
| | both T + Circle / Square | blade length +5 / −5 cm |
| | both T + Select | swap hands |
| Calibration | Move on each controller | "I'm in the pose" |
| | Triangle | start over from the first pose |
| | Cross | leave (to the track list or the pause menu) |
| Song | Start | pause |
| Pause | Cross / Circle / Square / Triangle | continue / restart / calibrate / track list |
| Tracking lost | Move on the lost controller (aimed at the camera) / Triangle | recalibrate its sphere / track list |
| Results | Cross, Circle or Move / Triangle | continue / replay |
| Between playlist songs | Cross, Circle or Move / Triangle / Square | next song now / replay the last one / stop |
| Playlist summary | Cross, Circle or Move | track list |

**Calibration** has three poses: both Moves on outstretched arms forward
(blades towards the camera), out to the sides, then down. Confirm each pose
with the Move button on both controllers. From the poses the game works out
which hand is which (the red sphere is the right hand, the blue one the left),
your arm span and the depth of the play area. You can calibrate again from
the pause menu.

**During a song** the score is in the top left and the combo in the top
right; the screen flashes red when the combo breaks. At the bottom are the
time left and the song progress. A cut is felt as a rumble in that hand;
touching a wall with a blade rumbles both controllers for as long as the blade
stays in it, and breaks the combo. While failing on energy is switched off,
every song plays to its end and no life bar is shown. The results show the
score, your best score on that difficulty, hits, max combo and misses.

**If the camera loses a Move**, the game pauses and resumes by itself as soon
as it sees both controllers again. If a sphere stays lost, aim that Move at
the camera and press its Move button.

The timing offset (−500…+500 ms; positive shows notes earlier) and the blade
length (50–140 cm) are saved in `USRDIR/settings.ini`.

## Importing songs

### Windows assistant

`MoveBeatTrackAssistant.exe` from Releases. It speaks English or Russian:
it follows the Windows display language, and the menu in the top right
switches it.

- **Starter set**: six maps and the "MoveBeat Starter" playlist.
- **Search** by name, author, ID (`54851`) or BSR command (`!bsr 54851`),
  with the cover, difficulties and download size; **Prepare level**
  downloads the map and checks its SHA-1. An already downloaded ZIP can be
  opened too.
- **USB stick:** **Level to USB** and **All levels to USB**. The assistant
  refuses drives that are not
  FAT32 (the PS3 only reads FAT32 from USB), copies levels to
  `MOVEBEAT/levels/` and checks every file by SHA-256.
- **FTP:** sends a level to a PS3 running webMAN MOD or another FTP server.
  Plain FTP is unencrypted, use it on a trusted network only; the password is
  not saved. Large files may fail over Wi-Fi; a USB stick is more reliable.

### USB stick → PS3

Plug the stick into the PS3 before starting the game or while the track list
is open. The game looks at `/dev_usb000`–`/dev_usb007` and copies new folders
from `MOVEBEAT/levels/` into its library on the hard disk ("USB COPY 3 OF
12", then "USB ADDED 12"). Copying only happens in the menus: during
calibration and play the camera needs the USB bus. A folder whose name is
already on the hard disk is skipped.

### Command line

```sh
python tools/starter_set.py --output out/levels
python tools/beatsaver_import.py --search "title or artist" --limit 10
python tools/beatsaver_import.py --key 3b31 --output out/levels
python tools/beatsaver_import.py --zip path/to/map.zip --output out/levels
python tools/playlist_import.py https://beatsaver.com/playlists/1176968
python tools/usb_export.py E: --all out/levels
```

The importer takes the newest version of a map, checks the archive against
the SHA-1 from the BeatSaver API and writes a level folder: `level.ini`, an
`.mbchart` per difficulty, the original audio and cover, and a `library.mblc`
card for the PS3 menu. Beatmap v2, v3 and v4, `Standard` and `NoArrows`,
bombs, walls, chains and arcs, all difficulties and BPM changes are
supported. The audio must be Ogg Vorbis (checked by content, so `.egg` works).

`playlist_import.py` takes a `.bplist` file, a BeatSaver playlist link or its
number, downloads the missing maps and writes a `playlist-<id>` folder. On the
PS3, Square switches shelves: all tracks, then the playlists. Starting from a
playlist shelf plays it from the selected song to the end, with results and
an 8-second countdown between songs and a summary at the end.

Levels can also be built into your own PKG (for personal use only):
`bash tools/build_ps3.sh --include-imported` takes everything in
`out/levels`. Without that flag the package only contains the First Steps
tutorial.

## Building

You need CMake 3.16+ and a C++11 compiler. The game core and its tests on a
PC:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, `-G "Visual Studio 17 2022" -A x64` with `--config Debug` works
best. `movebeat_demo` is a console simulator that plays a map with perfect
swings, to check that the core behaves the same on the PC and the PS3:

```sh
./build/movebeat_demo out/levels/<BeatSaver-ID>
```

The PS3 package is built with PS3DEV/PSL1GHT (on Linux or WSL2):

```sh
bash tools/build_ps3.sh
```

It produces `out/ps3/movebeat.pkg` and a separate package with the PPU tests.
CI (`.github/workflows/build.yml`) builds the core, the tests and the PKG on
every push. Automated RPCS3 runs: `tools/rpcs3_pkg_install_smoke.ps1`,
`tools/rpcs3_menu_smoke.ps1` (menu, playlist, pause, USB import),
`tools/rpcs3_ppu_tests.ps1`.

Windows assistant: `tools/build_assistant.ps1` (PyInstaller in its own
`.venv-assistant`, SHA-256 next to the EXE; bundled components are listed in
`assistant/THIRD_PARTY_NOTICES.md`). The Android client (Kotlin, an early
prototype: map search and conversion, FTP) is in [`android`](android/README_RU.md).

## Map format

`.mbchart` is UTF-8 text:

```text
# time_ms,lane,row,hand,direction
1000,1,1,L,D
1500,2,1,R,U
# W,time_ms,duration_ms,lane,width,row,height
W,2000,1000,0,1,0,3
# C,time_ms,lane,row,hand
C,2200,2,1,R
# A,head_ms,tail_ms,head_lane,head_row,tail_lane,tail_row,hand
A,2500,3200,0,0,3,2,L
```

- `lane`: `0..3` left to right; `row`: `0..2` bottom to top;
- `hand`: `L`, `R` or `B` (bomb);
- `direction`: `U`, `D`, `L`, `R`, `UL`, `UR`, `DL`, `DR` or `A` (any);
- `W` is a wall: start, duration and a rectangle on the 4×3 grid;
- `C` is a chain link; `A` is a visual arc.

The level folder format is described in
[`docs/MOVEBEAT_BUNDLE_FORMAT.md`](docs/MOVEBEAT_BUNDLE_FORMAT.md). The First
Steps tutorial (18 s, 120 BPM) is synthesized by `tools/generate_tutorial.py`
without third-party recordings.

## Documentation

- [`docs/TROUBLESHOOTING_RU.md`](docs/TROUBLESHOOTING_RU.md) (Russian):
  camera setup, on-screen messages, startup error codes and what to send in
  a bug report;
- [`ps3/README.md`](ps3/README.md): how the PS3 game is built and works, and
  the files it keeps in USRDIR;
- [`docs/MOVEBEAT_BUNDLE_FORMAT.md`](docs/MOVEBEAT_BUNDLE_FORMAT.md): the level
  folder format;
- [`CHANGELOG.md`](CHANGELOG.md): changes by version.

To record raw `gemState` data, hold both T triggers and press Start in the
track list: 30 seconds are written to `USRDIR/gem_capture.csv`.

## Legal

The code is released under the MIT license ([`LICENSE`](LICENSE)). MoveBeat
is not affiliated with Beat Games, Meta, Sony or any music rightsholders.
BeatSaver requires uploaders to hold the rights to distribute their maps and
forbids republishing them without the author's permission: importing is meant
for personal use, and the user is responsible for the rights to each song and
map.
