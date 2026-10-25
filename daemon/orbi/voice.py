"""Speech-to-text for push-to-talk audio (16 kHz mono 16-bit PCM), run locally with faster-whisper.
The audio is only held in memory while it's being transcribed."""
import math

import numpy as np

MODEL_NAME = "base.en"
SAMPLE_RATE = 16000
MIN_SECONDS = 0.3
NO_SPEECH_PROB = 0.6

model = None


def load_model():
    global model
    if model is None:
        from faster_whisper import WhisperModel

        model = WhisperModel(MODEL_NAME, device="cpu", compute_type="int8")
    return model


def transcribe(pcm):
    """Returns (text, confidence). Empty text means nothing usable was heard."""
    if len(pcm) < MIN_SECONDS * SAMPLE_RATE * 2:
        return "", 0.0
    audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
    segments, info = load_model().transcribe(audio, beam_size=5, vad_filter=True)

    texts = []
    probs = []
    for segment in segments:
        if segment.no_speech_prob > NO_SPEECH_PROB:
            continue
        texts.append(segment.text.strip())
        probs.append(math.exp(segment.avg_logprob))

    text = " ".join(texts).strip()
    if not text:
        return "", 0.0
    return text, sum(probs) / len(probs)
