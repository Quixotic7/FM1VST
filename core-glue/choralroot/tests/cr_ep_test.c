/* SPDX-License-Identifier: GPL-3.0-only */
/* ctest rfm1_ep_choralroot: ChoralRoot's engine parameters (cr_ep.c; engine-params/SPEC.md, BUILD-PLAN.md stages 1-2),
 * the whole firmware unit compiled natively with the adapter (core_choralroot.c with FM1_HOST_EP1), checked from inside:
 *
 *   1. the ids (cr_ep_ids.h): every cell of the walk has a pid; every line of the table is a cell with the same key,
 *      range, default and list-ness (a meaning that moved fails), or is retired; pids and keys unique;
 *   2. an ep1 SET equals the editor's knob byte for byte: for both layers (Chord, Bass), every engine, every parameter
 *      of every page, at several values (min, max, the default, the middle, their neighbours): from the same state (the
 *      value one step short), one child process turns the editor's knob onto the value (cr_edit.c ce_knob with the
 *      editor showing that cell, the knob at its finest step), another calls the adapter's set with that value; the
 *      parts (trk[], psnd[]), the UI's sound mirror (cs) and both parts' engine patches must be the same bytes;
 *   3. reads change nothing: two child processes play the same 4 s (a chord held, the frames and audio as a host runs
 *      them), one of them reading every value, the layers and a watched page's words after every frame as the shim
 *      does; the audio must be bit-identical.
 *
 *   cr_ep_test           the checks (exit 1 on a failure)
 *   cr_ep_test --gen     cr_ep_ids.h printed again with the cells it lacks appended (append-only: lines never move)
 *   cr_ep_test --dump    the description as made: engines, pages, each knob's pid, name, range and format */
#include "../core_choralroot.c"
#include <sys/wait.h>
#include <unistd.h>

static const fm1core_t *T;
static uint32_t T_ms;
static void run_ms(uint32_t n, uint64_t *h)   /* the device clock: ticks, a frame every 16 ms, audio blocks */
{
    static int16_t buf[FM1P_BLOCK * 2];
    static uint64_t frames;
    uint32_t i, k;
    for (i = 0; i < n; i++, T_ms++) {
        T->tick(T_ms);
        if (T_ms % 16u == 0)
            T->frame();
        while (frames + FM1P_BLOCK <= (uint64_t)T_ms * FM1P_FS / 1000u) {
            T->audio(buf, FM1P_BLOCK);
            frames += FM1P_BLOCK;
            if (h)
                for (k = 0; k < FM1P_BLOCK * 2u; k++)
                    *h = (*h ^ (uint16_t)buf[k]) * 1099511628211ull;
        }
    }
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 60) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ------------------------------------------------------------------------------------------------ 1. ids --- */
static void check_ids(void)
{
    uint32_t i, j;
    CHECK(cep_missing == 0, "%u cells have no pid in cr_ep_ids.h (run cr_ep_test --gen)", cep_missing);
    for (i = 0; i < NELEM(CR_EP_IDS); i++) {
        if (!CR_EP_IDS[i].pid)
            continue;
        for (j = i + 1u; j < NELEM(CR_EP_IDS); j++) {
            CHECK(CR_EP_IDS[j].pid != CR_EP_IDS[i].pid, "pid %u twice", CR_EP_IDS[i].pid);
            CHECK(!CR_EP_IDS[j].key || !cu_eq(CR_EP_IDS[j].key, CR_EP_IDS[i].key), "key %s twice", CR_EP_IDS[i].key);
        }
        for (j = 0; j < cep_np && cep_par[j].pid != CR_EP_IDS[i].pid; j++)
            ;
        if (j == cep_np) {
            CHECK(CR_EP_IDS[i].retired, "pid %u (%s) is no cell any more: retire it (retired = 1), never reuse it",
                  CR_EP_IDS[i].pid, CR_EP_IDS[i].key);
            continue;
        }
        CHECK(!CR_EP_IDS[i].retired, "pid %u (%s) is retired but still a cell", CR_EP_IDS[i].pid, CR_EP_IDS[i].key);
        CHECK(cu_eq(cep_key_of(j), CR_EP_IDS[i].key), "pid %u: key %s, the table says %s", CR_EP_IDS[i].pid,
              cep_key_of(j), CR_EP_IDS[i].key);
        CHECK(cep_par[j].min == CR_EP_IDS[i].min && cep_par[j].max == CR_EP_IDS[i].max && cep_par[j].def == CR_EP_IDS[i].def,
              "pid %u (%s): its meaning moved: %d..%d def %d, frozen as %d..%d def %d: give the cell a new pid",
              CR_EP_IDS[i].pid, CR_EP_IDS[i].key, cep_par[j].min, cep_par[j].max, cep_par[j].def, CR_EP_IDS[i].min,
              CR_EP_IDS[i].max, CR_EP_IDS[i].def);
        CHECK(!!(cep_par[j].flags & RFM1_EPF_LIST) == CR_EP_IDS[i].list, "pid %u (%s): its list-ness moved",
              CR_EP_IDS[i].pid, CR_EP_IDS[i].key);
    }
}
static void gen_ids(void)
{
    uint32_t i, j, n = 0;
    printf("/* SPDX-License-Identifier: GPL-3.0-only */\n");
    printf("/* ChoralRoot's ep1 parameter ids (cr_ep.c; engine-params/SPEC.md 2): APPEND-ONLY. A pid's meaning is frozen forever:\n"
           " * its key (the cell it is), its range, its default and whether it is a list never change; a cell whose meaning changes\n"
           " * gets a new pid, and a cell that is gone keeps its line with retired = 1. The ctest rfm1_ep_choralroot checks every\n"
           " * line against the firmware; tests/cr_ep_test.c --gen prints this file again with the new cells appended. */\n");
    printf("#ifndef CR_EP_IDS_H\n#define CR_EP_IDS_H\nstatic const struct {\n    uint16_t pid;\n    const char *key;\n"
           "    int16_t min, max, def;\n    uint8_t list, retired;\n} CR_EP_IDS[] = {\n");
    for (i = 0; i < NELEM(CR_EP_IDS); i++)
        if (CR_EP_IDS[i].pid) {
            printf("    {%u, \"%s\", %d, %d, %d, %u, %u},\n", CR_EP_IDS[i].pid, CR_EP_IDS[i].key, CR_EP_IDS[i].min,
                   CR_EP_IDS[i].max, CR_EP_IDS[i].def, CR_EP_IDS[i].list, CR_EP_IDS[i].retired);
            n++;
        }
    for (j = 0; j < cep_np; j++) {
        for (i = 0; i < NELEM(CR_EP_IDS) && CR_EP_IDS[i].pid != cep_par[j].pid; i++)
            ;
        if (i < NELEM(CR_EP_IDS) && CR_EP_IDS[i].pid)
            continue;
        printf("    {%u, \"%s\", %d, %d, %d, %u, 0},\n", cep_par[j].pid, cep_key_of(j), cep_par[j].min, cep_par[j].max,
               cep_par[j].def, (cep_par[j].flags & RFM1_EPF_LIST) ? 1u : 0u);
        n++;
    }
    if (!n)
        printf("    {0, 0, 0, 0, 0, 0, 0},\n");
    printf("};\n#endif\n");
}
static void dump(void)
{
    uint32_t e, p, k, tot = 0;
    static const char *const F[6] = {"INT", "FIXED", "TABLE", "NOTE", "PCT", "ONOFF"};
    for (e = 0; e < CEP_NENG; e++) {
        printf("engine %u id %u %s: %u pages\n", e, cep_eng[e].id, cep_eng[e].name, cep_eng[e].npages);
        for (p = 0; p < cep_eng[e].npages; p++) {
            const rfm1_ep_page_t *g = &cep_eng[e].pages[p];
            printf("  page %u \"%s\" group %u\n", g->id, g->name, g->group);
            for (k = 0; k < 8; k++) {
                const int x = g->pid[k] ? cep_find(g->pid[k]) : -1;
                if (x < 0) {
                    printf("    %u -\n", k + 1);
                    continue;
                }
                tot++;
                printf("    %u pid %u \"%s\" %d..%d def %d %s%s%s%s step %u [%s]\n", k + 1, cep_par[x].pid,
                       cep_par[x].name, cep_par[x].min, cep_par[x].max, cep_par[x].def, F[cep_par[x].fmt % 6],
                       (cep_par[x].flags & RFM1_EPF_BIPOLAR) ? " bip" : "", (cep_par[x].flags & RFM1_EPF_LIST) ? " list" : "",
                       (cep_par[x].flags & RFM1_EPF_DYN) ? " dyn" : "", cep_par[x].step, cep_key_of((uint32_t)x));
            }
        }
    }
    printf("%u knob slots, %u params\n", tot, cep_np);
}

/* ------------------------------------------------------------------------------------------- 2. the writes --- */
typedef struct {
    track_t trk[NTRK];
    cu_snd_t psnd[2];
    __typeof__(cs) cs;
    uint8_t blob[2][ENG_BLOB_MAX];
} state_t;
static void state_take(state_t *s)
{
    uint32_t p;
    memset(s, 0, sizeof *s);
    memcpy(s->trk, trk, sizeof trk);
    memcpy(s->psnd, psnd, sizeof psnd);
    memcpy(&s->cs, &cs, sizeof cs);
    for (p = 0; p < 2u; p++) {
        const track_t *t = cep_trk((int)p);
        const eng_deep_t *d = cp_deep(t);
        if (d && d->blob_get)
            d->blob_get(t, s->blob[p]);
    }
}
static const char *state_diff(const state_t *a, const state_t *b)
{
    if (memcmp(a->trk, b->trk, sizeof a->trk))
        return "trk[]";
    if (memcmp(a->psnd, b->psnd, sizeof a->psnd))
        return "psnd[]";
    if (memcmp(&a->cs, &b->cs, sizeof a->cs))
        return "cs";
    if (memcmp(a->blob, b->blob, sizeof a->blob))
        return "the engine's patch";
    return 0;
}
/* a child: one of the two writes, its state into the pipe */
static void child(int fd, int how, int part, const cep_at_t *at, int x, int32_t v, int s)
{
    state_t st;
    int32_t landed = v;
    if (how == 0) {                    /* the editor's knob: the cell shown, the finest step, one detent */
        ce.part = (uint8_t)part;
        cx.grp[part] = at->g;
        cx.scr[part][at->g] = at->scr;
        cx.lane[part][at->g] = at->ln;
        fm1_kn_fine();
        ce_knob(at->col, s, 0);
        fm1_kn_back();
        landed = cep_get(part, cep_par[x].pid);
    } else
        cep_set(part, cep_par[x].pid, v);
    state_take(&st);
    if (write(fd, &landed, sizeof landed) != sizeof landed || write(fd, &st, sizeof st) != (ssize_t)sizeof st)
        _exit(2);
    _exit(0);
}
static int fork_write(int how, int part, const cep_at_t *at, int x, int32_t v, int s, state_t *out, int32_t *landed)
{
    int fd[2], st = 0;
    pid_t pid;
    size_t got = 0;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    pid = fork();
    if (!pid) {
        close(fd[0]);
        child(fd[1], how, part, at, x, v, s);
    }
    close(fd[1]);
    if (read(fd[0], landed, sizeof *landed) != sizeof *landed)
        return 0;
    while (got < sizeof *out) {
        const ssize_t n = read(fd[0], (char *)out + got, sizeof *out - got);
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    close(fd[0]);
    waitpid(pid, &st, 0);
    return got == sizeof *out && WIFEXITED(st) && !WEXITSTATUS(st);
}
/* place pid's value at v quietly (the pre-state): the raw write, no edit mark (psnd put back) */
static void place(int part, int x, int32_t v)
{
    const cu_snd_t keep = psnd[part];
    const uint8_t user = cep_trk(part)->user;
    cep_set(part, cep_par[x].pid, v);
    psnd[part] = keep;
    cep_trk(part)->user = user;
}
static uint32_t n_cmp, n_skip, n_moved;
static void check_cell(int part, int x, const cep_at_t *at)
{
    track_t *t = cep_trk(part);
    const param_desc_t *d;
    int32_t vals[8], v0 = 0, mn, mx;
    int nv = 0, i, j;
    ce_view_t vw;
    ce_ref_t r = cep_x[x].r;
    if (!(d = ce_param(t, r, &v0)) && !cep_x[x].expr)
        return;
    if (cep_x[x].expr) {               /* (the slot set, so the editor's cell is the parameter) */
        const uint32_t src = r.a <= P_MVEL_SRC + 2u ? P_MVEL_SRC : P_MAT_SRC;
        if (!t->p[src])
            cep_set(part, cep_par[x].pid, cep_get(part, cep_par[x].pid));
        d = ce_param(t, r, &v0);
    }
    /* a matrix slot not in use shows its source only: in use first (the source 1), as a user would */
    ce_view_at(t, at->g, at->scr, at->ln, &vw);
    if (!ce_ref_eq(vw.ref[at->ln][at->col], r) && r.k == CE_R_DEEP) {
        cp_deep(t)->set(t, r.a, 0, 1);
        ce_view_at(t, at->g, at->scr, at->ln, &vw);
    }
    if (!ce_ref_eq(vw.ref[vw.active][at->col], r) || vw.active != at->ln) {
        n_skip++;
        CHECK(0, "part %d pid %u %s: the editor's knob (group %u screen %u lane %u col %u) is not this cell", part,
              cep_par[x].pid, cep_key_of((uint32_t)x), at->g, at->scr, at->ln, at->col);
        return;
    }
    mn = cep_par[x].min, mx = cep_par[x].max;
    {
        const int32_t cand[6] = {mn, mx, cep_par[x].def, mn + (mx - mn) / 2, mn + 1, mx - 1};
        for (i = 0; i < 6; i++) {
            int32_t v = cand[i];
            if (v < mn || v > mx || (d->fmt == F_ENUM && enum_orig(d, v) != v))
                continue;
            for (j = 0; j < nv && vals[j] != v; j++)
                ;
            if (j == nv)
                vals[nv++] = v;
        }
    }
    for (i = 0; i < nv; i++) {
        const int32_t v = vals[i], s = v > mn ? 1 : -1;
        int32_t pre = v - s, l1 = 0, l2 = 0;
        state_t a, b;
        const char *why;
        if (d->fmt == F_ENUM)          /* (an alias below: from the original under it) */
            while (pre > mn && pre < mx && enum_orig(d, pre) != pre)
                pre -= s;
        place(part, x, pre);
        if (!fork_write(0, part, at, x, v, s, &a, &l1)) {
            CHECK(0, "part %d pid %u: the editor's child failed", part, cep_par[x].pid);
            continue;
        }
        if (l1 != v)
            n_moved++;                 /* (the knob landed elsewhere: an alias, a ratio list, a pair: compared there) */
        if (!fork_write(1, part, at, x, l1, s, &b, &l2)) {
            CHECK(0, "part %d pid %u: the ep1 child failed", part, cep_par[x].pid);
            continue;
        }
        n_cmp++;
        why = state_diff(&a, &b);
        CHECK(!why && l1 == l2, "part %d %s pid %u %s (%s) = %d (from %d): ep1 SET and the editor's knob differ in %s "
              "(landed %d / %d)", part, ENGINES[eng_idx(t->eng_req)]->name, cep_par[x].pid, cep_key_of((uint32_t)x),
              cep_par[x].name, v, pre, why ? why : "the value", l1, l2);
    }
}
static void check_writes(void)
{
    int part, e, p, k;
    cs.bass_on = 1;
    for (part = 0; part < 2; part++)
        for (e = 0; e < (int)CEP_NENG; e++) {
            uint32_t n0 = n_cmp;
            cu_engine_switch((uint32_t)part, ENGINE_ORDER[e]);
            run_ms(40, 0);
            CHECK(eng_idx(cep_trk(part)->eng_req) == ENGINE_ORDER[e], "the engine switch to %s", cep_eng[e].name);
            for (p = 0; p < cep_eng[e].npages; p++)
                for (k = 0; k < 8; k++) {
                    const uint16_t pid = cep_eng[e].pages[p].pid[k];
                    const int x = pid ? cep_find(pid) : -1;
                    if (x >= 0)
                        check_cell(part, x, &cep_at[e][p][k]);
                }
            printf("%s %s: %u writes compared\n", CEP_LAYERS[part], cep_eng[e].name, n_cmp - n0);
        }
}

/* -------------------------------------------------------------------------------------------- 3. the reads --- */
static uint64_t play(int read_all)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    fm1panel_t *P = T->panel;
    run_ms(200, &h);
    P->key_vel[9] = P->key_vel[13] = P->key_vel[16] = 100;
    P->keys_tap |= (1u << 9) | (1u << 13) | (1u << 16);
    P->keys = (1u << 9) | (1u << 13) | (1u << 16);
    for (i = 0; i < 4000u; i += 16u) {
        if (i == 2500u)
            P->keys = 0;
        run_ms(16, &h);
        if (read_all) {                /* (as the shim after each frame: the layers, every value, the cells) */
            int l, e, ch, x, k;
            char nm[11], tx[11];
            for (l = 0; l < 2; l++) {
                cep_layer(l, &e, &ch);
                for (x = 0; x < cep_np; x++)
                    (void)cep_get(l, cep_par[x].pid);
                if (e >= 0)
                    for (k = 0; k < 8; k++)
                        if (cep_eng[e].pages[0].pid[k])
                            (void)cep_wordsf(l, cep_eng[e].pages[0].pid[k], nm, tx);
            }
        }
    }
    return h;
}
static void check_reads(void)
{
    uint64_t h[2] = {0, 0};
    int i;
    for (i = 0; i < 2; i++) {
        int fd[2], st;
        pid_t pid;
        if (pipe(fd))
            return;
        fflush(stdout);
        if (!(pid = fork())) {
            const uint64_t x = play(i);
            (void)!write(fd[1], &x, sizeof x);
            _exit(0);
        }
        close(fd[1]);
        if (read(fd[0], &h[i], sizeof h[i]) != sizeof h[i])
            h[i] = (uint64_t)i;
        close(fd[0]);
        waitpid(pid, &st, 0);
    }
    printf("audio: %016llx without reads, %016llx with every value read after every frame\n", (unsigned long long)h[0],
           (unsigned long long)h[1]);
    CHECK(h[0] == h[1], "the audio differs when the values are read");
}

int main(int argc, char **argv)
{
    const int gen = argc > 1 && !strcmp(argv[1], "--gen"), dmp = argc > 1 && !strcmp(argv[1], "--dump");
    T = fm1core_get(FM1CORE_ABI);
    if (!T)
        return 2;
    T->options(0, 1, 0, 1);
    if (T->boot_options)
        T->boot_options(-1, 0, -1);
    T->panel->ready = 2;
    T->init(0);
    run_ms(3000, 0);
    cep_gen = gen;
    if (!fm1core_ep()) {
        printf("FAIL: no description\n");
        return 1;
    }
    if (gen) {
        gen_ids();
        return 0;
    }
    if (dmp) {
        dump();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "--screens")) {   /* (the editor's screens of an engine: the walk's input) */
        static track_t tt;
        uint32_t g, k, ns, i;
        ce_scr_t sc;
        tt.eng_req = (uint8_t)atoi(argv[2]);
        for (g = 0; g < CE_NSEC; g++) {
            ns = ce_scr(&tt, g, 0, &sc);
            for (k = 0; k < ns; k++) {
                ce_scr(&tt, g, k, &sc);
                printf("group %u screen %u type %u n %u hs %u:", g, k, sc.type, sc.n, sc.hs);
                for (i = 0; i < sc.n && sc.type != CE_S_PLAT && sc.type != CE_S_EXPR; i++)
                    printf(" %u \"%s\"", sc.pg[i], cp_deep(&tt)->pages[sc.pg[i]].title);
                printf("\n");
            }
        }
        return 0;
    }
    {
        uint32_t e, np = 0;
        for (e = 0; e < CEP_NENG; e++)
            np += cep_eng[e].npages;
        printf("description: 2 layers, %u engines, %u pages, %u params (%u shown as numbers: no format gives their "
               "words and they are too many for a table)\n", (unsigned)CEP_NENG, np, cep_np, cep_approx);
    }
    check_ids();
    check_reads();
    check_writes();
    printf("%u writes compared (%u where the knob landed beside the value asked), %u cells skipped\n", n_cmp, n_moved,
           n_skip);
    printf(fails ? "rfm1_ep_choralroot: %d FAILED\n" : "rfm1_ep_choralroot: all passed%.0d\n", fails);
    return fails ? 1 : 0;
}
