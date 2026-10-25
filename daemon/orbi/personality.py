"""Orbi's character when nothing else is going on: curious, a little impatient, sleepy at night.
Blinks and glances happen on the sphere itself; this picks the idle expression."""
import random
import sys
import time

from . import library

AWAY_SECONDS = 5 * 60     # no keyboard or mouse for this long = you're away
BORED_SECONDS = 2 * 60    # idle this long and it starts yawning now and then
NIGHT_START, NIGHT_END = 23, 5
GREET_SECONDS = 2.5
YAWN_SECONDS = 2.5


def seconds_since_input():
    if sys.platform != "darwin":
        return 0
    import Quartz

    return Quartz.CGEventSourceSecondsSinceLastEventType(
        Quartz.kCGEventSourceStateHIDSystemState, Quartz.kCGAnyInputEventType
    )


class Personality:
    def __init__(self):
        self.away = False
        self.greet_until = 0
        self.yawn_until = 0
        self.next_yawn = 0
        self.behaviors = []  # (plugin name, fn) from plugins

    def idle_expression(self, now=None, idle=None, hour=None):
        if now is None:
            now = time.time()
        if idle is None:
            idle = seconds_since_input()
        if hour is None:
            hour = time.localtime(now).tm_hour

        if idle > AWAY_SECONDS:
            self.away = True
            return "sleepy"
        if self.away:
            self.away = False
            self.greet_until = now + GREET_SECONDS
        if now < self.greet_until:
            return "pleased"

        night = hour >= NIGHT_START or hour < NIGHT_END
        if night and idle > 60:
            return "sleepy"

        if idle > BORED_SECONDS:
            if now >= self.next_yawn:
                self.yawn_until = now + YAWN_SECONDS
                self.next_yawn = now + random.uniform(20, 60)
            if now < self.yawn_until:
                return "sleepy"
        return self.plugin_expression(idle, hour)

    def plugin_expression(self, idle, hour):
        for name, fn in list(self.behaviors):
            try:
                expression = fn(idle, hour)
            except Exception as e:
                print(f"Plugin {name}: idle behavior failed and was turned off: {e!r}", flush=True)
                self.behaviors.remove((name, fn))
                continue
            if expression in library.EXPRESSIONS:
                return expression
        return "idle"
