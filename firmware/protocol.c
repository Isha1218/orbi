#include "protocol.h"
#include "eye.h"
#include <stdlib.h>
#include <string.h>

static const char *EXPR_NAMES[EXPR_COUNT] = {
    "idle", "listening", "thinking", "busy", "pleased", "worried", "expectant", "confused", "sleepy"
};

// Minimal parser for our flat messages: returns a pointer to the value after "key":
static const char *value_of(const char *line, const char *key) {
    size_t n = strlen(key);
    for (const char *p = strchr(line, '"'); p; p = strchr(p + 1, '"')) {
        if (strncmp(p + 1, key, n) == 0 && p[1 + n] == '"') {
            p += n + 2;
            while (*p == ' ' || *p == ':') p++;
            return p;
        }
    }
    return NULL;
}

static int is_string(const char *val, const char *s) {
    size_t n = strlen(s);
    return val && *val == '"' && strncmp(val + 1, s, n) == 0 && val[1 + n] == '"';
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
        const char *name = value_of(line, "name");
        const char *intensity = value_of(line, "intensity");
        float k = 1.0f;
        if (intensity) {
            k = strtof(intensity, NULL);
        }
        for (int i = 0; i < EXPR_COUNT; i++) {
            if (is_string(name, EXPR_NAMES[i])) {
                eye_set_expression((expr_t)i, k);
            }
        }
    } else if (is_string(t, "blink")) {
        eye_blink();
    } else if (is_string(t, "listening")) {
        const char *on = value_of(line, "on");
        eye_set_listening(on && strncmp(on, "true", 4) == 0);
    }
}
