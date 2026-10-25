from orbi import voice


def test_transcribes_speech(weather_pcm):
    text, confidence = voice.transcribe(weather_pcm)
    assert "weather" in text.lower()
    assert 0 < confidence <= 1


def test_silence_is_empty():
    two_seconds = bytes(2 * 16000 * 2)
    assert voice.transcribe(two_seconds) == ("", 0.0)


def test_too_short_is_empty(weather_pcm):
    assert voice.transcribe(weather_pcm[:1000]) == ("", 0.0)
