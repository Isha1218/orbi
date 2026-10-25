// Native test harness: runs eye.c + protocol.c with fake time and writes PPM frames.
// Reads a script on stdin, one command per line:
//   @1500            advance the clock to 1500 ms (updates at 30 fps on the way)
//   snap out.ppm     write the current frame
//   avatar img.raw   load a 120x120 RGB565 little-endian image
//   bench 100        render 100 frames and print ms/frame
//   anything else    is passed to protocol_handle_line
#include "../eye.h"
#include "../protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint16_t fb[EYE_SIZE * EYE_SIZE];
static uint32_t now_ms = 1;

static void advance_to(uint32_t t) {
    while (now_ms + 33 < t) {
        now_ms += 33;
        eye_update(now_ms);
    }
    now_ms = t;
    eye_update(now_ms);
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", EYE_SIZE, EYE_SIZE);
    for (int i = 0; i < EYE_SIZE * EYE_SIZE; i++) {
        uint16_t p = fb[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((p >> 11) & 31) * 255 / 31);
        rgb[1] = (unsigned char)(((p >> 5) & 63) * 255 / 63);
        rgb[2] = (unsigned char)((p & 31) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void load_avatar(const char *path) {
    static uint16_t px[120 * 120];
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return;
    }
    size_t n = fread(px, 2, 120 * 120, f);
    fclose(f);
    if (n == 120 * 120) {
        eye_set_avatar(px);
    }
}

static void bench(int frames) {
    clock_t start = clock();
    for (int i = 0; i < frames; i++) {
        now_ms += 33;
        eye_update(now_ms);
        eye_render(fb);
    }
    double ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / frames;
    printf("%.2f ms/frame\n", ms);
}

int main(void) {
    char line[512];
    eye_init(12345);
    eye_update(now_ms);
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 0 || line[0] == '#') {
            continue;
        }
        if (line[0] == '@') {
            advance_to((uint32_t)atol(line + 1));
        } else if (strncmp(line, "snap ", 5) == 0) {
            eye_render(fb);
            write_ppm(line + 5);
        } else if (strncmp(line, "avatar ", 7) == 0) {
            load_avatar(line + 7);
        } else if (strncmp(line, "bench ", 6) == 0) {
            bench(atoi(line + 6));
        } else {
            protocol_handle_line(line);
        }
    }
    return 0;
}
