#!/usr/bin/env python3
"""Create original First Steps music and its synchronized 120 BPM chart.

No recorded samples or third-party music are used. Requires ffmpeg with Vorbis.
Run explicitly when regenerating the checked-in package assets.
"""
from array import array
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import wave

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

RATE = 48000
DURATION = 18


def synthesize() -> array:
    audio = array('f', [0.0]) * (RATE * DURATION)
    rng = random.Random(20260920)

    def tone(start: float, duration: float, frequency: float, gain: float) -> None:
        offset = round(start * RATE)
        for i in range(min(round(duration * RATE), len(audio) - offset)):
            t = i / RATE
            envelope = min(1.0, t / 0.008) * math.exp(-t * 9)
            audio[offset + i] += gain * envelope * (
                math.sin(math.tau * frequency * t) +
                0.2 * math.sin(math.tau * frequency * 2 * t))

    # Four quiet count-in beats, then eight bars with an alternating-hand chart.
    chords = ((220, 261.626, 329.628, 440), (174.614, 220, 261.626, 349.228),
              (130.813, 164.814, 195.998, 261.626), (195.998, 246.942, 293.665, 391.995))
    for beat in range(36):
        start = beat * 0.5
        offset = round(start * RATE)
        for i in range(min(RATE // 4, len(audio) - offset)):
            t = i / RATE
            # Analytically integrated pitch sweep creates a soft kick drum.
            phase = math.tau * (52 * t + 70 * (1 - math.exp(-30 * t)) / 30)
            audio[offset + i] += 0.3 * math.sin(phase) * math.exp(-22 * t)
        if beat < 4:
            tone(start, 0.15, 880 if beat == 3 else 440, 0.10)
            continue
        chord = chords[((beat - 4) // 4) % len(chords)]
        tone(start, 0.45, chord[0] / 2, 0.22)
        tone(start, 0.3, chord[(beat - 4) % 4], 0.16)
        tone(start + 0.25, 0.22, chord[((beat - 4) + 2) % 4] * 2, 0.07)
        if beat % 2:
            for i in range(min(RATE // 10, len(audio) - offset)):
                audio[offset + i] += 0.12 * rng.uniform(-1, 1) * math.exp(-i / RATE * 55)
    pcm = array('h')
    for i, sample in enumerate(audio):
        fade = min(1.0, (len(audio) - i) / (RATE * 0.2))
        pcm.append(round(max(-0.9, min(0.9, sample * fade)) * 32767))
    if sys.byteorder != 'little':
        pcm.byteswap()
    return pcm


def main() -> None:
    destination = Path(__file__).resolve().parents[1] / 'ps3/package/USRDIR/levels/movebeat_first_steps'
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        wav_path = Path(temporary) / 'first_steps.wav'
        with wave.open(str(wav_path), 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(synthesize().tobytes())
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav_path),
                        '-c:a', 'libvorbis', '-q:a', '5',
                        '-metadata', 'title=First Steps',
                        '-metadata', 'artist=MoveBeat',
                        str(destination / 'audio.ogg')], check=True)
    rows = ['# Original MoveBeat tutorial: 120 BPM, four count-in beats.']
    for beat in range(32):
        hand = 'L' if beat % 2 == 0 else 'R'
        lane = 1 if hand == 'L' else 2
        direction = 'A' if beat < 8 else ('D' if (beat // 2) % 2 == 0 else 'U')
        rows.append(f'{2000 + beat * 500},{lane},1,{hand},{direction}')
    (destination / 'Easy.mbchart').write_text('\n'.join(rows) + '\n', encoding='utf-8')
    (destination / 'level.ini').write_text(
        'format=1\nsource=movebeat\nsource_key=movebeat_first_steps_v1\n'
        'title=First Steps\nsong_author=MoveBeat\nmapper=MoveBeat\n'
        'bpm=120\naudio=audio.ogg\ncover=\nchart_count=1\n'
        'chart0_characteristic=Standard\nchart0_difficulty=Easy\n'
        'chart0_file=Easy.mbchart\nchart0_notes=32\nchart0_skipped=0\n', encoding='utf-8')
    from tools.library_card import write_card
    write_card(destination, '', 'First Steps', 'MoveBeat', 'MoveBeat')
    print(destination)


if __name__ == '__main__':
    main()
