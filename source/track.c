/*
 * RACING LEGENDS - track generation. See track.h.
 *
 * A circuit is a closed Catmull-Rom spline through hand-placed control points, resampled every 2 m. From the
 * samples come the road (with kerbs on the corners and run-off either side), the barriers, a racing line for
 * the AI and a speed profile along it, and a terrain heightfield that is flattened under the track and rises
 * into each theme's landscape (forest hills and a lake, canyon cliffs, snowy peaks, a harbour, valley
 * meadows). Lighting - sun, sky and every lamp - is baked into the vertex colours.
 */
#include "track_int.h"
#include "ui.h"

#include <string.h>
#include <stdlib.h>

Track trk;

int    g_w, g_h;
float  g_x0, g_z0;
float *g_hgt, *g_dst;
int16_t *g_smp;
float  g_wallmax;
static uint32_t *g_col;

#define SAMPLE_STEP 2.0f
#define MAX_SAMPLES 1400
#define MARGIN 150.0f

const TrackInfo TRACK_INFO[NUM_TRACKS] = {
    { "PINEWOOD LAKE", "ALPINE FOREST CIRCUIT", TH_FOREST, 0, SURF_ASPHALT, 1,
      { -0.50f, 0.62f, 0.60f }, 0xFFE9C8FF, 0x9DB8E0FF, 0x5A6A3CFF, 0x3E6A3AFF, 1.00f },
    { "RED ROCK CANYON", "DESERT DIRT RUN", TH_CANYON, 0, SURF_DIRT, 2,
      { 0.62f, 0.52f, -0.58f }, 0xFFDDB0FF, 0xA9B9D8FF, 0x8A5A3AFF, 0xC27E50FF, 0.93f },
    { "FROSTPEAK PASS", "SNOWBOUND MOUNTAIN", TH_SNOW, 0, SURF_SNOW, 3,
      { 0.45f, 0.58f, 0.68f }, 0xFFF2E4FF, 0xA8C4EEFF, 0x9AA6BCFF, 0xC8D4E6FF, 0.86f },
    { "NEON HARBOR", "CITY STREETS AT NIGHT", TH_HARBOR, 1, SURF_ASPHALT, 4,
      { 0.35f, 0.80f, 0.48f }, 0x6A78B4FF, 0x283460FF, 0x161C2AFF, 0x0B0E18FF, 1.00f },
    { "LANTERN VALLEY", "MIDNIGHT DIRT RALLY", TH_LANTERN, 1, SURF_DIRT, 5,
      { -0.40f, 0.75f, 0.52f }, 0x5A6C9AFF, 0x1E2A44FF, 0x141A12FF, 0x0C1210FF, 0.94f },
};

/* control points (x, z) in metres */
static const float P_FOREST[] = { 0,0, 90,0, 160,-10, 205,-50, 210,-110, 175,-150, 120,-150, 85,-180, 90,-240, 150,-270, 230,-260,
    290,-210, 320,-130, 310,-40, 270,40, 200,90, 110,95, 30,85, -40,95, -100,70, -120,20, -90,-10, -40,-5 };
static const float P_CANYON[] = { 0,0, 80,-20, 150,-70, 170,-140, 140,-200, 80,-210, 40,-250, 60,-310, 140,-330, 230,-300, 270,-230,
    250,-150, 290,-80, 360,-60, 400,0, 370,70, 290,90, 220,60, 160,90, 110,150, 30,150, -40,110, -70,50, -50,10 };
static const float P_SNOW[] = { 0,0, 100,0, 180,30, 210,100, 170,160, 100,170, 60,220, 90,290, 170,300, 240,270, 300,300, 320,370,
    270,430, 170,440, 70,420, -20,380, -80,320, -70,250, -110,190, -160,140, -150,70, -90,20 };
static const float P_HARBOR[] = { 0,0, 120,0, 170,-10, 190,-60, 190,-130, 220,-170, 280,-170, 300,-120, 300,-40, 330,20, 330,90,
    280,130, 200,130, 150,170, 150,240, 100,280, 20,280, -30,240, -30,170, -80,130, -110,80, -90,20, -50,0 };
static const float P_LANTERN[] = { 0,0, 90,10, 160,50, 170,120, 130,170, 150,230, 220,250, 270,210, 260,140, 300,90, 370,100,
    390,170, 350,260, 270,320, 170,330, 80,300, 30,240, -30,230, -90,190, -100,120, -60,60, -40,15 };

typedef struct { const float *p; int n; float hw, ro, hamp, ph1, ph2; float water; } TDef;
static const TDef TDEF[NUM_TRACKS] = {
    { P_FOREST, sizeof P_FOREST / 8, 7.6f, 5.0f, 5.0f, 0.4f, 2.1f, -2.6f },
    { P_CANYON, sizeof P_CANYON / 8, 7.4f, 5.0f, 8.0f, 1.3f, 0.2f, -5.0f },
    { P_SNOW, sizeof P_SNOW / 8, 8.0f, 6.0f, 12.0f, 2.6f, 1.0f, -2.0f },
    { P_HARBOR, sizeof P_HARBOR / 8, 7.2f, 4.0f, 0.0f, 0.0f, 0.0f, -2.2f },
    { P_LANTERN, sizeof P_LANTERN / 8, 7.4f, 5.0f, 6.0f, 0.9f, 3.0f, -1.8f },
};

/* lakes, sea and ponds: centre, radius (terrain dips under the water level there, never near the track) */
typedef struct { float x, z, r; } Basin;
static const Basin BASINS[NUM_TRACKS][6] = {
    { { 250, -110, 70 }, { -40, 200, 120 }, { 420, -60, 90 }, { 0, 0, 0 } },
    { { 330, -230, 60 }, { 120, -420, 110 }, { 0, 0, 0 } },
    { { 120, 360, 55 }, { -200, 300, 90 }, { 0, 0, 0 } },
    { { 120, -110, 80 }, { 60, -290, 170 }, { 420, -90, 120 }, { 230, -330, 120 }, { 0, 0, 0 } },
    { { 330, 200, 30 }, { 50, 140, 45 }, { 0, 0, 0 } },
};

/* ------------------------------------------------------------------ */
/* lighting                                                            */
/* ------------------------------------------------------------------ */
static float L_sun[3], L_sunc[3], L_sky[3], L_gnd[3];

static void col3(uint32_t c, float *o) { o[0] = (float)(c >> 24) / 255.0f; o[1] = (float)((c >> 16) & 255) / 255.0f; o[2] = (float)((c >> 8) & 255) / 255.0f; }

void add_light(float x, float y, float z, float r, uint32_t c, float glow)
{
    if (trk.nlight >= MAX_LIGHTS) return;
    TLight *l = &trk.light[trk.nlight++];
    l->x = x; l->y = y; l->z = z; l->r = r; l->c = c; l->glow = glow;
}

/* lights near a point: a coarse bucket grid so the bake stays quick */
#define LB 32.0f
#define LBN 40
static int16_t lb_head[LBN * LBN], lb_next[MAX_LIGHTS];
static int lb_ready;

static void light_buckets(void)
{
    for (int i = 0; i < LBN * LBN; i++) lb_head[i] = -1;
    for (int i = 0; i < trk.nlight; i++) {
        int bx = (int)((trk.light[i].x - g_x0) / LB), bz = (int)((trk.light[i].z - g_z0) / LB);
        if (bx < 0) bx = 0;
        if (bz < 0) bz = 0;
        if (bx >= LBN) bx = LBN - 1;
        if (bz >= LBN) bz = LBN - 1;
        lb_next[i] = lb_head[bz * LBN + bx];
        lb_head[bz * LBN + bx] = (int16_t)i;
    }
    lb_ready = 1;
}

uint32_t track_light(uint32_t c, float nx, float ny, float nz, float px, float py, float pz)
{
    float a[3];
    col3(c, a);
    float dl = nx * L_sun[0] + ny * L_sun[1] + nz * L_sun[2];
    dl = dl < 0 ? 0 : dl;
    float hs = 0.5f + 0.5f * ny;
    float lr = L_sky[0] * hs + L_gnd[0] * (1 - hs) + L_sunc[0] * dl;
    float lg = L_sky[1] * hs + L_gnd[1] * (1 - hs) + L_sunc[1] * dl;
    float lbl = L_sky[2] * hs + L_gnd[2] * (1 - hs) + L_sunc[2] * dl;
    float er = 0, eg = 0, eb = 0;
    if (lb_ready && trk.nlight) {
        int bx = (int)((px - g_x0) / LB), bz = (int)((pz - g_z0) / LB);
        for (int zz = bz - 1; zz <= bz + 1; zz++)
            for (int xx = bx - 1; xx <= bx + 1; xx++) {
                if (xx < 0 || zz < 0 || xx >= LBN || zz >= LBN) continue;
                for (int i = lb_head[zz * LBN + xx]; i >= 0; i = lb_next[i]) {
                    const TLight *L = &trk.light[i];
                    float dx = L->x - px, dy = L->y - py, dz = L->z - pz;
                    float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 >= L->r * L->r) continue;
                    float d = sqrtf(d2) + 1e-3f;
                    float att = 1.0f - d / L->r;
                    att *= att;
                    float ndl = (dx * nx + dy * ny + dz * nz) / d;
                    ndl = 0.35f + 0.65f * (ndl < 0 ? 0 : ndl);
                    float lc[3];
                    col3(L->c, lc);
                    float k = att * ndl * 1.7f;
                    lr += lc[0] * k; lg += lc[1] * k; lbl += lc[2] * k;
                    /* a touch of light added straight to the surface: the hot spot under a lamp */
                    float hk = att * att * 0.22f;
                    er += lc[0] * hk; eg += lc[1] * hk; eb += lc[2] * hk;
                }
            }
    }
    return RGBA((int)((a[0] * lr + er) * 255.0f), (int)((a[1] * lg + eg) * 255.0f), (int)((a[2] * lbl + eb) * 255.0f), (int)(c & 255));
}

/* ------------------------------------------------------------------ */
/* static geometry helpers (shared with scenery.c)                     */
/* ------------------------------------------------------------------ */
static void rv(RV *v, float x, float y, float z, uint32_t c) { v->x = x; v->y = y; v->z = z; v->c = c; }

void lit_tri(int mode, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t alb)
{
    float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0, vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l < 1e-8f) return;
    nx /= l; ny /= l; nz /= l;
    uint32_t c = track_light(alb, nx, ny, nz, (x0 + x1 + x2) / 3, (y0 + y1 + y2) / 3, (z0 + z1 + z2) / 3);
    RV a, b, d;
    rv(&a, x0, y0, z0, c); rv(&b, x1, y1, z1, c); rv(&d, x2, y2, z2, c);
    sm_tri(mode, &a, &b, &d);
}

void lit_quad(int mode, float x0, float y0, float z0, float x1, float y1, float z1,
              float x2, float y2, float z2, float x3, float y3, float z3, uint32_t alb)
{
    lit_tri(mode, x0, y0, z0, x1, y1, z1, x2, y2, z2, alb);
    lit_tri(mode, x0, y0, z0, x2, y2, z2, x3, y3, z3, alb);
}

void emis_tri(int mode, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t c)
{
    RV a, b, d;
    rv(&a, x0, y0, z0, c); rv(&b, x1, y1, z1, c); rv(&d, x2, y2, z2, c);
    sm_tri(mode, &a, &b, &d);
}

void emis_quad(int mode, float x0, float y0, float z0, float x1, float y1, float z1,
               float x2, float y2, float z2, float x3, float y3, float z3, uint32_t c)
{
    emis_tri(mode, x0, y0, z0, x1, y1, z1, x2, y2, z2, c);
    emis_tri(mode, x0, y0, z0, x2, y2, z2, x3, y3, z3, c);
}

void lit_box(float x, float y, float z, float hx, float hy, float hz, float yaw, uint32_t top, uint32_t side)
{
    float c = cosf(yaw), s = sinf(yaw);
    float px[4], pz[4];
    const float lx[4] = { -hx, hx, hx, -hx }, lz[4] = { -hz, -hz, hz, hz };
    for (int i = 0; i < 4; i++) { px[i] = x + lx[i] * c - lz[i] * s; pz[i] = z + lx[i] * s + lz[i] * c; }
    float y0 = y, y1 = y + hy * 2;
    /* top (seen from above: counter-clockwise in x-z with y up means order 3,2,1,0) */
    lit_quad(RM_OPAQUE, px[0], y1, pz[0], px[3], y1, pz[3], px[2], y1, pz[2], px[1], y1, pz[1], top);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        lit_quad(RM_OPAQUE, px[j], y0, pz[j], px[i], y0, pz[i], px[i], y1, pz[i], px[j], y1, pz[j], side);
    }
}

void lit_prism(float x, float y0, float z, float r0, float r1, float h, int n, float rot, uint32_t c, int cap)
{
    float y1 = y0 + h;
    for (int i = 0; i < n; i++) {
        float a0 = rot + (float)i / (float)n * TAU, a1 = rot + (float)(i + 1) / (float)n * TAU;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        if (r1 < 0.01f)
            lit_tri(RM_OPAQUE, x + c1 * r0, y0, z + s1 * r0, x + c0 * r0, y0, z + s0 * r0, x, y1, z, c);
        else
            lit_quad(RM_OPAQUE, x + c1 * r0, y0, z + s1 * r0, x + c0 * r0, y0, z + s0 * r0,
                     x + c0 * r1, y1, z + s0 * r1, x + c1 * r1, y1, z + s1 * r1, c);
        if (cap && r1 >= 0.01f) lit_tri(RM_OPAQUE, x, y1, z, x + c1 * r1, y1, z + s1 * r1, x + c0 * r1, y1, z + s0 * r1, c);
    }
}

/* shadows: project points along the sun onto the plane y = gy, take the convex hull, lay it on the ground */
static float cross2(const float *o, const float *a, const float *b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); }

static int cmp_pt(const void *a, const void *b)
{
    const float *p = (const float *)a, *q = (const float *)b;
    if (p[0] != q[0]) return p[0] < q[0] ? -1 : 1;
    return p[1] < q[1] ? -1 : (p[1] > q[1] ? 1 : 0);
}

void shadow_hull(const float *pts, int n, float gy)
{
    if (trk.info->night || n < 3 || n > 64) return;
    float p[64][2], h[130][2];
    float sx = L_sun[0] / L_sun[1], sz = L_sun[2] / L_sun[1];
    for (int i = 0; i < n; i++) {
        float up = pts[i * 3 + 1] - gy;
        p[i][0] = pts[i * 3] - sx * up;
        p[i][1] = pts[i * 3 + 2] - sz * up;
    }
    qsort(p, (size_t)n, sizeof p[0], cmp_pt);
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && cross2(h[k - 2], h[k - 1], p[i]) <= 0) k--;
        h[k][0] = p[i][0]; h[k][1] = p[i][1]; k++;
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && cross2(h[k - 2], h[k - 1], p[i]) <= 0) k--;
        h[k][0] = p[i][0]; h[k][1] = p[i][1]; k++;
    }
    k--;
    if (k < 3) return;
    uint32_t sc = trk.info->theme == TH_SNOW ? 0x3A5A9A70u : 0x18204066u;
    float cx = 0, cz = 0;
    for (int i = 0; i < k; i++) { cx += h[i][0]; cz += h[i][1]; }
    cx /= (float)k; cz /= (float)k;
    float cy = track_height(cx, cz) + 0.12f;
    for (int i = 0; i < k; i++) {
        int j = (i + 1) % k;
        RV a, b, c;
        rv(&a, cx, cy, cz, sc);
        rv(&b, h[i][0], track_height(h[i][0], h[i][1]) + 0.12f, h[i][1], sc);
        rv(&c, h[j][0], track_height(h[j][0], h[j][1]) + 0.12f, h[j][1], sc);
        sm_tri(RM_SHADOW, &a, &b, &c);
    }
}

/* soft darkening where a prop meets the ground */
void ao_blob(float x, float z, float r, float strength)
{
    const int N = 10;
    float y = track_height(x, z) + 0.1f;
    uint32_t ci = RGBA(10, 14, 20, (int)(strength * 255.0f)), co = 0x0A0E1400u;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * TAU, a1 = (float)(i + 1) / N * TAU;
        RV a, b, c;
        rv(&a, x, y, z, ci);
        rv(&b, x + cosf(a0) * r, track_height(x + cosf(a0) * r, z + sinf(a0) * r) + 0.1f, z + sinf(a0) * r, co);
        rv(&c, x + cosf(a1) * r, track_height(x + cosf(a1) * r, z + sinf(a1) * r) + 0.1f, z + sinf(a1) * r, co);
        sm_tri(RM_ALPHA, &a, &b, &c);
    }
}

/* ------------------------------------------------------------------ */
/* centre line                                                         */
/* ------------------------------------------------------------------ */
static TSample samples[MAX_SAMPLES];

static void catmull(const float *P, int n, float **out, int *nout)
{
    static float dense[20000 * 2];
    int k = 0;
    for (int i = 0; i < n; i++) {
        const float *p0 = &P[((i - 1 + n) % n) * 2], *p1 = &P[i * 2], *p2 = &P[((i + 1) % n) * 2], *p3 = &P[((i + 2) % n) * 2];
#define DST(a, b) sqrtf(sqrtf(((a)[0] - (b)[0]) * ((a)[0] - (b)[0]) + ((a)[1] - (b)[1]) * ((a)[1] - (b)[1])))
        float t0 = 0, t1 = t0 + DST(p0, p1), t2 = t1 + DST(p1, p2), t3 = t2 + DST(p2, p3);
        float seg = sqrtf((p2[0] - p1[0]) * (p2[0] - p1[0]) + (p2[1] - p1[1]) * (p2[1] - p1[1]));
        int m = (int)(seg / 0.5f) + 2;
        for (int j = 0; j < m && k < 20000; j++) {
            float t = t1 + (t2 - t1) * (float)j / (float)m;
            float q[2];
            for (int d = 0; d < 2; d++) {
                float A1 = p0[d] * (t1 - t) / (t1 - t0) + p1[d] * (t - t0) / (t1 - t0);
                float A2 = p1[d] * (t2 - t) / (t2 - t1) + p2[d] * (t - t1) / (t2 - t1);
                float A3 = p2[d] * (t3 - t) / (t3 - t2) + p3[d] * (t - t2) / (t3 - t2);
                float B1 = A1 * (t2 - t) / (t2 - t0) + A2 * (t - t0) / (t2 - t0);
                float B2 = A2 * (t3 - t) / (t3 - t1) + A3 * (t - t1) / (t3 - t1);
                q[d] = B1 * (t2 - t) / (t2 - t1) + B2 * (t - t1) / (t2 - t1);
            }
            dense[k * 2] = q[0]; dense[k * 2 + 1] = q[1]; k++;
        }
#undef DST
    }
    *out = dense; *nout = k;
}

static void build_centre(const TDef *D)
{
    float *d; int nd;
    catmull(D->p, D->n, &d, &nd);
    /* resample at a uniform step, starting 40 m after the first control point */
    float total = 0;
    for (int i = 0; i < nd; i++) {
        int j = (i + 1) % nd;
        total += sqrtf((d[j * 2] - d[i * 2]) * (d[j * 2] - d[i * 2]) + (d[j * 2 + 1] - d[i * 2 + 1]) * (d[j * 2 + 1] - d[i * 2 + 1]));
    }
    int n = (int)(total / SAMPLE_STEP);
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;
    float step = total / (float)n;
    float want = 40.0f, acc = 0;
    int i = 0, got = 0;
    while (got < n) {
        int j = (i + 1) % nd;
        float sl = sqrtf((d[j * 2] - d[i * 2]) * (d[j * 2] - d[i * 2]) + (d[j * 2 + 1] - d[i * 2 + 1]) * (d[j * 2 + 1] - d[i * 2 + 1]));
        if (acc + sl >= want) {
            float t = (want - acc) / (sl > 1e-6f ? sl : 1e-6f);
            samples[got].x = d[i * 2] + (d[j * 2] - d[i * 2]) * t;
            samples[got].z = d[i * 2 + 1] + (d[j * 2 + 1] - d[i * 2 + 1]) * t;
            got++;
            want += step;
        } else {
            acc += sl;
            i = j;
        }
    }
    trk.s = samples;
    trk.n = n;
    trk.length = (float)n * step;
    for (int k = 0; k < n; k++) {
        TSample *s = &samples[k];
        const TSample *a = &samples[(k - 1 + n) % n], *b = &samples[(k + 1) % n];
        float tx = b->x - a->x, tz = b->z - a->z, l = sqrtf(tx * tx + tz * tz);
        s->tx = tx / l; s->tz = tz / l;
        s->nx = -s->tz; s->nz = s->tx;            /* right of travel (x right, z down on the map) */
        s->dist = (float)k * step;
    }
    /* curvature from the turning of the tangent over +-3 samples */
    for (int k = 0; k < n; k++) {
        const TSample *a = &samples[(k - 3 + n) % n], *b = &samples[(k + 3) % n];
        float cr = a->tx * b->tz - a->tz * b->tx;
        float dt = asinf(clampf(cr, -1, 1));
        samples[k].curv = dt / (6.0f * step);
    }
    /* heights: gentle rolling along the lap */
    for (int k = 0; k < n; k++) {
        float u = (float)k / (float)n * TAU;
        samples[k].y = D->hamp * (0.6f * sinf(u + D->ph1) + 0.4f * sinf(2 * u + D->ph2) + 0.2f * sinf(3 * u + D->ph1 * 2.0f));
    }
}

static void build_widths(const TDef *D)
{
    int n = trk.n;
    static float wl[MAX_SAMPLES], wr[MAX_SAMPLES], hw[MAX_SAMPLES];
    for (int k = 0; k < n; k++) {
        float c = samples[k].curv, ac = fabsf(c);
        hw[k] = D->hw + clampf(ac * 30.0f, 0, 1.4f);
        float out = D->ro + 4.0f * clampf(ac * 28.0f, 0, 1), in = D->ro + 0.5f * clampf(ac * 28.0f, 0, 1);
        if (c > 0) { wl[k] = out; wr[k] = in; } else { wl[k] = in; wr[k] = out; }
    }
    /* smooth */
    for (int it = 0; it < 6; it++) {
        static float t1[MAX_SAMPLES], t2[MAX_SAMPLES], t3[MAX_SAMPLES];
        for (int k = 0; k < n; k++) {
            int a = (k - 4 + n) % n, b = (k + 4) % n;
            t1[k] = (wl[a] + wl[k] * 2 + wl[b]) * 0.25f;
            t2[k] = (wr[a] + wr[k] * 2 + wr[b]) * 0.25f;
            t3[k] = (hw[a] + hw[k] * 2 + hw[b]) * 0.25f;
        }
        memcpy(wl, t1, sizeof(float) * (size_t)n); memcpy(wr, t2, sizeof(float) * (size_t)n); memcpy(hw, t3, sizeof(float) * (size_t)n);
    }
    g_wallmax = 0;
    for (int k = 0; k < n; k++) {
        samples[k].hw = hw[k];
        samples[k].wl = hw[k] + wl[k];
        samples[k].wr = hw[k] + wr[k];
        if (samples[k].wl > g_wallmax) g_wallmax = samples[k].wl;
        if (samples[k].wr > g_wallmax) g_wallmax = samples[k].wr;
    }
    /* kerbs where the corner is tight enough: on the inside, and on the outside at the apex */
    for (int k = 0; k < n; k++) {
        float c = samples[k].curv, ac = fabsf(c);
        uint8_t m = 0;
        if (ac > 0.012f) m |= c > 0 ? 2 : 1;
        if (ac > 0.022f) m |= c > 0 ? 1 : 2;
        samples[k].kerb = m;
    }
    /* extend kerbs a little either side (and close one-sample gaps) */
    static uint8_t km[MAX_SAMPLES];
    for (int k = 0; k < n; k++) {
        uint8_t m = 0;
        for (int j = -3; j <= 3; j++) m |= samples[(k + j + n) % n].kerb;
        km[k] = m;
    }
    for (int k = 0; k < n; k++) samples[k].kerb = km[k];
    /* no kerbs right on the start line */
    for (int k = -10; k < 10; k++) samples[(k + n) % n].kerb = 0;
}

static void build_racing_line(void)
{
    int n = trk.n;
    static float off[MAX_SAMPLES], nw[MAX_SAMPLES];
    memset(off, 0, sizeof(float) * (size_t)n);
    const int KS[3] = { 9, 5, 3 };
    for (int pass = 0; pass < 3; pass++) {
        int K = KS[pass];
        for (int it = 0; it < 220; it++) {
            for (int k = 0; k < n; k++) {
                const TSample *a = &samples[(k - K + n) % n], *b = &samples[(k + K) % n], *s = &samples[k];
                float ax = a->x + a->nx * off[(k - K + n) % n], az = a->z + a->nz * off[(k - K + n) % n];
                float bx = b->x + b->nx * off[(k + K) % n], bz = b->z + b->nz * off[(k + K) % n];
                float mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f;
                float o = (mx - s->x) * s->nx + (mz - s->z) * s->nz;
                float lim = s->hw - 2.4f;
                nw[k] = clampf(o, -lim, lim);
            }
            for (int k = 0; k < n; k++) off[k] += (nw[k] - off[k]) * 0.6f;
        }
    }
    for (int k = 0; k < n; k++) samples[k].line = off[k];
    /* curvature of the line and the speed it allows */
    for (int k = 0; k < n; k++) {
        int a = (k - 4 + n) % n, b = (k + 4) % n;
        float ax = samples[a].x + samples[a].nx * off[a], az = samples[a].z + samples[a].nz * off[a];
        float px = samples[k].x + samples[k].nx * off[k], pz = samples[k].z + samples[k].nz * off[k];
        float bx = samples[b].x + samples[b].nx * off[b], bz = samples[b].z + samples[b].nz * off[b];
        float ab = sqrtf((px - ax) * (px - ax) + (pz - az) * (pz - az));
        float bc = sqrtf((bx - px) * (bx - px) + (bz - pz) * (bz - pz));
        float ac = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
        float cr = (px - ax) * (bz - az) - (pz - az) * (bx - ax);
        samples[k].lcurv = (ab * bc * ac > 1e-6f) ? 2.0f * cr / (ab * bc * ac) : 0.0f;
    }
    /* reference speed for a grip of 1 m/s^2 lateral is sqrt(1/k); scaled per car later.  Here: a "unit"
     * profile for 18 m/s^2 of grip and 15 m/s^2 of braking */
    for (int k = 0; k < n; k++) {
        float ak = fabsf(samples[k].lcurv);
        float v = ak > 1e-5f ? sqrtf(18.0f / ak) : 90.0f;
        samples[k].vline = v > 90.0f ? 90.0f : v;
    }
    float step = trk.length / (float)n;
    for (int rep = 0; rep < 2; rep++)
        for (int k = n - 1; k >= 0; k--) {
            float vn = samples[(k + 1) % n].vline;
            float lim = sqrtf(vn * vn + 2.0f * 15.0f * step);
            if (samples[k].vline > lim) samples[k].vline = lim;
        }
}

/* ------------------------------------------------------------------ */
/* terrain                                                             */
/* ------------------------------------------------------------------ */
static float natural(int theme, float x, float z, float d, float base)
{
    float h;
    switch (theme) {
    case TH_FOREST:
        h = base + (fbm(x / 70.0f, z / 70.0f, 11) - 0.45f) * 6.0f * smoothstep(20, 60, d)
          + 22.0f * smoothstep(90, 190, d) * fbm(x / 110.0f, z / 110.0f, 5);
        break;
    case TH_CANYON: {
        float n = fbm(x / 60.0f, z / 60.0f, 21);
        float cliff = (9.0f + 9.0f * n) * smoothstep(19, 29, d + (n - 0.5f) * 10.0f);
        float mesa = 10.0f * smoothstep(60, 120, d) * fbm(x / 140.0f, z / 140.0f, 9);
        h = base + cliff + mesa;
        break;
    }
    case TH_SNOW:
        h = base + (fbm(x / 60.0f, z / 60.0f, 31) - 0.4f) * 5.0f * smoothstep(20, 50, d)
          + 34.0f * smoothstep(50, 170, d) * fbm(x / 90.0f, z / 90.0f, 3);
        break;
    case TH_HARBOR:
        h = base * (1.0f - smoothstep(20, 60, d)) + 0.3f;
        break;
    default:
        h = base + (fbm(x / 55.0f, z / 55.0f, 41) - 0.4f) * 4.0f * smoothstep(18, 50, d)
          + 12.0f * smoothstep(70, 170, d) * fbm(x / 120.0f, z / 120.0f, 7);
        break;
    }
    /* basins */
    for (int i = 0; i < 6; i++) {
        const Basin *b = &BASINS[trk.id][i];
        if (b->r <= 0) break;
        float bd = sqrtf((x - b->x) * (x - b->x) + (z - b->z) * (z - b->z));
        float k = smoothstep(b->r, b->r * 0.55f, bd) * smoothstep(28, 46, d);
        if (k > 0) h = lerpf(h, trk.water - (theme == TH_HARBOR ? 6.0f : 4.0f), k);
    }
    return h;
}

static void build_grid(void)
{
    int n = trk.n;
    float minx = 1e9f, minz = 1e9f, maxx = -1e9f, maxz = -1e9f;
    for (int k = 0; k < n; k++) {
        if (samples[k].x < minx) minx = samples[k].x;
        if (samples[k].x > maxx) maxx = samples[k].x;
        if (samples[k].z < minz) minz = samples[k].z;
        if (samples[k].z > maxz) maxz = samples[k].z;
    }
    trk.minx = minx - MARGIN; trk.minz = minz - MARGIN; trk.maxx = maxx + MARGIN; trk.maxz = maxz + MARGIN;
    g_x0 = trk.minx; g_z0 = trk.minz;
    g_w = (int)((trk.maxx - trk.minx) / GRID) + 2;
    g_h = (int)((trk.maxz - trk.minz) / GRID) + 2;
    if (g_w > GMAX) g_w = GMAX;
    if (g_h > GMAX) g_h = GMAX;
    if (!g_hgt) {
        g_hgt = (float *)big_alloc(sizeof(float) * GMAX * GMAX);
        g_dst = (float *)big_alloc(sizeof(float) * GMAX * GMAX);
        g_smp = (int16_t *)big_alloc(sizeof(int16_t) * GMAX * GMAX);
        g_col = (uint32_t *)big_alloc(sizeof(uint32_t) * GMAX * GMAX);
    }
    /* sample buckets (32 m) for the nearest-sample search */
    enum { B = 32, BN = 64 };
    static int16_t head[BN * BN], next[MAX_SAMPLES];
    for (int i = 0; i < BN * BN; i++) head[i] = -1;
    for (int k = 0; k < n; k++) {
        int bx = (int)((samples[k].x - g_x0) / B), bz = (int)((samples[k].z - g_z0) / B);
        bx = bx < 0 ? 0 : (bx >= BN ? BN - 1 : bx);
        bz = bz < 0 ? 0 : (bz >= BN ? BN - 1 : bz);
        next[k] = head[bz * BN + bx];
        head[bz * BN + bx] = (int16_t)k;
    }
    for (int j = 0; j < g_h; j++)
        for (int i = 0; i < g_w; i++) {
            float x = g_x0 + (float)i * GRID, z = g_z0 + (float)j * GRID;
            int bx = (int)((x - g_x0) / B), bz = (int)((z - g_z0) / B);
            float best = 1e18f; int bi = -1;
            for (int r = 0; r <= 4 && bi < 0; r++) {
                /* rings of buckets until something is found, then one more ring to be sure */
                int rr = r + 1;
                for (int zz = bz - rr; zz <= bz + rr; zz++)
                    for (int xx = bx - rr; xx <= bx + rr; xx++) {
                        if (xx < 0 || zz < 0 || xx >= BN || zz >= BN) continue;
                        for (int k = head[zz * BN + xx]; k >= 0; k = next[k]) {
                            float dx = samples[k].x - x, dz = samples[k].z - z, d2 = dx * dx + dz * dz;
                            if (d2 < best) { best = d2; bi = k; }
                        }
                    }
            }
            if (bi < 0) {
                for (int k = 0; k < n; k += 4) {
                    float dx = samples[k].x - x, dz = samples[k].z - z, d2 = dx * dx + dz * dz;
                    if (d2 < best) { best = d2; bi = k; }
                }
            }
            int id = j * g_w + i;
            g_smp[id] = (int16_t)bi;
            g_dst[id] = sqrtf(best);
        }
    /* heights */
    const TrackInfo *I = trk.info;
    for (int j = 0; j < g_h; j++)
        for (int i = 0; i < g_w; i++) {
            int id = j * g_w + i;
            float x = g_x0 + (float)i * GRID, z = g_z0 + (float)j * GRID;
            const TSample *s = &samples[g_smp[id]];
            float d = g_dst[id];
            float side = (x - s->x) * s->nx + (z - s->z) * s->nz;
            float wall = side < 0 ? s->wl : s->wr;
            float base = s->y;
            float t = I->theme == TH_CANYON ? smoothstep(wall + 2.5f, wall + 9.0f, d) : smoothstep(wall + 3.5f, wall + 22.0f, d);
            float nat = natural(I->theme, x, z, d, base);
            g_hgt[id] = lerpf(base - 0.35f, nat, t);
        }
    /* a little smoothing far from the track (keeps the hills soft) */
    static float tmp[GMAX * GMAX];
    for (int it = 0; it < 2; it++) {
        memcpy(tmp, g_hgt, sizeof(float) * (size_t)(g_w * g_h));
        for (int j = 1; j < g_h - 1; j++)
            for (int i = 1; i < g_w - 1; i++) {
                int id = j * g_w + i;
                if (g_dst[id] < g_wallmax + 6.0f) continue;
                if (I->theme == TH_CANYON) continue;
                g_hgt[id] = (tmp[id] * 4 + tmp[id - 1] + tmp[id + 1] + tmp[id - g_w] + tmp[id + g_w]) * 0.125f;
            }
    }
}

float terrain_height(float x, float z)
{
    float fx = (x - g_x0) / GRID, fz = (z - g_z0) / GRID;
    int i = (int)floorf(fx), j = (int)floorf(fz);
    if (i < 0) { i = 0; fx = 0; }
    if (j < 0) { j = 0; fz = 0; }
    if (i >= g_w - 1) { i = g_w - 2; fx = (float)(g_w - 1); }
    if (j >= g_h - 1) { j = g_h - 2; fz = (float)(g_h - 1); }
    float u = fx - (float)i, v = fz - (float)j;
    u = clampf(u, 0, 1); v = clampf(v, 0, 1);
    const float *h = g_hgt;
    int id = j * g_w + i;
    /* the same triangle split the mesh uses */
    if (u + v <= 1.0f) return h[id] + (h[id + 1] - h[id]) * u + (h[id + g_w] - h[id]) * v;
    return h[id + g_w + 1] + (h[id + g_w] - h[id + g_w + 1]) * (1 - u) + (h[id + 1] - h[id + g_w + 1]) * (1 - v);
}

int track_nearest(float x, float z)
{
    int i = (int)((x - g_x0) / GRID + 0.5f), j = (int)((z - g_z0) / GRID + 0.5f);
    i = i < 0 ? 0 : (i >= g_w ? g_w - 1 : i);
    j = j < 0 ? 0 : (j >= g_h ? g_h - 1 : j);
    return g_smp[j * g_w + i];
}

float track_dist(float x, float z)
{
    int k = track_nearest(x, z);
    float lat, al;
    k = track_locate(k, x, z, &lat, &al);
    return fabsf(lat) + (fabsf(al) > 2.0f ? fabsf(al) - 2.0f : 0.0f);
}

float track_clearance(float x, float z)
{
    int k = track_nearest(x, z);
    float lat, al;
    k = track_locate(k, x, z, &lat, &al);
    float wall = lat < 0 ? samples[k].wl : samples[k].wr;
    float d = sqrtf(lat * lat + (fabsf(al) > 2.0f ? (fabsf(al) - 2.0f) * (fabsf(al) - 2.0f) : 0.0f));
    return d - wall;
}

int track_locate(int hint, float x, float z, float *lat, float *along)
{
    int n = trk.n, best = 0;
    float bd = 1e18f;
    if (hint < 0) {
        for (int k = 0; k < n; k++) {
            float dx = x - samples[k].x, dz = z - samples[k].z, d2 = dx * dx + dz * dz;
            if (d2 < bd) { bd = d2; best = k; }
        }
    } else {
        for (int o = -10; o <= 26; o++) {
            int k = (hint + o + n) % n;
            float dx = x - samples[k].x, dz = z - samples[k].z, d2 = dx * dx + dz * dz;
            if (d2 < bd) { bd = d2; best = k; }
        }
    }
    const TSample *s = &samples[best];
    float dx = x - s->x, dz = z - s->z;
    *lat = dx * s->nx + dz * s->nz;
    *along = dx * s->tx + dz * s->tz;
    return best;
}

int track_sample_at(float dist)
{
    float step = trk.length / (float)trk.n;
    int k = (int)floorf(dist / step);
    k %= trk.n;
    if (k < 0) k += trk.n;
    return k;
}

void track_point(float dist, float lat, float *x, float *z)
{
    float step = trk.length / (float)trk.n;
    float f = dist / step;
    int k = (int)floorf(f);
    float t = f - (float)k;
    k %= trk.n;
    if (k < 0) k += trk.n;
    const TSample *a = &samples[k], *b = &samples[(k + 1) % trk.n];
    float cx = lerpf(a->x, b->x, t), cz = lerpf(a->z, b->z, t);
    float nx = lerpf(a->nx, b->nx, t), nz = lerpf(a->nz, b->nz, t);
    *x = cx + nx * lat; *z = cz + nz * lat;
}

float track_height(float x, float z)
{
    if (!trk.n) return 0;
    int k = track_nearest(x, z);
    float lat, al;
    k = track_locate(k, x, z, &lat, &al);
    const TSample *s = &samples[k];
    float wall = lat < 0 ? s->wl : s->wr;
    if (fabsf(lat) <= wall + 2.5f && fabsf(al) < 3.0f) {
        const TSample *b = al >= 0 ? &samples[(k + 1) % trk.n] : &samples[(k - 1 + trk.n) % trk.n];
        float t = fabsf(al) / (trk.length / (float)trk.n);
        float y = lerpf(s->y, b->y, clampf(t, 0, 1));
        if (fabsf(lat) > s->hw + 0.8f) y -= 0.03f;
        return y;
    }
    return terrain_height(x, z);
}

/* ------------------------------------------------------------------ */
/* materials per theme                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t grass1, grass2, dirt, rock, road1, road2, rubber, edge, shoulder, runoff, kerb1, kerb2, water, under;
} Pal;

static const Pal PAL[NUM_TRACKS] = {
    /* forest */  { 0x6FA548FF, 0x4E8A3AFF, 0x9C8458FF, 0x8C8C88FF, 0x6E7079FF, 0x656771FF, 0x4E5058FF, 0xF2F2EEFF, 0x6A9E46FF, 0xD9C38FFF, 0xE03A30FF, 0xF4F4F0FF, 0x3A8CB4FF, 0x6A8A6AFF },
    /* canyon */  { 0xD69A62FF, 0xC9844FFF, 0xE0B07CFF, 0xB8673FFF, 0xE2B07EFF, 0xD9A672FF, 0xBE8A5CFF, 0xF0D6B0FF, 0xD9A06CFF, 0xE7BE8EFF, 0xE04030FF, 0xF2EEE6FF, 0x3AB4B0FF, 0xB08060FF },
    /* snow */    { 0xF4F8FFFF, 0xE2EAF6FF, 0xB8C2D2FF, 0x8A94A6FF, 0x8E98AAFF, 0x9AA4B6FF, 0x6A7486FF, 0xFFFFFFFF, 0xEEF3FAFF, 0xDCE6F2FF, 0x2F6CE0FF, 0xF6F8FFFF, 0x7ABCE8FF, 0xC6D6EAFF },
    /* harbor */  { 0x5E636EFF, 0x555A64FF, 0x6A6A6AFF, 0x7A7E86FF, 0x50545EFF, 0x4A4E57FF, 0x3C4048FF, 0xF6F2E6FF, 0x6A6E78FF, 0x7A7F8AFF, 0xF0A020FF, 0x2A2C30FF, 0x1E4C78FF, 0x40464EFF },
    /* lantern */ { 0x5E9A44FF, 0x4A8236FF, 0xA36A40FF, 0x7A7A70FF, 0xB27448FF, 0xA4683EFF, 0x8E5A36FF, 0xE8D8B0FF, 0x6A9C46FF, 0xB88458FF, 0xD84030FF, 0xF2EEE2FF, 0x2E6E8EFF, 0x506A44FF },
};

static uint32_t ground_albedo(float x, float z, float d, float h, float slope)
{
    const Pal *P = &PAL[trk.id];
    float n1 = fbm(x / 26.0f, z / 26.0f, 77), n2 = vnoise(x / 7.0f, z / 7.0f, 91);
    uint32_t c = col_mix(P->grass1, P->grass2, smoothstep(0.35f, 0.65f, n1));
    c = col_scale(c, 0.93f + 0.14f * n2);
    switch (trk.info->theme) {
    case TH_FOREST:
    case TH_LANTERN:
        c = col_mix(c, P->dirt, smoothstep(0.62f, 0.75f, fbm(x / 40.0f, z / 40.0f, 5)) * 0.8f);
        c = col_mix(c, P->rock, smoothstep(0.55f, 0.85f, slope));
        break;
    case TH_CANYON: {
        /* layered rock strata on the cliffs */
        float band = 0.5f + 0.5f * sinf(h * 1.1f + n1 * 2.0f);
        uint32_t strata = col_mix(0xB8653EFFu, 0xD8925EFFu, band);
        strata = col_mix(strata, 0x9C4E30FFu, smoothstep(0.7f, 1.0f, 0.5f + 0.5f * sinf(h * 0.37f)) * 0.6f);
        c = col_mix(c, strata, smoothstep(0.3f, 0.6f, slope));
        break;
    }
    case TH_SNOW:
        c = col_mix(c, P->rock, smoothstep(0.65f, 0.95f, slope) * 0.85f);
        break;
    case TH_HARBOR: {
        /* paving with a grid of joints */
        float gx = fabsf(fmodf(x + 1000.0f, 12.0f) - 6.0f), gz = fabsf(fmodf(z + 1000.0f, 12.0f) - 6.0f);
        if (gx > 5.4f || gz > 5.4f) c = col_scale(c, 0.85f);
        break;
    }
    }
    if (h < trk.water + 0.6f) c = col_mix(c, P->under, smoothstep(trk.water + 0.6f, trk.water - 1.0f, h));
    (void)d;
    return c;
}

static void build_terrain(void)
{
    /* vertex colours (lit), then two triangles per cell */
    for (int j = 0; j < g_h; j++)
        for (int i = 0; i < g_w; i++) {
            int id = j * g_w + i;
            int i0 = i > 0 ? i - 1 : i, i1 = i < g_w - 1 ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < g_h - 1 ? j + 1 : j;
            float dx = (g_hgt[j * g_w + i1] - g_hgt[j * g_w + i0]) / ((float)(i1 - i0) * GRID);
            float dz = (g_hgt[j1 * g_w + i] - g_hgt[j0 * g_w + i]) / ((float)(j1 - j0) * GRID);
            float nx = -dx, ny = 1.0f, nz = -dz, l = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= l; ny /= l; nz /= l;
            float x = g_x0 + (float)i * GRID, z = g_z0 + (float)j * GRID;
            uint32_t alb = ground_albedo(x, z, g_dst[id], g_hgt[id], 1.0f - ny);
            g_col[id] = track_light(alb, nx, ny, nz, x, g_hgt[id], z);
        }
    for (int j = 0; j < g_h - 1; j++)
        for (int i = 0; i < g_w - 1; i++) {
            int id = j * g_w + i;
            /* hidden completely under the road and its shoulders? skip */
            if (g_dst[id] < 4.0f && g_dst[id + 1] < 4.0f && g_dst[id + g_w] < 4.0f && g_dst[id + g_w + 1] < 4.0f) continue;
            float x = g_x0 + (float)i * GRID, z = g_z0 + (float)j * GRID;
            RV a, b, c, d;
            rv(&a, x, g_hgt[id], z, g_col[id]);
            rv(&b, x + GRID, g_hgt[id + 1], z, g_col[id + 1]);
            rv(&c, x, g_hgt[id + g_w], z + GRID, g_col[id + g_w]);
            rv(&d, x + GRID, g_hgt[id + g_w + 1], z + GRID, g_col[id + g_w + 1]);
            sm_tri(RM_OPAQUE, &a, &b, &c);
            sm_tri(RM_OPAQUE, &d, &c, &b);
        }
    /* water */
    if (trk.water > -999.0f) {
        const Pal *P = &PAL[trk.id];
        for (int j = 0; j < g_h - 1; j++)
            for (int i = 0; i < g_w - 1; i++) {
                int id = j * g_w + i;
                float lo = fminf(fminf(g_hgt[id], g_hgt[id + 1]), fminf(g_hgt[id + g_w], g_hgt[id + g_w + 1]));
                if (lo > trk.water - 0.05f) continue;
                if (g_dst[id] < g_wallmax + 4.0f) continue;
                float x = g_x0 + (float)i * GRID, z = g_z0 + (float)j * GRID;
                RV v[4];
                const float ox[4] = { 0, GRID, 0, GRID }, oz[4] = { 0, 0, GRID, GRID };
                for (int k = 0; k < 4; k++) {
                    float px = x + ox[k], pz = z + oz[k];
                    float depth = trk.water - g_hgt[id + (k & 1) + (k >> 1) * g_w];
                    float wv = vnoise(px / 9.0f, pz / 9.0f, 3);
                    uint32_t wc = col_mix(col_scale(P->water, 1.25f), col_scale(P->water, 0.7f), smoothstep(0.0f, 4.0f, depth));
                    wc = col_scale(wc, 0.9f + 0.2f * wv);
                    uint32_t lc = track_light(wc, 0, 1, 0, px, trk.water, pz);
                    rv(&v[k], px, trk.water, pz, col_seta(lc, (int)(150 + 90 * smoothstep(0.0f, 3.0f, depth))));
                }
                sm_tri(RM_ALPHA, &v[0], &v[1], &v[2]);
                sm_tri(RM_ALPHA, &v[3], &v[2], &v[1]);
            }
    }
}

/* ------------------------------------------------------------------ */
/* the road                                                            */
/* ------------------------------------------------------------------ */
#define NCOL 15

static void build_road(void)
{
    const Pal *P = &PAL[trk.id];
    int n = trk.n;
    int asphalt = trk.info->surface == SURF_ASPHALT;
    static RV prev[NCOL], cur[NCOL];
    static uint32_t prevbase[NCOL], curbase[NCOL];
    for (int kk = 0; kk <= n; kk++) {
        int k = kk % n;
        const TSample *s = &samples[k];
        float hw = s->hw, kl = (s->kerb & 1) ? 1.1f : 0.0f, kr = (s->kerb & 2) ? 1.1f : 0.0f;
        /* lateral offsets of the cross-section columns */
        float off[NCOL] = {
            -s->wl - 3.0f, -s->wl, -hw - kl - 0.01f, -hw - 0.0f, -hw + 0.25f, -hw + 0.55f, -hw * 0.5f, 0.0f,
            hw * 0.5f, hw - 0.55f, hw - 0.25f, hw, hw + kr + 0.01f, s->wr, s->wr + 3.0f
        };
        int block = (k / 2) & 1;
        for (int c = 0; c < NCOL; c++) {
            float x = s->x + s->nx * off[c], z = s->z + s->nz * off[c];
            float y = s->y;
            uint32_t alb;
            float lineo = off[c] - s->line;
            float rub = smoothstep(4.0f, 0.5f, fabsf(lineo));
            float nn = vnoise(x / 3.0f, z / 3.0f, 17);
            uint32_t road = col_mix(P->road1, P->road2, nn);
            road = col_mix(road, P->rubber, rub * (asphalt ? 0.55f : 0.45f));
            if (c == 0 || c == NCOL - 1) {
                alb = ground_albedo(x, z, fabsf(off[c]), y, 0.0f);
                y = y - 0.12f;
            } else if (c == 1 || c == NCOL - 2) {
                float sl = c == 1 ? s->wl - hw : s->wr - hw;
                alb = col_mix(P->shoulder, P->runoff, smoothstep(5.0f, 8.0f, sl));
                y -= 0.03f;
            } else if (c == 2 || c == 3) {
                int on = (s->kerb & 1) != 0;
                if (on) { alb = block ? P->kerb1 : P->kerb2; y += 0.07f; }
                else { alb = c == 2 ? col_mix(P->shoulder, P->runoff, 0.5f) : road; if (c == 2) y -= 0.02f; }
                if (on && c == 3) y += 0.0f;
            } else if (c == NCOL - 3 || c == NCOL - 4) {
                int on = (s->kerb & 2) != 0;
                if (on) { alb = block ? P->kerb1 : P->kerb2; y += 0.07f; }
                else { alb = c == NCOL - 3 ? col_mix(P->shoulder, P->runoff, 0.5f) : road; if (c == NCOL - 3) y -= 0.02f; }
            } else if (c == 4 || c == 5 || c == NCOL - 5 || c == NCOL - 6) {
                /* the white edge line (asphalt) or a lighter verge (dirt / snow) */
                alb = asphalt ? P->edge : col_mix(road, P->edge, 0.35f);
                if (c == 5 || c == NCOL - 6) alb = road;
            } else {
                alb = road;
            }
            uint32_t col = track_light(alb, 0, 1, 0, x, y, z);
            rv(&cur[c], x, y, z, col);
            {
                /* the colour this column has without a kerb (for the neighbouring quads) */
                uint32_t bal = alb;
                if (c == 2 || c == NCOL - 3) bal = col_mix(P->shoulder, P->runoff, 0.5f);
                if (c == 3 || c == NCOL - 4) bal = asphalt ? P->edge : col_mix(road, P->edge, 0.35f);
                curbase[c] = (bal == alb) ? col : track_light(bal, 0, 1, 0, x, y, z);
            }
        }
        /* kerbs: each block is one flat colour, and the quads beside a kerb don't pick up its colour */
        if (kk > 0) {
            const TSample *ps = &samples[(kk - 1) % n];
            for (int c = 0; c < NCOL - 1; c++) {
                RV a = prev[c], b = prev[c + 1], d = cur[c], e = cur[c + 1];
                int left = c == 2, right = c == NCOL - 4;
                if (left || right) {
                    int on = left ? ((ps->kerb & 1) && (s->kerb & 1)) : ((ps->kerb & 2) && (s->kerb & 2));
                    if (on) {
                        int blk = (((kk - 1) % n) / 2) & 1;
                        uint32_t kc = track_light(blk ? P->kerb1 : P->kerb2, 0, 1, 0, a.x, a.y, a.z);
                        a.c = b.c = d.c = e.c = kc;
                    } else {
                        a.c = prevbase[c]; b.c = prevbase[c + 1]; d.c = curbase[c]; e.c = curbase[c + 1];
                    }
                }
                if (c == 1 || c == NCOL - 5) { b.c = prevbase[c + 1]; e.c = curbase[c + 1]; }
                if (c == 3 || c == NCOL - 3) { a.c = prevbase[c]; d.c = curbase[c]; }
                sm_tri(RM_OPAQUE, &a, &d, &b);
                sm_tri(RM_OPAQUE, &b, &d, &e);
            }
        }
        memcpy(prevbase, curbase, sizeof curbase);
        memcpy(prev, cur, sizeof cur);
    }
    /* dashed centre line on the city streets */
    if (trk.info->theme == TH_HARBOR) {
        for (int k = 0; k < n; k += 4) {
            const TSample *s = &samples[k], *b = &samples[(k + 2) % n];
            uint32_t c = track_light(0xE8D890FFu, 0, 1, 0, s->x, s->y, s->z);
            float w = 0.18f;
            RV v[4];
            rv(&v[0], s->x - s->nx * w, s->y + 0.02f, s->z - s->nz * w, c);
            rv(&v[1], s->x + s->nx * w, s->y + 0.02f, s->z + s->nz * w, c);
            rv(&v[2], b->x + b->nx * w, b->y + 0.02f, b->z + b->nz * w, c);
            rv(&v[3], b->x - b->nx * w, b->y + 0.02f, b->z - b->nz * w, c);
            sm_quad(RM_OPAQUE, &v[0], &v[1], &v[2], &v[3]);
        }
    }
    /* start / finish: a chequered band and the grid slots */
    {
        const TSample *s = &samples[0];
        int cols = 12;
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < cols; c++) {
                float l0 = -s->hw + 2 * s->hw * (float)c / (float)cols, l1 = -s->hw + 2 * s->hw * (float)(c + 1) / (float)cols;
                float a0 = -1.0f + (float)r, a1 = a0 + 1.0f;
                uint32_t alb = ((r + c) & 1) ? 0x202024FFu : 0xF4F4F4FFu;
                uint32_t col = track_light(alb, 0, 1, 0, s->x, s->y, s->z);
                RV v[4];
                float y = s->y + 0.03f;
                rv(&v[0], s->x + s->nx * l0 + s->tx * a0, y, s->z + s->nz * l0 + s->tz * a0, col);
                rv(&v[1], s->x + s->nx * l1 + s->tx * a0, y, s->z + s->nz * l1 + s->tz * a0, col);
                rv(&v[2], s->x + s->nx * l1 + s->tx * a1, y, s->z + s->nz * l1 + s->tz * a1, col);
                rv(&v[3], s->x + s->nx * l0 + s->tx * a1, y, s->z + s->nz * l0 + s->tz * a1, col);
                sm_quad(RM_OPAQUE, &v[0], &v[3], &v[2], &v[1]);
            }
        /* grid boxes: white L brackets */
        for (int g = 0; g < 8; g++) {
            float back = 7.0f + (float)g * 5.0f, lat = (g & 1) ? 3.2f : -3.2f;
            float x, z;
            track_point(trk.length - back, lat, &x, &z);
            int k = track_sample_at(trk.length - back);
            const TSample *t = &samples[k];
            uint32_t col = track_light(0xF0F0F0FFu, 0, 1, 0, x, t->y, z);
            float y = t->y + 0.03f;
            for (int side = -1; side <= 1; side += 2) {
                float l0 = side * 1.5f, l1 = side * 1.25f;
                RV v[4];
                rv(&v[0], x + t->nx * l0 + t->tx * 1.4f, y, z + t->nz * l0 + t->tz * 1.4f, col);
                rv(&v[1], x + t->nx * l1 + t->tx * 1.4f, y, z + t->nz * l1 + t->tz * 1.4f, col);
                rv(&v[2], x + t->nx * l1 - t->tx * 0.6f, y, z + t->nz * l1 - t->tz * 0.6f, col);
                rv(&v[3], x + t->nx * l0 - t->tx * 0.6f, y, z + t->nz * l0 - t->tz * 0.6f, col);
                sm_quad(RM_OPAQUE, &v[0], &v[3], &v[2], &v[1]);
            }
            RV v[4];
            rv(&v[0], x - t->nx * 1.5f + t->tx * 1.4f, y, z - t->nz * 1.5f + t->tz * 1.4f, col);
            rv(&v[1], x + t->nx * 1.5f + t->tx * 1.4f, y, z + t->nz * 1.5f + t->tz * 1.4f, col);
            rv(&v[2], x + t->nx * 1.5f + t->tx * 1.15f, y, z + t->nz * 1.5f + t->tz * 1.15f, col);
            rv(&v[3], x - t->nx * 1.5f + t->tx * 1.15f, y, z - t->nz * 1.5f + t->tz * 1.15f, col);
            sm_quad(RM_OPAQUE, &v[0], &v[3], &v[2], &v[1]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* barriers along both walls                                           */
/* ------------------------------------------------------------------ */
static void wall_pt(int k, int side, float extra, float *x, float *z)
{
    const TSample *s = &samples[k];
    float o = side < 0 ? -(s->wl + extra) : (s->wr + extra);
    *x = s->x + s->nx * o; *z = s->z + s->nz * o;
}

static void build_barriers(void)
{
    int n = trk.n, th = trk.info->theme;
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 0; k < n; k += 2) {
            int k2 = (k + 2) % n;
            const TSample *s = &samples[k];
            float x0, z0, x1, z1, xo0, zo0, xo1, zo1;
            wall_pt(k, side, 0.0f, &x0, &z0);
            wall_pt(k2, side, 0.0f, &x1, &z1);
            wall_pt(k, side, 0.6f, &xo0, &zo0);
            wall_pt(k2, side, 0.6f, &xo1, &zo1);
            float y0 = s->y - 0.05f, y1 = samples[k2].y - 0.05f;
            float ac = fabsf(s->curv);
            int outside = (s->curv > 0 && side < 0) || (s->curv < 0 && side > 0);
            /* the inner face (towards the track) runs from (x1,z1) to (x0,z0) for side < 0 so it faces the road */
            float ax = x0, az = z0, bx = x1, bz = z1, aox = xo0, aoz = zo0, box_ = xo1, boz = zo1;
            if (side > 0) { ax = x1; az = z1; bx = x0; bz = z0; aox = xo1; aoz = zo1; box_ = xo0; boz = zo0; float t = y0; y0 = y1; y1 = t; }
            switch (th) {
            case TH_FOREST:
            case TH_CANYON:
                if (th == TH_FOREST && outside && ac > 0.016f) {
                    /* tyre wall: stacked red and white tyres */
                    uint32_t c = ((k / 2) & 1) ? 0xD83A30FFu : 0xF2F2F0FFu;
                    float cx = (x0 + xo0) * 0.5f, cz = (z0 + zo0) * 0.5f;
                    lit_prism(cx, s->y - 0.05f, cz, 0.55f, 0.55f, 0.5f, 6, 0.3f, 0x2A2A2EFFu, 0);
                    lit_prism(cx, s->y + 0.45f, cz, 0.55f, 0.55f, 0.5f, 6, 0.0f, c, 1);
                    float cx2 = (x0 + x1 + xo0 + xo1) * 0.25f, cz2 = (z0 + z1 + zo0 + zo1) * 0.25f;
                    lit_prism(cx2, (y0 + y1) * 0.5f, cz2, 0.55f, 0.55f, 0.5f, 6, 0.0f, 0x2A2A2EFFu, 0);
                    lit_prism(cx2, (y0 + y1) * 0.5f + 0.5f, cz2, 0.55f, 0.55f, 0.5f, 6, 0.3f, c == 0xD83A30FFu ? 0xF2F2F0FFu : 0xD83A30FFu, 1);
                } else {
                    /* armco: two-faced rail on posts */
                    uint32_t rail = th == TH_CANYON ? 0xC8C6C0FFu : 0xD6DADEFFu;
                    lit_quad(RM_OPAQUE, ax, y0 + 0.35f, az, bx, y1 + 0.35f, bz, bx, y1 + 0.75f, bz, ax, y0 + 0.75f, az, rail);
                    lit_quad(RM_OPAQUE, bx, y1 + 0.35f, bz, ax, y0 + 0.35f, az, ax, y0 + 0.75f, az, bx, y1 + 0.75f, bz, col_scale(rail, 0.8f));
                    lit_quad(RM_OPAQUE, ax, y0 + 0.75f, az, bx, y1 + 0.75f, bz, box_, y1 + 0.75f, boz, aox, y0 + 0.75f, aoz, rail);
                    if ((k & 3) == 0) {
                        float px = (x0 + xo0) * 0.5f + (xo0 - x0) * 0.4f, pz = (z0 + zo0) * 0.5f + (zo0 - z0) * 0.4f;
                        lit_box(px, s->y - 0.1f, pz, 0.09f, 0.42f, 0.09f, 0, 0x5A4A3AFFu, th == TH_CANYON ? 0x4A3A2AFFu : 0x606468FFu);
                    }
                }
                break;
            case TH_SNOW: {
                /* a rounded snow bank */
                uint32_t sn = 0xF4F8FFFFu;
                float h0 = 0.9f + 0.4f * vnoise((float)k * 0.3f, (float)side, 4), h1 = 0.9f + 0.4f * vnoise((float)k2 * 0.3f, (float)side, 4);
                float xm0, zm0, xm1, zm1, xf0, zf0, xf1, zf1;
                wall_pt(k, side, 1.0f, &xm0, &zm0); wall_pt(k2, side, 1.0f, &xm1, &zm1);
                wall_pt(k, side, 2.6f, &xf0, &zf0); wall_pt(k2, side, 2.6f, &xf1, &zf1);
                if (side > 0) { float t; t = xm0; xm0 = xm1; xm1 = t; t = zm0; zm0 = zm1; zm1 = t; t = xf0; xf0 = xf1; xf1 = t; t = zf0; zf0 = zf1; zf1 = t; t = h0; h0 = h1; h1 = t; }
                lit_quad(RM_OPAQUE, ax, y0 - 0.1f, az, bx, y1 - 0.1f, bz, xm1, y1 + h1, zm1, xm0, y0 + h0, zm0, sn);
                lit_quad(RM_OPAQUE, xm0, y0 + h0, zm0, xm1, y1 + h1, zm1, xf1, y1 - 0.1f, zf1, xf0, y0 - 0.1f, zf0, sn);
                if (outside && ac > 0.014f) {
                    /* orange safety netting on poles */
                    uint32_t net = 0xF08A2AFFu;
                    lit_quad(RM_OPAQUE, xm0, y0 + h0 + 0.1f, zm0, xm1, y1 + h1 + 0.1f, zm1, xm1, y1 + h1 + 0.9f, zm1, xm0, y0 + h0 + 0.9f, zm0, net);
                    lit_quad(RM_OPAQUE, xm1, y1 + h1 + 0.1f, zm1, xm0, y0 + h0 + 0.1f, zm0, xm0, y0 + h0 + 0.9f, zm0, xm1, y1 + h1 + 0.9f, zm1, col_scale(net, 0.8f));
                }
                break;
            }
            case TH_HARBOR: {
                /* concrete blocks, painted at the corners */
                uint32_t cc = (outside && ac > 0.012f) ? (((k / 2) & 1) ? 0xE84030FFu : 0xF2F2F0FFu) : 0xB4B6B8FFu;
                lit_quad(RM_OPAQUE, ax, y0, az, bx, y1, bz, bx, y1 + 0.9f, bz, ax, y0 + 0.9f, az, cc);
                lit_quad(RM_OPAQUE, ax, y0 + 0.9f, az, bx, y1 + 0.9f, bz, box_, y1 + 0.9f, boz, aox, y0 + 0.9f, aoz, col_scale(cc, 1.05f));
                lit_quad(RM_OPAQUE, box_, y1, boz, aox, y0, aoz, aox, y0 + 0.9f, aoz, box_, y1 + 0.9f, boz, cc);
                break;
            }
            default: {
                /* wooden ranch fence */
                uint32_t wood = 0x8A5A38FFu;
                for (int r = 0; r < 2; r++) {
                    float yy = 0.45f + 0.45f * (float)r;
                    lit_quad(RM_OPAQUE, ax, y0 + yy, az, bx, y1 + yy, bz, bx, y1 + yy + 0.16f, bz, ax, y0 + yy + 0.16f, az, wood);
                    lit_quad(RM_OPAQUE, ax, y0 + yy + 0.16f, az, bx, y1 + yy + 0.16f, bz, box_, y1 + yy + 0.16f, boz, aox, y0 + yy + 0.16f, aoz, col_scale(wood, 1.15f));
                }
                if ((k & 3) == 0) lit_box(x0, s->y - 0.05f, z0, 0.12f, 0.6f, 0.12f, 0, 0x9A6A44FFu, 0x6E4628FFu);
                break;
            }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
void track_load(int id)
{
    const TDef *D = &TDEF[id];
    memset(&trk, 0, sizeof trk);
    trk.id = id;
    trk.info = &TRACK_INFO[id];
    trk.water = D->water;
    lb_ready = 0;
    const TrackInfo *I = trk.info;
    float sl = sqrtf(I->sun[0] * I->sun[0] + I->sun[1] * I->sun[1] + I->sun[2] * I->sun[2]);
    for (int k = 0; k < 3; k++) L_sun[k] = I->sun[k] / sl;
    col3(I->sun_col, L_sunc); col3(I->sky_col, L_sky); col3(I->gnd_col, L_gnd);
    float sk = I->night ? 0.75f : 0.62f, gk = I->night ? 0.6f : 0.45f, sc = I->night ? 0.45f : 0.82f;
    for (int k = 0; k < 3; k++) { L_sky[k] *= sk; L_gnd[k] *= gk; L_sunc[k] *= sc; }

    build_centre(D);
    {
        float lo = 1e9f;
        for (int k = 0; k < trk.n; k++) if (samples[k].y < lo) lo = samples[k].y;
        trk.water = lo + D->water;
    }
    build_widths(D);
    build_racing_line();
    build_grid();

    sm_reset(trk.minx - 10, trk.minz - 10, trk.maxx + 10, trk.maxz + 10);
    scenery_plan();
    light_buckets();
    build_terrain();
    build_road();
    build_barriers();
    scenery_build();
    sm_finish();
}

/* ------------------------------------------------------------------ */
void track_draw_world(void)
{
    sm_cull(14.0f);
}

void track_draw_glows(float t)
{
    float cx = r_cam.tx, cz = r_cam.tz;
    r3d_mode(RM_ADD);
    for (int i = 0; i < trk.nlight; i++) {
        const TLight *L = &trk.light[i];
        if (L->glow <= 0) continue;
        float dx = L->x - cx, dz = L->z - cz;
        if (dx * dx + dz * dz > 95.0f * 95.0f) continue;
        float fl = 1.0f;
        if (trk.info->theme == TH_LANTERN) fl = 0.85f + 0.15f * sinf(t * 7.0f + (float)i * 1.7f) * sinf(t * 3.1f + (float)i);
        r3d_glow(L->x, L->y, L->z, L->glow * fl, col_a(L->c, 0.8f * fl));
        r3d_glow(L->x, L->y, L->z, L->glow * 0.35f, col_a(0xFFFFFFFFu, 0.55f * fl));
        /* reflections on the water */
        if (trk.water > -999.0f && L->y > trk.water) {
            float gh = terrain_height(L->x, L->z + 3.0f);
            if (gh < trk.water - 0.2f) {
                float wob = sinf(t * 2.0f + (float)i) * 0.3f;
                RV a, b, c, d;
                uint32_t cc = col_a(L->c, 0.35f), ce = col_a(L->c, 0.0f);
                a.x = L->x - 0.8f + wob; a.y = trk.water + 0.05f; a.z = L->z + 1.0f; a.c = cc;
                b.x = L->x + 0.8f + wob; b.y = trk.water + 0.05f; b.z = L->z + 1.0f; b.c = cc;
                c.x = L->x + 0.3f - wob; c.y = trk.water + 0.05f; c.z = L->z + 9.0f; c.c = ce;
                d.x = L->x - 0.3f - wob; d.y = trk.water + 0.05f; d.z = L->z + 9.0f; d.c = ce;
                r3d_quad(&a, &b, &c, &d);
            }
        }
    }
}

void track_map_xform(float x, float z, float bx, float by, float bw, float bh, float *ox, float *oy)
{
    float mx0 = trk.minx + MARGIN - 10, mz0 = trk.minz + MARGIN - 10, mx1 = trk.maxx - MARGIN + 10, mz1 = trk.maxz - MARGIN + 10;
    float sx = bw / (mx1 - mx0), sz = bh / (mz1 - mz0), s = sx < sz ? sx : sz;
    float offx = bx + (bw - (mx1 - mx0) * s) * 0.5f, offy = by + (bh - (mz1 - mz0) * s) * 0.5f;
    *ox = offx + (x - mx0) * s;
    *oy = offy + (z - mz0) * s;
}

/* the outline of a track for the menus, without building it: x, y in 0..1 (aspect kept) */
void track_preview_path(int id, float *xy, int max, int *nout)
{
    const TDef *D = &TDEF[id];
    float *d; int nd;
    catmull(D->p, D->n, &d, &nd);
    int step = nd / max + 1, n = 0;
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    for (int i = 0; i < nd; i++) {
        if (d[i * 2] < x0) x0 = d[i * 2];
        if (d[i * 2] > x1) x1 = d[i * 2];
        if (d[i * 2 + 1] < z0) z0 = d[i * 2 + 1];
        if (d[i * 2 + 1] > z1) z1 = d[i * 2 + 1];
    }
    float s = 1.0f / fmaxf(x1 - x0, z1 - z0);
    float ox = (1.0f - (x1 - x0) * s) * 0.5f, oz = (1.0f - (z1 - z0) * s) * 0.5f;
    /* start where the race starts (40 m after the first point) */
    int first = (int)(40.0f / 0.5f);
    for (int k = 0; k < nd && n < max; k += step) {
        int i = (k + first) % nd;
        xy[n * 2] = ox + (d[i * 2] - x0) * s;
        xy[n * 2 + 1] = oz + (d[i * 2 + 1] - z0) * s;
        n++;
    }
    *nout = n;
}
