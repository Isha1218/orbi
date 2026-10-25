from orbi.display import check_face, screen_fallback


def test_good_scene():
    cmd, reason = check_face(scene="rain", mood="grumpy", label="62° rain 3pm")
    assert reason is None
    assert cmd == {"t": "scene", "name": "rain", "mood": "grumpy", "label": "62° rain 3pm", "duration_ms": 4000}


def test_scene_without_label_or_mood():
    cmd, reason = check_face(scene="sun")
    assert cmd == {"t": "scene", "name": "sun", "mood": "neutral", "duration_ms": 4000}


def test_unknown_scene():
    cmd, reason = check_face(scene="tornado")
    assert cmd is None
    assert "tornado" in reason


def test_doc_example_label_is_too_long():
    label = "62° · rain 3pm"
    assert len(label) == 14
    cmd, reason = check_face(scene="rain", mood="grumpy", label=label)
    assert cmd is None
    assert "12" in reason


def test_label_counts_characters_not_bytes():
    label = "°·→°·→°·→°·→"
    assert len(label) == 12
    assert len(label.encode()) > 12
    cmd, reason = check_face(card={"icon": "mail"}, label=label)
    assert reason is None
    assert cmd["label"] == label
    cmd, reason = check_face(card={"icon": "mail"}, label=label + "!")
    assert cmd is None


def test_unknown_mood_and_icon():
    assert check_face(scene="rain", mood="furious")[0] is None
    assert check_face(card={"icon": "rocket"})[0] is None


def test_cards():
    cmd, reason = check_face(card={"initials": "SJ"}, label="Demo → Fri")
    assert cmd == {"t": "card", "initials": "SJ", "label": "Demo → Fri", "duration_ms": 4000}
    cmd, reason = check_face(card={"avatar": "/tmp/me.png"}, label="Sarah J.")
    assert cmd["avatar"] == "/tmp/me.png"


def test_unsupported_content():
    assert check_face()[0] is None
    assert check_face(scene="rain", card={"icon": "mail"})[0] is None
    assert check_face(card={"icon": "mail", "initials": "SJ"})[0] is None
    assert check_face(card={"photo": "x.png"})[0] is None
    assert check_face(card={"initials": "ABCD"})[0] is None
    assert check_face(scene="rain", label="two\nlines")[0] is None


def test_screen_fallback_markdown():
    content = screen_fallback(scene="rain", mood="grumpy", label="62° · rain 3pm")
    assert "## 62° · rain 3pm" in content
    assert "Scene: rain" in content
