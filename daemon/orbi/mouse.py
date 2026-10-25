"""Cursor position across all monitors, as -1..1 on each axis (center of the whole desktop = 0)."""
import sys


def cursor():
    if sys.platform != "darwin":
        return None  # TODO: Windows/Linux
    import Quartz

    pos = Quartz.CGEventGetLocation(Quartz.CGEventCreate(None))
    err, ids, count = Quartz.CGGetActiveDisplayList(16, None, None)
    if err or not count:
        return None
    left = top = float("inf")
    right = bottom = float("-inf")
    for display in ids[:count]:
        b = Quartz.CGDisplayBounds(display)
        left = min(left, b.origin.x)
        top = min(top, b.origin.y)
        right = max(right, b.origin.x + b.size.width)
        bottom = max(bottom, b.origin.y + b.size.height)
    w = (right - left) / 2
    h = (bottom - top) / 2
    x = (pos.x - left - w) / w
    y = (pos.y - top - h) / h
    return max(-1.0, min(1.0, x)), max(-1.0, min(1.0, y))
