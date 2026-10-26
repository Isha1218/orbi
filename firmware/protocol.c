#include "protocol.h"
#include "eye.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DURATION_MS 4000

static const char *EXPR_NAMES[EXPR_COUNT] = {
    "idle", "listening", "thinking", "busy", "pleased", "worried", "expectant", "confused", "sleepy"
};
static const char *SCENE_NAMES[SCENE_COUNT] = {
    "rain", "snow", "sun", "storm", "cloud", "confetti", "sparkles", "zzz"
};
static const char *MOOD_NAMES[MOOD_COUNT] = {
    "neutral", "happy", "grumpy"
};
static const char *ICON_NAMES[ICON_COUNT] = {
    "mail", "calendar", "check", "cross", "warning", "clock", "bell", "chat", "code", "music", "star", "heart"
};

// Minimal parser for our flat messages: returns a pointer to the value after "key":
static const char *value_of(const char *line, const char *key) {
    size_t n = strlen(key);
    for (const char *p = strchr(line, '"'); p; p = strchr(p + 1, '"')) {
        if (strncmp(p + 1, key, n) == 0 && p[1 + n] == '"') {
            p += n + 2;
            while (*p == ' ' || *p == ':') {
                p++;
            }
            return p;
        }
    }
    return NULL;
}

static int is_string(const char *val, const char *s) {
    size_t n = strlen(s);
    return val && *val == '"' && strncmp(val + 1, s, n) == 0 && val[1 + n] == '"';
}

// Index of the name the value matches, or -1.
static int find_name(const char *val, const char **names, int count) {
    for (int i = 0; i < count; i++) {
        if (is_string(val, names[i])) {
            return i;
        }
    }
    return -1;
}

static int put_utf8(char *out, int pos, int size, unsigned code) {
    char bytes[3];
    int n;
    if (code < 0x80) {
        bytes[0] = (char)code;
        n = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xc0 | (code >> 6));
        bytes[1] = (char)(0x80 | (code & 0x3f));
        n = 2;
    } else {
        bytes[0] = (char)(0xe0 | (code >> 12));
        bytes[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        bytes[2] = (char)(0x80 | (code & 0x3f));
        n = 3;
    }
    if (pos + n >= size) {
        return pos;
    }
    memcpy(out + pos, bytes, n);
    return pos + n;
}

// Copies a JSON string value into out, handling escapes. Returns out, or NULL if not a string.
static const char *string_value(const char *val, char *out, int size) {
    if (val == NULL || *val != '"') {
        return NULL;
    }
    int pos = 0;
    const char *p = val + 1;
    while (*p && *p != '"') {
        unsigned code = (unsigned char)*p;
        p++;
        if (code == '\\' && *p) {
            char esc = *p;
            p++;
            if (esc == 'u' && strlen(p) >= 4) {
                char hex[5] = {p[0], p[1], p[2], p[3], 0};
                code = (unsigned)strtoul(hex, NULL, 16);
                p += 4;
            } else if (esc == 'n' || esc == 't') {
                code = ' ';
            } else {
                code = (unsigned char)esc;
            }
            pos = put_utf8(out, pos, size, code);
        } else if (pos + 1 < size) {
            out[pos] = (char)code;
            pos++;
        }
    }
    out[pos] = 0;
    return out;
}

static uint32_t duration_of(const char *line) {
    const char *val = value_of(line, "duration_ms");
    if (val == NULL) {
        return DEFAULT_DURATION_MS;
    }
    long ms = strtol(val, NULL, 10);
    if (ms <= 0) {
        return DEFAULT_DURATION_MS;
    }
    return (uint32_t)ms;
}

static void handle_scene(const char *line) {
    char label[64];
    int scene = find_name(value_of(line, "name"), SCENE_NAMES, SCENE_COUNT);
    int mood = find_name(value_of(line, "mood"), MOOD_NAMES, MOOD_COUNT);
    if (scene < 0) {
        return;
    }
    if (mood < 0) {
        mood = MOOD_NEUTRAL;
    }
    const char *text = string_value(value_of(line, "label"), label, sizeof label);
    eye_scene((scene_t)scene, (mood_t)mood, text, duration_of(line));
}

static void handle_card(const char *line) {
    char label[64];
    char initials[16];
    const char *text = string_value(value_of(line, "label"), label, sizeof label);
    uint32_t ms = duration_of(line);
    const char *icon_val = value_of(line, "icon");
    const char *initials_val = value_of(line, "initials");
    const char *avatar_val = value_of(line, "avatar");
    if (icon_val != NULL) {
        int icon = find_name(icon_val, ICON_NAMES, ICON_COUNT);
        if (icon >= 0) {
            eye_card_icon((icon_t)icon, text, ms);
        }
    } else if (initials_val != NULL) {
        const char *s = string_value(initials_val, initials, sizeof initials);
        if (s != NULL) {
            eye_card_initials(s, text, ms);
        }
    } else if (avatar_val != NULL && strncmp(avatar_val, "true", 4) == 0) {
        eye_card_avatar(text, ms);
    }
}

void protocol_handle_line(const char *line) {
    const char *t = value_of(line, "t");
    if (is_string(t, "gaze")) {
        const char *x = value_of(line, "x");
        const char *y = value_of(line, "y");
        if (x && y) {
            eye_gaze(strtof(x, NULL), strtof(y, NULL));
        }
    } else if (is_string(t, "expression")) {
        const char *intensity = value_of(line, "intensity");
        float k = 1.0f;
        if (intensity) {
            k = strtof(intensity, NULL);
        }
        int ex = find_name(value_of(line, "name"), EXPR_NAMES, EXPR_COUNT);
        if (ex >= 0) {
            eye_set_expression((expr_t)ex, k);
        }
    } else if (is_string(t, "blink")) {
        eye_blink();
    } else if (is_string(t, "listening")) {
        const char *on = value_of(line, "on");
        eye_set_listening(on && strncmp(on, "true", 4) == 0);
    } else if (is_string(t, "scene")) {
        handle_scene(line);
    } else if (is_string(t, "card")) {
        handle_card(line);
    }
}
