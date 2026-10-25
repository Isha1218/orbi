"""Push-to-talk: hold Right Option anywhere on the computer to talk, let go to stop."""
from pynput import keyboard

TALK_KEY = keyboard.Key.alt_r

PERMISSION_MESSAGE = (
    "Push-to-talk can't see the keyboard. Open System Settings > Privacy & Security and turn on\n"
    "Input Monitoring and Accessibility for your terminal app, then restart orbi."
)


def start(daemon):
    """Starts the key listener on its own thread."""
    held = False

    def on_press(key):
        nonlocal held
        if key != TALK_KEY or held:
            return  # other keys, or auto-repeat while held
        held = True
        daemon.call_soon(daemon.talk_pressed)

    def on_release(key):
        nonlocal held
        if key != TALK_KEY:
            return
        held = False
        daemon.call_soon(daemon.talk_released)

    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.daemon = True
    listener.start()
    listener.wait()
    if not getattr(listener, "IS_TRUSTED", True) or not listener.is_alive():
        print(PERMISSION_MESSAGE, flush=True)
    return listener
