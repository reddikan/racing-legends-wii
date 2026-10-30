/*
 * RACING LEGENDS - scenery: everything that stands beside the track.
 *
 * scenery_plan() scatters props over the landscape (deterministically, per track) and registers every lamp,
 * so the ground can be baked with their light. scenery_build() then emits the props' low-poly meshes, their
 * sun shadows and soft contact shadows.
 */
#include "track_int.h"

#include <string.h>

enum {
    P_PINE, P_TREE, P_ROCK, P_CACTUS, P_BUSH, P_STAND, P_GANTRY, P_LAMP, P_BUILDING, P_CONTAINERS, P_CRANE,
    P_CABIN, P_SNOWMAN, P_HAY, P_TENT, P_BOARD, P_LANTERN, P_FLOOD, P_FIRE, P_BOAT, P_DEADTREE, P_FLAGPOLE,
    P_CRYSTAL, P_DOME, P_ANTENNA,
};

typedef struct { uint8_t type, var; float x, z, y, s, rot; } Prop;
#define MAX_PROPS 5200
static Prop props[MAX_PROPS];
static int nprops;
static int ao_ready;

static Prop *add_prop(int type, float x, float z, float s, float rot, int var)
{
    if (nprops >= MAX_PROPS) return NULL;
    Prop *p = &props[nprops++];
    p->type = (uint8_t)type; p->var = (uint8_t)var; p->x = x; p->z = z; p->s = s; p->rot = rot;
    p->y = track_height(x, z);
    return p;
}

static int dry(float x, float z) { return trk.water < -999.0f || terrain_height(x, z) > trk.water + 0.4f; }

/* the start straight: a point beside the track at distance `dist`, `out` metres beyond the wall */
static void beside(float dist, int side, float out, float *x, float *z, float *yaw)
{
    int k = track_sample_at(dist);
    const TSample *s = &trk.s[k];
    float o = side < 0 ? -(s->wl + out) : (s->wr + out);
    track_point(dist, o, x, z);
    /* yaw so that local +z faces the track */
    float fx = -s->nx * (float)side, fz = -s->nz * (float)side;
    *yaw = atan2f(fx, fz);
}

/* ------------------------------------------------------------------ */
/* planning                                                            */
/* ------------------------------------------------------------------ */
static void scatter(int type, float cell, float min_clear, float max_clear, float density, uint32_t seed, float smin, float smax, int var)
{
    Rng r = { seed * 2654435761u + 1 };
    for (float z = trk.minz; z < trk.maxz; z += cell)
        for (float x = trk.minx; x < trk.maxx; x += cell) {
            float px = x + rng_r(&r, 0, cell), pz = z + rng_r(&r, 0, cell);
            float roll = rng_f(&r), sc = rng_r(&r, smin, smax), rot = rng_r(&r, 0, TAU);
            float cl = track_clearance(px, pz);
            if (cl < min_clear || cl > max_clear) continue;
            float d = density * (0.35f + 1.1f * smoothstep(0.35f, 0.7f, fbm(px / 60.0f, pz / 60.0f, seed)));
            if (roll > d) continue;
            if (!dry(px, pz)) continue;
            add_prop(type, px, pz, sc, rot, var);
        }
}

static void stands_at_start(int lights)
{
    float x, z, yaw;
    /* grandstands either side of the start, offset so they don't face each other directly */
    beside(trk.length - 40.0f, -1, 4.0f, &x, &z, &yaw);
    add_prop(P_STAND, x, z, 1.0f, yaw, lights);
    beside(25.0f, 1, 4.0f, &x, &z, &yaw);
    add_prop(P_STAND, x, z, 1.0f, yaw, lights);
    beside(0.0f, 1, 0.0f, &x, &z, &yaw);
    add_prop(P_GANTRY, trk.s[0].x, trk.s[0].z, 1.0f, atan2f(trk.s[0].tx, trk.s[0].tz), lights);
    /* advertising boards along the straight */
    for (float d = -120.0f; d < 140.0f; d += 9.0f) {
        if (d > -60 && d < 45) continue;
        for (int side = -1; side <= 1; side += 2) {
            beside(trk.length + d, side, 1.6f, &x, &z, &yaw);
            add_prop(P_BOARD, x, z, 1.0f, yaw, (int)((d + 200) / 9) % 5);
        }
    }
    if (lights) {
        const float fd[4] = { trk.length - 62.0f, 50.0f, trk.length - 80.0f, 110.0f };
        const int fs[4] = { -1, 1, 1, -1 };
        const float fo[4] = { 14.0f, 14.0f, 4.0f, 4.0f };
        for (int i = 0; i < 4; i++) {
            beside(fd[i], fs[i], fo[i], &x, &z, &yaw);
            Prop *p = add_prop(P_FLOOD, x, z, 1.0f, yaw, 0);
            if (!p) continue;
            float c = sinf(yaw), s = cosf(yaw);
            add_light(x + c * 16.0f, p->y + 9.0f, z + s * 16.0f, 44.0f, 0xD8E4FFFFu, 0.0f);
            add_light(x + c * 0.9f, p->y + 16.5f, z + s * 0.9f, 0.0f, 0xEEF4FFFFu, 4.5f);
        }
    }
}

static void plan_forest(void)
{
    stands_at_start(0);
    scatter(P_PINE, 10.0f, 4.0f, 66.0f, 0.5f, 1, 0.8f, 1.35f, 0);
    scatter(P_TREE, 13.0f, 4.0f, 66.0f, 0.22f, 2, 0.8f, 1.2f, 0);
    scatter(P_ROCK, 17.0f, 3.0f, 72.0f, 0.22f, 3, 0.6f, 1.6f, 0);
    scatter(P_BUSH, 9.0f, 2.0f, 30.0f, 0.25f, 4, 0.7f, 1.2f, 0);
    scatter(P_HAY, 40.0f, 1.5f, 5.0f, 0.5f, 5, 1.0f, 1.0f, 0);
    scatter(P_TENT, 70.0f, 3.0f, 12.0f, 0.6f, 6, 1.0f, 1.0f, 0);
    scatter(P_FLAGPOLE, 55.0f, 1.5f, 4.0f, 0.6f, 7, 1.0f, 1.0f, 0);
}

/* ---- the hover worlds ---- */
static void edge_lights(uint32_t a, uint32_t b, float r)
{
    for (int k = 0; k < trk.n; k += 9) {
        const TSample *s = &trk.s[k];
        int side = ((k / 9) & 1) ? 1 : -1;
        float o = side < 0 ? -(s->wl - 0.4f) : (s->wr - 0.4f);
        add_light(s->x + s->nx * o, s->y + 1.6f, s->z + s->nz * o, r, side < 0 ? a : b, 0.0f);
    }
}

static void plan_city(void)
{
    edge_lights(0x2AD8FFFFu, 0xFF3AC8FFu, 12.0f);
    Rng r = { 777 };
    for (float z = trk.minz; z < trk.maxz; z += 15.0f)
        for (float x = trk.minx; x < trk.maxx; x += 15.0f) {
            float px = x + rng_r(&r, 1, 14), pz = z + rng_r(&r, 1, 14);
            float w = rng_r(&r, 3.5f, 6.5f), d = rng_r(&r, 3.5f, 6.5f), roll = rng_f(&r);
            float cl = track_clearance(px, pz);
            if (cl < 5.0f + fmaxf(w, d) || cl > 70.0f || roll > 0.85f) continue;
            float hmax = clampf((cl - 6.0f) / 30.0f, 0.12f, 1.8f);
            Prop *p = add_prop(P_BUILDING, px, pz, rng_r(&r, 0.1f, 1.0f) * hmax, 0.0f, (int)(roll * 100.0f) % 6);
            if (p) {
                p->rot = floorf(w) * 16.0f + floorf(d);
                const uint32_t neon[5] = { 0xFF3AA8FFu, 0x3AE8FFFFu, 0xFFD23AFFu, 0x9A5AFFFFu, 0x5AFF8AFFu };
                if (cl < 26.0f) add_light(px, p->y + 4.0f, pz, 16.0f, neon[(int)(p->s * 5) % 5], 0.0f);
            }
        }
}

static void plan_mesa(void)
{
    scatter(P_ROCK, 10.0f, 4.0f, 80.0f, 0.4f, 11, 0.8f, 2.6f, 1);
    scatter(P_CRYSTAL, 14.0f, 4.0f, 90.0f, 0.3f, 12, 0.7f, 1.4f, 1);
    scatter(P_DOME, 60.0f, 8.0f, 60.0f, 0.5f, 13, 1.0f, 1.6f, 0);
    scatter(P_ANTENNA, 70.0f, 6.0f, 50.0f, 0.5f, 14, 1.0f, 1.0f, 0);
}

static void plan_ice(void)
{
    scatter(P_CRYSTAL, 9.0f, 4.0f, 80.0f, 0.45f, 21, 0.8f, 1.8f, 0);
    scatter(P_ROCK, 16.0f, 4.0f, 80.0f, 0.25f, 22, 0.8f, 2.2f, 2);
    scatter(P_DOME, 55.0f, 8.0f, 60.0f, 0.5f, 23, 1.0f, 1.8f, 1);
    scatter(P_ANTENNA, 70.0f, 6.0f, 50.0f, 0.4f, 24, 1.0f, 1.0f, 1);
}

static void plan_magma(void)
{
    edge_lights(0xFF6A1AFFu, 0xFFB03AFFu, 14.0f);
    scatter(P_ROCK, 9.0f, 4.0f, 80.0f, 0.4f, 41, 0.8f, 2.6f, 3);
    scatter(P_CRYSTAL, 13.0f, 4.0f, 90.0f, 0.35f, 42, 0.7f, 1.5f, 2);
    /* the lava lights its surroundings */
    Rng q = { 31 };
    for (int tries = 0; tries < 6000 && trk.nlight < MAX_LIGHTS - 20; tries++) {
        float px = rng_r(&q, trk.minx, trk.maxx), pz = rng_r(&q, trk.minz, trk.maxz);
        if (terrain_height(px, pz) > trk.water - 0.5f) continue;
        if (rng_f(&q) > 0.5f) continue;
        add_light(px, trk.water + 4.0f, pz, 34.0f, 0xFF5A1AFFu, 0.0f);
    }
}

void scenery_plan(void)
{
    nprops = 0;
    ao_ready = 0;
    switch (trk.info->theme) {
    case TH_FOREST: plan_forest(); break;
    case TH_CANYON: plan_mesa(); break;
    case TH_SNOW: plan_ice(); break;
    case TH_HARBOR: plan_city(); break;
    default: plan_magma(); break;
    }
    /* keep props from overlapping each other badly: drop small props inside bigger ones */
    for (int i = 0; i < nprops; i++) {
        Prop *a = &props[i];
        if (a->type != P_STAND && a->type != P_BUILDING && a->type != P_CABIN && a->type != P_GANTRY && a->type != P_FLOOD) continue;
        float rad = a->type == P_STAND ? 18.0f : (a->type == P_BUILDING ? 11.0f : (a->type == P_GANTRY ? 3.0f : 7.0f));
        for (int j = 0; j < nprops; j++) {
            Prop *b = &props[j];
            if (j == i || b->type == P_STAND || b->type == P_BUILDING || b->type == P_GANTRY || b->type == P_FLOOD || b->type == P_LAMP) continue;
            float dx = b->x - a->x, dz = b->z - a->z;
            if (dx * dx + dz * dz < rad * rad) b->type = 255;
        }
    }
}

/* ------------------------------------------------------------------ */
/* meshes                                                              */
/* ------------------------------------------------------------------ */
static void pine(const Prop *p)
{
    float s = p->s, h = 9.0f * s, y = p->y;
    int snowy = p->var == 1;
    uint32_t trunk = 0x6A4A32FFu;
    uint32_t g[3] = { 0x2E7A3EFFu, 0x3C8C46FFu, 0x52A452FFu };
    if (trk.info->theme == TH_LANTERN) { g[0] = 0x245A30FFu; g[1] = 0x2C6636FFu; g[2] = 0x387640FFu; }
    lit_prism(p->x, y - 0.2f, p->z, 0.35f * s, 0.25f * s, 2.0f * s, 5, p->rot, trunk, 0);
    float r = 3.0f * s, by = y + 1.4f * s;
    for (int t = 0; t < 3; t++) {
        float th = h * (0.42f - 0.06f * (float)t);
        uint32_t c = g[t];
        lit_prism(p->x, by, p->z, r, 0.0f, th, 7, p->rot + (float)t * 0.4f, c, 0);
        if (snowy) {
            /* white tip on each tier */
            float ty = by + th * 0.55f;
            lit_prism(p->x, ty, p->z, r * 0.45f + 0.05f, 0.0f, th * 0.45f + 0.02f, 7, p->rot + (float)t * 0.4f, 0xF2F6FFFFu, 0);
        }
        by += th * 0.55f;
        r *= 0.72f;
    }
    float pts[] = { p->x - 3 * s, y, p->z, p->x + 3 * s, y, p->z, p->x, y, p->z - 3 * s, p->x, y, p->z + 3 * s,
                    p->x - 2.2f * s, y + 3 * s, p->z, p->x + 2.2f * s, y + 3 * s, p->z, p->x, y + 3 * s, p->z - 2.2f * s, p->x, y + 3 * s, p->z + 2.2f * s,
                    p->x, by + 1.0f, p->z };
    shadow_hull(pts, 9, y);
    ao_blob(p->x, p->z, 3.2f * s, 0.35f);
}

static void blob_tree(const Prop *p)
{
    float s = p->s, y = p->y;
    uint32_t trunk = 0x6E4E36FFu;
    uint32_t leaf = p->var ? 0x4A9240FFu : 0x72B24AFFu;
    if (trk.info->theme == TH_LANTERN) leaf = 0x3A6A30FFu;
    lit_prism(p->x, y - 0.2f, p->z, 0.4f * s, 0.3f * s, 3.0f * s, 5, p->rot, trunk, 0);
    /* canopy: two stacked octagonal "domes" */
    float r = 3.4f * s, cy = y + 3.0f * s;
    uint32_t sd = (uint32_t)(p->x * 7.0f + p->z * 13.0f);
    lit_blob(p->x, cy + 2.2f * s, p->z, r * 1.0f, r * 0.9f, r * 1.0f, leaf, 0.06f, sd);
    lit_blob(p->x + cosf(p->rot) * r * 0.62f, cy + 1.2f * s, p->z + sinf(p->rot) * r * 0.62f, r * 0.62f, r * 0.58f, r * 0.62f, col_scale(leaf, 0.94f), 0.06f, sd + 1);
    lit_blob(p->x - cosf(p->rot + 0.9f) * r * 0.55f, cy + 1.5f * s, p->z - sinf(p->rot + 0.9f) * r * 0.55f, r * 0.58f, r * 0.55f, r * 0.58f, col_scale(leaf, 1.04f), 0.06f, sd + 2);
    float pts[16 * 3];
    for (int i = 0; i < 8; i++) {
        float a = (float)i / 8.0f * TAU;
        pts[i * 3] = p->x + cosf(a) * r; pts[i * 3 + 1] = cy + 1.4f * s; pts[i * 3 + 2] = p->z + sinf(a) * r;
        pts[24 + i * 3] = p->x + cosf(a) * 0.4f * s; pts[24 + i * 3 + 1] = y; pts[24 + i * 3 + 2] = p->z + sinf(a) * 0.4f * s;
    }
    shadow_hull(pts, 16, y);
    ao_blob(p->x, p->z, 3.4f * s, 0.35f);
}

static void dead_tree(const Prop *p)
{
    float s = p->s, y = p->y;
    uint32_t c = 0x8A6A52FFu;
    lit_prism(p->x, y - 0.2f, p->z, 0.3f * s, 0.15f * s, 4.5f * s, 5, p->rot, c, 0);
    for (int b = 0; b < 3; b++) {
        float a = p->rot + (float)b * 2.1f, by = y + (1.8f + (float)b * 0.9f) * s;
        float ex = p->x + cosf(a) * 1.8f * s, ez = p->z + sinf(a) * 1.8f * s;
        lit_quad(RM_OPAQUE, p->x, by, p->z, p->x, by + 0.25f * s, p->z, ex, by + 1.4f * s, ez, ex, by + 1.2f * s, ez, c);
        lit_quad(RM_OPAQUE, ex, by + 1.2f * s, ez, ex, by + 1.4f * s, ez, p->x, by + 0.25f * s, p->z, p->x, by, p->z, c);
    }
}

static void rock(const Prop *p)
{
    /* a squashed, jittered octahedron-ish boulder */
    float s = p->s * 1.4f;
    uint32_t base = p->var == 1 ? 0xB86E46FFu : (p->var == 2 ? 0x8A92A2FFu : (p->var == 3 ? 0x3A3436FFu : 0x8E8C88FFu));
    Rng r = { (uint32_t)(p->x * 131.0f + p->z * 17.0f) | 1u };
    const int N = 6;
    float ring[6][3], top[3] = { p->x + rng_r(&r, -0.3f, 0.3f) * s, p->y + rng_r(&r, 1.1f, 1.6f) * s, p->z + rng_r(&r, -0.3f, 0.3f) * s };
    for (int i = 0; i < N; i++) {
        float a = p->rot + (float)i / (float)N * TAU, rr = s * rng_r(&r, 0.85f, 1.25f);
        ring[i][0] = p->x + cosf(a) * rr; ring[i][1] = p->y + s * rng_r(&r, 0.35f, 0.65f); ring[i][2] = p->z + sinf(a) * rr;
    }
    lit_blob(p->x, p->y + 0.25f * s, p->z, s * 1.05f, s * 0.75f, s * 0.9f, base, 0.35f, (uint32_t)(p->x * 31.0f + p->z));
    if (p->var == 2 || trk.info->theme == TH_SNOW) {
        /* snow cap */
        float cy = top[1] - 0.05f;
        (void)cy;
        lit_blob(p->x, p->y + 0.7f * s, p->z, s * 0.75f, s * 0.38f, s * 0.65f, 0xF4F8FFFFu, 0.15f, (uint32_t)(p->z * 17.0f));
    }
    float pts[7 * 3];
    for (int i = 0; i < N; i++) { pts[i * 3] = ring[i][0]; pts[i * 3 + 1] = ring[i][1]; pts[i * 3 + 2] = ring[i][2]; }
    pts[18] = top[0]; pts[19] = top[1]; pts[20] = top[2];
    shadow_hull(pts, 7, p->y);
    ao_blob(p->x, p->z, 1.6f * s, 0.3f);
}

static void cactus(const Prop *p)
{
    float s = p->s, y = p->y;
    uint32_t c = 0x4E8A44FFu;
    lit_prism(p->x, y - 0.2f, p->z, 0.45f * s, 0.4f * s, 4.2f * s, 7, p->rot, c, 1);
    for (int a = 0; a < 2; a++) {
        float ang = p->rot + (float)a * PI, ah = y + (1.6f + (float)a * 0.7f) * s;
        float ax = p->x + cosf(ang) * 1.0f * s, az = p->z + sinf(ang) * 1.0f * s;
        float mx = p->x + cosf(ang) * 0.5f * s, mz = p->z + sinf(ang) * 0.5f * s;
        lit_box(mx, ah, mz, 0.5f * s, 0.25f * s, 0.25f * s, -ang, c, col_scale(c, 0.95f));
        lit_prism(ax, ah, az, 0.3f * s, 0.27f * s, 1.6f * s, 6, 0, c, 1);
    }
    float pts[] = { p->x - 1.2f * s, y, p->z, p->x + 1.2f * s, y, p->z, p->x, y + 4.2f * s, p->z, p->x - 1.2f * s, y + 3.0f * s, p->z, p->x + 1.2f * s, y + 3.0f * s, p->z };
    shadow_hull(pts, 5, y);
}

static void bush(const Prop *p)
{
    float s = p->s;
    uint32_t c = p->var ? 0x9A8A54FFu : 0x5A9A40FFu;
    lit_blob(p->x, p->y + 0.3f * s, p->z, 1.1f * s, 0.75f * s, 1.0f * s, c, 0.25f, (uint32_t)(p->x * 5.0f + p->z * 3.0f));
}

/* a box with only the faces that can be seen from above (no bottom) */
static void hay(const Prop *p)
{
    float s = p->s;
    float yaw = p->rot;
    lit_box(p->x, p->y - 0.05f, p->z, 0.8f * s, 0.45f * s, 0.6f * s, yaw, 0xE6C468FFu, 0xD0A850FFu);
    lit_box(p->x + cosf(yaw) * 1.8f, p->y - 0.05f, p->z + sinf(yaw) * 1.8f, 0.8f * s, 0.45f * s, 0.6f * s, yaw, 0xE6C468FFu, 0xD0A850FFu);
    lit_box(p->x + cosf(yaw) * 0.9f, p->y + 0.85f, p->z + sinf(yaw) * 0.9f, 0.8f * s, 0.45f * s, 0.6f * s, yaw + 0.1f, 0xEACB70FFu, 0xD4AC56FFu);
    float pts[] = { p->x - 1.2f, p->y, p->z - 1.2f, p->x + 3.0f, p->y, p->z + 1.2f, p->x + 1.0f, p->y + 1.8f, p->z, p->x - 1, p->y + 0.9f, p->z + 1, p->x + 2.8f, p->y + 0.9f, p->z - 1 };
    shadow_hull(pts, 5, p->y);
}

static void tent(const Prop *p)
{
    /* a pop-up canopy: four legs and a peaked roof in a bright colour */
    const uint32_t cols[6] = { 0xE83A3AFFu, 0x2A7AE8FFu, 0xF2C02AFFu, 0x2AB86AFFu, 0xF2F2F2FFu, 0xE87A2AFFu };
    uint32_t c = cols[(int)(p->x * 7 + p->z * 3) % 6 < 0 ? 0 : (int)(fabsf(p->x * 7 + p->z * 3)) % 6];
    float h = 2.4f, r = 2.2f, y = p->y;
    for (int i = 0; i < 4; i++) {
        float a = p->rot + (float)i * PI * 0.5f + PI * 0.25f;
        lit_box(p->x + cosf(a) * r, y, p->z + sinf(a) * r, 0.06f, h * 0.5f, 0.06f, 0, 0xCCCCCCFFu, 0xAAAAAAFFu);
    }
    lit_prism(p->x, y + h, p->z, r * 1.414f, 0.0f, 0.9f, 4, p->rot, c, 0);
    float pts[] = { p->x - r, y + h, p->z - r, p->x + r, y + h, p->z - r, p->x + r, y + h, p->z + r, p->x - r, y + h, p->z + r, p->x, y + h + 0.9f, p->z };
    shadow_hull(pts, 5, y);
    /* a table and some people under it */
    lit_box(p->x, y, p->z, 0.8f, 0.4f, 0.4f, p->rot, 0xF0F0F0FFu, 0xD0D0D0FFu);
}

static void person(float x, float y, float z, float yaw, uint32_t shirt, uint32_t skin)
{
    lit_box(x, y, z, 0.22f, 0.36f, 0.14f, yaw, shirt, col_scale(shirt, 0.9f));
    lit_box(x, y + 0.72f, z, 0.13f, 0.13f, 0.13f, yaw, skin, col_scale(skin, 0.95f));
}

static void stand(const Prop *p)
{
    /* a grandstand facing the track (local +z), 32 m long, 6 rows, roof on posts, crowd */
    const uint32_t shirts[8] = { 0xE84A3AFFu, 0x3A7AE8FFu, 0xF2D23AFFu, 0xF2F2F2FFu, 0x3AB86AFFu, 0xE87A2AFFu, 0x8A4AD8FFu, 0x2A2A30FFu };
    const uint32_t skins[4] = { 0xF0C8A0FFu, 0xD6A27AFFu, 0x9A6A48FFu, 0x6A4A32FFu };
    float c = cosf(p->rot), s = sinf(p->rot);
    /* local -> world: x' = x*c + z*s, z' = -x*s + z*c  (so local +z maps to (s, c)) */
#define LX(lx, lz) (p->x + (lx) * c + (lz) * s)
#define LZ(lx, lz) (p->z - (lx) * s + (lz) * c)
    float y = p->y, L = 16.0f;
    Rng r = { (uint32_t)(p->x * 31.0f) | 1u };
    for (int row = 0; row < 6; row++) {
        float lz = -(float)row * 1.4f, ly = y + (float)row * 0.75f;
        lit_box(LX(0, lz), y - 0.2f, LZ(0, lz), L, (ly - y + 0.55f) * 0.5f + 0.1f, 0.7f, -p->rot, 0xA8AEB8FFu, 0x7A808AFFu);
        for (float lx = -L + 0.8f; lx < L - 0.5f; lx += 0.95f) {
            if (rng_f(&r) < 0.12f) continue;
            float jx = rng_r(&r, -0.15f, 0.15f);
            person(LX(lx + jx, lz), ly + 0.55f, LZ(lx + jx, lz), -p->rot, shirts[rng_u(&r) % 8], skins[rng_u(&r) % 4]);
        }
    }
    /* back wall */
    float bz = -6 * 1.4f - 0.2f;
    lit_quad(RM_OPAQUE, LX(-L, bz), y, LZ(-L, bz), LX(L, bz), y, LZ(L, bz), LX(L, bz), y + 7.0f, LZ(L, bz), LX(-L, bz), y + 7.0f, LZ(-L, bz), 0x5A6270FFu);
    /* roof over the back rows only, so the crowd shows from above */
    uint32_t roof = p->var ? 0x30343CFFu : 0xD8DEE6FFu;
    float ry = y + 8.0f;
    lit_quad(RM_OPAQUE, LX(-L - 1, bz - 0.5f), ry, LZ(-L - 1, bz - 0.5f), LX(-L - 1, -3.6f), ry - 0.5f, LZ(-L - 1, -3.6f),
             LX(L + 1, -3.6f), ry - 0.5f, LZ(L + 1, -3.6f), LX(L + 1, bz - 0.5f), ry, LZ(L + 1, bz - 0.5f), roof);
    /* a red trim on the roof's front edge */
    lit_quad(RM_OPAQUE, LX(-L - 1, -3.6f), ry - 0.5f, LZ(-L - 1, -3.6f), LX(-L - 1, -3.6f), ry - 1.1f, LZ(-L - 1, -3.6f),
             LX(L + 1, -3.6f), ry - 1.1f, LZ(L + 1, -3.6f), LX(L + 1, -3.6f), ry - 0.5f, LZ(L + 1, -3.6f), 0xD83A30FFu);
    for (float lx = -L; lx <= L; lx += 8.0f) lit_box(LX(lx, -3.6f), y + 2.4f, LZ(lx, -3.6f), 0.12f, 2.6f, 0.12f, -p->rot, 0x90969EFFu, 0x70767EFFu);
    if (p->var) {
        for (float lx = -L + 4; lx <= L - 4; lx += 8.0f) add_light(LX(lx, -3.4f), ry - 1.3f, LZ(lx, -3.4f), 0.0f, 0xFFF0D0FFu, 0.7f);
    }
    float pts[] = { LX(-L - 1, bz - 0.5f), ry, LZ(-L - 1, bz - 0.5f), LX(-L - 1, -3.6f), ry, LZ(-L - 1, -3.6f),
                    LX(L + 1, -3.6f), ry, LZ(L + 1, -3.6f), LX(L + 1, bz - 0.5f), ry, LZ(L + 1, bz - 0.5f),
                    LX(-L, bz), y, LZ(-L, bz), LX(L, bz), y, LZ(L, bz), LX(-L, 1.8f), y, LZ(-L, 1.8f), LX(L, 1.8f), y, LZ(L, 1.8f) };
    shadow_hull(pts, 8, y);
#undef LX
#undef LZ
}

static void gantry(const Prop *p)
{
    /* the start/finish arch across the track: pillars just outside the road, a beam with a chequered band */
    const TSample *s = &trk.s[0];
    float w = s->hw + 1.6f, y = s->y;
    float lx = s->x - s->nx * w, lz = s->z - s->nz * w, rx = s->x + s->nx * w, rz = s->z + s->nz * w;
    uint32_t pil = 0x2C3038FFu, beam = 0xE8ECF0FFu;
    float yaw = -atan2f(s->tz, s->tx);
    lit_box(lx, y, lz, 0.4f, 3.6f, 0.4f, yaw, pil, pil);
    lit_box(rx, y, rz, 0.4f, 3.6f, 0.4f, yaw, pil, pil);
    float by = y + 6.4f, bh = 1.3f, t = 0.35f;
    /* beam faces (front and back), checker on the top band */
    float fx = s->tx * t, fz = s->tz * t;
    lit_quad(RM_OPAQUE, rx - fx, by, rz - fz, lx - fx, by, lz - fz, lx - fx, by + bh, lz - fz, rx - fx, by + bh, rz - fz, beam);
    lit_quad(RM_OPAQUE, lx + fx, by, lz + fz, rx + fx, by, rz + fz, rx + fx, by + bh, rz + fz, lx + fx, by + bh, lz + fz, beam);
    int nch = 16;
    for (int i = 0; i < nch; i++) {
        for (int r = 0; r < 2; r++) {
            float a = (float)i / (float)nch, b = (float)(i + 1) / (float)nch;
            float ax = lx + (rx - lx) * a, az = lz + (rz - lz) * a, bx = lx + (rx - lx) * b, bz = lz + (rz - lz) * b;
            float y0 = by + bh + (float)r * 0.35f;
            uint32_t c = ((i + r) & 1) ? 0x1A1A1EFFu : 0xF4F4F4FFu;
            lit_quad(RM_OPAQUE, ax - fx, y0, az - fz, ax - fx, y0 + 0.35f, az - fz, bx - fx, y0 + 0.35f, bz - fz, bx - fx, y0, bz - fz, c);
            lit_quad(RM_OPAQUE, bx + fx, y0, bz + fz, bx + fx, y0 + 0.35f, bz + fz, ax + fx, y0 + 0.35f, az + fz, ax + fx, y0, az + fz, c);
        }
    }
    lit_quad(RM_OPAQUE, lx - fx, by + bh + 0.7f, lz - fz, lx + fx, by + bh + 0.7f, lz + fz, rx + fx, by + bh + 0.7f, rz + fz, rx - fx, by + bh + 0.7f, rz - fz, 0xE83A30FFu);
    if (p->var) {
        for (int i = 1; i < 6; i++) {
            float a = (float)i / 6.0f;
            add_light(lx + (rx - lx) * a, by - 0.2f, lz + (rz - lz) * a, 0.0f, 0xFFF4E0FFu, 0.7f);
        }
    }
    float pts[] = { lx, by + bh + 0.7f, lz, rx, by + bh + 0.7f, rz, lx + fx * 2, by, lz + fz * 2, rx + fx * 2, by, rz + fz * 2, lx, y, lz, rx, y, rz };
    shadow_hull(pts, 6, y);
    (void)p;
}

static void board(const Prop *p)
{
    /* advertising hoarding: a panel with bold stripes in a "sponsor" colour scheme */
    const uint32_t A[5] = { 0xE8302AFFu, 0x1E5AD8FFu, 0xF2C02AFFu, 0x1AA860FFu, 0x202024FFu };
    const uint32_t B[5] = { 0xF4F4F4FFu, 0xF2C02AFFu, 0x202024FFu, 0xF4F4F4FFu, 0xE8302AFFu };
    float c = cosf(p->rot), s = sinf(p->rot), w = 4.2f, y = p->y, h = 1.1f;
    float x0 = p->x - c * w, z0 = p->z + s * w, x1 = p->x + c * w, z1 = p->z - s * w;
    /* front faces the track (+z local = (s, c)) */
    uint32_t ca = A[p->var % 5], cb = B[p->var % 5];
    lit_quad(RM_OPAQUE, x1, y, z1, x0, y, z0, x0, y + h * 0.55f, z0, x1, y + h * 0.55f, z1, ca);
    lit_quad(RM_OPAQUE, x1, y + h * 0.55f, z1, x0, y + h * 0.55f, z0, x0, y + h, z0, x1, y + h, z1, cb);
    float mx = p->x, mz = p->z;
    lit_quad(RM_OPAQUE, mx + c * 0.6f, y + 0.3f, mz - s * 0.6f, mx - c * 1.8f, y + 0.3f, mz + s * 1.8f, mx - c * 1.8f, y + 0.8f, mz + s * 1.8f, mx + c * 0.6f, y + 0.8f, mz - s * 0.6f, cb);
    lit_quad(RM_OPAQUE, x0, y, z0, x1, y, z1, x1, y + h, z1, x0, y + h, z0, 0x50545CFFu);
}

static void lamp(const Prop *p)
{
    float y = p->y, c = sinf(p->rot), s = cosf(p->rot);
    lit_box(p->x, y, p->z, 0.12f, 3.6f, 0.12f, 0, 0x3A3E46FFu, 0x30343CFFu);
    lit_box(p->x + c * 0.8f, y + 7.0f, p->z + s * 0.8f, 0.9f, 0.08f, 0.1f, -p->rot + PI * 0.5f, 0x3A3E46FFu, 0x30343CFFu);
    emis_quad(RM_OPAQUE, p->x + c * 1.2f - 0.35f, y + 6.95f, p->z + s * 1.2f - 0.35f, p->x + c * 1.2f + 0.35f, y + 6.95f, p->z + s * 1.2f - 0.35f,
              p->x + c * 1.2f + 0.35f, y + 6.95f, p->z + s * 1.2f + 0.35f, p->x + c * 1.2f - 0.35f, y + 6.95f, p->z + s * 1.2f + 0.35f,
              p->var ? 0xFFE0B0FFu : 0xEAF2FFFFu);
}

static void lantern_post(const Prop *p)
{
    float y = p->y;
    lit_box(p->x, y, p->z, 0.1f, 1.5f, 0.1f, p->rot, 0x5A3A22FFu, 0x4A2E1AFFu);
    lit_box(p->x, y + 2.8f, p->z, 0.25f, 0.3f, 0.25f, p->rot, 0xFFC070FFu, 0xFFB050FFu);
    lit_prism(p->x, y + 3.4f, p->z, 0.36f, 0.0f, 0.3f, 4, p->rot, 0x2A1E16FFu, 0);
}

static void flood(const Prop *p)
{
    float y = p->y;
    lit_prism(p->x, y, p->z, 0.35f, 0.25f, 16.0f, 6, 0, 0x8A9098FFu, 0);
    float c = sinf(p->rot), s = cosf(p->rot);
    float hx = p->x + c * 0.6f, hz = p->z + s * 0.6f;
    lit_box(hx, y + 15.5f, hz, 2.0f, 1.0f, 0.3f, -p->rot, 0x50545CFFu, 0x50545CFFu);
    emis_quad(RM_OPAQUE, hx - 1.8f * cosf(p->rot) + c * 0.32f, y + 15.6f, hz + 1.8f * sinf(p->rot) + s * 0.32f,
              hx + 1.8f * cosf(p->rot) + c * 0.32f, y + 15.6f, hz - 1.8f * sinf(p->rot) + s * 0.32f,
              hx + 1.8f * cosf(p->rot) + c * 0.32f, y + 17.4f, hz - 1.8f * sinf(p->rot) + s * 0.32f,
              hx - 1.8f * cosf(p->rot) + c * 0.32f, y + 17.4f, hz + 1.8f * sinf(p->rot) + s * 0.32f, 0xF4F8FFFFu);
}

static void fire(const Prop *p)
{
    float y = p->y;
    for (int i = 0; i < 5; i++) {
        float a = (float)i / 5.0f * TAU;
        lit_box(p->x + cosf(a) * 0.9f, y - 0.1f, p->z + sinf(a) * 0.9f, 0.3f, 0.2f, 0.2f, a, 0x6A6A70FFu, 0x5A5A60FFu);
    }
    emis_tri(RM_OPAQUE, p->x - 0.6f, y + 0.1f, p->z, p->x + 0.6f, y + 0.1f, p->z, p->x, y + 1.4f, p->z, 0xFFB040FFu);
    emis_tri(RM_OPAQUE, p->x, y + 0.1f, p->z - 0.6f, p->x, y + 0.1f, p->z + 0.6f, p->x, y + 1.4f, p->z, 0xFF8A20FFu);
    for (int i = 0; i < 6; i++) {
        float a = (float)i / 6.0f * TAU + 0.5f;
        lit_box(p->x + cosf(a) * 3.0f, y, p->z + sinf(a) * 3.0f, 0.6f, 0.2f, 0.25f, a + 1.57f, 0x7A5A3AFFu, 0x6A4A2AFFu);
    }
}

static void building(const Prop *p)
{
    float w = floorf(p->rot / 16.0f), d = p->rot - w * 16.0f;
    float h = 6.0f + p->s * 22.0f;
    const uint32_t walls[6] = { 0x4A505CFFu, 0x5C4A44FFu, 0x3E4656FFu, 0x5A5A62FFu, 0x44505AFFu, 0x56483EFFu };
    uint32_t wall = walls[p->var % 6];
    float y = p->y - 0.3f;
    lit_box(p->x, y, p->z, w, h * 0.5f, d, 0, col_scale(wall, 0.8f), wall);
    /* rooftop box */
    lit_box(p->x + w * 0.3f, y + h, p->z - d * 0.2f, w * 0.3f, 0.8f, d * 0.3f, 0, 0x30343CFFu, 0x3A3E46FFu);
    /* windows: emissive at night in a few warm / cool tints, a scattering of dark ones */
    Rng r = { (uint32_t)(p->x * 13 + p->z * 7) | 1u };
    for (int face = 0; face < 4; face++) {
        float fw = (face & 1) ? d : w;
        float nx = face == 0 ? 0 : (face == 1 ? 1 : (face == 2 ? 0 : -1)), nz = face == 0 ? -1 : (face == 1 ? 0 : (face == 2 ? 1 : 0));
        float tx = -nz, tz = nx;
        float cx = p->x + nx * (w + 0.05f), cz = p->z + nz * (d + 0.05f);
        if (face & 1) { cx = p->x + nx * (w + 0.05f); cz = p->z; }
        else { cx = p->x; cz = p->z + nz * (d + 0.05f); }
        for (float yy = y + 2.0f; yy < y + h - 1.5f; yy += 3.0f)
            for (float u = -fw + 1.2f; u < fw - 1.0f; u += 2.2f) {
                float roll = rng_f(&r);
                if (roll < 0.35f) continue;
                uint32_t wc = roll < 0.75f ? 0xFFD9A0FFu : (roll < 0.9f ? 0xB8D8FFFFu : 0xFFF4E0FFu);
                wc = col_scale(wc, 0.75f + 0.25f * rng_f(&r));
                float x0 = cx + tx * u, z0 = cz + tz * u, x1 = cx + tx * (u + 1.2f), z1 = cz + tz * (u + 1.2f);
                emis_quad(RM_OPAQUE, x0, yy, z0, x1, yy, z1, x1, yy + 1.5f, z1, x0, yy + 1.5f, z0, wc);
            }
    }
    /* neon strip around the roof edge when close to the track */
    if (track_clearance(p->x, p->z) < 30.0f) {
        const uint32_t neon[5] = { 0xFF3AA8FFu, 0x3AE8FFFFu, 0xFFD23AFFu, 0x9A5AFFFFu, 0x5AFF8AFFu };
        uint32_t nc = neon[(int)(p->s * 5) % 5];
        float ny = y + h - 0.6f;
        float xs[5] = { p->x - w - 0.1f, p->x + w + 0.1f, p->x + w + 0.1f, p->x - w - 0.1f, p->x - w - 0.1f };
        float zs[5] = { p->z - d - 0.1f, p->z - d - 0.1f, p->z + d + 0.1f, p->z + d + 0.1f, p->z - d - 0.1f };
        for (int i = 0; i < 4; i++)
            emis_quad(RM_OPAQUE, xs[i], ny, zs[i], xs[i + 1], ny, zs[i + 1], xs[i + 1], ny + 0.35f, zs[i + 1], xs[i], ny + 0.35f, zs[i], nc);
        /* a vertical sign */
        emis_quad(RM_OPAQUE, p->x - w - 0.2f, y + h * 0.35f, p->z - 0.6f, p->x - w - 0.2f, y + h * 0.35f, p->z + 0.6f,
                  p->x - w - 0.2f, y + h * 0.8f, p->z + 0.6f, p->x - w - 0.2f, y + h * 0.8f, p->z - 0.6f, nc);
    }
}

static void containers(const Prop *p)
{
    const uint32_t cols[6] = { 0xC83A2AFFu, 0x2A6AB8FFu, 0x2A9A5AFFu, 0xD89A2AFFu, 0x8A8E96FFu, 0x6A3A8AFFu };
    Rng r = { (uint32_t)(p->x * 5 + p->z * 11) | 1u };
    float c = cosf(p->rot), s = sinf(p->rot);
    for (int i = 0; i < 3; i++)
        for (int lvl = 0; lvl < 3; lvl++) {
            if (rng_f(&r) < 0.3f * (float)lvl) continue;
            float ox = (float)(i - 1) * 2.6f;
            lit_box(p->x + c * ox, p->y + (float)lvl * 2.6f - 0.1f, p->z + s * ox, 1.25f, 1.3f, 6.0f, -p->rot, cols[rng_u(&r) % 6], cols[rng_u(&r) % 6]);
        }
}

static void crane(const Prop *p)
{
    float y = p->y;
    uint32_t c = 0xE8A02AFFu;
    for (int i = 0; i < 4; i++) {
        float a = (float)i * PI * 0.5f + PI * 0.25f;
        lit_box(p->x + cosf(a) * 2.5f, y, p->z + sinf(a) * 2.5f, 0.35f, 9.0f, 0.35f, 0, c, col_scale(c, 0.85f));
    }
    lit_box(p->x, y + 18.0f, p->z, 3.0f, 2.0f, 3.0f, p->rot, c, col_scale(c, 0.9f));
    float ca = cosf(p->rot), sa = sinf(p->rot);
    lit_box(p->x + ca * 8.0f, y + 22.0f, p->z + sa * 8.0f, 14.0f, 0.6f, 0.8f, -p->rot, c, col_scale(c, 0.85f));
}

static void boat(const Prop *p)
{
    float c = cosf(p->rot), s = sinf(p->rot), L = 7.0f * p->s, W = 2.2f * p->s;
    float y = trk.water - 0.2f;
    uint32_t hull = p->var == 0 ? 0xF0F0F0FFu : (p->var == 1 ? 0x2A4A8AFFu : 0xB8302AFFu);
    float bx = p->x + c * L, bz = p->z + s * L;
    float lx = -s * W, lz = c * W;
    lit_quad(RM_OPAQUE, p->x - c * L + lx, y + 1.2f, p->z - s * L + lz, p->x + c * L * 0.5f + lx, y + 1.2f, p->z + s * L * 0.5f + lz,
             p->x + c * L * 0.5f - lx, y + 1.2f, p->z + s * L * 0.5f - lz, p->x - c * L - lx, y + 1.2f, p->z - s * L - lz, 0xC8B49AFFu);
    lit_tri(RM_OPAQUE, p->x + c * L * 0.5f - lx, y + 1.2f, p->z + s * L * 0.5f - lz, p->x + c * L * 0.5f + lx, y + 1.2f, p->z + s * L * 0.5f + lz, bx, y + 1.3f, bz, 0xC8B49AFFu);
    lit_box(p->x - c * L * 0.2f, y + 1.2f, p->z - s * L * 0.2f, 2.0f * p->s, 0.8f, 1.4f * p->s, -p->rot, 0xF4F4F4FFu, hull);
    if (trk.info->night) add_light(bx, y + 3.0f, bz, 0.0f, 0x40FF60FFu, 0.8f);
}

static void cabin(const Prop *p)
{
    float y = p->y - 0.2f;
    float c = cosf(p->rot), s = sinf(p->rot);
    lit_box(p->x, y, p->z, 3.5f, 1.4f, 2.8f, -p->rot, 0x7A5236FFu, 0x8A5C3AFFu);
    /* pitched roof with snow */
    float hx = c * 4.0f, hz = -s * 4.0f, dx = s * 3.4f, dz = c * 3.4f;
    float ry = y + 2.8f, ty = y + 4.6f;
    lit_quad(RM_OPAQUE, p->x - hx - dx, ry, p->z - hz - dz, p->x + hx - dx, ry, p->z + hz - dz, p->x + hx, ty, p->z + hz, p->x - hx, ty, p->z - hz, 0xF4F8FFFFu);
    lit_quad(RM_OPAQUE, p->x + hx + dx, ry, p->z + hz + dz, p->x - hx + dx, ry, p->z - hz + dz, p->x - hx, ty, p->z - hz, p->x + hx, ty, p->z + hz, 0xF4F8FFFFu);
    lit_box(p->x + hx * 0.5f, y + 3.0f, p->z + hz * 0.5f, 0.4f, 1.2f, 0.4f, -p->rot, 0x7A7A80FFu, 0x6A6A70FFu);
    float pts[] = { p->x - hx - dx, ry, p->z - hz - dz, p->x + hx - dx, ry, p->z + hz - dz, p->x + hx + dx, ry, p->z + hz + dz, p->x - hx + dx, ry, p->z - hz + dz, p->x, ty, p->z };
    shadow_hull(pts, 5, y);
}

static void snowman(const Prop *p)
{
    float y = p->y;
    lit_prism(p->x, y - 0.1f, p->z, 0.8f, 0.85f, 0.7f, 8, 0, 0xF6F8FFFFu, 0);
    lit_prism(p->x, y + 0.6f, p->z, 0.85f, 0.45f, 0.6f, 8, 0, 0xF6F8FFFFu, 0);
    lit_prism(p->x, y + 1.2f, p->z, 0.55f, 0.5f, 0.45f, 8, 0, 0xF6F8FFFFu, 0);
    lit_prism(p->x, y + 1.65f, p->z, 0.5f, 0.1f, 0.4f, 8, 0, 0xF6F8FFFFu, 0);
    lit_prism(p->x, y + 1.9f, p->z, 0.35f, 0.35f, 0.4f, 6, 0, 0x2A2A30FFu, 1);
    float c = cosf(p->rot), s = sinf(p->rot);
    lit_tri(RM_OPAQUE, p->x + c * 0.4f, y + 1.55f, p->z + s * 0.4f, p->x + c * 0.4f, y + 1.45f, p->z + s * 0.4f + 0.1f, p->x + c * 0.9f, y + 1.5f, p->z + s * 0.9f, 0xF08A2AFFu);
    lit_box(p->x, y + 1.15f, p->z, 0.5f, 0.08f, 0.5f, p->rot, 0xD83A30FFu, 0xD83A30FFu);
}

static void flagpole(const Prop *p)
{
    float y = p->y;
    lit_box(p->x, y, p->z, 0.06f, 2.6f, 0.06f, 0, 0xD0D0D0FFu, 0xB0B0B0FFu);
    const uint32_t cols[4] = { 0xE83A30FFu, 0x2A6AE8FFu, 0xF2C02AFFu, 0x2AB86AFFu };
    uint32_t c = p->var == 2 ? (((int)fabsf(p->x)) & 1 ? 0xE83A30FFu : 0x2A5AD8FFu) : cols[(int)fabsf(p->x + p->z) % 4];
    float a = p->rot, fx = cosf(a) * 1.6f, fz = sinf(a) * 1.6f;
    lit_quad(RM_OPAQUE, p->x, y + 3.8f, p->z, p->x + fx, y + 4.4f, p->z + fz, p->x + fx, y + 5.0f, p->z + fz, p->x, y + 5.2f, p->z, c);
    lit_quad(RM_OPAQUE, p->x + fx, y + 4.4f, p->z + fz, p->x, y + 3.8f, p->z, p->x, y + 5.2f, p->z, p->x + fx, y + 5.0f, p->z + fz, col_scale(c, 0.85f));
}

static void crystal(const Prop *p)
{
    const uint32_t C[3] = { 0x5AE8F0FFu, 0xB86AFFFFu, 0xFF8A3AFFu };
    uint32_t c = C[p->var % 3];
    Rng r = { (uint32_t)(p->x * 17 + p->z * 29) | 1u };
    for (int i = 0; i < 4; i++) {
        float a = rng_r(&r, 0, TAU), d = rng_r(&r, 0, 1.2f) * p->s, h = rng_r(&r, 1.5f, 4.0f) * p->s, rr = rng_r(&r, 0.3f, 0.6f) * p->s;
        float x = p->x + cosf(a) * d, z = p->z + sinf(a) * d, y = p->y - 0.2f;
        lit_prism(x, y, z, rr, rr * 0.8f, h * 0.7f, 6, a, c, 0);
        lit_prism(x, y + h * 0.7f, z, rr * 0.8f, 0.0f, h * 0.3f, 6, a, col_scale(c, 1.2f), 0);
    }
    if (trk.info->night) add_light(p->x, p->y + 2.0f, p->z, 0.0f, c, 1.2f * p->s);
    ao_blob(p->x, p->z, 2.0f * p->s, 0.35f);
}

static void dome(const Prop *p)
{
    float r = 5.0f * p->s;
    lit_prism(p->x, p->y - 0.3f, p->z, r * 1.05f, r * 1.05f, 0.8f, 10, 0, 0x6A7080FFu, 0);
    lit_blob(p->x, p->y + 0.5f, p->z, r, r * 0.7f, r, p->var ? 0x9AC8F0FFu : 0xC8A898FFu, 0.0f, 3);
    lit_box(p->x + r, p->y, p->z, 1.4f, 1.0f, 1.0f, 0, 0x8A90A0FFu, 0x6A7080FFu);
    float pts[] = { p->x - r, p->y, p->z, p->x + r, p->y, p->z, p->x, p->y, p->z - r, p->x, p->y, p->z + r, p->x, p->y + r * 0.7f, p->z };
    shadow_hull(pts, 5, p->y);
}

static void antenna(const Prop *p)
{
    lit_prism(p->x, p->y - 0.2f, p->z, 0.5f, 0.15f, 14.0f, 6, 0, 0x8A90A0FFu, 0);
    lit_box(p->x, p->y + 9.0f, p->z, 1.6f, 0.08f, 0.08f, 0.6f, 0x8A90A0FFu, 0x6A7080FFu);
    add_light(p->x, p->y + 14.2f, p->z, 0.0f, 0xFF3020FFu, 0.9f);
    float pts[] = { p->x - 0.5f, p->y, p->z, p->x + 0.5f, p->y, p->z, p->x, p->y + 14.0f, p->z };
    shadow_hull(pts, 3, p->y);
}

void scenery_build(void)
{
    for (int i = 0; i < nprops; i++) {
        const Prop *p = &props[i];
        switch (p->type) {
        case P_PINE: pine(p); break;
        case P_TREE: blob_tree(p); break;
        case P_DEADTREE: dead_tree(p); break;
        case P_ROCK: rock(p); break;
        case P_CACTUS: cactus(p); break;
        case P_BUSH: bush(p); break;
        case P_HAY: hay(p); break;
        case P_TENT: tent(p); break;
        case P_STAND: stand(p); break;
        case P_GANTRY: gantry(p); break;
        case P_BOARD: board(p); break;
        case P_LAMP: lamp(p); break;
        case P_LANTERN: lantern_post(p); break;
        case P_FLOOD: flood(p); break;
        case P_FIRE: fire(p); break;
        case P_BUILDING: building(p); break;
        case P_CONTAINERS: containers(p); break;
        case P_CRANE: crane(p); break;
        case P_BOAT: boat(p); break;
        case P_CABIN: cabin(p); break;
        case P_SNOWMAN: snowman(p); break;
        case P_FLAGPOLE: flagpole(p); break;
        case P_CRYSTAL: crystal(p); break;
        case P_DOME: dome(p); break;
        case P_ANTENNA: antenna(p); break;
        default: break;
        }
    }
}

/* soft contact shading: every prop darkens the ground around it */
#define AOB 16.0f
#define AON 96
static int16_t ao_head[AON * AON], ao_next[MAX_PROPS];

float scenery_ao(float x, float z)
{
    if (!ao_ready) {
        for (int i = 0; i < AON * AON; i++) ao_head[i] = -1;
        for (int i = 0; i < nprops; i++) {
            int bx = (int)((props[i].x - trk.minx) / AOB), bz = (int)((props[i].z - trk.minz) / AOB);
            if (bx < 0 || bz < 0 || bx >= AON || bz >= AON) continue;
            ao_next[i] = ao_head[bz * AON + bx]; ao_head[bz * AON + bx] = (int16_t)i;
        }
        ao_ready = 1;
    }
    int bx = (int)((x - trk.minx) / AOB), bz = (int)((z - trk.minz) / AOB);
    float k = 1.0f;
    for (int zz = bz - 1; zz <= bz + 1; zz++)
        for (int xx = bx - 1; xx <= bx + 1; xx++) {
            if (xx < 0 || zz < 0 || xx >= AON || zz >= AON) continue;
            for (int i = ao_head[zz * AON + xx]; i >= 0; i = ao_next[i]) {
                const Prop *p = &props[i];
                float r, st;
                switch (p->type) {
                case P_PINE: case P_TREE: r = 5.5f * p->s; st = 0.42f; break;
                case P_ROCK: r = 2.8f * p->s; st = 0.35f; break;
                case P_BUSH: case P_HAY: case P_CACTUS: r = 2.4f * p->s; st = 0.3f; break;
                case P_STAND: r = 20.0f; st = 0.35f; break;
                case P_BUILDING: case P_CABIN: case P_CONTAINERS: r = 11.0f; st = 0.35f; break;
                case P_TENT: case P_LANTERN: case P_LAMP: case P_SNOWMAN: case P_FLAGPOLE: r = 2.6f; st = 0.25f; break;
                default: continue;
                }
                float dx = x - p->x, dz = z - p->z, d2 = dx * dx + dz * dz;
                if (d2 >= r * r) continue;
                float f = 1.0f - sqrtf(d2) / r;
                k *= 1.0f - st * f * f;
            }
        }
    return k < 0.45f ? 0.45f : k;
}
