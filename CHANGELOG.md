# Changelog

## Unreleased

- Experimental 1080p (`resolution=1080` in `renderer.ini`): when the system
  already outputs 1080p, the RSX draws the same layout at full resolution.
  Off by default: switching the output up from 720p rebooted a console.
- The track list shows each difficulty's best score beside the track, in
  the difficulty's color.
- Fixed: depth noise no longer freezes a hand for 83 ms at a time as a
  false tracking jump.
- Tracking diagnostics: every camera loss of a Move during a song goes to
  `tracking_log.csv` (where the sphere was last seen, how long, what libgem
  said); `run_status.ini` sums them up and adds libgem's status flags and
  the camera's exposure, gain and pitch.
- libgem's camera pitch correction is on, so a tilted PS Eye no longer
  mixes hand height into depth (`pitch_correction=off` in `renderer.ini`).
- Cut cubes split along the blade's swing into two halves that fly apart,
  tumble and fall, then fade out; the cut faces glow with a neon rim and a
  slash streak marks the cut. Each half gets its own spread, fall, spin and
  slide, so many cuts do not fall in step. Switched off with `effects=off`.

## 0.1.0 — 2026-10-13

First public release.

- Two PS Move blades tracked by the PS Eye, with the Moves' own sensors for
  orientation and short losses, and camera depth limited to the play area.
- Three-pose calibration (arms forward, out to the sides, down) that finds
  which hand is which, your arm span and the play-area depth.
- BeatSaver maps: notes in eight directions, bombs, walls, chains and arcs,
  all difficulties; the PC importer converts maps, playlists and the starter
  set.
- Track library with covers and a gesture carousel, difficulty switching,
  a randomizer and playlist shelves; playlists play through with results
  between songs and a summary at the end.
- Pause menu (continue, restart, calibrate, back to tracks), automatic pause
  and resume when the camera loses a Move.
- Rumble on cuts and long rumble on walls, a red flash when the combo breaks,
  max combo in the results. No failing on energy for now.
- Levels from a FAT32 USB stick are copied into the console's library, even
  when the stick is plugged in while the game runs.
- Windows track assistant in English and Russian: search, starter set,
  export to USB, FTP upload.
