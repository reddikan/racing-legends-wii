/*
 * RACING LEGENDS - sound synthesis. See audio.h.
 */
#include "audio.h"
#include "game.h"
#include "r3d.h"

#include <math.h>
#include <string.h>

#define SR ((float)AUDIO_RATE)
#define TAU_F 6.2831853f

#define POOL_SAMPLES (1250000)
static int16_t POOL[POOL_SAMPLES] __attribute__((aligned(32)));
static int pool_used;
static int s_off[SND_COUNT], s_len[SND_COUNT], s_loop[SND_COUNT];
static float s_gain[SND_COUNT];

#define MAXN 100000
static float *buf, *tmp, *tmp2;           /* work buffers, MAXN floats each */
static uint32_t seed = 0x1234567u;

static float frand(void)            /* -1..1 */
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float)(seed >> 8) / 8388608.0f - 1.0f;
}

/* ------------------------------------------------------------------ */
/* filters                                                             */
/* ------------------------------------------------------------------ */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } Bq;

static void bq_set(Bq *f, int type, float fc, float q)
{
    if (fc > SR * 0.45f) fc = SR * 0.45f;
    if (fc < 10.0f) fc = 10.0f;
    float w = TAU_F * fc / SR, cw = cosf(w), sw = sinf(w), al = sw / (2.0f * q);
    float b0, b1, b2, a0, a1, a2;
    switch (type) {
    case 0: /* low pass */
        b0 = (1 - cw) * 0.5f; b1 = 1 - cw; b2 = (1 - cw) * 0.5f; break;
    case 1: /* band pass, 0 dB peak */
        b0 = al; b1 = 0; b2 = -al; break;
    default: /* high pass */
        b0 = (1 + cw) * 0.5f; b1 = -(1 + cw); b2 = (1 + cw) * 0.5f; break;
    }
    a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0; f->a1 = a1 / a0; f->a2 = a2 / a0;
}
static void bq_init(Bq *f, int type, float fc, float q) { memset(f, 0, sizeof *f); bq_set(f, type, fc, q); }
static inline float bq(Bq *f, float x)
{
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
    return y;
}

/* ------------------------------------------------------------------ */
/* storing                                                             */
/* ------------------------------------------------------------------ */
static void store(int id, float *x, int n, int loop, float gain)
{
    n = (n + 15) & ~15;
    if (n > MAXN) n = MAXN;
    if (pool_used + n > POOL_SAMPLES) n = (POOL_SAMPLES - pool_used) & ~15;
    float peak = 1e-6f;
    double rms = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(x[i]); if (a > peak) peak = a; rms += (double)x[i] * x[i]; }
    rms = sqrt(rms / (double)(n > 0 ? n : 1));
    /* very peaky sounds get a soft compression so they're not lost under the engine */
    if (peak / (rms + 1e-9) > 5.0) {
        float k = 1.0f / peak, g = 2.2f, tg = tanhf(g);
        for (int i = 0; i < n; i++) x[i] = tanhf(x[i] * k * g) / tg;
        peak = 1.0f;
    }
    float k = 0.92f * 32767.0f / peak;
    int16_t *d = &POOL[pool_used];
    for (int i = 0; i < n; i++) {
        float v = x[i] * k;
        if (!loop) {
            /* tiny fades so nothing clicks */
            if (i < 32) v *= (float)i / 32.0f;
            if (n - i < 64) v *= (float)(n - i) / 64.0f;
        }
        int s = (int)v;
        d[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
    s_off[id] = pool_used; s_len[id] = n; s_loop[id] = loop; s_gain[id] = gain;
    pool_used += n;
    pool_used = (pool_used + 15) & ~15;
}

/* make a buffer of length n (with `fade` extra samples generated after it) loop seamlessly */
static void loopify(float *x, int n, int fade)
{
    for (int i = 0; i < fade; i++) {
        float t = (float)i / (float)fade;
        x[i] = x[i] * t + x[n + i] * (1.0f - t);
    }
}

static void clear(int n) { memset(buf, 0, sizeof(float) * (size_t)n); }

/* ------------------------------------------------------------------ */
/* hover turbines: one second loops, every partial a whole number of Hz so they loop seamlessly          */
/* ------------------------------------------------------------------ */
typedef struct { float hum, whine, blade, air, buzz, detune, lp; } Turbine;
static const Turbine TURB[4] = {
    /* hum (Hz), whine, blade pass, air band, mag buzz, detune, low pass */
    { 110, 440, 1320, 900, 120, 0, 5200 },        /* COMET: a clean, mid turbine */
    { 165, 660, 1980, 1400, 180, 0, 7000 },       /* VIPER: higher and sharper */
    { 55, 275, 825, 600, 60, 0, 3200 },           /* TITAN: deep, heavy */
    { 82, 328, 984, 760, 82, 1, 4200 },           /* FUTURE 88: a deep chorused synth hum */
};

static void turbine(int id, int type, int on)
{
    const int N = 32000;
    const Turbine *T = &TURB[type];
    seed = 0x5151u + (uint32_t)(type * 31 + on);
    static float nz[32000];
    for (int i = 0; i < N; i++) nz[i] = frand();
    Bq air, lp, hp, crk;
    bq_init(&air, 1, T->air, 1.4f);
    bq_init(&lp, 0, on ? T->lp : T->lp * 0.55f, 0.707f);
    bq_init(&hp, 2, 30.0f, 0.707f);
    bq_init(&crk, 1, 3200.0f, 3.0f);
    /* run twice through the filters (steady state), keep the second second */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < N; i++) {
            float t = (float)i / SR;
            float v = 0;
            /* the hum: fundamental + 2nd + 3rd, gently pulsing at 2 Hz (whole cycles per second) */
            float pulse = 0.85f + 0.15f * sinf(TAU_F * 2.0f * t);
            v += (sinf(TAU_F * T->hum * t) * 0.9f + sinf(TAU_F * T->hum * 2.0f * t) * 0.45f + sinf(TAU_F * T->hum * 3.0f * t) * 0.2f) * pulse;
            if (T->detune) {
                /* a second, slightly detuned saw-ish voice: the chorus */
                float f2 = T->hum + 1.0f;
                for (int h = 1; h <= 6; h++) v += sinf(TAU_F * f2 * (float)h * t) * 0.35f / (float)h;
            }
            /* the turbine whine and the blade-pass tone above it */
            float wv = sinf(TAU_F * T->whine * t) * 0.5f + sinf(TAU_F * T->whine * 2.0f * t) * 0.12f;
            float bl = sinf(TAU_F * T->blade * t) * (0.18f + 0.08f * sinf(TAU_F * 7.0f * t));
            v += (wv + bl) * (on ? 1.0f : 0.45f);
            /* the mag-lev field: a soft buzz */
            float ph = T->buzz * t; ph -= floorf(ph);
            v += (ph < 0.5f ? 0.12f : -0.12f) * (on ? 0.6f : 0.35f);
            /* rushing air and a faint crackle of the field */
            v += bq(&air, nz[i]) * (on ? 1.4f : 1.0f);
            v += bq(&crk, nz[(i * 13) % N]) * 0.25f;
            v = tanhf(v * (on ? 1.3f : 1.0f));
            v = bq(&lp, v);
            v = bq(&hp, v);
            if (pass) buf[i] = v;
        }
    store(id, buf, N, 1, on ? 0.9f : 0.75f);
}

/* ------------------------------------------------------------------ */
/* loops                                                               */
/* ------------------------------------------------------------------ */
/* the drift: the mag field scraping sideways - an electric sizzle with crackles and a shimmering whistle */
static void skid_loop(void)
{
    const int N = 32000, F = 3000;
    clear(N + F);
    seed = 991;
    Bq b1, b2, b3;
    bq_init(&b1, 1, 2600.0f, 2.2f);
    bq_init(&b2, 1, 5200.0f, 3.0f);
    bq_init(&b3, 0, 400.0f, 0.8f);
    float crack = 0, ph = 0, wob = 0, wv = 0;
    for (int i = 0; i < N + F; i++) {
        if (frand() > 0.985f) crack = 0.7f + 0.3f * frand();
        crack *= 0.9f;
        wv += frand() * 0.003f; wv *= 0.996f; wob += wv; wob *= 0.9995f;
        float f0 = 1900.0f + wob * 600.0f;
        ph += f0 / SR; ph -= floorf(ph);
        float n = frand();
        buf[i] = bq(&b1, n) * 1.2f + bq(&b2, n * crack * 6.0f) * 0.9f + sinf(ph * TAU_F) * 0.18f + bq(&b3, n) * 0.4f;
    }
    loopify(buf, N, F);
    store(SND_SKID, buf, N, 1, 0.75f);
}

static void gravel_loop(void)
{
    const int N = 32000, F = 3000;
    clear(N + F);
    seed = 4321;
    Bq lp, rum, clk;
    bq_init(&lp, 0, 1100.0f, 0.7f);
    bq_init(&rum, 0, 180.0f, 0.8f);
    bq_init(&clk, 1, 2600.0f, 2.0f);
    float click = 0;
    for (int i = 0; i < N + F; i++) {
        if (frand() > 0.993f) click = 0.6f + 0.4f * frand();
        click *= 0.93f;
        float n = frand();
        buf[i] = bq(&lp, n) * 0.6f + bq(&rum, n) * 1.6f + bq(&clk, n * click * 8.0f) * 0.9f;
    }
    loopify(buf, N, F);
    store(SND_GRAVEL, buf, N, 1, 0.8f);
}

static void nitro_loop(void)
{
    const int N = 32000, F = 3000;
    clear(N + F);
    seed = 777;
    Bq b1, hs, lo;
    bq_init(&b1, 1, 950.0f, 0.8f);
    bq_init(&hs, 2, 4200.0f, 0.7f);
    bq_init(&lo, 0, 220.0f, 0.9f);
    float ph = 0;
    for (int i = 0; i < N + F; i++) {
        float n = frand();
        float fl = 0.85f + 0.15f * sinf((float)i / SR * TAU_F * 23.0f) * sinf((float)i / SR * TAU_F * 3.1f);
        ph += 140.0f / SR; ph -= floorf(ph);
        buf[i] = (bq(&b1, n) * 1.2f + bq(&hs, n) * 0.35f + bq(&lo, n) * 2.2f + sinf(ph * TAU_F) * 0.25f) * fl;
    }
    loopify(buf, N, F);
    store(SND_NITRO_LOOP, buf, N, 1, 0.8f);
}

static void wind_loop(void)
{
    const int N = 32000, F = 4000;
    clear(N + F);
    seed = 5150;
    Bq lp, g;
    bq_init(&lp, 0, 520.0f, 0.7f);
    bq_init(&g, 1, 1300.0f, 1.0f);
    for (int i = 0; i < N + F; i++) {
        float n = frand();
        float m = 0.7f + 0.3f * sinf((float)i / SR * TAU_F * 0.9f);
        buf[i] = (bq(&lp, n) * 1.5f + bq(&g, n) * 0.4f) * m;
    }
    loopify(buf, N, F);
    store(SND_WIND, buf, N, 1, 0.6f);
}

static void scrape_loop(void)
{
    const int N = 24000, F = 2400;
    clear(N + F);
    seed = 8080;
    Bq b1, b2, lp;
    bq_init(&b1, 1, 2400.0f, 5.0f);
    bq_init(&b2, 1, 4100.0f, 6.0f);
    bq_init(&lp, 0, 300.0f, 0.8f);
    float sq = 0, sf = 0, sp = 0;
    for (int i = 0; i < N + F; i++) {
        float n = frand();
        float ring = 0.6f + 0.4f * sinf((float)i / SR * TAU_F * 61.0f);
        if (frand() > 0.9995f) { sq = 1.0f; sf = 3000.0f + 2000.0f * (frand() * 0.5f + 0.5f); }
        sq *= 0.9993f;
        sp += sf / SR; sp -= floorf(sp); sf *= 0.99998f;
        buf[i] = (bq(&b1, n) * 1.2f + bq(&b2, n) * 0.8f) * ring + bq(&lp, n) * 0.8f + sinf(sp * TAU_F) * sq * 0.35f;
    }
    loopify(buf, N, F);
    store(SND_SCRAPE, buf, N, 1, 0.7f);
}

/* ------------------------------------------------------------------ */
/* one-shots                                                           */
/* ------------------------------------------------------------------ */
static void add_tone(float t0, float dur, float f0, float f1, float vol, float atk, float decay, int harm)
{
    if (t0 < 0) t0 = 0;
    int s0 = (int)(t0 * SR), n = (int)(dur * SR);
    float ph = 0;
    for (int i = 0; i < n && s0 + i < MAXN; i++) {
        float t = (float)i / SR;
        float f = f0 * powf(f1 / f0, (float)i / (float)n);
        ph += f / SR; ph -= floorf(ph);
        float e = (atk > 0 ? fminf(1.0f, t / atk) : 1.0f) * (decay > 0 ? expf(-t / decay) : 1.0f);
        float v = sinf(ph * TAU_F);
        if (harm) { float p2 = ph * 2 - floorf(ph * 2), p3 = ph * 3 - floorf(ph * 3); v += sinf(p2 * TAU_F) * 0.45f + sinf(p3 * TAU_F) * 0.25f; }
        buf[s0 + i] += v * e * vol;
    }
}

static void add_noise(float t0, float dur, int type, float f0, float f1, float q, float vol, float atk, float decay)
{
    if (t0 < 0) t0 = 0;
    int s0 = (int)(t0 * SR), n = (int)(dur * SR);
    Bq f;
    bq_init(&f, type, f0, q);
    for (int i = 0; i < n && s0 + i < MAXN; i++) {
        float t = (float)i / SR;
        if ((i & 31) == 0) bq_set(&f, type, f0 * powf(f1 / f0, (float)i / (float)n), q);
        float e = (atk > 0 ? fminf(1.0f, t / atk) : 1.0f) * (decay > 0 ? expf(-t / decay) : 1.0f);
        buf[s0 + i] += bq(&f, frand()) * e * vol;
    }
}

/* a struck metal panel: inharmonic partials */
static void add_metal(float t0, float f, float vol, float decay)
{
    const float R[6] = { 1.0f, 2.32f, 3.86f, 5.1f, 6.7f, 8.9f };
    for (int k = 0; k < 6; k++) add_tone(t0, decay * 5.0f, f * R[k], f * R[k] * 0.98f, vol / (1.0f + (float)k * 0.7f), 0.0005f, decay / (1.0f + (float)k * 0.4f), 0);
}

static int secs(float s) { return (int)(s * SR); }

static void oneshots(void)
{
    /* light impact: thump, crunch, a clank */
    clear(secs(0.8f)); seed = 11;
    add_tone(0, 0.4f, 95, 55, 1.0f, 0.002f, 0.08f, 0);
    add_noise(0, 0.25f, 1, 1800, 600, 0.9f, 1.0f, 0.001f, 0.05f);
    add_noise(0, 0.5f, 0, 400, 200, 0.7f, 0.8f, 0.002f, 0.1f);
    add_metal(0.005f, 380, 0.25f, 0.06f);
    store(SND_IMPACT_S, buf, secs(0.8f), 0, 0.9f);
    /* heavy impact: bigger, with debris */
    clear(secs(1.6f)); seed = 12;
    add_tone(0, 0.7f, 75, 40, 1.2f, 0.002f, 0.15f, 0);
    add_noise(0, 0.5f, 1, 2400, 500, 0.8f, 1.3f, 0.001f, 0.09f);
    add_noise(0, 0.9f, 0, 500, 150, 0.7f, 1.0f, 0.002f, 0.2f);
    add_metal(0.004f, 290, 0.35f, 0.12f);
    add_metal(0.03f, 510, 0.2f, 0.08f);
    for (int k = 0; k < 9; k++) add_metal(0.12f + (float)k * 0.07f + frand() * 0.03f, 1800 + 900 * frand(), 0.05f, 0.03f);
    store(SND_IMPACT_L, buf, secs(1.6f), 0, 1.0f);
    /* nitro: a thump and a rising rush */
    clear(secs(1.3f)); seed = 13;
    add_tone(0, 0.3f, 70, 45, 0.8f, 0.003f, 0.08f, 0);
    add_tone(0, 0.4f, 300, 1800, 0.35f, 0.002f, 0.1f, 1);                  /* the field charging */
    add_noise(0, 1.2f, 1, 300, 2600, 0.7f, 1.4f, 0.04f, 0.45f);
    add_noise(0.02f, 0.9f, 2, 3000, 6000, 0.7f, 0.5f, 0.01f, 0.3f);
    add_noise(0, 1.2f, 0, 180, 300, 0.8f, 1.2f, 0.02f, 0.5f);
    store(SND_NITRO_START, buf, secs(1.3f), 0, 0.95f);
    /* mini-turbo: blow-off valve chirp + whoosh */
    clear(secs(0.9f)); seed = 14;
    add_tone(0, 0.25f, 2600, 900, 0.35f, 0.002f, 0.08f, 0);
    add_noise(0, 0.25f, 1, 3500, 1200, 2.0f, 0.8f, 0.002f, 0.07f);
    add_noise(0.03f, 0.8f, 1, 500, 2000, 0.7f, 1.0f, 0.03f, 0.25f);
    store(SND_BOOST, buf, secs(0.9f), 0, 0.85f);
    /* drift tier up: a bright spark crackle with a rising ping */
    clear(secs(0.5f)); seed = 15;
    add_tone(0, 0.3f, 1400, 2400, 0.4f, 0.002f, 0.09f, 0);
    add_tone(0.06f, 0.3f, 2100, 3200, 0.3f, 0.002f, 0.08f, 0);
    add_noise(0, 0.35f, 2, 5000, 7000, 0.7f, 0.5f, 0.001f, 0.06f);
    store(SND_TIER, buf, secs(0.5f), 0, 0.6f);
    /* gear shift: clunk */
    clear(secs(0.25f)); seed = 16;
    add_noise(0, 0.15f, 1, 900, 500, 1.5f, 0.8f, 0.001f, 0.025f);
    add_tone(0, 0.12f, 140, 110, 0.5f, 0.001f, 0.03f, 0);
    store(SND_SHIFT, buf, secs(0.25f), 0, 0.5f);
    /* backfire: pop pop */
    clear(secs(0.6f)); seed = 17;
    for (int k = 0; k < 3; k++) {
        float t = (float)k * 0.075f + frand() * 0.015f;
        add_noise(t, 0.2f, 0, 1400, 300, 0.7f, k == 0 ? 1.5f : 0.9f, 0.0005f, 0.025f);
        add_tone(t, 0.12f, 90, 60, k == 0 ? 0.9f : 0.5f, 0.0005f, 0.03f, 0);
    }
    store(SND_BACKFIRE, buf, secs(0.6f), 0, 0.8f);
    /* landing thump with a spring creak */
    clear(secs(0.5f)); seed = 18;
    add_tone(0, 0.3f, 85, 50, 1.0f, 0.002f, 0.07f, 0);
    add_noise(0, 0.2f, 1, 700, 400, 1.0f, 0.6f, 0.001f, 0.04f);
    add_tone(0.03f, 0.3f, 620, 540, 0.12f, 0.01f, 0.08f, 1);
    store(SND_LAND, buf, secs(0.5f), 0, 0.7f);
    /* hop: a spring and a puff */
    clear(secs(0.35f)); seed = 19;
    add_tone(0, 0.25f, 260, 520, 0.4f, 0.004f, 0.08f, 1);
    add_noise(0, 0.2f, 1, 800, 1600, 1.0f, 0.4f, 0.005f, 0.05f);
    store(SND_HOP, buf, secs(0.35f), 0, 0.5f);
    /* horn: two-tone, buzzy */
    clear(secs(0.6f)); seed = 20;
    add_tone(0, 0.55f, 415, 415, 0.6f, 0.01f, 0, 1);
    add_tone(0, 0.55f, 523, 523, 0.5f, 0.01f, 0, 1);
    {
        Bq f; bq_init(&f, 1, 1300, 1.2f);
        for (int i = 0; i < secs(0.6f); i++) buf[i] = tanhf((buf[i] + bq(&f, buf[i]) * 2.0f) * 1.8f);
    }
    store(SND_HORN, buf, secs(0.6f), 0, 0.75f);
    /* countdown beeps */
    clear(secs(0.35f));
    add_tone(0, 0.3f, 880, 880, 0.7f, 0.003f, 0.12f, 1);
    store(SND_BEEP, buf, secs(0.35f), 0, 0.6f);
    clear(secs(0.9f));
    add_tone(0, 0.85f, 1760, 1760, 0.6f, 0.003f, 0.35f, 1);
    add_tone(0, 0.85f, 880, 880, 0.3f, 0.003f, 0.35f, 1);
    store(SND_GO, buf, secs(0.9f), 0, 0.7f);
    /* lap and final lap chimes */
    clear(secs(0.8f));
    add_tone(0, 0.6f, 1318, 1318, 0.5f, 0.002f, 0.2f, 0);
    add_tone(0.12f, 0.6f, 1760, 1760, 0.5f, 0.002f, 0.25f, 0);
    store(SND_LAP, buf, secs(0.8f), 0, 0.55f);
    clear(secs(1.2f));
    for (int k = 0; k < 3; k++) add_tone((float)k * 0.13f, 0.7f, 1318.0f * powf(1.26f, (float)k), 1318.0f * powf(1.26f, (float)k), 0.45f, 0.002f, 0.22f, 0);
    store(SND_FINAL, buf, secs(1.2f), 0, 0.6f);
    /* finish: an air horn blast */
    clear(secs(1.4f)); seed = 21;
    add_tone(0, 1.2f, 233, 228, 0.6f, 0.03f, 0.9f, 1);
    add_tone(0, 1.2f, 277, 272, 0.5f, 0.03f, 0.9f, 1);
    add_tone(0, 1.2f, 349, 342, 0.4f, 0.03f, 0.9f, 1);
    for (int i = 0; i < secs(1.4f); i++) buf[i] = tanhf(buf[i] * 2.2f);
    store(SND_FINISH, buf, secs(1.4f), 0, 0.75f);
    /* the crowd: lots of formant-filtered noisy voices swelling up, a few whistles */
    {
        int n = secs(3.0f);
        clear(n); seed = 22;
        for (int v = 0; v < 26; v++) {
            Bq f1, f2;
            bq_init(&f1, 1, 600.0f + 300.0f * (frand() * 0.5f + 0.5f), 4.0f);
            bq_init(&f2, 1, 1100.0f + 700.0f * (frand() * 0.5f + 0.5f), 5.0f);
            float st = (frand() * 0.5f + 0.5f) * 0.4f, sw = 0.8f + 0.6f * (frand() * 0.5f + 0.5f);
            float rate = 2.0f + 4.0f * (frand() * 0.5f + 0.5f);
            for (int i = 0; i < n; i++) {
                float t = (float)i / SR;
                if (t < st) continue;
                float e = fminf(1.0f, (t - st) / 0.25f) * expf(-fmaxf(0.0f, t - st - sw) / 0.6f);
                e *= 0.75f + 0.25f * sinf(t * TAU_F * rate + (float)v);
                float x = frand();
                buf[i] += (bq(&f1, x) + bq(&f2, x) * 0.7f) * e;
            }
        }
        for (int k = 0; k < 4; k++) {
            float t = 0.2f + (frand() * 0.5f + 0.5f) * 1.2f, f = 1800.0f + 900.0f * frand();
            add_tone(t, 0.5f, f, f * 1.3f, 0.12f, 0.03f, 0.25f, 0);
        }
        store(SND_CROWD, buf, n, 0, 0.8f);
    }
    /* boost pad: a rising zip */
    clear(secs(0.5f)); seed = 31;
    add_tone(0, 0.35f, 500, 2400, 0.5f, 0.003f, 0.12f, 0);
    add_noise(0, 0.4f, 1, 800, 5000, 1.2f, 0.9f, 0.005f, 0.12f);
    store(SND_PAD, buf, secs(0.5f), 0, 0.7f);
    /* boost ready: two soft blue notes */
    clear(secs(0.5f));
    add_tone(0, 0.3f, 1318, 1318, 0.4f, 0.004f, 0.1f, 0);
    add_tone(0.08f, 0.35f, 1976, 1976, 0.4f, 0.004f, 0.14f, 0);
    store(SND_READY, buf, secs(0.5f), 0, 0.45f);
    /* 88 mph: a sonic boom - thump, crack, a rolling low tail and a bright sweep of shimmer */
    {
        int n = secs(2.4f);
        clear(n); seed = 32;
        add_tone(0, 0.6f, 70, 38, 1.4f, 0.002f, 0.18f, 0);
        add_noise(0, 0.12f, 1, 3500, 1500, 0.8f, 1.6f, 0.0005f, 0.03f);
        add_noise(0, 2.2f, 0, 600, 90, 0.7f, 1.2f, 0.01f, 0.6f);
        add_noise(0.05f, 1.6f, 1, 1500, 9000, 1.5f, 0.45f, 0.05f, 0.45f);
        add_tone(0.05f, 1.2f, 880, 3520, 0.18f, 0.05f, 0.35f, 1);
        Bq f; bq_init(&f, 0, 9000, 0.7f);
        for (int i = 0; i < n; i++) buf[i] = bq(&f, tanhf(buf[i] * 1.4f));
        store(SND_SONIC, buf, n, 0, 1.0f);
    }
    /* the code unlock: a rising sparkle and a whoosh */
    {
        int n = secs(1.8f);
        clear(n); seed = 33;
        const float notes[6] = { 523, 659, 784, 1047, 1319, 1568 };
        for (int k = 0; k < 6; k++) add_tone((float)k * 0.07f, 1.0f, notes[k], notes[k], 0.3f, 0.003f, 0.35f, 1);
        add_noise(0, 1.2f, 1, 400, 6000, 0.9f, 0.6f, 0.08f, 0.35f);
        add_tone(0.45f, 1.2f, 2093, 2093, 0.25f, 0.005f, 0.5f, 0);
        store(SND_UNLOCK, buf, n, 0, 0.9f);
    }
    /* interface */
    clear(secs(0.06f));
    add_tone(0, 0.05f, 1500, 1300, 0.6f, 0.001f, 0.015f, 0);
    store(SND_UI_MOVE, buf, secs(0.06f), 0, 0.45f);
    clear(secs(0.2f));
    add_tone(0, 0.15f, 900, 900, 0.5f, 0.001f, 0.04f, 1);
    add_tone(0.05f, 0.15f, 1350, 1350, 0.5f, 0.001f, 0.05f, 1);
    store(SND_UI_OK, buf, secs(0.2f), 0, 0.55f);
    clear(secs(0.2f));
    add_tone(0, 0.15f, 900, 600, 0.5f, 0.001f, 0.05f, 1);
    store(SND_UI_BACK, buf, secs(0.2f), 0, 0.5f);
    clear(secs(0.6f)); seed = 23;
    add_metal(0, 1250, 0.4f, 0.12f);
    add_metal(0.09f, 1670, 0.4f, 0.16f);
    add_noise(0, 0.06f, 1, 3000, 2000, 1.0f, 0.3f, 0.001f, 0.02f);
    store(SND_UI_BUY, buf, secs(0.6f), 0, 0.6f);
    clear(secs(0.3f));
    add_tone(0, 0.25f, 140, 130, 0.6f, 0.002f, 0.1f, 1);
    add_tone(0, 0.25f, 147, 137, 0.6f, 0.002f, 0.1f, 1);
    for (int i = 0; i < secs(0.3f); i++) buf[i] = tanhf(buf[i] * 3.0f);
    store(SND_UI_ERR, buf, secs(0.3f), 0, 0.5f);
}

void audio_init(void)
{
    if (!buf) {
        buf = (float *)big_alloc(sizeof(float) * MAXN);
        tmp = (float *)big_alloc(sizeof(float) * MAXN);
        tmp2 = (float *)big_alloc(sizeof(float) * MAXN);
    }
    pool_used = 0;
    turbine(SND_ENG_A_ON, 0, 1);
    turbine(SND_ENG_A_OFF, 0, 0);
    turbine(SND_ENG_B_ON, 1, 1);
    turbine(SND_ENG_B_OFF, 1, 0);
    turbine(SND_ENG_C_ON, 2, 1);
    turbine(SND_ENG_C_OFF, 2, 0);
    turbine(SND_ENG_D_ON, 3, 1);
    turbine(SND_ENG_D_OFF, 3, 0);
    skid_loop();
    gravel_loop();
    nitro_loop();
    wind_loop();
    scrape_loop();
    oneshots();
}

const int16_t *audio_data(int s) { return &POOL[s_off[s]]; }
int audio_len(int s) { return s_len[s]; }
int audio_is_loop(int s) { return s_loop[s]; }
float audio_gain(int s) { return s_gain[s]; }
const int16_t *audio_pool(void) { return POOL; }
int audio_pool_bytes(void) { return pool_used * 2; }
