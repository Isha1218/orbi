import json
import subprocess
import wave

import pytest

from orbi.agents import Agents
from orbi.daemon import Daemon
from orbi.display import Display
from orbi.hermes import Hermes


class FakeDevice:
    def __init__(self):
        self.sent = []

    async def send(self, msg):
        if isinstance(msg, bytes):
            self.sent.append(msg)
        else:
            self.sent.append(json.loads(msg))

    async def command(self, cmd):
        self.sent.append(cmd)


class FakePanel:
    def __init__(self):
        self.shown = []
        self.toasts = []

    def show(self, title, content, actions):
        self.shown.append({"title": title, "content": content, "actions": actions})

    def toast(self, text):
        self.toasts.append(text)


@pytest.fixture
def daemon():
    d = Daemon(panel=FakePanel())
    d.personality.idle_expression = lambda: "idle"
    d.display = Display(d)
    d.agents = Agents(d)
    d.hermes = Hermes(d)
    return d

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
