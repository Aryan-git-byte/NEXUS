"""Generate the original, short PCM chime distributed with NEXUS DECK."""
from __future__ import annotations

import math
import struct
import wave
from pathlib import Path


def generate(path: Path) -> None:
    rate = 22050
    samples = bytearray()
    for frequency, duration in ((784.0,0.28),(0.0,0.09),(1046.5,0.38)):
        count = round(rate * duration)
        for index in range(count):
            t = index / rate
            envelope = max(0.0,min(1.0,t / 0.008,(duration - t) / 0.03)) * math.exp(-3 * t / duration)
            value = 0.19 * envelope * (math.sin(2 * math.pi * frequency * t) +
                                      0.2 * math.sin(4 * math.pi * frequency * t))
            samples.extend(struct.pack("<h",round(value * 32767)))
    path.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(path),"wb") as output:
        output.setnchannels(1); output.setsampwidth(2); output.setframerate(rate)
        output.writeframes(samples)


if __name__ == "__main__":
    destination = Path(__file__).resolve().parents[1] / "assets/timer.wav"
    generate(destination)
    print(f"Generated {destination.name}: {destination.stat().st_size} bytes")
