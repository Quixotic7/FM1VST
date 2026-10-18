/* SPDX-License-Identifier: GPL-3.0-only */
/* The Dinghy family's golden check: a core against its firmware's own host sim (tests/host.c, built from the same
 * pinned tree by dinghy.cmake), driven by the same input script, compared at one point on the screen.
 *
 *   dinghy_golden CORE.fm1core HOSTSIM SCRIPT T OUT_DIR
 *
 * SCRIPT is the host sim's own format (a line each: "T keys MASK", "T btn ID" (a tap), "T btndown ID" / "T press ID",
 * "T btnup ID" / "T release ID", "T enc ID N" / "T turn ID N", "T midi ST D1 D2", "T shot", "T end"). The host sim
 * runs it (HOSTSIM OUT_DIR/<name> SCRIPT) and writes OUT_DIR/<name>-T.ppm (a "T shot" line, or FM-106's fixed SHOT
 * times). The core runs the same events on the host sim's clock, exactly:
 *   host.c   platform_poll at ms k: the events with t <= k, then the 256-frame blocks due (k * 44100 / 1000), then the
 *            shot at k; then the main loop's pass k
 *   here     for k = 1 ..: the events of k into the panel, the blocks due (audio(256): one firmware block each), then
 *            tick(k) (pass k: the glue sets fm1_ms = k) and frame()
 * so host.c's shot at T is the screen after pass T - 1, which is the core's LCD after tick(T - 1). Button and encoder
 * ids are Dinghy's (= the panel's matrix ids); a tap is a press this pass and a release the next, as OberVave's and
 * FM-106's host.c taps (Frodhes' "btn" releases in the same pass: its scripts use press / release).
 * PASS: the two screens are identical (every pixel, as host.c writes its PPM). */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "ppm.h"

typedef struct {
    uint32_t t;
    int kind;                                  /* 0 keys, 1 tap, 2 down, 3 up, 4 enc, 5 midi */
    uint32_t a;
    int32_t b;
    uint8_t st, d1, d2;
} ev_t;

static ev_t ev[4096];
static uint32_t nev;

static int read_script(const char *path)
{
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 0;
    }
    while (fgets(line, sizeof line, f) && nev < 4096u) {
        ev_t e;
        unsigned t = 0, a = 0, b = 0, c = 0;
        int x = 0, y = 0;
        memset(&e, 0, sizeof e);
        if (line[0] == '#' || sscanf(line, "%u %15s", &t, what) != 2)
            continue;
        e.t = t;
        if (!strcmp(what, "keys")) {
            e.kind = 0;
            sscanf(line, "%*u %*s %i", &x);
            e.a = (uint32_t)x;
        } else if (!strcmp(what, "btn") || !strcmp(what, "btndown") || !strcmp(what, "press") ||
                   !strcmp(what, "btnup") || !strcmp(what, "release")) {
            e.kind = what[0] == 'b' && what[3] == 0 ? 1 : (!strcmp(what, "btndown") || !strcmp(what, "press")) ? 2 : 3;
            sscanf(line, "%*u %*s %d", &x);
            e.a = (uint32_t)x;
        } else if (!strcmp(what, "enc") || !strcmp(what, "turn")) {
            e.kind = 4;
            sscanf(line, "%*u %*s %d %d", &x, &y);
            e.a = (uint32_t)x;
            e.b = y;
        } else if (!strcmp(what, "midi")) {
            e.kind = 5;
            sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c);
            e.st = (uint8_t)a;
            e.d1 = (uint8_t)b;
            e.d2 = (uint8_t)c;
        } else
            continue;                          /* (shot, end, expect: the host sim's) */
        ev[nev++] = e;
    }
    fclose(f);
    return 1;
}

static int read_ppm(const char *path, uint8_t *rgb)   /* (common/ppm.h, with the host sim's file named on failure) */
{
    if (fm1g_read_ppm(path, rgb))
        return 1;
    fprintf(stderr, "%s: no 240 x 240 P6 PPM there\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    static uint8_t host_rgb[240 * 240 * 3], core_rgb[240 * 240 * 3];
    char out[1024], cmd[4096], path[1100];
    const char *name;
    uint32_t T, k, i, owed = 0, diff = 0;
    int16_t blk[256 * 2];
    void *h;
    fm1core_get_fn get;
    const fm1core_t *c;
    fm1panel_t *p;
    if (argc < 6) {
        fprintf(stderr, "usage: dinghy_golden CORE.fm1core HOSTSIM SCRIPT T OUT_DIR\n");
        return 2;
    }
    T = (uint32_t)atoi(argv[4]);
    name = strrchr(argv[3], '/') ? strrchr(argv[3], '/') + 1 : argv[3];
    snprintf(out, sizeof out, "%s/%.*s", argv[5], (int)(strcspn(name, ".")), name);
    if (!read_script(argv[3]) || T < 2)
        return 2;

    /* the firmware's own host sim */
    snprintf(cmd, sizeof cmd, "'%s' '%s' '%s' > '%s.log' 2>&1", argv[2], out, argv[3], out);
    if (system(cmd) != 0)
        fprintf(stderr, "dinghy_golden: the host sim exited non-zero (see %s.log)\n", out);
    snprintf(path, sizeof path, "%s-%u.ppm", out, T);
    if (!read_ppm(path, host_rgb))
        return 1;

    /* the core, on the host sim's clock */
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dinghy_golden: %s\n", dlerror());
        return 1;
    }
    get = (fm1core_get_fn)dlsym(h, FM1CORE_SYMBOL);
    c = get ? get(FM1CORE_ABI) : NULL;
    if (!c) {
        fprintf(stderr, "dinghy_golden: %s: no ABI %u descriptor\n", argv[1], FM1CORE_ABI);
        return 1;
    }
    p = c->panel;
    c->flash_stage(NULL, 0);
    c->options(FM1CORE_FLASH_STAGED, 0, 0, 1);
    p->ready = 2;
    c->init(0);
    for (k = 1; k < T; k++) {
        for (i = 0; i < nev; i++) {
            const ev_t *e = &ev[i];
            if (e->t != k && !(k == 1 && e->t < 1))
                continue;
            switch (e->kind) {
            case 0: p->keys = e->a & ((1u << FM1P_NKEY) - 1u); break;
            case 1: p->buttons_tap |= 1u << e->a; break;
            case 2: p->buttons |= 1u << e->a; break;
            case 3: p->buttons &= ~(1u << e->a); break;
            case 4: if (e->a < FM1P_NENC) p->enc[e->a] += e->b; break;
            case 5: c->midi_in((uint32_t)(e->st >> 4) | (uint32_t)e->st << 8 | (uint32_t)e->d1 << 16 | (uint32_t)e->d2 << 24); break;
            }
        }
        owed += FM1P_FS;
        while (owed >= 1000u * 256u) {
            owed -= 1000u * 256u;
            c->audio(blk, 256);
        }
        c->tick(k);
        c->frame();
    }
    fm1g_lcd_rgb(p->lcd, core_rgb);               /* (common/ppm.h: as host.c writes its PPM) */
    for (i = 0; i < 240u * 240u; i++)
        diff += memcmp(core_rgb + 3 * i, host_rgb + 3 * i, 3) != 0;
    snprintf(path, sizeof path, "%s-core-%u.ppm", out, T);
    fm1g_write_ppm(path, core_rgb);
    printf("dinghy_golden %s: the screen at %u ms, the core (%s) against the host sim (%s-%u.ppm): %u of 57600 pixels "
           "differ: %s\n", c->id, T, path, out, T, diff, diff ? "FAIL" : "PASS");
    c->shutdown();
    return diff ? 1 : 0;
}
