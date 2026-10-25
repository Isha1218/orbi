import subprocess
import wave

import pytest


def speak(text, folder):
    """Makes real speech with macOS `say`, as 16 kHz mono 16-bit PCM bytes."""
    aiff = folder / "speech.aiff"
    wav = folder / "speech.wav"
    subprocess.run(["say", "-o", str(aiff), text], check=True)
    subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", str(aiff), str(wav)], check=True)
    with wave.open(str(wav)) as w:
        return w.readframes(w.getnframes())


@pytest.fixture
def weather_pcm(tmp_path):
    return speak("what's the weather", tmp_path)
