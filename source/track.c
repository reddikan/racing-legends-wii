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
#include <stdio.h>
#include <stdlib.h>

Track trk;

int    g_w, g_h;
float  g_x0, g_z0;
float *g_hgt, *g_dst;
int16_t *g_smp;
float  g_wallmax;
static uint32_t *g_col;

#define SAMPLE_STEP 2.0f
#define HOVER_ELEV 10.0f
#define MAX_SAMPLES 1400
#define MARGIN 150.0f
#define TERRAIN_REACH 105.0f

const TrackInfo TRACK_INFO[NUM_TRACKS] = {
    { "SKYLINE SPRINT", "ORBITAL GARDENS", TH_FOREST, 0, SURF_ASPHALT, 1,
      { -0.50f, 0.62f, 0.60f }, 0xFFE9C8FF, 0x9DB8E0FF, 0x5A6A3CFF, 0x3E6A3AFF, 1.00f },
    { "MESA PRIME", "DUSK ON A RED PLANET", TH_CANYON, 0, SURF_DIRT, 2,
      { 0.70f, 0.38f, -0.58f }, 0xFFB890FF, 0x8A7AC8FF, 0x6A3A4AFF, 0x5A3A5AFF, 0.93f },
    { "CRYO STATION", "ICE PLANET RING", TH_SNOW, 0, SURF_SNOW, 3,
      { 0.45f, 0.58f, 0.68f }, 0xFFF2E4FF, 0xA8C8FFFF, 0x9AA6BCFF, 0x9AC8F0FF, 0.86f },
    { "NEON METRO", "MEGACITY AT NIGHT", TH_HARBOR, 1, SURF_ASPHALT, 4,
      { 0.35f, 0.80f, 0.48f }, 0x5A5AA0FF, 0x2A2A60FF, 0x1A1430FF, 0x0A0818FF, 1.00f },
    { "MAGMA RIFT", "VOLCANIC MOON", TH_LANTERN, 1, SURF_DIRT, 5,
      { -0.40f, 0.75f, 0.52f }, 0x9A5A4AFF, 0x5A3040FF, 0x6A2A10FF, 0x1A0A08FF, 0.94f },
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
    { { 330, 200, 60 }, { 50, 140, 70 }, { 230, -60, 80 }, { -150, 320, 90 }, { 470, 340, 90 }, { 0, 0, 0 } },
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

/* smooth-shaded triangle: each vertex lit with its own normal (Gouraud) */
void lit_tri_n(int mode, const float *p0, const float *n0, const float *p1, const float *n1, const float *p2, const float *n2, uint32_t alb)
{
    RV a, b, d;
    rv(&a, p0[0], p0[1], p0[2], track_light(alb, n0[0], n0[1], n0[2], p0[0], p0[1], p0[2]));
    rv(&b, p1[0], p1[1], p1[2], track_light(alb, n1[0], n1[1], n1[2], p1[0], p1[1], p1[2]));
    rv(&d, p2[0], p2[1], p2[2], track_light(alb, n2[0], n2[1], n2[2], p2[0], p2[1], p2[2]));
    sm_tri(mode, &a, &b, &d);
}

void lit_prism(float x, float y0, float z, float r0, float r1, float h, int n, float rot, uint32_t c, int cap)
{
    /* round things get more sides and smooth normals; 4-sided shapes stay sharp */
    if (n <= 6) {                                   /* crystals, trunks, small boxes: faceted */
        float y1 = y0 + h;
        for (int i = 0; i < n; i++) {
            float a0 = rot + (float)i / (float)n * TAU, a1 = rot + (float)(i + 1) / (float)n * TAU;
            float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
            if (r1 < 0.01f) lit_tri(RM_OPAQUE, x + c1 * r0, y0, z + s1 * r0, x + c0 * r0, y0, z + s0 * r0, x, y1, z, c);
            else lit_quad(RM_OPAQUE, x + c1 * r0, y0, z + s1 * r0, x + c0 * r0, y0, z + s0 * r0, x + c0 * r1, y1, z + s0 * r1, x + c1 * r1, y1, z + s1 * r1, c);
        }
        return;
    }
    n = n * 2 > 12 ? 12 : n * 2;
    float y1 = y0 + h;
    float nl = sqrtf(h * h + (r0 - r1) * (r0 - r1));
    float nh = h / nl, ny = (r0 - r1) / nl;
    for (int i = 0; i < n; i++) {
        float a0 = rot + (float)i / (float)n * TAU, a1 = rot + (float)(i + 1) / (float)n * TAU;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        float n0[3] = { c0 * nh, ny, s0 * nh }, n1[3] = { c1 * nh, ny, s1 * nh };
        float b0[3] = { x + c0 * r0, y0, z + s0 * r0 }, b1[3] = { x + c1 * r0, y0, z + s1 * r0 };
        float t0[3] = { x + c0 * r1, y1, z + s0 * r1 }, t1[3] = { x + c1 * r1, y1, z + s1 * r1 };
        if (r1 < 0.01f) {
            float am = (a0 + a1) * 0.5f, na[3] = { cosf(am) * nh * 0.5f, 0.85f, sinf(am) * nh * 0.5f };
            float ap[3] = { x, y1, z };
            lit_tri_n(RM_OPAQUE, b1, n1, b0, n0, ap, na, c);
        } else {
            lit_tri_n(RM_OPAQUE, b1, n1, b0, n0, t0, n0, c);
            lit_tri_n(RM_OPAQUE, b1, n1, t0, n0, t1, n1, c);
            if (cap) {
                float up[3] = { 0, 1, 0 }, ct[3] = { x, y1, z };
                lit_tri_n(RM_OPAQUE, ct, up, t1, up, t0, up, c);
            }
        }
    }
}

/* a smooth ellipsoid (tree canopies, bushes, boulders); wob = how lumpy */
void lit_blob(float x, float y, float z, float rx, float ry, float rz, uint32_t c, float wob, uint32_t seed)
{
    const int NA = 10, NB = 6;
    float P[7][11][3], N[7][11][3];
    for (int j = 0; j <= NB; j++) {
        float b = -PI * 0.5f + PI * (float)j / (float)NB, cb = cosf(b), sb = sinf(b);
        for (int i = 0; i <= NA; i++) {
            float a = TAU * (float)(i % NA) / (float)NA, ca = cosf(a), sa = sinf(a);
            float k = 1.0f + wob * (hash2f(i % NA, j, seed) - 0.5f) * (j > 0 && j < NB ? 1.0f : 0.0f);
            P[j][i][0] = x + ca * cb * rx * k; P[j][i][1] = y + sb * ry * k; P[j][i][2] = z + sa * cb * rz * k;
            float nx = ca * cb / rx, ny2 = sb / ry, nz = sa * cb / rz, l = sqrtf(nx * nx + ny2 * ny2 + nz * nz);
            N[j][i][0] = nx / l; N[j][i][1] = ny2 / l; N[j][i][2] = nz / l;
        }
    }
    for (int j = 0; j < NB; j++)
        for (int i = 0; i < NA; i++) {
            /* a little colour variation per patch reads as foliage / rock texture */
            uint32_t cc = col_scale(c, 0.97f + 0.06f * hash2f(i, j, seed + 5));
            lit_tri_n(RM_OPAQUE, P[j][i], N[j][i], P[j + 1][i], N[j + 1][i], P[j + 1][i + 1], N[j + 1][i + 1], cc);
            lit_tri_n(RM_OPAQUE, P[j][i], N[j][i], P[j + 1][i + 1], N[j + 1][i + 1], P[j][i + 1], N[j][i + 1], cc);
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
    uint32_t sc = trk.info->theme == TH_SNOW ? 0x3A5A9A60u : 0x2A305A58u;
    float cx = 0, cz = 0;
    for (int i = 0; i < k; i++) { cx += h[i][0]; cz += h[i][1]; }
    cx /= (float)k; cz /= (float)k;
    float cy = track_height(cx, cz) + 0.12f;
    /* a soft edge: the hull pulled in for the solid core, pushed out for a fading rim */
    float in[130][2], out[130][2];
    for (int i = 0; i < k; i++) {
        float dx = h[i][0] - cx, dz = h[i][1] - cz, d = sqrtf(dx * dx + dz * dz) + 1e-4f;
        float pull = fminf(1.2f, d * 0.4f);
        in[i][0] = h[i][0] - dx / d * pull; in[i][1] = h[i][1] - dz / d * pull;
        out[i][0] = h[i][0] + dx / d * 1.4f; out[i][1] = h[i][1] + dz / d * 1.4f;
    }
    for (int i = 0; i < k; i++) {
        int j = (i + 1) % k;
        RV a, b, c, d;
        rv(&a, cx, cy, cz, sc);
        rv(&b, in[i][0], track_height(in[i][0], in[i][1]) + 0.12f, in[i][1], sc);
        rv(&c, in[j][0], track_height(in[j][0], in[j][1]) + 0.12f, in[j][1], sc);
        sm_tri(RM_SHADOW, &a, &b, &c);
        uint32_t co = sc & 0xFFFFFF00u;
        rv(&d, out[j][0], track_height(out[j][0], out[j][1]) + 0.11f, out[j][1], co);
        RV e;
        rv(&e, out[i][0], track_height(out[i][0], out[i][1]) + 0.11f, out[i][1], co);
        sm_tri(RM_ALPHA, &b, &e, &d);
        sm_tri(RM_ALPHA, &b, &d, &c);
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
        samples[k].y = HOVER_ELEV + D->hamp * 0.5f * (0.6f * sinf(u + D->ph1) + 0.4f * sinf(2 * u + D->ph2) + 0.2f * sinf(3 * u + D->ph1 * 2.0f));
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
        float cliff = (6.0f + 8.0f * n) * smoothstep(34, 56, d + (n - 0.5f) * 10.0f);
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
            float base = s->y - HOVER_ELEV;
            g_hgt[id] = natural(I->theme, x, z, d + 6.0f, base);
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
    /* forest */  { 0x7AB44EFF, 0x5A9A42FF, 0x9C8458FF, 0x8C8C88FF, 0x66697AFF, 0x5E6172FF, 0x4A4C58FF, 0xF2F2EEFF, 0x78B24CFF, 0xD9C38FFF, 0xE03A30FF, 0xF4F4F0FF, 0x3A8CB4FF, 0x6A8A6AFF },
    /* canyon */  { 0xD69A62FF, 0xC9844FFF, 0xE0B07CFF, 0xB8673FFF, 0xE2B07EFF, 0xD9A672FF, 0xBE8A5CFF, 0xF0D6B0FF, 0xD9A06CFF, 0xE7BE8EFF, 0xE04030FF, 0xF2EEE6FF, 0x3AB4B0FF, 0xB08060FF },
    /* snow */    { 0xF4F8FFFF, 0xE2EAF6FF, 0xB8C2D2FF, 0x8A94A6FF, 0x8E98AAFF, 0x9AA4B6FF, 0x6A7486FF, 0xFFFFFFFF, 0xEEF3FAFF, 0xDCE6F2FF, 0x2F6CE0FF, 0xF6F8FFFF, 0x7ABCE8FF, 0xC6D6EAFF },
    /* harbor */  { 0x5E636EFF, 0x555A64FF, 0x6A6A6AFF, 0x7A7E86FF, 0x50545EFF, 0x4A4E57FF, 0x3C4048FF, 0xF6F2E6FF, 0x6A6E78FF, 0x7A7F8AFF, 0xF0A020FF, 0x2A2C30FF, 0x1E4C78FF, 0x40464EFF },
    /* lantern */ { 0x3A3234FF, 0x2C2628FF, 0x4A3A34FF, 0x4A4244FF, 0xB27448FF, 0xA4683EFF, 0x8E5A36FF, 0xE8D8B0FF, 0x6A9C46FF, 0xB88458FF, 0xD84030FF, 0xF2EEE2FF, 0xFF6A1AFF, 0x6A2A10FF },
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
            {
                /* contact shading: props, and the foot of the barriers */
                const TSample *ss = &samples[g_smp[id]];
                float side = (x - ss->x) * ss->nx + (z - ss->z) * ss->nz;
                float wall = side < 0 ? ss->wl : ss->wr;
                float dw = fabsf(fabsf(side) - wall);
                float ao = scenery_ao(x, z) * (1.0f - 0.3f * smoothstep(3.0f, 0.0f, dw));
                g_col[id] = col_scale(g_col[id], ao);
            }
            if (trk.info->theme == TH_LANTERN) {
                /* glowing lava veins through the rock */
                float v1 = fabsf(vnoise(x / 34.0f, z / 34.0f, 61) - 0.5f), v2 = fabsf(vnoise(x / 13.0f, z / 13.0f, 62) - 0.5f);
                float glow = smoothstep(0.045f, 0.0f, v1) + 0.6f * smoothstep(0.03f, 0.0f, v2) * smoothstep(0.12f, 0.0f, v1);
                if (glow > 0) g_col[id] = col_mix(g_col[id], 0xFF6A1AFFu, sat(glow));
            }
        }
    for (int j = 0; j < g_h - 1; j++)
        for (int i = 0; i < g_w - 1; i++) {
            int id = j * g_w + i;
            /* hidden completely under the road and its shoulders? skip */
            if (g_dst[id] < 4.0f && g_dst[id + 1] < 4.0f && g_dst[id + g_w] < 4.0f && g_dst[id + g_w + 1] < 4.0f) continue;
            /* too far from the track for the camera ever to see it */
            if (g_dst[id] > TERRAIN_REACH && g_dst[id + 1] > TERRAIN_REACH && g_dst[id + g_w] > TERRAIN_REACH && g_dst[id + g_w + 1] > TERRAIN_REACH) continue;
            float x = g_x0 + (float)i * GRID, z = g_z0 + (float)j * GRID;
            RV a, b, c, d;
            rv(&a, x, g_hgt[id], z, g_col[id]);
            rv(&b, x + GRID, g_hgt[id + 1], z, g_col[id + 1]);
            rv(&c, x, g_hgt[id + g_w], z + GRID, g_col[id + g_w]);
            rv(&d, x + GRID, g_hgt[id + g_w + 1], z + GRID, g_col[id + g_w + 1]);
            sm_tri(RM_GROUND, &a, &b, &c);
            sm_tri(RM_GROUND, &d, &c, &b);
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
                    uint32_t lc = trk.info->theme == TH_LANTERN ? col_scale(wc, 0.8f + 0.5f * vnoise(px / 6.0f, pz / 6.0f, 9)) : track_light(wc, 0, 1, 0, px, trk.water, pz);
                    if (trk.info->theme == TH_LANTERN) depth = 9.0f;
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

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* the hover track: a floating deck with glowing edges, energy walls,  */
/* boost pads, gates and pylons                                        */
/* ------------------------------------------------------------------ */
typedef struct { uint32_t deck, deck2, margin, accent, accent2, skirt; } HPal;
static const HPal HP[NUM_TRACKS] = {
    { 0x8A94A8FFu, 0x7E889CFFu, 0x5A6274FFu, 0x2AD8FFFFu, 0xFF4AA8FFu, 0x3A4252FFu },
    { 0x5E5258FFu, 0x564A50FFu, 0x3E3438FFu, 0xFF8A2AFFu, 0x3AE8E0FFu, 0x2E262AFFu },
    { 0x5A6A88FFu, 0x52627EFFu, 0x3E4A62FFu, 0x3AD0FFFFu, 0xFFFFFFFFu, 0x3A4660FFu },
    { 0x2A2E40FFu, 0x262A3AFFu, 0x1A1C28FFu, 0x2AE8FFFFu, 0xFF3AC8FFu, 0x14161EFFu },
    { 0x46404AFFu, 0x403A44FFu, 0x2E282EFFu, 0xFF6A1AFFu, 0xFFD23AFFu, 0x241E22FFu },
};
static int boost_at[4], nboost;

static int is_boost(int k)
{
    for (int i = 0; i < nboost; i++) { int d = k - boost_at[i]; if (d >= 0 && d < 10) return 1; }
    return 0;
}

int track_is_boost(int k) { return is_boost(k); }
float track_pad_lat(int k) { return samples[k].line * 0.6f; }

static void build_hover_track(void)
{
    const HPal *P = &HP[trk.id];
    int n = trk.n;
    /* boost pads on the three straightest stretches */
    nboost = 0;
    for (int b = 0; b < 3; b++) {
        float best = 1e9f; int bk = -1;
        for (int k = 20; k < n - 20; k += 4) {
            int near = 0;
            for (int i = 0; i < nboost; i++) { int d = abs(k - boost_at[i]); if (d < n / 5) near = 1; }
            if (near) continue;
            float c = 0;
            for (int j = -10; j <= 30; j++) c += fabsf(samples[(k + j + n) % n].curv);
            if (c < best) { best = c; bk = k; }
        }
        if (bk >= 0) boost_at[nboost++] = bk;
    }
    const int NC = 9;
    for (int k = 0; k < n; k++) {
        int k2 = (k + 1) % n;
        const TSample *a = &samples[k], *b = &samples[k2];
        float la[9], lb[9];
        for (int c = 0; c < NC; c++) {
            float f = (float)c / (float)(NC - 1);
            la[c] = -a->wl + (a->wl + a->wr) * f;
            lb[c] = -b->wl + (b->wl + b->wr) * f;
        }
        int panel = (k / 5) & 1;
        for (int c = 0; c < NC - 1; c++) {
            float lm = (la[c] + la[c + 1]) * 0.5f;
            uint32_t alb = panel ? P->deck : P->deck2;
            if (fabsf(lm) > a->hw + 1.0f) alb = P->margin;
            RV v[4];
            float ls[4] = { la[c], la[c + 1], lb[c + 1], lb[c] };
            const TSample *ss[4] = { a, a, b, b };
            for (int q = 0; q < 4; q++) {
                float x = ss[q]->x + ss[q]->nx * ls[q], z = ss[q]->z + ss[q]->nz * ls[q], y = ss[q]->y;
                float edge = smoothstep(2.5f, 0.0f, ss[q]->wl + ls[q] < ss[q]->wr - ls[q] ? ss[q]->wl + ls[q] : ss[q]->wr - ls[q]);
                uint32_t col = track_light(col_scale(alb, 1.0f - 0.25f * edge), 0, 1, 0, x, y, z);
                rv(&v[q], x, y, z, col);
            }
            sm_tri(RM_GROUND, &v[0], &v[1], &v[2]);
            sm_tri(RM_GROUND, &v[0], &v[2], &v[3]);
        }
        /* glowing edge strips and corner chevrons */
        for (int side = -1; side <= 1; side += 2) {
            float wa = side < 0 ? a->wl : a->wr, wb = side < 0 ? b->wl : b->wr;
            float y = a->y + 0.03f, yb = b->y + 0.03f;
            float o0a = (float)side * (wa - 0.55f), o1a = (float)side * (wa - 0.25f), o0b = (float)side * (wb - 0.55f), o1b = (float)side * (wb - 0.25f);
            emis_quad(RM_OPAQUE, a->x + a->nx * o0a, y, a->z + a->nz * o0a, a->x + a->nx * o1a, y, a->z + a->nz * o1a,
                      b->x + b->nx * o1b, yb, b->z + b->nz * o1b, b->x + b->nx * o0b, yb, b->z + b->nz * o0b, P->accent);
            int kerb = side < 0 ? (a->kerb & 1) : (a->kerb & 2);
            if (kerb && ((k / 2) & 1)) {
                float h0 = (float)side * a->hw, h1 = (float)side * (a->hw + 1.0f), g0 = (float)side * b->hw, g1 = (float)side * (b->hw + 1.0f);
                emis_quad(RM_OPAQUE, a->x + a->nx * h0, y, a->z + a->nz * h0, a->x + a->nx * h1, y, a->z + a->nz * h1,
                          b->x + b->nx * g1, yb, b->z + b->nz * g1, b->x + b->nx * g0, yb, b->z + b->nz * g0, P->accent2);
            }
            /* the deck's side skirt */
            float sa = (float)side * wa, sb = (float)side * wb;
            lit_quad(RM_OPAQUE, a->x + a->nx * sa, a->y - 1.6f, a->z + a->nz * sa, b->x + b->nx * sb, b->y - 1.6f, b->z + b->nz * sb,
                     b->x + b->nx * sb, b->y, b->z + b->nz * sb, a->x + a->nx * sa, a->y, a->z + a->nz * sa, P->skirt);
            emis_quad(RM_OPAQUE, a->x + a->nx * sa * 1.001f, a->y - 0.5f, a->z + a->nz * sa * 1.001f, b->x + b->nx * sb * 1.001f, b->y - 0.5f, b->z + b->nz * sb * 1.001f,
                      b->x + b->nx * sb * 1.001f, b->y - 0.35f, b->z + b->nz * sb * 1.001f, a->x + a->nx * sa * 1.001f, a->y - 0.35f, a->z + a->nz * sa * 1.001f, col_scale(P->accent, 0.8f));
            /* energy wall: posts and a glowing translucent band */
            if ((k & 3) == 0) {
                float px = a->x + a->nx * (float)side * (wa - 0.1f), pz = a->z + a->nz * (float)side * (wa - 0.1f);
                lit_box(px, a->y, pz, 0.12f, 0.75f, 0.12f, 0, 0x3A4050FFu, 0x2A3040FFu);
                emis_quad(RM_OPAQUE, px - 0.13f, a->y + 1.5f, pz - 0.13f, px + 0.13f, a->y + 1.5f, pz - 0.13f, px + 0.13f, a->y + 1.5f, pz + 0.13f, px - 0.13f, a->y + 1.5f, pz + 0.13f, P->accent);
            }
            RV w[4];
            float wxa = a->x + a->nx * (float)side * (wa - 0.1f), wza = a->z + a->nz * (float)side * (wa - 0.1f);
            float wxb = b->x + b->nx * (float)side * (wb - 0.1f), wzb = b->z + b->nz * (float)side * (wb - 0.1f);
            uint32_t lo = col_seta(P->accent, 120), hi = col_seta(P->accent, 0);
            rv(&w[0], wxa, a->y + 0.1f, wza, lo); rv(&w[1], wxb, b->y + 0.1f, wzb, lo);
            rv(&w[2], wxb, b->y + 1.45f, wzb, hi); rv(&w[3], wxa, a->y + 1.45f, wza, hi);
            sm_quad(RM_ADD, &w[0], &w[1], &w[2], &w[3]);
            rv(&w[2], wxb, b->y + 1.45f, wzb, col_seta(P->accent, 200)); rv(&w[3], wxa, a->y + 1.45f, wza, col_seta(P->accent, 200));
            rv(&w[0], wxa, a->y + 1.3f, wza, col_seta(P->accent, 0)); rv(&w[1], wxb, b->y + 1.3f, wzb, col_seta(P->accent, 0));
            sm_quad(RM_ADD, &w[0], &w[1], &w[2], &w[3]);
        }
        /* centre line */
        if ((k & 3) < 2) {
            float y = a->y + 0.03f, w = 0.12f;
            emis_quad(RM_OPAQUE, a->x - a->nx * w, y, a->z - a->nz * w, a->x + a->nx * w, y, a->z + a->nz * w,
                      b->x + b->nx * w, b->y + 0.03f, b->z + b->nz * w, b->x - b->nx * w, b->y + 0.03f, b->z - b->nz * w, col_scale(P->accent, 0.55f));
        }
        /* boost pads: chevrons pointing down the track */
        if (is_boost(k) && (k & 1) == 0) {
            float y = a->y + 0.04f;
            float cx = a->x + a->nx * a->line * 0.6f, cz = a->z + a->nz * a->line * 0.6f;
            for (int sd = -1; sd <= 1; sd += 2) {
                float x0 = cx + a->nx * (float)sd * 2.4f - a->tx * 1.2f, z0 = cz + a->nz * (float)sd * 2.4f - a->tz * 1.2f;
                float x1 = cx + a->tx * 0.4f, z1 = cz + a->tz * 0.4f;
                emis_quad(RM_OPAQUE, x0, y, z0, x1, y, z1, x1 + a->tx * 0.7f, y, z1 + a->tz * 0.7f, x0 + a->tx * 0.7f, y, z0 + a->tz * 0.7f, (k & 2) ? 0xFFD23AFFu : 0xFF8A2AFFu);
            }
            add_light(cx, y + 1.0f, cz, 0.0f, 0xFFB040FFu, 1.6f);
        }
        /* pylons under the deck */
        if (k % 22 == 0) {
            float gy = terrain_height(a->x, a->z);
            if (gy < a->y - 2.0f) {
                lit_prism(a->x, gy - 0.5f, a->z, 1.3f, 0.9f, a->y - 1.6f - gy + 0.5f, 8, 0, 0x4A5060FFu, 0);
                emis_quad(RM_OPAQUE, a->x - 1.0f, a->y - 2.4f, a->z - 1.0f, a->x + 1.0f, a->y - 2.4f, a->z - 1.0f, a->x + 1.0f, a->y - 2.4f, a->z + 1.0f, a->x - 1.0f, a->y - 2.4f, a->z + 1.0f, P->accent);
            }
        }
        /* gates across the track */
        if (k % 130 == 0) {
            float lx = a->x - a->nx * (a->wl + 0.6f), lz = a->z - a->nz * (a->wl + 0.6f), rx = a->x + a->nx * (a->wr + 0.6f), rz = a->z + a->nz * (a->wr + 0.6f);
            lit_box(lx, a->y, lz, 0.5f, 3.6f, 0.5f, 0, 0x3A4252FFu, 0x2E3440FFu);
            lit_box(rx, a->y, rz, 0.5f, 3.6f, 0.5f, 0, 0x3A4252FFu, 0x2E3440FFu);
            float by = a->y + 7.2f, t = 0.4f;
            lit_quad(RM_OPAQUE, rx - a->tx * t, by, rz - a->tz * t, lx - a->tx * t, by, lz - a->tz * t, lx - a->tx * t, by + 1.0f, lz - a->tz * t, rx - a->tx * t, by + 1.0f, rz - a->tz * t, 0x3A4252FFu);
            lit_quad(RM_OPAQUE, lx - a->tx * t, by + 1.0f, lz - a->tz * t, lx + a->tx * t, by + 1.0f, lz + a->tz * t, rx + a->tx * t, by + 1.0f, rz + a->tz * t, rx - a->tx * t, by + 1.0f, rz - a->tz * t, 0x4A5262FFu);
            emis_quad(RM_OPAQUE, rx - a->tx * (t + 0.01f), by + 0.25f, rz - a->tz * (t + 0.01f), lx - a->tx * (t + 0.01f), by + 0.25f, lz - a->tz * (t + 0.01f),
                      lx - a->tx * (t + 0.01f), by + 0.55f, lz - a->tz * (t + 0.01f), rx - a->tx * (t + 0.01f), by + 0.55f, rz - a->tz * (t + 0.01f), k == 0 ? 0xFFFFFFFFu : P->accent2);
            for (int i = 1; i < 8; i++) {
                float f = (float)i / 8.0f;
                add_light(lx + (rx - lx) * f, by - 0.1f, lz + (rz - lz) * f, 0.0f, P->accent2, 0.6f);
            }
        }
    }
    /* chequered start band */
    {
        const TSample *s = &samples[0];
        int cols = 14;
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < cols; c++) {
                float l0 = -s->wl + (s->wl + s->wr) * (float)c / (float)cols, l1 = -s->wl + (s->wl + s->wr) * (float)(c + 1) / (float)cols;
                float a0 = -1.0f + (float)r, a1 = a0 + 1.0f, y = s->y + 0.05f;
                uint32_t col = ((r + c) & 1) ? 0x1A1A20FFu : 0xF4F4F4FFu;
                emis_quad(RM_OPAQUE, s->x + s->nx * l0 + s->tx * a0, y, s->z + s->nz * l0 + s->tz * a0, s->x + s->nx * l0 + s->tx * a1, y, s->z + s->nz * l0 + s->tz * a1,
                          s->x + s->nx * l1 + s->tx * a1, y, s->z + s->nz * l1 + s->tz * a1, s->x + s->nx * l1 + s->tx * a0, y, s->z + s->nz * l1 + s->tz * a0, col);
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
    float sk = I->night ? 0.8f : 0.66f, gk = I->night ? 0.6f : 0.5f, sc = I->night ? 0.45f : 0.88f;
    for (int k = 0; k < 3; k++) { L_sky[k] *= sk; L_gnd[k] *= gk; L_sunc[k] *= sc; }

    build_centre(D);
    {
        float lo = 1e9f;
        for (int k = 0; k < trk.n; k++) if (samples[k].y < lo) lo = samples[k].y;
        trk.water = lo - HOVER_ELEV + D->water;
    }
    build_widths(D);
    build_racing_line();
    build_grid();

    sm_reset(trk.minx - 10, trk.minz - 10, trk.maxx + 10, trk.maxz + 10);
    scenery_plan();
    light_buckets();
    int n0 = sm_tri_count();
    build_terrain();
    int n1 = sm_tri_count();
    build_hover_track();
    int n2 = sm_tri_count();
    scenery_build();
    int n3 = sm_tri_count();
#ifdef TRACK_STATS
    printf("  terrain %d  track %d  scenery %d\n", n1 - n0, n2 - n1, n3 - n2);
#else
    (void)n0; (void)n1; (void)n2; (void)n3;
#endif
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
