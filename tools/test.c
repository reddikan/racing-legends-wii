/*
 * RACING LEGENDS - native test harness.
 *   gtest out shots [track]        screenshots of races (with the HUD) and the menus
 *   gtest out bots [track]         full races with a bot in the player's seat, at several upgrade levels
 *   gtest out reel track           frames for a preview video (out/reel/f0000.ppm ...)
 */
#include "../source/game_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "../source/audio.h"

void swr_render(void);
void swr_save_ppm(const char *path);
extern long swr_tris_drawn;

static const char *OUT = "tools/out";

static void shot(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s.ppm", OUT, name);
    game_render();
    swr_render();
    swr_save_ppm(p);
}

/* a bot that drives the player's car through the real controls */
static float bot_skill = 0.96f;
static int bot_drift = 1;
static void bot_input(Input *in, int drift_ok)
{
    memset(in, 0, sizeof *in);
    Car *c = &cars[0];
    float sk = c->skill, ln = c->lane;
    c->skill = bot_skill; c->lane = 0;
    int was = c->ai;
    car_ai_input(c, 1.0f / 60.0f);
    c->ai = was; c->skill = sk; c->lane = ln;
    in->sx = c->in.steer;
    if (c->in.throttle > 0.5f) in->held |= BTN_A;
    if (c->in.brake > 0.3f || (drift_ok && c->in.drift)) in->held |= BTN_L;
    in->lt = (c->in.brake > 0.3f || (drift_ok && c->in.drift)) ? 1.0f : 0.0f;
    if (c->in.nitro) in->pressed |= BTN_R;
}

/* a small software mixer that does what main.c does with ASND, for the preview video's soundtrack */
static FILE *wav;
static long wav_n;
typedef struct { int snd; double pos; float rate, vl, vr; } Voice;
static Voice vo[16];
static void mix_frame(void)
{
    LoopState L[NUM_LOOPS];
    game_loops(L);
    for (int i = 0; i < NUM_LOOPS; i++) {
        if (L[i].snd != vo[i].snd) { vo[i].snd = L[i].snd; vo[i].pos = 0; }
        float v = L[i].snd >= 0 ? L[i].vol * audio_gain(L[i].snd) : 0;
        vo[i].rate = L[i].rate; vo[i].vl = v * (L[i].pan > 0 ? 1 - L[i].pan * 0.6f : 1); vo[i].vr = v * (L[i].pan < 0 ? 1 + L[i].pan * 0.6f : 1);
    }
    int id; float r, v, p;
    while (game_pop_sound(&id, &r, &v, &p)) {
        int k = NUM_LOOPS;
        for (int i = NUM_LOOPS; i < 16; i++) if (vo[i].snd < 0) { k = i; break; }
        vo[k].snd = id; vo[k].pos = 0; vo[k].rate = r;
        float gg = v * audio_gain(id);
        vo[k].vl = gg * (p > 0 ? 1 - p * 0.6f : 1); vo[k].vr = gg * (p < 0 ? 1 + p * 0.6f : 1);
    }
    if (!wav) return;
    for (int n = 0; n < AUDIO_RATE / 60; n++) {
        float l = 0, rr = 0;
        for (int i = 0; i < 16; i++) {
            Voice *q = &vo[i];
            if (q->snd < 0) continue;
            int len = audio_len(q->snd);
            int i0 = (int)q->pos;
            if (i0 >= len) { if (audio_is_loop(q->snd) && i < NUM_LOOPS) { q->pos -= len; i0 = (int)q->pos; } else { q->snd = -1; continue; } }
            float s0 = audio_data(q->snd)[i0] / 32768.0f;
            l += s0 * q->vl * 0.45f; rr += s0 * q->vr * 0.45f;
            q->pos += q->rate;
        }
        short o[2] = { (short)(fmaxf(-1, fminf(1, l)) * 32000), (short)(fmaxf(-1, fminf(1, rr)) * 32000) };
        fwrite(o, 2, 2, wav); wav_n++;
    }
}
static void wav_open(const char *path)
{
    wav = fopen(path, "wb");
    for (int i = 0; i < 16; i++) vo[i].snd = -1;
    unsigned char h[44] = { 0 };
    fwrite(h, 1, 44, wav);
}
static void wav_close(void)
{
    long bytes = wav_n * 4;
    unsigned char h[44] = { 'R','I','F','F' };
    unsigned int v;
    v = (unsigned)(36 + bytes); memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    unsigned short s16 = 1; memcpy(h + 20, &s16, 2); s16 = 2; memcpy(h + 22, &s16, 2);
    v = AUDIO_RATE; memcpy(h + 24, &v, 4); v = AUDIO_RATE * 4; memcpy(h + 28, &v, 4);
    s16 = 4; memcpy(h + 32, &s16, 2); s16 = 16; memcpy(h + 34, &s16, 2);
    memcpy(h + 36, "data", 4); v = (unsigned)bytes; memcpy(h + 40, &v, 4);
    fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav); wav = NULL;
}
static void step(const Input *in) { game_update(in, 1.0f / 60.0f); mix_frame(); }

static void run_race(int track, int laps, const uint8_t *lv, int verbose, float *out_place, float *out_time)
{
    memcpy(sv.lv, lv, NUM_UPGRADES);
    sv.unlocked = 5;
    sv.laps = (uint8_t)laps;
    g.loading_track = track;
    g.state = ST_LOADING; g.loading_frames = 0;
    Input in;
    memset(&in, 0, sizeof in);
    for (int i = 0; i < 4; i++) step(&in);
    /* skip the intro */
    in.pressed = BTN_A; step(&in); in.pressed = 0;
    int ai_off = 0, frames = 0, wall_frames = 0, off_frames = 0, drift_frames = 0;
    while (g.state == ST_RACE && g.phase != RP_DONE && frames < 60 * 60 * 6) {
        bot_input(&in, bot_drift);
        step(&in);
        frames++;
        if (cars[0].impact > 0) wall_frames++;
        if (cars[0].offroad) { off_frames++; if (off_frames % 30 == 1 && getenv("DBG")) printf("    off: sample %d lat %.1f hw %.1f line %.1f speed %.1f drifting %d steer %.2f curv %.4f t %.1f\n", cars[0].sample, cars[0].lat, trk.s[cars[0].sample].hw, trk.s[cars[0].sample].line, cars[0].speed, cars[0].drifting, cars[0].in.steer, trk.s[cars[0].sample].curv, g.race_t); }
        if (getenv("DBG2") && g.race_t > 76.5f && g.race_t < 81.0f && frames % 6 == 0)
            printf("    t %.2f s %d lat %.1f line %.1f v %.1f vline %.1f steer %.2f drift %d boost %.2f mini %.2f imp %.2f\n", g.race_t, cars[0].sample, cars[0].lat,
                   trk.s[cars[0].sample].line, cars[0].speed, trk.s[cars[0].sample].vline, cars[0].in.steer, cars[0].drifting, cars[0].boost_t, cars[0].mini_t, cars[0].impact);
        if (cars[0].drifting) drift_frames++;
        for (int q = 1; q < ncars; q++) if (cars[q].offroad) ai_off++;
    }
    Car *pl = &cars[0];
    if (verbose) {
        printf("  track %d lv[%d %d %d %d %d %d]: place %d  time %.1f  best lap %.1f  drift %.0f  wall %d off %d drifting %d frames\n", track,
               lv[0], lv[1], lv[2], lv[3], lv[4], lv[5], pl->place, pl->finish_time, pl->best_lap, pl->drift_total, wall_frames, off_frames, drift_frames);
        printf("  ai offroad frames per car %d\n", ai_off / 7);
        for (int i = 0; i < ncars; i++) {
            const Car *c = &cars[g.order[i]];
            printf("     %d. %-12s lap %d best %.1f  vmax %.1f grip %.1f skill %.3f\n", i + 1, c->name, c->lap, c->best_lap, c->st.vmax, c->st.grip, c->skill);
        }
    }
    *out_place = (float)pl->place;
    *out_time = pl->finished ? pl->finish_time : -1;
    /* let the results come up */
    for (int i = 0; i < 60 * 6 && g.state != ST_RESULTS; i++) { memset(&in, 0, sizeof in); step(&in); }
}

int main(int argc, char **argv)
{
    if (argc > 1) OUT = argv[1];
    const char *mode = argc > 2 ? argv[2] : "shots";
    int only = argc > 3 ? atoi(argv[3]) : -1;
    clock_t t0 = clock();
    audio_init();
    game_init();
    game_storage(1);
    printf("init %.2fs\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    Input in;
    memset(&in, 0, sizeof in);

    if (!strcmp(mode, "shots")) {
        for (int i = 0; i < 90; i++) step(&in);
        shot("menu_title");
        in.pressed = BTN_A; step(&in); in.pressed = 0;
        sv.unlocked = 3; sv.best_place[0] = 1; sv.best_lap[0] = 41.2f;
        for (int i = 0; i < 20; i++) step(&in);
        shot("menu_tracks");
        in.pressed = BTN_X; step(&in); in.pressed = 0;
        sv.credits = 2400;
        for (int i = 0; i < 30; i++) step(&in);
        shot("menu_garage");
        in.pressed = BTN_R; step(&in); in.pressed = 0;
        in.pressed = BTN_R; step(&in); in.pressed = 0;
        for (int i = 0; i < 10; i++) step(&in);
        shot("menu_paint");
        uint8_t lv[NUM_UPGRADES] = { 2, 2, 2, 1, 2, 2 };
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (only >= 0 && t != only) continue;
            memcpy(sv.lv, lv, sizeof lv);
            g.loading_track = t; g.state = ST_LOADING; g.loading_frames = 0;
            clock_t tl = clock();
            for (int i = 0; i < 4; i++) step(&in);
            printf("track %d load %.2fs, %d static tris, %d lights\n", t, (double)(clock() - tl) / CLOCKS_PER_SEC, sm_tri_count(), trk.nlight);
            char name[64];
            for (int i = 0; i < 70; i++) step(&in);
            sprintf(name, "race%d_intro", t); shot(name);
            in.pressed = BTN_A; step(&in); in.pressed = 0;
            for (int i = 0; i < 100; i++) step(&in);
            sprintf(name, "race%d_count", t); shot(name);
            int f = 0, k = 0;
            const int at[4] = { 60 * 6, 60 * 14, 60 * 23, 60 * 33 };
            while (k < 4 && g.state == ST_RACE) {
                bot_input(&in, 1);
                step(&in);
                f++;
                if (f == at[k]) {
                    sprintf(name, "race%d_%d", t, k); shot(name);
                    printf("  shot %d: %d static visible, %d dyn verts, %ld rasterized, speed %.1f drifting %d\n", k, sm_visible_tris(), r_nv, swr_tris_drawn, cars[0].speed, cars[0].drifting);
                    k++;
                }
            }
        }
        return 0;
    }
    if (!strcmp(mode, "bots")) {
        const uint8_t LV[4][NUM_UPGRADES] = { { 0, 0, 0, 0, 0, 0 }, { 1, 1, 1, 1, 1, 1 }, { 3, 3, 3, 2, 3, 2 }, { 5, 5, 5, 3, 5, 5 } };
        float skills[2] = { 0.93f, 0.98f };
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (only >= 0 && t != only) continue;
            for (int l = 0; l < 4; l++)
                for (int s = 0; s < 2; s++) {
                    bot_skill = skills[s];
                    float pl, tm;
                    run_race(t, 3, LV[l], s == 1 && (l == 0 || l == 3), &pl, &tm);
                    printf("track %d  upgrades %d  bot skill %.2f  ->  place %.0f  time %.1f\n", t, l, bot_skill, pl, tm);
                }
        }
        return 0;
    }
    if (!strcmp(mode, "fx")) {
        /* the code on the title screen, then a race: grab the 88 mph moment and a boost */
        for (int i = 0; i < 60; i++) step(&in);
        const uint32_t code[10] = { BTN_UP, BTN_UP, BTN_DOWN, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_B, BTN_A };
        for (int k = 0; k < 10; k++) { memset(&in, 0, sizeof in); in.pressed = code[k]; step(&in); memset(&in, 0, sizeof in); for (int i = 0; i < 6; i++) step(&in); }
        printf("secret %d unlock_t %.2f\n", sv.secret, g.unlock_t);
        for (int i = 0; i < 80; i++) step(&in);
        shot("fx_unlock");
        in.pressed = BTN_A; step(&in); in.pressed = 0;
        sv.body = BODY_FUTURE88;
        uint8_t lv[NUM_UPGRADES] = { 2, 2, 2, 2, 2, 2 };
        memcpy(sv.lv, lv, sizeof lv);
        g.loading_track = only >= 0 ? only : 3; g.state = ST_LOADING; g.loading_frames = 0;
        for (int i = 0; i < 4; i++) step(&in);
        in.pressed = BTN_A; step(&in); in.pressed = 0;
        int got88 = 0, gotb = 0, n88 = 0, nb = 0;
        float minspeed = 1e9f;
        for (int f = 0; f < 60 * 60 && g.state == ST_RACE; f++) {
            if (g.phase == RP_RACE) bot_input(&in, 1); else { memset(&in, 0, sizeof in); if (g.phase == RP_COUNT) { if (cars[0].speed < minspeed) minspeed = cars[0].speed; } }
            /* pre-race check: nobody may move */
            if (g.phase != RP_RACE) for (int q = 0; q < ncars; q++) if (fabsf(cars[q].vx) + fabsf(cars[q].vz) > 0.01f) { printf("car %d moving before GO!\n", q); break; }
            float prev88 = g.mph88_t, prevb = g.boost_anim;
            step(&in);
            if (g.mph88_t > 0 && prev88 == 0) n88++;
            if (g.boost_anim > prevb + 0.3f) nb++;
            if (!got88 && g.mph88_t > 0.18f) { shot("fx_88mph"); got88 = 1; }
            if (!gotb && g.boost_anim > 0.75f && g.boost_anim < 0.85f) { shot("fx_boost"); gotb = 1; }
        }
        printf("88 mph moments %d, boosts %d\n", n88, nb);
        return 0;
    }
    if (!strcmp(mode, "cmp")) {
        uint8_t lv[NUM_UPGRADES] = { 0, 0, 0, 0, 0, 0 };
        for (int d = 0; d < 2; d++) {
            bot_drift = d; bot_skill = 0.98f;
            float pl, tm;
            run_race(only >= 0 ? only : 0, 3, lv, 1, &pl, &tm);
        }
        return 0;
    }
    if (!strcmp(mode, "reel")) {
        int t = only >= 0 ? only : 0;
        uint8_t lv[NUM_UPGRADES] = { 3, 3, 3, 2, 3, 3 };
        memcpy(sv.lv, lv, sizeof lv);
        g.loading_track = t; g.state = ST_LOADING; g.loading_frames = 0;
        for (int i = 0; i < 4; i++) step(&in);
        int n = 0;
        char p[256];
        snprintf(p, sizeof p, "%s/reel/audio.wav", OUT);
        wav_open(p);
        for (int f = 0; f < 60 * 34; f++) {
            if (g.phase == RP_INTRO || g.phase == RP_COUNT) memset(&in, 0, sizeof in);
            else bot_input(&in, 1);
            if (g.phase == RP_COUNT && g.phase_t > 2.4f) { in.held |= BTN_A; if (g.phase_t < 2.43f) in.pressed |= BTN_A; }
            step(&in);
            if (f % 2 == 0) {
                game_render(); swr_render();
                snprintf(p, sizeof p, "%s/reel/f%04d.ppm", OUT, n++);
                swr_save_ppm(p);
            }
        }
        wav_close();
        return 0;
    }
    if (!strcmp(mode, "audio")) {
        audio_init();
        int total = 0;
        for (int i = 0; i < SND_COUNT; i++) {
            const int16_t *d = audio_data(i);
            int n = audio_len(i), peak = 0; double rms = 0;
            for (int k = 0; k < n; k++) { int a = abs(d[k]); if (a > peak) peak = a; rms += (double)d[k] * d[k]; }
            printf("snd %2d: %6d samples %s peak %5d rms %6.0f\n", i, n, audio_is_loop(i) ? "loop" : "    ", peak, sqrt(rms / (n ? n : 1)));
            total += n;
        }
        printf("pool %d bytes\n", audio_pool_bytes());
        return 0;
    }
    return 0;
}
