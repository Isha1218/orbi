"""Weather from Open-Meteo (free, no API key), with a hint for what to show on the face."""
import asyncio
import json
import urllib.parse
import urllib.request

from orbi.plugins import define_plugin

GEOCODE_URL = "https://geocoding-api.open-meteo.com/v1/search"
FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
RAIN_LIKELY = 50  # percent
WET = ["drizzle", "rain", "snow", "storm"]


def fetch_json(url, params):
    full = url + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(full, timeout=10) as resp:
        return json.loads(resp.read())


def describe(code):
    """WMO weather code -> one short word."""
    if code <= 1:
        return "clear"
    if code <= 3:
        return "cloudy"
    if code <= 48:
        return "fog"
    if code <= 57:
        return "drizzle"
    if code <= 67:
        return "rain"
    if code <= 77:
        return "snow"
    if code <= 82:
        return "rain"
    if code <= 86:
        return "snow"
    return "storm"


def scene_for(word):
    if word == "drizzle":
        return "rain"
    if word in WET:
        return word
    return "sun"


def mood_for(word):
    if word == "clear":
        return "happy"
    if word in WET:
        return "grumpy"
    return "neutral"


def hour_label(time_text):
    """"2026-10-25T15:00" -> "3pm"."""
    hour = int(time_text[11:13])
    suffix = "am"
    if hour >= 12:
        suffix = "pm"
    hour = hour % 12
    if hour == 0:
        hour = 12
    return f"{hour}{suffix}"


def next_wet_hour(forecast):
    """The first hour from now with rain, snow or a storm likely, as (word, "3pm"), or None."""
    now = forecast["current"]["time"]
    hourly = forecast["hourly"]
    for i, time_text in enumerate(hourly["time"]):
        if time_text[:13] < now[:13]:
            continue
        word = describe(hourly["weather_code"][i])
        chance = hourly["precipitation_probability"][i]
        if word in WET and chance is not None and chance >= RAIN_LIKELY:
            return word, hour_label(time_text)
    return None


def summarize(place, forecast):
    current = forecast["current"]
    daily = forecast["daily"]
    temp = round(current["temperature_2m"])
    celsius = round((current["temperature_2m"] - 32) * 5 / 9)
    word = describe(current["weather_code"])
    high = round(daily["temperature_2m_max"][0])
    low = round(daily["temperature_2m_min"][0])

    lines = [f"{place}: {temp}°F ({celsius}°C), {word}. Today {low}–{high}°F."]
    label = f"{temp}° {word}"
    scene_word = word
    if word not in WET:
        later = next_wet_hour(forecast)
        if later:
            lines.append(f"{later[0].capitalize()} likely from {later[1]}.")
            scene_word = later[0]
            label = f"{temp}° {later[0]} {later[1]}"
            if len(label) > 12:
                label = f"{temp}° {later[0]}"
    scene = scene_for(scene_word)
    mood = mood_for(scene_word)
    lines.append(
        f'Show it on the face: face_show(scene="{scene}", mood="{mood}", label="{label}"). '
        "Use a different label if you like, but keep it at most 12 characters."
    )
    return "\n".join(lines)


async def get_weather(city: str) -> str:
    """Current weather and today's forecast for a city, plus what to show on the face."""
    try:
        found = await asyncio.to_thread(fetch_json, GEOCODE_URL, {"name": city, "count": 1})
        if not found.get("results"):
            return f"Couldn't find a place called {city}."
        spot = found["results"][0]
        params = {
            "latitude": spot["latitude"],
            "longitude": spot["longitude"],
            "current": "temperature_2m,weather_code",
            "hourly": "weather_code,precipitation_probability",
            "daily": "temperature_2m_max,temperature_2m_min",
            "temperature_unit": "fahrenheit",
            "timezone": "auto",
            "forecast_days": 1,
        }
        forecast = await asyncio.to_thread(fetch_json, FORECAST_URL, params)
        place = spot["name"]
        if spot.get("country"):
            place = f"{place}, {spot['country']}"
        return summarize(place, forecast)
    except Exception as e:
        return f"Couldn't get the weather for {city}: {e}"


def setup(ctx):
    ctx.tools.register("get_weather", get_weather)


plugin = define_plugin(name="weather", setup=setup)
