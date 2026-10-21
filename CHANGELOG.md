# Changelog

## Unreleased

- Arcs (the lines between two notes) are guides, as in Beat Saber: following
  one earns bonus points, leaving it is no miss and no longer breaks the
  combo or costs energy.
- An arc lights up as a neon tube in the hand's color while its blade
  follows it, and fades within 0.2 s after the blade leaves.
- In a long track list the lowest row no longer runs into the control hints.
- `trim_charts.py --max-difficulty` drops charts harder than a cap.

## 0.2.1 — 2026-10-20

- Russian screens. The game follows the PS3 system language (English or
  Russian); `language=en|ru` in `settings.ini` overrides it. The menu font
  gains Cyrillic capitals (Й and К after the public-domain misc-fixed
  fonts), digits and punctuation.
- Buttons are shown as icons in their own colors (triangle, circle, cross,
  square, a tall Move oval, T, Start). The track list groups its controls,
  with Move to play in the middle; the pause menu is centered; the song
  screen drops the SCORE and COMBO labels for a small x before the combo
  and a stopwatch for the time left.
- Calibration asks to hold both Moves straight ahead and shows each hand in
  its sphere's real color.
- The two spheres keep their colors at least 120° apart: when libgem moves
  one close to the other, it is asked for another hue (`min_hue_gap` in
  `renderer.ini`, 0 turns it off).
- The tracking pause shows the start pose only while Start re-centers the
  play space.

## 0.2.0 — 2026-10-20

- The starter set is 51 maps in six themed playlists (Throwback Hits, Party
  Hits, Fresh Hits, Pop Rock Anthems, Electro and Memes, Russian Rock and
  Rap), each with a cover made of its songs' covers. The single "MoveBeat
  Starter" list of 0.1.0 is replaced.
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
