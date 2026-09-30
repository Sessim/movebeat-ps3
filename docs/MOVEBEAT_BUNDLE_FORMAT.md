# MoveBeat Level Bundle format 1

This is the interchange contract between the command-line importer, MoveBeat
Track Assistant for PC, the planned Android client, and the PS3 package build.
It applies to imported levels; the built-in tutorial may remain a minimal
hand-authored bundle.

## Directory layout

Every bundle is one directory with only regular files at its root:

```text
<safe-id>/
  level.ini
  conversion-report.json
  audio.ogg or audio.egg
  cover.png                 # optional
  library.mblc              # derived PS3 track card
  Standard_Easy.mbchart
  Standard_Hard.mbchart
  ...
```

Nested paths and symbolic links are forbidden. Names are ASCII-safe leaves.
`level.ini` format 1 remains the runtime authority on PS3. The JSON report is
an interchange/integrity record and is not parsed by the game.

Optional per-chart keys `chartN_njs` and `chartN_start_beat_offset` preserve
Beat Saber note-jump speed and start-beat offset from supported Info.dat
versions. `N` is the zero-based chart index matching the chart list. If absent,
the game keeps its legacy jump configuration. Readers validate values before
using them; an invalid optional pair rejects the manifest rather than silently
changing note timing.

## Audio

Format 1 accepts an Ogg container with Vorbis audio. `.egg` is treated as a
filename convention only: importers must verify the `OggS` container signature
and Vorbis identification header rather than trusting the extension. Other
codecs must be transcoded before export and must not be silently copied into a
PS3 bundle.

## `conversion-report.json`

The file is UTF-8 JSON with sorted keys. It contains no timestamp or local
absolute path. The rendered card can vary across systems with different fonts.

Required top-level members:

- `bundleFormat`: integer `1`;
- `converter`: `name` and semantic `version` strings;
- `source`: `type` (`beatsaver` or `beat_saber_zip`), `key`, lowercase
  40-hex-character `contentSha1` using the BeatSaver/SongCore content identity
  algorithm, and lowercase 40-hex-character `archiveSha1` of the ZIP bytes;
- `level`: title, song author, mapper, BPM, audio filename, `ogg` container,
  `vorbis` codec, optional cover filename, and `libraryCard` filename;
- `charts`: ordered converted chart summaries;
- `warnings`: human-readable conversion warnings;
- `files`: every other regular file in the bundle, sorted by filename.

Each `files` entry contains a safe leaf `path`, exact byte `size`, and lowercase
SHA-256. The report does not list or hash itself. Consumers must reject missing,
extra, duplicated, nested, size-mismatched, or hash-mismatched files.

`library.mblc` uses an 8-byte header: ASCII `MBLC`, big-endian 16-bit width
`512`, and big-endian 16-bit height `96`. Exactly 512×96 big-endian RGB565
pixels follow. The left 96×96 pixels are a cropped/downscaled cover; the rest
contains pre-rendered UTF-8 title, artist and mapper text. PS3 does not decode the
original cover. Older valid bundles lacking this optional card remain playable
but show a generic placeholder in the menu.

Each chart summary contains:

- `characteristic`, `difficulty`, and `.mbchart` filename;
- counts for `notes`, `obstacles`, `chains`, `arcs`, and `skipped` objects.

`contentSha1` is not the SHA-1 of the ZIP. It hashes the exact `Info.dat` bytes
followed by every referenced beatmap in Info order. For format 4 maps, the BPM
data file is inserted after `Info.dat`, and each separate lightshow file follows
its beatmap file. Repeated references are hashed repeatedly. This value is the
version identity published by BeatSaver; `archiveSha1` detects changes to the
downloaded container itself.

## Compatibility rules

- Readers must reject an unknown `bundleFormat`; guessing is not allowed.
- New optional JSON members may be added without changing format 1.
- Removing/renaming a required member, changing its meaning, allowing nested
  paths, or changing runtime chart syntax requires a new bundle format.
- A newer converter version alone does not imply an incompatible bundle.
- The PS3 package builder may include the report unchanged for later recovery
  over FTP.
