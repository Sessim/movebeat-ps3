# Changelog

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
