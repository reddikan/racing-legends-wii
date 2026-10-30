/*
 * RACING LEGENDS - a race: the grid, the countdown, the player's controls, the AI field, effects, the
 * camera, sound and the finish.
 */
#include "game_int.h"

#include <stdio.h>
#include <string.h>

static const char *AI_NAMES[14] = { "MAX VOLTA", "LUNA REYES", "KAI STORM", "ZOE BLAZE", "RICO DIAZ", "NINA FROST",
    "JET HARLOW", "SKYE RIVERA", "BRUNO KOVAC", "MIA TANAKA", "DUKE MORGAN", "IVY CHEN", "OTTO VANCE", "RAE NOVAK" };
static const float AI_TIER[NUM_TRACKS] = { 1.0f, 2.3f, 3.2f, 4.0f, 4.6f };

static Rng rr;

/* expanding light rings on the deck (boosts, the 88 mph shockwave) */
typedef struct { float x, y, z, t, dur, r0, r1, thick; uint32_t col; } Wave;
#define MAX_WAVES 12
static Wave waves[MAX_WAVES];

static void wave_add(float x, float y, float z, float dur, float r0, float r1, float thick, uint32_t col)
{
    int k = 0;
    for (int i = 0; i < MAX_WAVES; i++) if (waves[i].t >= waves[i].dur) { k = i; break; }
    Wave *w = &waves[k];
    w->x = x; w->y = y; w->z = z; w->t = 0; w->dur = dur; w->r0 = r0; w->r1 = r1; w->thick = thick; w->col = col;
}

static void waves_update(float dt) { for (int i = 0; i < MAX_WAVES; i++) if (waves[i].t < waves[i].dur) waves[i].t += dt; }

static void waves_draw(void)
{
#ifdef NO_WAVES
    return;
#endif
    r3d_mode(RM_ADD);
    for (int i = 0; i < MAX_WAVES; i++) {
        const Wave *w = &waves[i];
        if (w->t >= w->dur || w->dur <= 0) continue;
        float k = w->t / w->dur, e = 1.0f - (1.0f - k) * (1.0f - k);
        float r = w->r0 + (w->r1 - w->r0) * e, a = (1.0f - k) * (1.0f - k);
        float th = w->thick * (1.0f + 0.5f * k);
        const int N = 40;
        uint32_t cin = col_a(w->col, 0.18f * a), cmid = col_a(w->col, a), cout = col_a(w->col, 0.0f);
        /* a faint wash inside the ring, so it reads as a wave of light rather than a hole */
        {
            uint32_t cc = col_a(w->col, 0.0f);
            float y = w->y + 0.14f;
            for (int j = 0; j < N; j++) {
                float a0 = (float)j / N * TAU, a1 = (float)(j + 1) / N * TAU;
                r3d_tri3(w->x, y, w->z, cc, w->x + cosf(a0) * (r - th), y, w->z + sinf(a0) * (r - th), cin,
                         w->x + cosf(a1) * (r - th), y, w->z + sinf(a1) * (r - th), cin);
            }
        }
        for (int j = 0; j < N; j++) {
            float a0 = (float)j / N * TAU, a1 = (float)(j + 1) / N * TAU;
            float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
            float y = w->y + 0.15f;
            /* a soft band: transparent - bright - transparent */
            r3d_tri3(w->x + c0 * (r - th), y, w->z + s0 * (r - th), cin, w->x + c1 * (r - th), y, w->z + s1 * (r - th), cin, w->x + c1 * r, y, w->z + s1 * r, cmid);
            r3d_tri3(w->x + c0 * (r - th), y, w->z + s0 * (r - th), cin, w->x + c1 * r, y, w->z + s1 * r, cmid, w->x + c0 * r, y, w->z + s0 * r, cmid);
            r3d_tri3(w->x + c0 * r, y, w->z + s0 * r, cmid, w->x + c1 * r, y, w->z + s1 * r, cmid, w->x + c1 * (r + th), y, w->z + s1 * (r + th), cout);
            r3d_tri3(w->x + c0 * r, y, w->z + s0 * r, cmid, w->x + c1 * (r + th), y, w->z + s1 * (r + th), cout, w->x + c0 * (r + th), y, w->z + s0 * (r + th), cout);
        }
    }
}

/* the player's big moments */
static void boost_burst(Car *c)
{
    float fx = sinf(c->h), fz = cosf(c->h);
    int sig = c->livery == LIVERY_SIGNATURE;
    wave_add(c->x, c->y, c->z, 0.7f, 2.0f, 15.0f, 0.7f, sig ? 0x3AA8FFFFu : 0x6AD8FFFFu);
    wave_add(c->x, c->y, c->z, 0.45f, 1.0f, 8.0f, 0.45f, 0xE8F8FFFFu);
    for (int k = 0; k < 26; k++) {
        float a = rng_r(&rr, -0.6f, 0.6f), sp = rng_r(&rr, 8, 22);
        fx_spawn(PT_SPARK, c->x - fx * 2.6f, c->y + 0.9f, c->z - fz * 2.6f,
                 c->vx * 0.5f - (fx * cosf(a) - fz * sinf(a)) * sp, rng_r(&rr, 1, 5), c->vz * 0.5f - (fz * cosf(a) + fx * sinf(a)) * sp,
                 rng_r(&rr, 0.25f, 0.55f), 0.07f, k & 1 ? 0xBFF4FFFFu : 0x3AA8FFFFu);
    }
    for (int k = 0; k < 6; k++)
        fx_spawn(PT_FLAME, c->x - fx * (2.8f + (float)k * 0.6f), c->y + 1.0f, c->z - fz * (2.8f + (float)k * 0.6f), c->vx * 0.8f, 0, c->vz * 0.8f, 0.35f, 1.6f - (float)k * 0.15f, 0x9AE8FFFFu);
}

static void mph88_burst(Car *c)
{
    float fx = sinf(c->h), fz = cosf(c->h);
    wave_add(c->x, c->y, c->z, 1.1f, 3.0f, 34.0f, 1.0f, 0x5AC8FFFFu);
    wave_add(c->x, c->y, c->z, 0.8f, 2.0f, 20.0f, 0.6f, 0xFFFFFFFFu);
    wave_add(c->x, c->y, c->z, 1.4f, 4.0f, 46.0f, 1.4f, 0x2A5AFFFFu);
    /* streaks of light peeling off the craft and shooting back down the track */
    for (int k = 0; k < 40; k++) {
        float side = rng_r(&rr, -1.6f, 1.6f), sp = rng_r(&rr, 25, 45);
        fx_spawn(PT_SPARK, c->x + fz * side, c->y + rng_r(&rr, 0.4f, 1.6f), c->z - fx * side,
                 c->vx * 0.3f - fx * sp, rng_r(&rr, -0.5f, 0.5f), c->vz * 0.3f - fz * sp, rng_r(&rr, 0.3f, 0.6f), 0.09f, k % 3 ? 0x9AE8FFFFu : 0xFFFFFFFFu);
    }
}

static void setup_ai(Car *c, int i, int track, int attract)
{
    float tier = attract ? 2.5f : AI_TIER[track];
    uint8_t lv[NUM_UPGRADES];
    /* the field is spread: some drivers are better equipped than others */
    float var = -1.1f + 1.6f * (float)i / 7.0f + rng_r(&rr, -0.3f, 0.3f);
    for (int k = 0; k < NUM_UPGRADES; k++) {
        float v = tier + var + rng_r(&rr, -0.5f, 0.5f);
        int l = (int)floorf(v + 0.5f);
        lv[k] = (uint8_t)(l < 0 ? 0 : (l > UP_MAX ? UP_MAX : l));
    }
    car_stats_from_levels(lv, &c->st);
    c->skill = 0.95f + 0.045f * (float)i / 7.0f + rng_r(&rr, -0.005f, 0.005f);
    if (track == 0 && !attract) c->skill -= 0.015f;
    c->ai = 1;
    c->lane = rng_r(&rr, -1.2f, 1.2f);
    c->ai_nitro_t = rng_r(&rr, 2.0f, 8.0f);
    c->body = (int)(rng_u(&rr) % (NUM_BODIES - 1));          /* never the hidden craft */
    int p;
    do { p = (int)(rng_u(&rr) % NUM_PAINTS); } while (!attract && p == sv.paint);
    c->paint = PAINT[p];
    c->paint2 = PAINT[(p + 3 + (int)(rng_u(&rr) % 6)) % NUM_PAINTS];
    if (c->paint2 == c->paint) c->paint2 = 0xF4F4F2FFu;
    c->livery = (int)(rng_u(&rr) % 4);
}

void race_setup(int track, int laps, int attract)
{
    g.track = track; g.laps = laps; g.attract = attract;
    g.phase = attract ? RP_RACE : RP_INTRO;
    g.phase_t = 0; g.race_t = 0; g.paused = 0; g.final_lap_shown = 0; g.count_last = 4;
    g.shake = 0; g.wrong_t = 0;
    g.boost_anim = 0; g.mph88_t = 0; g.armed88 = 1; g.since88 = 10.0f;
    for (int i = 0; i < MAX_WAVES; i++) waves[i].t = waves[i].dur = 0;
    for (int i = 0; i < MAX_POPUPS; i++) g.pop[i].t = 0;
    rr.s = 0x9E3779B9u ^ (uint32_t)(track * 7919 + sv.races * 31 + 1);
    fx_reset();
    ncars = MAX_CARS;
    /* shuffle names */
    int names[14];
    for (int i = 0; i < 14; i++) names[i] = i;
    for (int i = 13; i > 0; i--) { int j = (int)(rng_u(&rr) % (uint32_t)(i + 1)); int t = names[i]; names[i] = names[j]; names[j] = t; }
    int player_slot = attract ? -1 : 4;
    int ai_i = 0;
    for (int slot = 0; slot < MAX_CARS; slot++) {
        Car *c;
        if (slot == player_slot) c = &cars[0];
        else c = &cars[1 + (attract ? slot - 1 : ai_i)];
        if (attract && slot == 0) c = &cars[0];
        memset(c, 0, sizeof *c);
        float back = 7.0f + (float)slot * 5.0f, lat = (slot & 1) ? 3.2f : -3.2f;
        car_place(c, trk.length - back, lat);
        c->lap = 0; c->cp = 1;
        c->number = slot + 1;
        c->best_lap = 0;
        if (slot == player_slot) {
            c->is_player = 1;
            c->body = sv.body; c->paint = PAINT[sv.paint]; c->paint2 = PAINT[sv.paint2]; c->livery = sv.body == BODY_FUTURE88 ? LIVERY_SIGNATURE : sv.livery;
            car_stats_from_levels(sv.lv, &c->st);
            strcpy(c->name, "YOU");
        } else {
            /* better drivers start further up */
            int rank = attract ? slot : (slot < player_slot ? 7 - slot : 7 - slot + 0);
            setup_ai(c, rank > 7 ? 7 : rank, track, attract);
            strcpy(c->name, AI_NAMES[names[ai_i % 14]]);
            ai_i++;
        }
        c->nitro = 1.0f;
        c->sample = track_locate(-1, c->x, c->z, &c->lat, &c->along);
        c->progress = (float)(c->lap - 1) * trk.length + trk.s[c->sample].dist + c->along;
    }
    if (attract) for (int i = 0; i < ncars; i++) cars[i].is_player = 0;
    race_camera(0, 1);
    g.intro_a = 0;
}

static void respawn(Car *c)
{
    const TSample *s = &trk.s[(c->sample + 2) % trk.n];
    int lap = c->lap, cp = c->cp;
    float nitro = c->nitro;
    car_place(c, s->dist, s->line * 0.5f);
    c->lap = lap; c->cp = cp; c->nitro = nitro;
    c->sample = track_locate(c->sample, c->x, c->z, &c->lat, &c->along);
}

/* ------------------------------------------------------------------ */
/* player controls                                                     */
/* ------------------------------------------------------------------ */
static float steer_smooth;

static void player_input(Car *c, const Input *in, float dt)
{
    float sx = in->sx;
    int digital = 0;
    if (in->held & BTN_LEFT) { sx = -1.0f; digital = 1; }
    if (in->held & BTN_RIGHT) { sx = 1.0f; digital = 1; }
    /* the stick is used directly (with a gentle curve); the d-pad ramps in */
    float target = sx * fabsf(sx) * 0.3f + sx * 0.7f;
    if (digital) steer_smooth += (target - steer_smooth) * damp(fabsf(target) > fabsf(steer_smooth) ? 9.0f : 18.0f, dt);
    else steer_smooth += (target - steer_smooth) * damp(30.0f, dt);
    c->in.steer = steer_smooth;
    /* A accelerates; L / ZL brakes and drifts; R / ZR fires the boost */
    float thr = (in->held & BTN_A) ? 1.0f : 0.0f;
    float brk = (in->held & (BTN_L | BTN_ZL)) ? 1.0f : 0.0f;
    if (in->lt > brk) brk = in->lt;
    c->in.throttle = thr;
    c->in.brake = brk;
    c->in.drift = brk > 0.5f;
    c->in.nitro = (in->pressed & (BTN_R | BTN_ZR)) != 0;
    c->in.hop = (in->pressed & BTN_B) != 0;
    if (in->pressed & BTN_Y) { c->ev_horn = 1; }
}

/* ------------------------------------------------------------------ */
/* effects                                                             */
/* ------------------------------------------------------------------ */
static const uint32_t TIER_COL[4] = { 0xFFFFFFFFu, 0x4AB8FFFFu, 0xFF9A2AFFu, 0xC860FFFFu };

static void car_effects(Car *c, int idx, float dt)
{
    float dx = c->x - r_cam.tx, dz = c->z - r_cam.tz;
    int near = dx * dx + dz * dz < 85.0f * 85.0f;
    float sh = sinf(c->h), ch = cosf(c->h);
    float fx = sh, fz = ch, rx = -ch, rz = sh;
    int surf = trk.info->surface;
    float ws = 1.1f, wz = 1.5f;
    float sliding = 0;
    if (c->air <= 0.05f) {
        if (c->drifting) sliding = 1.0f;
        else if (fabsf(c->slip) > 0.18f && c->speed > 8.0f) sliding = sat((fabsf(c->slip) - 0.18f) * 4.0f);
        else if (c->in.brake > 0.5f && c->speed > 14.0f && !c->drifting) sliding = 0.6f;
    }
    for (int w = 0; w < 2; w++) {
        float s = w ? 1.0f : -1.0f;
        float px = c->x - fx * wz + rx * ws * s, pz = c->z - fz * wz + rz * ws * s;
        (void)skid_add;
        skid_break(idx * 2 + w);
        if (!near) continue;
        float spd = c->speed;
        if (0) {
            uint32_t sc = surf == SURF_ASPHALT ? 0xE6E8ECFFu : (surf == SURF_DIRT ? 0xC89A6AFFu : 0xF4F8FFFFu);
            if (trk.info->theme == TH_LANTERN) sc = 0x8A6A50FFu;
            if (trk.info->night && surf == SURF_ASPHALT) sc = 0x6A7084FFu;
            int ty = surf == SURF_ASPHALT ? PT_SMOKE : (surf == SURF_DIRT ? PT_DUST : PT_SNOW);
            fx_spawn(ty, px + rng_r(&rr, -0.3f, 0.3f), c->y + 0.4f, pz + rng_r(&rr, -0.3f, 0.3f),
                     c->vx * 0.25f + rx * s * 1.5f + rng_r(&rr, -1, 1), rng_r(&rr, 0.6f, 1.6f), c->vz * 0.25f + rz * s * 1.5f + rng_r(&rr, -1, 1),
                     rng_r(&rr, 0.8f, 1.5f), rng_r(&rr, 0.7f, 1.1f), sc);
        }
        if (c->drifting && c->drift_tier > 0 && rng_f(&rr) < 0.7f) {
            uint32_t tc = TIER_COL[c->drift_tier];
            fx_spawn(PT_SPARK, px - rx * s * 0.3f, c->y + 0.15f, pz - rz * s * 0.3f,
                     -fx * spd * 0.2f + rx * s * rng_r(&rr, 1, 4) + rng_r(&rr, -2, 2), rng_r(&rr, 2, 5), -fz * spd * 0.2f + rz * s * rng_r(&rr, 1, 4) + rng_r(&rr, -2, 2),
                     rng_r(&rr, 0.18f, 0.35f), 0.05f, tc);
        }
        if (0) {
            uint32_t dc = surf == SURF_SNOW ? 0xF4F8FFFFu : (trk.info->theme == TH_FOREST ? 0x9A8A60FFu : (trk.info->theme == TH_HARBOR ? 0x8A8E96FFu : 0xC8956AFFu));
            if (trk.info->night) dc = col_scale(dc, 0.6f);
            fx_spawn(surf == SURF_SNOW ? PT_SNOW : PT_DUST, px, c->y + 0.3f, pz, c->vx * 0.2f + rng_r(&rr, -1, 1), rng_r(&rr, 0.5f, 1.5f), c->vz * 0.2f + rng_r(&rr, -1, 1),
                     rng_r(&rr, 0.6f, 1.2f), rng_r(&rr, 0.6f, 1.0f), dc);
        }
    }
    if (!near) return;
    if (c->speed > 8.0f && rng_f(&rr) < 0.6f)
        fx_spawn(PT_FLAME, c->x - fx * 2.8f + rng_r(&rr, -0.3f, 0.3f), c->y + 1.1f, c->z - fz * 2.8f + rng_r(&rr, -0.3f, 0.3f),
                 c->vx * 0.6f, 0.0f, c->vz * 0.6f, 0.35f, 0.45f, c->drifting ? 0xC860FFFFu : 0x3AB8FFFFu);
    if (c->boost_t > 0 && rng_f(&rr) < 0.8f) {
        for (int s = -1; s <= 1; s += 2)
            fx_spawn(PT_FLAME, c->x - fx * 2.8f + rx * 0.45f * (float)s, c->y + 0.55f, c->z - fz * 2.8f + rz * 0.45f * (float)s,
                     -fx * 6.0f + c->vx * 0.7f, 0.5f, -fz * 6.0f + c->vz * 0.7f, 0.18f, 0.6f, 0x5AA8FFFFu);
    }
    if (c->scrape > 0.3f && c->speed > 6.0f) {
        float s = c->lat > 0 ? 1.0f : -1.0f;
        for (int k = 0; k < 2; k++)
            fx_spawn(PT_SPARK, c->x + rx * 1.3f * s + fx * rng_r(&rr, -1.5f, 1.5f), c->y + 0.5f, c->z + rz * 1.3f * s + fz * rng_r(&rr, -1.5f, 1.5f),
                     -c->vx * 0.15f + rng_r(&rr, -3, 3), rng_r(&rr, 1, 4), -c->vz * 0.15f + rng_r(&rr, -3, 3), rng_r(&rr, 0.2f, 0.45f), 0.06f, 0xFFD27AFFu);
    }
    if (c->ev_impact) {
        for (int k = 0; k < 16; k++)
            fx_spawn(PT_SPARK, c->x + rng_r(&rr, -1, 1), c->y + 0.6f, c->z + rng_r(&rr, -1, 1),
                     rng_r(&rr, -7, 7), rng_r(&rr, 2, 7), rng_r(&rr, -7, 7), rng_r(&rr, 0.25f, 0.6f), 0.07f, 0xFFE2A0FFu);
    }
    if (c->ev_backfire) {
        fx_spawn(PT_FLAME, c->x - fx * 2.7f, c->y + 0.5f, c->z - fz * 2.7f, -fx * 3.0f, 0.3f, -fz * 3.0f, 0.12f, 0.7f, 0xFF9A3AFFu);
    }
}

/* ------------------------------------------------------------------ */
/* sounds from events                                                  */
/* ------------------------------------------------------------------ */
static float pan_of(const Car *c)
{
    float sx, sy;
    if (!r3d_project(c->x, c->y, c->z, &sx, &sy)) return 0;
    return clampf((sx - VW * 0.5f) / (VW * 0.5f), -1, 1);
}

static float vol_of(const Car *c)
{
    if (c->is_player) return 1.0f;
    float dx = c->x - r_cam.tx, dz = c->z - r_cam.tz;
    float d = sqrtf(dx * dx + dz * dz);
    float v = 1.0f - d / 70.0f;
    return v > 0 ? v * v * 0.8f : 0.0f;
}

static void car_sounds(Car *c)
{
    float v = vol_of(c), p = pan_of(c);
    if (v > 0.01f) {
        if (c->ev_impact) snd(c->ev_impact_v > 9.0f ? SND_IMPACT_L : SND_IMPACT_S, v * clampf(c->ev_impact_v / 12.0f, 0.35f, 1.0f), rng_r(&rr, 0.9f, 1.1f), p);
        if (c->ev_nitro) { snd(SND_NITRO_START, v, 1.0f, p); boost_burst(c); }
        if (c->ev_pad && !c->is_player) snd(SND_PAD, v * 0.6f, 1.0f, p);
        if (c->ev_boost) snd(SND_BOOST, v * (0.6f + 0.15f * (float)c->ev_boost), 0.9f + 0.1f * (float)c->ev_boost, p);
        if (c->ev_tier && c->is_player) snd(SND_TIER, 0.7f, 0.8f + 0.25f * (float)c->ev_tier, p);
        if (c->ev_shift && c->is_player) snd(SND_SHIFT, 0.35f, 1.0f, p);
        if (c->ev_backfire) snd(SND_BACKFIRE, v * 0.7f, rng_r(&rr, 0.85f, 1.15f), p);
        if (c->ev_land) snd(SND_LAND, v * 0.7f, 1.0f, p);
        if (c->ev_horn) snd(SND_HORN, v, c->body == 1 ? 0.8f : (c->body == 2 ? 1.15f : 1.0f), p);
    }
    if (c->is_player) {
        if (c->ev_nitro) { g.nitro_flash = 1.0f; g.boost_anim = 1.0f; g.shake = fmaxf(g.shake, 0.45f); }
        if (c->ev_pad) { snd(SND_PAD, 0.8f, 1.0f, 0); g.shake = fmaxf(g.shake, 0.2f); }
        if (c->ev_ready) snd(SND_READY, 0.6f, 1.0f, 0);
        if (c->ev_tier) { g.tier_flash = 1.0f; g.tier_col = c->ev_tier; }
        if (c->ev_impact) g.shake = fmaxf(g.shake, clampf(c->ev_impact_v / 14.0f, 0.2f, 1.0f));
        if (c->ev_boost) { g.shake = fmaxf(g.shake, 0.25f); }
    }
    c->ev_impact = c->ev_nitro = c->ev_boost = c->ev_land = c->ev_tier = c->ev_shift = c->ev_backfire = c->ev_horn = c->ev_pad = c->ev_ready = 0;
}

/* ------------------------------------------------------------------ */
/* standings                                                           */
/* ------------------------------------------------------------------ */
static void standings(void)
{
    for (int i = 0; i < ncars; i++) g.order[i] = i;
    for (int i = 1; i < ncars; i++) {
        int k = g.order[i], j = i - 1;
        while (j >= 0) {
            const Car *a = &cars[g.order[j]], *b = &cars[k];
            int b_ahead;
            if (a->finished && b->finished) b_ahead = b->finish_time < a->finish_time;
            else if (a->finished || b->finished) b_ahead = b->finished;
            else b_ahead = b->progress > a->progress;
            if (!b_ahead) break;
            g.order[j + 1] = g.order[j];
            j--;
        }
        g.order[j + 1] = k;
    }
    for (int i = 0; i < ncars; i++) cars[g.order[i]].place = i + 1;
}

int race_reward(int place)
{
    static const int BASE[8] = { 1200, 850, 650, 450, 320, 220, 150, 100 };
    float mult = 1.0f + 0.3f * (float)g.track;
    return (int)((float)BASE[place - 1] * mult) / 10 * 10;
}

static void finish_player(Car *c)
{
    standings();
    g.res_place = c->place;
    g.res_time = c->finish_time;
    g.res_base = race_reward(c->place) * g.laps / 3;
    if (g.res_base < 50) g.res_base = 50;
    g.res_drift = (int)(c->drift_total / 8.0f) / 10 * 10;
    g.res_credits = g.res_base + g.res_drift;
    g.res_unlock = 0;
    g.res_best = 0;
    if (c->best_lap > 0 && (sv.best_lap[g.track] <= 0 || c->best_lap < sv.best_lap[g.track])) { sv.best_lap[g.track] = c->best_lap; g.res_best = 1; }
    if (sv.best_race[g.track] <= 0 || (g.laps == 3 && c->finish_time < sv.best_race[g.track])) if (g.laps == 3) sv.best_race[g.track] = c->finish_time;
    if (sv.best_place[g.track] == 0 || c->place < sv.best_place[g.track]) sv.best_place[g.track] = (uint8_t)c->place;
    if (c->place <= 3 && g.track + 2 > sv.unlocked && sv.unlocked < NUM_TRACKS) { sv.unlocked = (uint8_t)(g.track + 2); g.res_unlock = 1; }
    sv.credits += g.res_credits;
    sv.races++;
    if (c->place == 1) sv.wins++;
    if (c->place <= 3) sv.podiums++;
    if ((int)c->drift_best > sv.best_drift) sv.best_drift = (int)c->drift_best;
    save_mark_dirty();
}

/* ------------------------------------------------------------------ */
void race_update(const Input *in, float dt)
{
    Car *pl = g.attract ? NULL : &cars[0];
    if (!g.attract && (in->pressed & BTN_PLUS) && g.phase != RP_DONE) { g.paused = !g.paused; g.pause_sel = 0; snd(SND_UI_OK, 0.6f, 1.0f, 0); }
    if (g.paused) {
        if (in->pressed & (BTN_UP | BTN_DOWN)) { g.pause_sel = (g.pause_sel + ((in->pressed & BTN_DOWN) ? 1 : 2)) % 3; snd(SND_UI_MOVE, 0.5f, 1, 0); }
        if (in->pressed & BTN_A) {
            snd(SND_UI_OK, 0.6f, 1.0f, 0);
            if (g.pause_sel == 0) g.paused = 0;
            else if (g.pause_sel == 1) { race_setup(g.track, g.laps, 0); }
            else { g.paused = 0; menu_enter(ST_TRACKS); }
        }
        if (in->pressed & BTN_B) g.paused = 0;
        return;
    }
    g.phase_t += dt;
    int racing = 0;
    switch (g.phase) {
    case RP_INTRO:
        g.intro_a += dt;
        if (g.phase_t > 3.2f || (in->pressed & (BTN_A | BTN_PLUS))) { g.phase = RP_COUNT; g.phase_t = 0; g.count_last = 4; g.thr_press_t = -1.0f; }
        break;
    case RP_COUNT: {
        int n = 3 - (int)floorf(g.phase_t / 0.85f);
        if (n < g.count_last && n >= 1) { snd(SND_BEEP, 0.8f, 1.0f, 0); g.count_last = n; }
        /* press A as the last light comes on for a rocket start */
        if (pl) {
            float thr = (in->held & BTN_A) ? 1.0f : 0.0f;
            if (in->pressed & BTN_A) g.thr_press_t = g.phase_t;
            pl->rpm += ((thr > 0.5f ? 0.85f : 0.18f) - pl->rpm) * damp(6.0f, dt);
            pl->in.throttle = thr;
        }
        if (g.phase_t >= 2.55f) {
            g.phase = RP_RACE; g.phase_t = 0;
            snd(SND_GO, 0.9f, 1.0f, 0);
            hud_popup("GO!", 0x5AF27AFFu, 64);
            for (int i = 0; i < ncars; i++) { cars[i].lap_start = 0; if (cars[i].ai) cars[i].start_boost = rng_r(&rr, 0.0f, 0.5f); }
            if (pl) {
                int held = (in->held & BTN_A) != 0;
                if (held && g.thr_press_t >= 1.6f) { pl->start_boost = 1.0f; hud_popup("ROCKET START!", 0xFFD23AFFu, 26); snd(SND_BOOST, 0.8f, 1.1f, 0); }
                else pl->start_boost = 0;
            }
        }
        break;
    }
    case RP_RACE:
    case RP_DONE:
        racing = 1;
        g.race_t += dt;
        break;
    }

    /* inputs */
    for (int i = 0; i < ncars; i++) {
        Car *c = &cars[i];
        if (c->is_player && !c->finished && g.phase == RP_RACE) player_input(c, in, dt);
        else if (racing) car_ai_input(c, dt);
        if (c->is_player && c->finished) { c->in.nitro = 0; c->skill = 0.85f; }
    }
    if (pl && (in->pressed & BTN_X)) { sv.cam = (uint8_t)((sv.cam + 1) % NUM_CAMS); save_mark_dirty(); snd(SND_UI_MOVE, 0.5f, 1.2f, 0); }

    /* physics (two sub-steps for stability) */
    for (int s = 0; s < 2; s++) cars_step(dt * 0.5f, racing);

    for (int i = 0; i < ncars; i++) {
        Car *c = &cars[i];
        if (racing && !c->finished) c->race_time = g.race_t;
        /* laps */
        if (racing) {
            static int seen[MAX_CARS];
            if (g.phase_t < dt * 1.5f && g.phase == RP_RACE) seen[i] = c->lap;
            if (c->lap > seen[i]) {
                if (c->lap >= 2) {
                    float lt = g.race_t - c->lap_start;
                    c->last_lap = lt;
                    if (c->best_lap <= 0 || lt < c->best_lap) c->best_lap = lt;
                    c->lap_start = g.race_t;
                    if (c->is_player) {
                        if (c->lap == g.laps && !g.final_lap_shown) { hud_popup("FINAL LAP", 0xFFD23AFFu, 40); snd(SND_FINAL, 0.8f, 1.0f, 0); g.final_lap_shown = 1; }
                        else if (c->lap <= g.laps) snd(SND_LAP, 0.7f, 1.0f, 0);
                    }
                } else {
                    c->lap_start = g.race_t;
                }
                seen[i] = c->lap;
            }
            if (!c->finished && c->lap > g.laps) {
                c->finished = 1;
                c->finish_time = g.race_t;
                if (c->is_player) {
                    finish_player(c);
                    g.phase = RP_DONE; g.phase_t = 0;
                    snd(SND_FINISH, 0.9f, 1.0f, 0);
                    snd(SND_CROWD, 0.8f, 1.0f, 0);
                    c->ai = 1; c->lane = 0;
                    if (c->place == 1) {
                        for (int k = 0; k < 160; k++)
                            fx_spawn(PT_CONFETTI, c->x + rng_r(&rr, -14, 14), c->y + rng_r(&rr, 6, 14), c->z + rng_r(&rr, -14, 14),
                                     rng_r(&rr, -3, 3), rng_r(&rr, -1, 3), rng_r(&rr, -3, 3), rng_r(&rr, 2.5f, 4.5f), 1, col_hsv(rng_f(&rr), 0.7f, 1.0f, 255));
                    }
                }
            }
        }
        /* recovery */
        if (racing && (c->stuck_t > (c->ai ? 2.0f : 3.0f) || c->wrong_t > (c->ai ? 2.0f : 6.0f))) {
            respawn(c);
        }
        car_effects(c, i, dt);
        car_sounds(c);
    }
    standings();
    fx_update(dt);
    hud_update(dt);

    if (pl && g.phase == RP_RACE) {
        g.wrong_t = pl->wrong_t;
    }
    if (g.phase == RP_DONE && g.phase_t > 5.0f && !g.attract && g.state == ST_RACE) {
        menu_enter(ST_RESULTS);
        return;
    }
    if (g.attract && cars[0].lap > 2) race_setup(g.track, g.laps, 1);
    if (g.nitro_flash > 0) g.nitro_flash -= dt * 2.5f;
    if (g.boost_anim > 0) g.boost_anim -= dt * 1.4f;
    waves_update(dt);
    /* 88 mph: every time the player gets up to it (it re-arms below 80) */
    if (pl && g.phase == RP_RACE && !pl->finished) {
        float mph = pl->speed * MPS_TO_MPH;
        if (g.armed88 && mph >= 88.0f && g.since88 > 2.5f) {
            g.armed88 = 0; g.since88 = 0; g.mph88_t = 0.0001f;
            mph88_burst(pl);
            for (int q = 0; q < MAX_POPUPS; q++) if (g.pop[q].t > 0.2f) g.pop[q].t = 0.2f;
            snd(SND_SONIC, 1.0f, 1.0f, 0);
            g.shake = fmaxf(g.shake, 0.55f);
        }
        if (mph < 80.0f) g.armed88 = 1;
    }
    g.since88 += dt;
    if (g.mph88_t > 0) { g.mph88_t += dt; if (g.mph88_t > 1.6f) g.mph88_t = 0; }
    if (g.tier_flash > 0) g.tier_flash -= dt * 3.0f;
    race_camera(dt, 0);
}

/* ------------------------------------------------------------------ */
/* camera                                                              */
/* ------------------------------------------------------------------ */
static int cam_target(void)
{
    if (!g.attract) return 0;
    /* attract: follow whoever leads, switching now and then */
    int k = ((int)(g.race_t / 9.0f)) % 3;
    return g.order[k];
}

void race_camera(float dt, int snap)
{
    const Car *c = &cars[cam_target()];
    float vx = c->vx, vz = c->vz;
    float spd = c->speed;
    int mode = g.attract ? CAM_FOLLOW : sv.cam;
    /* where to look: the car plus a little of where it's going */
    float lead = mode == CAM_CLOSE ? 0.22f : 0.28f;
    float tx = c->x + vx * lead, tz = c->z + vz * lead;
    float k = snap ? 1.0f : damp(6.0f, dt);
    g.look_x += (tx - g.look_x) * k;
    g.look_z += (tz - g.look_z) * k;
    /* yaw: follow the direction of travel lazily (the heading when slow) */
    float want = spd > 4.0f ? atan2f(vx, vz) : c->h;
    if (c->drifting) want = lerpf(want, c->h, 0.0f);
    if (mode == CAM_FIXED) want = atan2f(trk.s[0].tx, trk.s[0].tz) * 0.0f + PI * 0.0f;
    float dy = wrap_angle(want - g.cyaw);
    float yr = mode == CAM_CLOSE ? 3.2f : 2.2f;
    g.cyaw = snap ? want : wrap_angle(g.cyaw + dy * damp(yr, dt));
    float sp = clampf(spd / 62.0f, 0, 1.3f);
    float dist, height, fov;
    switch (mode) {
    case CAM_CLOSE: dist = 14.0f + sp * 4.0f; height = 9.5f + sp * 2.5f; fov = 50.0f + sp * 8.0f; break;
    case CAM_FIXED: dist = 18.0f + sp * 4.0f; height = 40.0f + sp * 6.0f; fov = 40.0f; break;
    default:        dist = 24.0f + sp * 6.0f; height = 17.0f + sp * 3.0f; fov = 30.0f + sp * 5.0f; break;
    }
    if (c->boost_t > 0) fov += 8.0f + 8.0f * sat(g.boost_anim);
    else if (c->mini_t > 0) fov += 3.0f;
    if (g.mph88_t > 0) fov += 7.0f * (1.0f - smooth01(g.mph88_t / 1.2f));
    float kk = snap ? 1.0f : damp(3.0f, dt);
    g.cdist += (dist - g.cdist) * kk;
    g.cheight += (height - g.cheight) * kk;
    g.cfov += (fov - g.cfov) * (snap ? 1.0f : damp(4.0f, dt));
    float ty = c->y + 0.8f;
    g.cy += (ty - g.cy) * (snap ? 1.0f : damp(4.0f, dt));
    float yaw = g.cyaw;
    /* intro: a slow orbit around the grid */
    if (g.phase == RP_INTRO && !g.attract) {
        float a = g.intro_a / 3.2f;
        yaw += (1.0f - smooth01(a)) * 2.4f;
        g.cdist = lerpf(48.0f, dist, smooth01(a));
        g.cheight = lerpf(26.0f, height, smooth01(a));
    }
    float ex = g.look_x - sinf(yaw) * g.cdist, ez = g.look_z - cosf(yaw) * g.cdist, ey = g.cy + g.cheight;
    {
        /* a fine rumble at high speed */
        float r = sat((spd - 40.0f) / 30.0f) * (c->boost_t > 0 ? 0.11f : 0.05f);
        ex += sinf(g.t * 97.0f) * r; ey += sinf(g.t * 83.0f + 2.0f) * r;
    }
    if (g.shake > 0) {
        float s = g.shake * g.shake * 0.7f;
        ex += sinf(g.t * 71.0f) * s; ey += sinf(g.t * 53.0f + 1.0f) * s * 0.6f; ez += cosf(g.t * 67.0f) * s;
        g.shake -= dt * 2.2f;
        if (g.shake < 0) g.shake = 0;
    }
    g.cx = ex; g.cz = ez;
    r3d_camera(ex, ey, ez, g.look_x, g.cy, g.look_z, g.cfov);
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
void race_draw_world(int hud)
{
    int night = trk.info->night;
    track_draw_world();
    sm_draw_mode(RM_OPAQUE, 0);
    sm_draw_mode(RM_GROUND, 0);
    skid_draw();
    sm_draw_mode(RM_SHADOW, 0);
    for (int i = 0; i < ncars; i++) {
        const Car *c = &cars[i];
        float dx = c->x - r_cam.tx, dz = c->z - r_cam.tz;
        if (dx * dx + dz * dz > 110.0f * 110.0f) continue;
        car_draw(c, g.t, night, 1.0f);
    }
    sm_draw_mode(RM_ALPHA, 0);
    if (night) {
        for (int i = 0; i < ncars; i++) {
            const Car *c = &cars[i];
            float dx = c->x - r_cam.tx, dz = c->z - r_cam.tz;
            if (dx * dx + dz * dz > 90.0f * 90.0f) continue;
            car_headlights(c);
        }
    }
    sm_draw_mode(RM_ADD, 0);
    sm_draw_mode(RM_MUL2, 0);
    fx_draw_alpha();
    fx_draw_add();
    waves_draw();
    track_draw_glows(g.t);
    if (sv.tilt) {
        r_post_top = 1.0f; r_post_bot = 0.85f;
        r3d_post();
    }
    if (hud) hud_draw();
}

void race_render(void)
{
    r3d_begin(trk.info->clear);
    r3d_camera(r_cam.ex, r_cam.ey, r_cam.ez, r_cam.tx, r_cam.ty, r_cam.tz, r_cam.fovy);
    /* the RGB split swells with boosts and the 88 mph moment */
    r_fx_split = 1.0f;
    if (!g.attract && g.state == ST_RACE) {
        r_fx_split += 2.0f * sat(g.boost_anim) + (g.mph88_t > 0 ? 3.0f * (1.0f - smooth01(g.mph88_t / 0.9f)) : 0.0f);
        if (cars[0].boost_t > 0) r_fx_split += 0.6f;
    }
    race_draw_world(!g.attract && g.state == ST_RACE);
}

/* ------------------------------------------------------------------ */
/* the looping sounds                                                  */
/* ------------------------------------------------------------------ */
static int eng_snd(int body, int on)
{
    switch (body) {
    case 1: return on ? SND_ENG_B_ON : SND_ENG_B_OFF;
    case 2: return on ? SND_ENG_C_ON : SND_ENG_C_OFF;
    case 3: return on ? SND_ENG_D_ON : SND_ENG_D_OFF;
    default: return on ? SND_ENG_A_ON : SND_ENG_A_OFF;
    }
}

static float eng_rate(const Car *c)
{
    /* the turbine loops play at their natural pitch at about half spin */
    return 0.62f + 0.75f * c->rpm;
}

void race_loops(LoopState *o)
{
    for (int i = 0; i < NUM_LOOPS; i++) { o[i].snd = -1; o[i].rate = 1; o[i].vol = 0; o[i].pan = 0; }
    if (g.state != ST_RACE || g.paused) return;
    const Car *pl = g.attract ? NULL : &cars[0];
    float mv = g.attract ? 0.0f : 1.0f;
    if (pl) {
        float thr = pl->in.throttle;
        float on = 0.3f + 0.7f * thr;
        if (g.phase == RP_INTRO) on *= 0.6f;
        o[LOOP_ENG_ON].snd = eng_snd(pl->body, 1); o[LOOP_ENG_ON].rate = eng_rate(pl); o[LOOP_ENG_ON].vol = on * 0.85f * mv;
        o[LOOP_ENG_OFF].snd = eng_snd(pl->body, 0); o[LOOP_ENG_OFF].rate = eng_rate(pl); o[LOOP_ENG_OFF].vol = (1.0f - thr) * 0.55f * mv;
        float sk = 0;
        if (pl->drifting) sk = 0.75f + 0.25f * sat(fabsf(pl->slip) * 2.0f);
        else if (fabsf(pl->slip) > 0.2f && pl->speed > 8.0f) sk = sat((fabsf(pl->slip) - 0.2f) * 3.0f) * 0.7f;
        else if (pl->in.brake > 0.5f && pl->speed > 12.0f) sk = 0.5f;
        if (pl->air > 0.05f) sk = 0;
        if (trk.info->surface != SURF_ASPHALT) sk *= 0.55f;
        o[LOOP_SKID].snd = SND_SKID; o[LOOP_SKID].vol = sk * 0.75f; o[LOOP_SKID].rate = 0.92f + 0.12f * sat(pl->speed / 40.0f) + (pl->drift_tier >= 2 ? 0.06f : 0.0f);
        float gr = 0;
        if (pl->offroad) gr = sat(pl->speed / 20.0f);
        else if (trk.info->surface != SURF_ASPHALT) gr = sat(pl->speed / 40.0f) * 0.45f + (pl->drifting ? 0.35f : 0.0f);
        o[LOOP_GRAVEL].snd = SND_GRAVEL; o[LOOP_GRAVEL].vol = gr * 0.65f; o[LOOP_GRAVEL].rate = 0.8f + 0.4f * sat(pl->speed / 40.0f);
        o[LOOP_NITRO].snd = SND_NITRO_LOOP; o[LOOP_NITRO].vol = pl->boost_t > 0 ? 0.7f : (pl->mini_t > 0 ? 0.35f : 0.0f); o[LOOP_NITRO].rate = 1.0f;
        o[LOOP_WIND].snd = SND_WIND; o[LOOP_WIND].vol = sat((pl->speed - 10.0f) / 40.0f) * 0.45f; o[LOOP_WIND].rate = 0.8f + 0.5f * sat(pl->speed / 50.0f);
        o[LOOP_SCRAPE].snd = SND_SCRAPE; o[LOOP_SCRAPE].vol = pl->scrape > 0.2f && pl->speed > 4.0f ? pl->scrape * 0.7f : 0.0f; o[LOOP_SCRAPE].rate = 0.9f + 0.3f * sat(pl->speed / 30.0f);
    }
    /* the three nearest other cars */
    int best[3] = { -1, -1, -1 };
    float bd[3] = { 1e9f, 1e9f, 1e9f };
    for (int i = 0; i < ncars; i++) {
        const Car *c = &cars[i];
        if (c->is_player) continue;
        float dx = c->x - r_cam.tx, dz = c->z - r_cam.tz, d = dx * dx + dz * dz;
        for (int k = 0; k < 3; k++)
            if (d < bd[k]) {
                for (int m = 2; m > k; m--) { bd[m] = bd[m - 1]; best[m] = best[m - 1]; }
                bd[k] = d; best[k] = i;
                break;
            }
    }
    for (int k = 0; k < 3; k++) {
        if (best[k] < 0) continue;
        const Car *c = &cars[best[k]];
        float v = vol_of(c);
        if (g.attract) v = 0;
        o[LOOP_AI0 + k].snd = eng_snd(c->body, 1);
        o[LOOP_AI0 + k].rate = eng_rate(c) * (1.0f + 0.02f * (float)(c->number % 3));
        o[LOOP_AI0 + k].vol = v * (0.25f + 0.35f * c->in.throttle);
        o[LOOP_AI0 + k].pan = pan_of(c);
    }
}
