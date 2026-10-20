#!/usr/bin/env python3
"""Write an original, short three-note PCM WAV for manual sound tests."""

import math
from pathlib import Path
import struct
import sys
import wave


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: generate_sound.py OUTPUT.wav")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), "wb") as sound:
        sound.setparams((1, 2, 22050, 0, "NONE", "not compressed"))
        for frequency in (523.25, 659.25, 783.99):
            for frame in range(6615):
                envelope = min(1, frame / 220) * (1 - frame / 6615)
                value = int(16000 * envelope * math.sin(
                    2 * math.pi * frequency * frame / 22050))
                sound.writeframesraw(struct.pack("<h", value))
            sound.writeframesraw(bytes(2205 * 2))


if __name__ == "__main__":
    main()
