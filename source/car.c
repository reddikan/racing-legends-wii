/*
 * RACING LEGENDS - cars. See car.h.
 *
 * Handling is arcade: the heading turns at a rate limited by the tyres' grip, and the velocity is pulled
 * towards the heading by a lateral grip term. Holding L in a corner breaks the rear loose (low lateral grip,
 * more yaw), which keeps most of the speed; the longer and harder the drift, the bigger the mini-turbo and
 * the more nitro it banks when you let go.
 */
#include "car.h"
#include "track.h"
#include "r3d.h"
#include "util.h"

#include <string.h>

Car cars[MAX_CARS];
int ncars;

const char *BODY_NAME[NUM_BODIES] = { "COMET", "VIPER", "TITAN", "FUTURE 88" };
const uint32_t PAINT[NUM_PAINTS] = {
    0x1E5ADCFFu, 0xE8302AFFu, 0xF2C42AFFu, 0x22A85AFFu, 0xF4F4F2FFu, 0x26262CFFu,
    0xF2782AFFu, 0x8A3AD8FFu, 0x2AC8E0FFu, 0xF25AA8FFu, 0x9AA2AEFFu, 0x6A2A1EFFu,
};
const char *PAINT_NAME[NUM_PAINTS] = { "RALLY BLUE", "RACING RED", "SUNBURST", "EMERALD", "PEARL WHITE", "MIDNIGHT",
                                       "BLAZE ORANGE", "ULTRAVIOLET", "ICE CYAN", "BUBBLEGUM", "GUNMETAL", "MAHOGANY" };

void car_stats_from_levels(const uint8_t *lv, CarStats *st)
{
    st->vmax     = 38.0f + 3.0f * (float)lv[UP_ENGINE];       /* 106 .. 148 mph */
    st->accel    = 13.0f + 1.8f * (float)lv[UP_TURBO];
    st->grip     = 15.5f + 1.25f * (float)lv[UP_GRIP];
    st->recharge = 7.0f - 0.9f * (float)lv[UP_RECHARGE];       /* 7.0 .. 2.5 s */
    st->npow     = (float)lv[UP_BOOST];
    st->drift = 1.0f + 0.22f * (float)lv[UP_DRIFT];
}

/* ------------------------------------------------------------------ */
/* models                                                              */
/* ------------------------------------------------------------------ */
enum { M_PAINT, M_PAINT_LO, M_PAINT_ROOF, M_STRIPE, M_GLASS, M_DARK, M_CHROME, M_HEAD, M_TAIL, M_TIRE, M_RIM, M_SEAT, M_HELMET, M_AMBER, M_THRUST };

typedef struct { float p[3][3]; float n[3]; float vn[3][3]; uint8_t m; } MTri;
#define MAX_MTRI 420
typedef struct { MTri t[MAX_MTRI]; int n; float wx, wz, wy, wr, ww; float len, wid, hgt; float hx, hy, hz, tx, ty; } Model;
static Model models[NUM_BODIES];
static Model *cur;

static void mtri(const float *a, const float *b, const float *c, int m, const float *center)
{
    if (cur->n >= MAX_MTRI) return;
    MTri *t = &cur->t[cur->n++];
    memcpy(t->p[0], a, 12); memcpy(t->p[1], b, 12); memcpy(t->p[2], c, 12);
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l < 1e-9f) { cur->n--; return; }
    nx /= l; ny /= l; nz /= l;
    if (center) {
        float fx = (a[0] + b[0] + c[0]) / 3 - center[0], fy = (a[1] + b[1] + c[1]) / 3 - center[1], fz = (a[2] + b[2] + c[2]) / 3 - center[2];
        if (fx * nx + fy * ny + fz * nz < 0) { nx = -nx; ny = -ny; nz = -nz; }
    }
    t->n[0] = nx; t->n[1] = ny; t->n[2] = nz;
    t->m = (uint8_t)m;
}

static void mquad(const float *a, const float *b, const float *c, const float *d, int m, const float *center)
{
    mtri(a, b, c, m, center);
    mtri(a, c, d, m, center);
}

/* frustum box: bottom rectangle (+-wb, yb, zb0..zb1) and top rectangle (+-wt, yt, zt0..zt1)
 * mats: top, front (+z), back (-z), sides */
static void fbox(float wb, float yb, float zb0, float zb1, float wt, float yt, float zt0, float zt1, int mtop, int mfront, int mback, int mside)
{
    /* the cross-section is an octagon: rounded corners seen from above */
    float cb = fminf(0.32f, 0.2f * fminf(wb * 2.0f, zb1 - zb0)), ct = fminf(0.32f, 0.2f * fminf(wt * 2.0f, zt1 - zt0));
    float B[8][3] = { { -wb + cb, yb, zb0 }, { wb - cb, yb, zb0 }, { wb, yb, zb0 + cb }, { wb, yb, zb1 - cb },
                      { wb - cb, yb, zb1 }, { -wb + cb, yb, zb1 }, { -wb, yb, zb1 - cb }, { -wb, yb, zb0 + cb } };
    float T[8][3] = { { -wt + ct, yt, zt0 }, { wt - ct, yt, zt0 }, { wt, yt, zt0 + ct }, { wt, yt, zt1 - ct },
                      { wt - ct, yt, zt1 }, { -wt + ct, yt, zt1 }, { -wt, yt, zt1 - ct }, { -wt, yt, zt0 + ct } };
    float c[3] = { 0, (yb + yt) * 0.5f, (zb0 + zb1 + zt0 + zt1) * 0.25f };
    if (mtop >= 0) {
        float tc[3] = { 0, yt, (zt0 + zt1) * 0.5f };
        for (int i = 0; i < 8; i++) mtri(tc, T[i], T[(i + 1) & 7], mtop, c);
    }
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        int m = i == 4 ? mfront : (i == 0 ? mback : mside);
        if (m < 0) continue;
        mquad(B[i], B[j], T[j], T[i], m, c);
    }
}

/* plain box centred at (x, y0..y1, z) */
static void mbox(float x, float y0, float y1, float z, float hx, float hz, int mtop, int mside)
{
    float B[4][3] = { { x - hx, y0, z - hz }, { x + hx, y0, z - hz }, { x + hx, y0, z + hz }, { x - hx, y0, z + hz } };
    float T[4][3] = { { x - hx, y1, z - hz }, { x + hx, y1, z - hz }, { x + hx, y1, z + hz }, { x - hx, y1, z + hz } };
    float c[3] = { x, (y0 + y1) * 0.5f, z };
    mquad(T[0], T[1], T[2], T[3], mtop, c);
    for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; mquad(B[i], B[j], T[j], T[i], mside, c); }
}

/* a flat panel on a face: four points (emissive lights, grilles) */
static void panel(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3, int m)
{
    float a[3] = { x0, y0, z0 }, b[3] = { x1, y1, z1 }, c[3] = { x2, y2, z2 }, d[3] = { x3, y3, z3 };
    mquad(a, b, c, d, m, NULL);
}

/* stripes along the top of a box section (two bands either side of the centre line) */
static void stripes(float y, float z0, float z1, float wsl)
{
    for (int s = -1; s <= 1; s += 2) {
        float x0 = (float)s * 0.12f, x1 = (float)s * (0.12f + wsl);
        panel(x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1, M_STRIPE);
        /* make the normal point up */
        MTri *t = &cur->t[cur->n - 1], *u = &cur->t[cur->n - 2];
        if (t->n[1] < 0) { t->n[0] = -t->n[0]; t->n[1] = -t->n[1]; t->n[2] = -t->n[2]; }
        if (u->n[1] < 0) { u->n[0] = -u->n[0]; u->n[1] = -u->n[1]; u->n[2] = -u->n[2]; }
    }
}

static int mat_group(int m) { return (m == M_PAINT || m == M_PAINT_LO || m == M_PAINT_ROOF) ? 0 : m + 1; }

/* smoothing: each vertex takes the average normal of the faces that share it (within ~55 degrees) */
static void smooth_normals(Model *M)
{
    for (int i = 0; i < M->n; i++)
        for (int k = 0; k < 3; k++) {
            MTri *t = &M->t[i];
            float ax = 0, ay = 0, az = 0;
            for (int j = 0; j < M->n; j++) {
                const MTri *u = &M->t[j];
                if (mat_group(u->m) != mat_group(t->m)) continue;
                float d = t->n[0] * u->n[0] + t->n[1] * u->n[1] + t->n[2] * u->n[2];
                if (d < 0.35f) continue;
                for (int q = 0; q < 3; q++) {
                    float dx = u->p[q][0] - t->p[k][0], dy = u->p[q][1] - t->p[k][1], dz = u->p[q][2] - t->p[k][2];
                    if (dx * dx + dy * dy + dz * dz < 1e-4f) { ax += u->n[0]; ay += u->n[1]; az += u->n[2]; break; }
                }
            }
            float l = sqrtf(ax * ax + ay * ay + az * az);
            if (l < 1e-6f) { ax = t->n[0]; ay = t->n[1]; az = t->n[2]; l = 1; }
            t->vn[k][0] = ax / l; t->vn[k][1] = ay / l; t->vn[k][2] = az / l;
        }
}

/* fbox shifted sideways (side pods, wings, fins) */
static void fbox_at(float cx, float wb, float yb, float zb0, float zb1, float wt, float yt, float zt0, float zt1, int mt, int mf, int mb, int ms)
{
    int n0 = cur->n;
    fbox(wb, yb, zb0, zb1, wt, yt, zt0, zt1, mt, mf, mb, ms);
    for (int i = n0; i < cur->n; i++) for (int k = 0; k < 3; k++) cur->t[i].p[k][0] += cx;
}

static void thruster(float x, float y, float z, float r)
{
    mbox(x, y - r, y + r, z + 0.15f, r, 0.18f, M_DARK, M_DARK);
    panel(x - r * 0.75f, y - r * 0.75f, z - 0.04f, x + r * 0.75f, y - r * 0.75f, z - 0.04f, x + r * 0.75f, y + r * 0.75f, z - 0.04f, x - r * 0.75f, y + r * 0.75f, z - 0.04f, M_THRUST);
}

static void build_comet(void)
{
    Model *m = cur;
    fbox(0.9f, 0.25f, -2.2f, 1.2f, 0.75f, 0.62f, -2.0f, 0.9f, M_PAINT, M_PAINT, M_PAINT, M_PAINT_LO);
    fbox(0.75f, 0.25f, 1.2f, 3.0f, 0.3f, 0.5f, 0.9f, 2.8f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(0.42f, 0.62f, -0.8f, 0.9f, 0.2f, 1.02f, -0.45f, 0.4f, M_GLASS, M_GLASS, M_GLASS, M_GLASS);
    stripes(0.63f, -2.0f, -0.85f, 0.14f);
    for (int s = -1; s <= 1; s += 2) {
        fbox_at((float)s * 1.3f, 0.36f, 0.18f, -2.3f, 0.5f, 0.26f, 0.64f, -2.1f, 0.2f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
        thruster((float)s * 1.3f, 0.42f, -2.32f, 0.2f);
        panel((float)s * 0.55f, 0.36f, 2.38f, (float)s * 0.72f, 0.36f, 2.1f, (float)s * 0.72f, 0.46f, 2.08f, (float)s * 0.55f, 0.46f, 2.36f, M_HEAD);
    }
    mbox(0, 0.36f, 0.48f, -1.1f, 1.2f, 0.45f, M_PAINT_LO, M_PAINT_LO);
    fbox(0.07f, 0.62f, -2.05f, -1.25f, 0.05f, 1.4f, -2.15f, -1.75f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
    thruster(-0.42f, 0.42f, -2.22f, 0.24f);
    thruster(0.42f, 0.42f, -2.22f, 0.24f);
    m->len = 5.4f; m->wid = 3.0f; m->hgt = 1.4f;
    m->hx = 0.64f; m->hy = 0.4f; m->hz = 2.3f; m->tx = 0.42f; m->ty = 0.42f;
}

static void build_viper(void)
{
    Model *m = cur;
    fbox(0.62f, 0.25f, -2.4f, 0.5f, 0.5f, 0.62f, -2.2f, 0.3f, M_PAINT, M_PAINT, M_PAINT, M_PAINT_LO);
    fbox(0.55f, 0.25f, 0.5f, 3.5f, 0.16f, 0.44f, 0.3f, 3.3f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(0.34f, 0.62f, -0.9f, 0.5f, 0.18f, 0.98f, -0.6f, 0.15f, M_GLASS, M_GLASS, M_GLASS, M_GLASS);
    stripes(0.45f, 0.6f, 3.0f, 0.08f);
    fbox(1.75f, 0.32f, -2.45f, -0.9f, 1.6f, 0.44f, -2.35f, -1.35f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
    for (int s = -1; s <= 1; s += 2) {
        fbox_at((float)s * 1.68f, 0.08f, 0.44f, -2.4f, -1.3f, 0.06f, 1.15f, -2.45f, -1.9f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
        panel((float)s * 0.3f, 0.32f, 2.6f, (float)s * 0.45f, 0.32f, 2.0f, (float)s * 0.45f, 0.4f, 1.98f, (float)s * 0.3f, 0.4f, 2.58f, M_HEAD);
    }
    thruster(0, 0.44f, -2.42f, 0.36f);
    m->len = 5.9f; m->wid = 3.5f; m->hgt = 1.2f;
    m->hx = 0.38f; m->hy = 0.36f; m->hz = 2.4f; m->tx = 0.0f; m->ty = 0.44f;
}

static void build_titan(void)
{
    Model *m = cur;
    fbox(1.25f, 0.25f, -2.4f, 2.2f, 1.15f, 0.9f, -2.25f, 1.9f, -1, M_PAINT_LO, M_PAINT_LO, M_PAINT_LO);
    fbox(1.15f, 0.9f, -2.25f, 1.9f, 1.05f, 1.05f, -2.1f, 1.7f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(0.92f, 1.05f, -0.2f, 1.5f, 0.76f, 1.55f, 0.0f, 1.1f, M_PAINT_ROOF, M_GLASS, M_GLASS, M_GLASS);
    stripes(1.06f, -2.0f, -0.4f, 0.22f);
    mbox(0, 1.05f, 1.35f, -1.3f, 0.8f, 0.6f, M_DARK, M_DARK);
    for (int s = -1; s <= 1; s += 2) {
        thruster((float)s * 0.75f, 0.55f, -2.42f, 0.28f);
        thruster((float)s * 0.3f, 0.75f, -2.42f, 0.2f);
        panel((float)s * 0.5f, 0.6f, 2.21f, (float)s * 1.0f, 0.6f, 2.21f, (float)s * 1.0f, 0.8f, 2.18f, (float)s * 0.5f, 0.8f, 2.18f, M_HEAD);
        fbox_at((float)s * 1.32f, 0.12f, 0.3f, -1.8f, 1.6f, 0.1f, 0.8f, -1.7f, 1.5f, M_DARK, M_DARK, M_DARK, M_DARK);
    }
    m->len = 4.9f; m->wid = 2.8f; m->hgt = 1.6f;
    m->hx = 0.75f; m->hy = 0.7f; m->hz = 2.25f; m->tx = 0.75f; m->ty = 0.55f;
}

/* half-ellipsoid (a bubble canopy) centred at (0, y, z), radii rx, ry, rz */
static void dome(float y, float z, float rx, float ry, float rz, int mat)
{
    const int NA = 12, NB = 4;
    float c[3] = { 0, y, z };
    for (int j = 0; j < NB; j++)
        for (int i = 0; i < NA; i++) {
            float b0 = (float)j / NB * 1.5707963f, b1 = (float)(j + 1) / NB * 1.5707963f;
            float a0 = (float)i / NA * 6.2831853f, a1 = (float)(i + 1) / NA * 6.2831853f;
            float p00[3] = { cosf(a0) * cosf(b0) * rx, y + sinf(b0) * ry, z + sinf(a0) * cosf(b0) * rz };
            float p10[3] = { cosf(a1) * cosf(b0) * rx, y + sinf(b0) * ry, z + sinf(a1) * cosf(b0) * rz };
            float p01[3] = { cosf(a0) * cosf(b1) * rx, y + sinf(b1) * ry, z + sinf(a0) * cosf(b1) * rz };
            float p11[3] = { cosf(a1) * cosf(b1) * rx, y + sinf(b1) * ry, z + sinf(a1) * cosf(b1) * rz };
            mtri(p00, p10, p11, mat, c);
            if (j < NB - 1) mtri(p00, p11, p01, mat, c);
        }
}

/* a ring (torus with a square section) facing backwards, centred at (0, y, z) */
static void ring(float y, float z, float r, float t, int mat)
{
    const int N = 14;
    float c[3] = { 0, y, z };
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * 6.2831853f, a1 = (float)(i + 1) / N * 6.2831853f;
        float ro0[3] = { cosf(a0) * (r + t), y + sinf(a0) * (r + t), z }, ro1[3] = { cosf(a1) * (r + t), y + sinf(a1) * (r + t), z };
        float ri0[3] = { cosf(a0) * r, y + sinf(a0) * r, z }, ri1[3] = { cosf(a1) * r, y + sinf(a1) * r, z };
        float bo0[3] = { ro0[0], ro0[1], z - t }, bo1[3] = { ro1[0], ro1[1], z - t }, bi0[3] = { ri0[0], ri0[1], z - t }, bi1[3] = { ri1[0], ri1[1], z - t };
        mquad(ro0, ro1, bo1, bo0, mat, c);            /* outer */
        mquad(ri0, bi0, bi1, ri1, mat, NULL);         /* inner */
        mquad(bo0, bo1, bi1, bi0, mat, NULL);         /* back face */
        mquad(ro0, ri0, ri1, ro1, mat, NULL);         /* front face */
        /* the glowing field inside the ring */
        float cc[3] = { 0, y, z - t * 0.5f }, q0[3] = { ri0[0], ri0[1], z - t * 0.5f }, q1[3] = { ri1[0], ri1[1], z - t * 0.5f };
        mtri(cc, q1, q0, M_THRUST, NULL);
    }
}

/* FUTURE 88: an original arrowhead hover craft - a long tapered silver hull, a bubble canopy, swept twin fins,
 * side nacelles, and a ring thruster at the tail */
static void build_future88(void)
{
    Model *m = cur;
    /* hull: a broad tail section tapering into a long pointed nose */
    fbox(1.05f, 0.25f, -2.1f, 0.4f, 0.95f, 0.72f, -1.95f, 0.3f, M_PAINT, M_PAINT, M_PAINT, M_PAINT_LO);
    fbox(1.05f, 0.25f, 0.4f, 2.0f, 0.55f, 0.58f, 0.3f, 1.8f, M_PAINT, M_PAINT, M_PAINT, M_PAINT_LO);
    fbox(0.55f, 0.25f, 2.0f, 3.1f, 0.12f, 0.42f, 1.8f, 3.0f, M_PAINT, M_PAINT, M_PAINT, M_PAINT_LO);
    /* the cockpit bubble */
    dome(0.72f, -0.15f, 0.6f, 0.5f, 1.15f, M_GLASS);
    /* a glowing spine line down the nose */
    panel(-0.05f, 0.585f, 0.35f, 0.05f, 0.585f, 0.35f, 0.05f, 0.43f, 2.95f, -0.05f, 0.43f, 2.95f, M_THRUST);
    for (int s = -1; s <= 1; s += 2) {
        float sx = (float)s;
        /* side nacelles with blue intakes */
        fbox_at(sx * 1.25f, 0.28f, 0.2f, -2.05f, 1.2f, 0.22f, 0.56f, -1.9f, 1.05f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
        panel(sx * 1.25f - 0.18f, 0.28f, 1.21f, sx * 1.25f + 0.18f, 0.28f, 1.21f, sx * 1.25f + 0.14f, 0.5f, 1.07f, sx * 1.25f - 0.14f, 0.5f, 1.07f, M_THRUST);
        thruster(sx * 1.25f, 0.38f, -2.07f, 0.16f);
        /* swept fins, leaning outwards */
        float f0[3] = { sx * 0.5f, 0.7f, -1.15f }, f1[3] = { sx * 0.6f, 0.7f, -1.95f }, f2[3] = { sx * 0.98f, 1.5f, -2.25f }, f3[3] = { sx * 0.88f, 1.5f, -1.85f };
        mquad(f0, f1, f2, f3, M_PAINT, NULL);
        mquad(f3, f2, f1, f0, M_PAINT, NULL);
        float g0[3] = { sx * 0.9f, 1.42f, -1.88f }, g1[3] = { sx * 0.97f, 1.44f, -2.2f }, g2[3] = { sx * 0.99f, 1.52f, -2.22f }, g3[3] = { sx * 0.92f, 1.5f, -1.88f };
        mquad(g0, g1, g2, g3, M_THRUST, NULL);
        mquad(g3, g2, g1, g0, M_THRUST, NULL);
        /* a blue line along each flank, and slim headlights in the nose */
        panel(sx * 1.052f, 0.4f, -1.9f, sx * 1.052f, 0.4f, 1.9f, sx * 1.052f, 0.46f, 1.9f, sx * 1.052f, 0.46f, -1.9f, M_THRUST);
        panel(sx * 0.18f, 0.3f, 3.05f, sx * 0.42f, 0.3f, 2.5f, sx * 0.44f, 0.36f, 2.48f, sx * 0.2f, 0.38f, 3.03f, M_HEAD);
    }
    /* the ring thruster */
    ring(0.62f, -2.05f, 0.42f, 0.14f, M_PAINT_ROOF);
    /* slim tail lights under the ring */
    panel(-0.7f, 0.3f, -2.11f, 0.7f, 0.3f, -2.11f, 0.7f, 0.36f, -2.11f, -0.7f, 0.36f, -2.11f, M_TAIL);
    m->len = 5.3f; m->wid = 3.0f; m->hgt = 1.55f;
    m->hx = 0.3f; m->hy = 0.33f; m->hz = 2.8f; m->tx = 0.0f; m->ty = 0.62f;
}

void cars_models_init(void)
{
    void (*b[NUM_BODIES])(void) = { build_comet, build_viper, build_titan, build_future88 };
    for (int i = 0; i < NUM_BODIES; i++) { cur = &models[i]; cur->n = 0; b[i](); smooth_normals(cur); }
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
typedef struct { float r[3], u[3], f[3], o[3]; } Xf;

static inline void xf_pt(const Xf *x, const float *p, float *o)
{
    o[0] = x->o[0] + x->r[0] * p[0] + x->u[0] * p[1] + x->f[0] * p[2];
    o[1] = x->o[1] + x->r[1] * p[0] + x->u[1] * p[1] + x->f[1] * p[2];
    o[2] = x->o[2] + x->r[2] * p[0] + x->u[2] * p[1] + x->f[2] * p[2];
}
static inline void xf_dir(const Xf *x, const float *p, float *o)
{
    o[0] = x->r[0] * p[0] + x->u[0] * p[1] + x->f[0] * p[2];
    o[1] = x->r[1] * p[0] + x->u[1] * p[1] + x->f[1] * p[2];
    o[2] = x->r[2] * p[0] + x->u[2] * p[1] + x->f[2] * p[2];
}

static void make_xf(Xf *x, float px, float py, float pz, float h, float pitch, float roll)
{
    float sh = sinf(h), ch = cosf(h);
    float f0[3] = { sh, 0, ch }, r0[3] = { -ch, 0, sh }, u0[3] = { 0, 1, 0 };
    float cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    float f1[3], u1[3];
    for (int i = 0; i < 3; i++) { f1[i] = f0[i] * cp + u0[i] * sp; u1[i] = u0[i] * cp - f0[i] * sp; }
    for (int i = 0; i < 3; i++) { x->r[i] = r0[i] * cr - u1[i] * sr; x->u[i] = u1[i] * cr + r0[i] * sr; x->f[i] = f1[i]; }
    x->o[0] = px; x->o[1] = py; x->o[2] = pz;
}

typedef struct {
    float sun[3], sunc[3], amb[3], half[3], view[3];
    float lx[3], lc[3];          /* the strongest nearby lamp (night) */
    int night, braking;
    uint32_t paint, paint2;
    int livery;
} Shade;

static uint32_t mat_albedo(const Shade *S, int m, int *emis, int *shiny)
{
    *emis = 0; *shiny = 0;
    switch (m) {
    case M_PAINT: *shiny = 1; return S->paint;
    case M_PAINT_LO: *shiny = 1; return S->livery == 2 ? S->paint2 : S->paint;
    case M_PAINT_ROOF: *shiny = 1; return (S->livery == 3 || S->livery == 2) ? S->paint2 : S->paint;
    case M_STRIPE: *shiny = 1; return S->livery == 1 ? S->paint2 : (S->livery == 3 ? S->paint2 : 0);
    case M_GLASS: *shiny = 2; return 0x1C2636FFu;
    case M_DARK: return 0x26272CFFu;
    case M_CHROME: *shiny = 2; return 0xC8CED6FFu;
    case M_HEAD: *emis = 1; return S->night ? 0xFFFBEAFFu : 0xF2F0E6FFu;
    case M_TAIL: *emis = 1; return S->braking ? 0xFF4038FFu : (S->night ? 0xE0262AFFu : 0xB8202AFFu);
    case M_TIRE: return 0x1C1C20FFu;
    case M_RIM: *shiny = 2; return 0xB8BEC8FFu;
    case M_SEAT: return 0x2E3038FFu;
    case M_HELMET: *shiny = 1; return 0xF4F4F4FFu;
    case M_AMBER: *emis = 1; return S->night ? 0xFFE8A0FFu : 0xF2E2A8FFu;
    case M_THRUST: *emis = 1; return 0x9AF0FFFFu;
    }
    return 0xFF00FFFFu;
}

static uint32_t shade(const Shade *S, const float *n, int m)
{
    int emis, shiny;
    uint32_t a = mat_albedo(S, m, &emis, &shiny);
    if (m == M_STRIPE && S->livery != 1 && S->livery != 3) return 0;
    if (emis) return a;
    float ar = (float)(a >> 24) / 255.0f, ag = (float)((a >> 16) & 255) / 255.0f, ab = (float)((a >> 8) & 255) / 255.0f;
    float d = n[0] * S->sun[0] + n[1] * S->sun[1] + n[2] * S->sun[2];
    d = d < 0 ? 0 : d;
    float hs = 0.6f + 0.4f * n[1];
    float lr = S->amb[0] * hs + S->sunc[0] * d, lg = S->amb[1] * hs + S->sunc[1] * d, lb = S->amb[2] * hs + S->sunc[2] * d;
    if (S->night) {
        float dl = n[0] * S->lx[0] + n[1] * S->lx[1] + n[2] * S->lx[2];
        dl = 0.3f + 0.7f * (dl < 0 ? 0 : dl);
        lr += S->lc[0] * dl; lg += S->lc[1] * dl; lb += S->lc[2] * dl;
    }
    float r = ar * lr, g = ag * lg, b = ab * lb;
    if (shiny) {
        float sp = n[0] * S->half[0] + n[1] * S->half[1] + n[2] * S->half[2];
        sp = sp < 0 ? 0 : sp;
        sp *= sp; sp *= sp; sp *= sp;                    /* ^8 */
        float k = (shiny == 2 ? 0.55f : 0.3f) * (S->night ? 0.5f : 1.0f);
        r += sp * k * S->sunc[0] + sp * 0.1f; g += sp * k * S->sunc[1] + sp * 0.1f; b += sp * k * S->sunc[2] + sp * 0.1f;
        /* a broad soft sheen and a rim of sky light at grazing angles: the glossy toy look */
        float bs = n[0] * S->half[0] + n[1] * S->half[1] + n[2] * S->half[2];
        bs = bs < 0 ? 0 : bs * bs;
        r += bs * 0.12f; g += bs * 0.12f; b += bs * 0.11f;
        float nv = n[0] * S->view[0] + n[1] * S->view[1] + n[2] * S->view[2];
        float rim = 1.0f - (nv < 0 ? 0 : nv);
        rim = rim * rim * rim * 0.45f;
        r += rim * 0.75f; g += rim * 0.85f; b += rim * 1.0f;
        /* a cool sky reflection on upward faces (the "toy car lacquer" look) */
        float up = n[1] > 0 ? n[1] : 0;
        r += up * 0.03f; g += up * 0.035f; b += up * 0.05f;
    }
    return RGBA((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f), 255);
}

static void setup_shade(Shade *S, float px, float py, float pz, int night)
{
    const TrackInfo *I = trk.info;
    float sl = sqrtf(I->sun[0] * I->sun[0] + I->sun[1] * I->sun[1] + I->sun[2] * I->sun[2]);
    for (int k = 0; k < 3; k++) S->sun[k] = I->sun[k] / sl;
    uint32_t sc = I->sun_col, ac = I->sky_col;
    float sk = night ? 0.55f : 0.85f, ak = night ? 0.9f : 0.62f;
    S->sunc[0] = (float)(sc >> 24) / 255.0f * sk; S->sunc[1] = (float)((sc >> 16) & 255) / 255.0f * sk; S->sunc[2] = (float)((sc >> 8) & 255) / 255.0f * sk;
    S->amb[0] = (float)(ac >> 24) / 255.0f * ak; S->amb[1] = (float)((ac >> 16) & 255) / 255.0f * ak; S->amb[2] = (float)((ac >> 8) & 255) / 255.0f * ak;
    float vx = r_cam.ex - px, vy = r_cam.ey - py, vz = r_cam.ez - pz, vl = sqrtf(vx * vx + vy * vy + vz * vz);
    vx /= vl; vy /= vl; vz /= vl;
    float hx = S->sun[0] + vx, hy = S->sun[1] + vy, hz = S->sun[2] + vz, hl = sqrtf(hx * hx + hy * hy + hz * hz);
    S->half[0] = hx / hl; S->half[1] = hy / hl; S->half[2] = hz / hl;
    S->view[0] = vx; S->view[1] = vy; S->view[2] = vz;
    S->night = night;
    S->lc[0] = S->lc[1] = S->lc[2] = 0; S->lx[0] = 0; S->lx[1] = 1; S->lx[2] = 0;
    if (night) {
        /* gather the lamps around the car into one light */
        float bx = 0, by = 0, bz = 0;
        for (int i = 0; i < trk.nlight; i++) {
            const TLight *L = &trk.light[i];
            if (L->r <= 0) continue;
            float dx = L->x - px, dy = L->y - py, dz = L->z - pz, d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > L->r * L->r) continue;
            float d = sqrtf(d2) + 0.01f, a = 1.0f - d / L->r;
            a = a * a * 1.6f;
            S->lc[0] += (float)(L->c >> 24) / 255.0f * a; S->lc[1] += (float)((L->c >> 16) & 255) / 255.0f * a; S->lc[2] += (float)((L->c >> 8) & 255) / 255.0f * a;
            bx += dx / d * a; by += dy / d * a; bz += dz / d * a;
        }
        float bl = sqrtf(bx * bx + by * by + bz * bz);
        if (bl > 1e-4f) { S->lx[0] = bx / bl; S->lx[1] = by / bl; S->lx[2] = bz / bl; }
        /* the light itself gives paint a highlight */
        float hx2 = S->lx[0] + vx, hy2 = S->lx[1] + vy, hz2 = S->lx[2] + vz, hl2 = sqrtf(hx2 * hx2 + hy2 * hy2 + hz2 * hz2);
        if (S->lc[0] + S->lc[1] + S->lc[2] > 0.3f) { S->half[0] = hx2 / hl2; S->half[1] = hy2 / hl2; S->half[2] = hz2 / hl2;
            S->sunc[0] += S->lc[0] * 0.6f; S->sunc[1] += S->lc[1] * 0.6f; S->sunc[2] += S->lc[2] * 0.6f; }
    }
}

static void draw_model(const Model *M, const Xf *X, const Shade *S)
{
    for (int i = 0; i < M->n; i++) {
        const MTri *t = &M->t[i];
        float a[3], b[3], c[3], n0[3], n1[3], n2[3];
        xf_dir(X, t->vn[0], n0); xf_dir(X, t->vn[1], n1); xf_dir(X, t->vn[2], n2);
        uint32_t c0, c1, c2;
        if (S->livery == LIVERY_SIGNATURE && (t->m == M_PAINT || t->m == M_PAINT_LO || t->m == M_PAINT_ROOF || t->m == M_STRIPE)) {
            /* LIQUID SILVER: polished metal - cool blue sky reflections up top, darker steel low down, a sharp sheen */
            uint32_t cc[3];
            for (int k = 0; k < 3; k++) {
                const float *nn = k == 0 ? n0 : (k == 1 ? n1 : n2);
                float dl = nn[0] * S->sun[0] + nn[1] * S->sun[1] + nn[2] * S->sun[2];
                dl = dl < 0 ? 0 : dl;
                float up = nn[1] > 0 ? nn[1] : 0;
                float sp = nn[0] * S->half[0] + nn[1] * S->half[1] + nn[2] * S->half[2];
                sp = sp < 0 ? 0 : sp; sp *= sp; sp *= sp; sp *= sp;
                uint32_t base = t->m == M_PAINT_ROOF ? 0x5A6270FFu : (t->m == M_PAINT_LO ? 0x7A828EFFu : 0xB4BCC8FFu);
                if (t->m == M_STRIPE) base = 0x3AC8FFFFu;
                uint32_t col = col_scale(base, 0.5f + 0.45f * dl + 0.2f * up);
                col = col_mix(col, 0x9AD8FFFFu, up * 0.18f);                       /* the sky in the metal */
                if (S->night) col = col_mix(col, 0x2A8AFFFFu, 0.22f);               /* neon spill at night */
                col = col_mix(col, 0xFFFFFFFFu, sp * 0.55f);
                cc[k] = col;
            }
            c0 = cc[0]; c1 = cc[1]; c2 = cc[2];
        } else {
            c0 = shade(S, n0, t->m);
            if (!c0) continue;
            c1 = shade(S, n1, t->m); c2 = shade(S, n2, t->m);
        }
        xf_pt(X, t->p[0], a); xf_pt(X, t->p[1], b); xf_pt(X, t->p[2], c);
        r3d_tri3(a[0], a[1], a[2], c0, b[0], b[1], b[2], c1, c[0], c[1], c[2], c2);
    }
}

static void draw_shadow(const Model *M, const Xf *X, float gy, int night, float strength)
{
    if (night) {
        r3d_mode(RM_ALPHA);
        r3d_blob(X->o[0], gy + 0.12f, X->o[2], M->wid * 0.62f, M->len * 0.6f, 0, RGBA(0, 0, 0, (int)(150 * strength)), 0x00000000u);
        return;
    }
    const TrackInfo *I = trk.info;
    float sl = sqrtf(I->sun[0] * I->sun[0] + I->sun[1] * I->sun[1] + I->sun[2] * I->sun[2]);
    float sx = I->sun[0] / sl, sy = I->sun[1] / sl, sz = I->sun[2] / sl;
    float hx = M->wid * 0.5f, hl = M->len * 0.5f;
    float pts[8][2];
    for (int i = 0; i < 8; i++) {
        float lp[3] = { (i & 1) ? hx : -hx, (i & 2) ? M->hgt * 0.92f : 0.15f, (i & 4) ? hl : -hl }, w[3];
        xf_pt(X, lp, w);
        float up = w[1] - gy;
        pts[i][0] = w[0] - sx / sy * up; pts[i][1] = w[2] - sz / sy * up;
    }
    /* convex hull (gift wrap, 8 points) */
    int hull[9], nh = 0, start = 0;
    for (int i = 1; i < 8; i++) if (pts[i][0] < pts[start][0]) start = i;
    int p = start;
    do {
        hull[nh++] = p;
        int q = (p + 1) % 8;
        for (int i = 0; i < 8; i++) {
            float cr = (pts[q][0] - pts[p][0]) * (pts[i][1] - pts[p][1]) - (pts[q][1] - pts[p][1]) * (pts[i][0] - pts[p][0]);
            if (cr < 0) q = i;
        }
        p = q;
    } while (p != start && nh < 8);
    r3d_mode(RM_SHADOW);
    float cx = 0, cz = 0;
    for (int i = 0; i < nh; i++) { cx += pts[hull[i]][0]; cz += pts[hull[i]][1]; }
    cx /= (float)nh; cz /= (float)nh;
    uint32_t sc = RGBA(16, 22, 48, (int)(118 * strength));
    for (int i = 0; i < nh; i++) {
        int a = hull[i], b = hull[(i + 1) % nh];
        r3d_tri3(cx, gy + 0.14f, cz, sc, pts[a][0], gy + 0.14f, pts[a][1], sc, pts[b][0], gy + 0.14f, pts[b][1], sc);
    }
    r3d_mode(RM_ALPHA);
    r3d_blob(X->o[0], gy + 0.16f, X->o[2], M->wid * 0.55f, M->len * 0.55f, 0, RGBA(0, 0, 0, (int)(70 * strength)), 0x00000000u);
}

void car_draw(const Car *c, float t, int night, float shadow)
{
    const Model *M = &models[c->body];
    Xf X;
    float gy = c->y;
    float lift = 0.7f + 0.09f * sinf(t * 3.2f + (float)c->number * 1.7f);
    make_xf(&X, c->x, c->y + c->air + lift, c->z, c->h, c->pitch * 0.6f, c->roll * 1.8f + c->steer_vis * -0.06f);
    Shade S;
    setup_shade(&S, c->x, c->y, c->z, night);
    S.paint = c->paint; S.paint2 = c->paint2; S.livery = c->livery;
    S.braking = c->in.brake > 0.2f && c->speed > 1.0f;
    Xf G;
    make_xf(&G, c->x, gy, c->z, c->h, 0, 0);
    if (shadow > 0) draw_shadow(M, &G, gy, night, shadow);
    r3d_mode(RM_OPAQUE);
    draw_model(M, &X, &S);
    /* the hover field: a soft glow on the deck under the car */
    r3d_mode(RM_ADD);
    if (c->livery == LIVERY_SIGNATURE) {
        r3d_blob(c->x, gy + 0.08f, c->z, M->wid * 1.0f, M->len * 0.72f, -c->h, 0x2A9AFFD0u, 0x2A9AFF00u);
        r3d_blob(c->x, gy + 0.09f, c->z, M->wid * 0.45f, M->len * 0.4f, -c->h, 0xA8E8FFA0u, 0xA8E8FF00u);
        if (c->boost_t > 0) {
            /* PRISM DRIVE: a ribbon of spectrum light streaming off the tail, and a shock ring */
            float fx = sinf(c->h), fz = cosf(c->h), rx = -fz, rz = fx;
            for (int i = 0; i < 26; i++) {
                float d = 2.4f + (float)i * 0.75f, w = sinf(t * 9.0f - (float)i * 0.45f) * 0.25f * (float)i / 26.0f;
                uint32_t hc = col_hsv(0.5f + (float)i / 26.0f * 0.18f, 0.55f + 0.4f * (float)i / 26.0f, 1.0f, (int)(150.0f * (1.0f - (float)i / 26.0f)));
                for (int s = -1; s <= 1; s += 2) {
                    float px = c->x - fx * d + rx * ((float)s * 0.55f + w), pz = c->z - fz * d + rz * ((float)s * 0.55f + w);
                    r3d_glow(px, gy + 1.0f, pz, 0.6f + (float)i * 0.025f, hc);
                }
            }
            float ring = fmodf(t * 1.6f, 1.0f), rr = 2.0f + ring * 7.0f;
            for (int i = 0; i < 24; i++) {
                float a = (float)i / 24.0f * 6.2831853f;
                r3d_glow(c->x + cosf(a) * rr, gy + 0.3f, c->z + sinf(a) * rr, 0.6f, col_hsv(0.55f + 0.08f * sinf((float)i), 0.7f, 1.0f, (int)(130.0f * (1.0f - ring))));
            }
        }
    } else
    r3d_blob(c->x, gy + 0.08f, c->z, M->wid * 0.75f, M->len * 0.6f, -c->h, 0x5AD8FFB0u, 0x5AD8FF00u);
    r3d_blob(c->x, gy + 0.09f, c->z, M->wid * 0.35f, M->len * 0.35f, -c->h, 0xC8F4FF90u, 0xC8F4FF00u);
    float th = 0.45f + 0.55f * c->in.throttle;
    for (int s = -1; s <= 1; s += 2) {
        float e0[3] = { (float)s * (M->tx > 0.1f ? M->tx : 0.0f), M->ty, -M->len * 0.45f - 0.1f };
        float w0[3];
        xf_pt(&X, e0, w0);
        r3d_glow(w0[0], w0[1], w0[2], 0.5f + 0.3f * th, 0x8AE8FF80u);
        float e1[3] = { e0[0], e0[1], e0[2] - 1.2f * th }, w1[3];
        xf_pt(&X, e1, w1);
        r3d_glow(w1[0], w1[1], w1[2], 0.45f * th, 0x3A8AFF90u);
        if (M->tx <= 0.1f) break;
    }
    r3d_mode(RM_OPAQUE);
    /* glows: headlights at night, brake lights, nitro exhaust */
    r3d_mode(RM_ADD);
    float hp[2][3], tp[2][3];
    for (int s = 0; s < 2; s++) {
        float sx = s ? M->hx : -M->hx;
        float a[3] = { sx, M->hy, M->hz + 0.05f }, b[3] = { sx, M->ty, -M->hz - 0.05f };
        xf_pt(&X, a, hp[s]); xf_pt(&X, b, tp[s]);
        if (night) r3d_glow(hp[s][0], hp[s][1], hp[s][2], 0.8f, 0xDDF4FF60u);
        if (S.braking || night) r3d_glow(tp[s][0], tp[s][1], tp[s][2], S.braking ? 1.2f : 0.7f, S.braking ? 0xFF2A20C0u : 0xFF202080u);
    }
    if (c->boost_t > 0 || c->mini_t > 0) {
        float k = c->boost_t > 0 ? 1.0f : 0.6f;
        float fl = 0.8f + 0.2f * sinf(t * 60.0f + (float)c->number);
        for (int s = -1; s <= 1; s += 2) {
            float e0[3] = { (float)s * 0.45f, 0.5f, -M->len * 0.5f - 0.2f }, e1[3] = { (float)s * 0.45f, 0.5f, -M->len * 0.5f - 1.8f * k * fl };
            float w0[3], w1[3];
            xf_pt(&X, e0, w0); xf_pt(&X, e1, w1);
            uint32_t core = c->boost_t > 0 ? 0x9AD8FFF0u : 0xFFB050E0u;
            r3d_glow(w0[0], w0[1], w0[2], 0.8f * k, core);
            r3d_glow((w0[0] + w1[0]) * 0.5f, (w0[1] + w1[1]) * 0.5f, (w0[2] + w1[2]) * 0.5f, 0.7f * k, c->boost_t > 0 ? 0x3A7AFFC0u : 0xFF6A20B0u);
            r3d_glow(w1[0], w1[1], w1[2], 0.45f * k, 0xFF8A3A90u);
        }
    }
}

void car_headlights(const Car *c)
{
    const Model *M = &models[c->body];
    float sh = sinf(c->h), ch = cosf(c->h);
    float fx = sh, fz = ch, rx = -ch, rz = sh;
    float ox = c->x + fx * (M->len * 0.5f), oz = c->z + fz * (M->len * 0.5f);
    const int NA = 6, ND = 5;
    const float L = 24.0f, spread = 0.42f;
    static float px[7][6], py[7][6], pz[7][6];
    static float kk[7][6];
    for (int i = 0; i <= NA; i++) {
        float a = ((float)i / (float)NA - 0.5f) * 2.0f * spread;
        float dx = fx * cosf(a) + rx * sinf(a), dz = fz * cosf(a) + rz * sinf(a);
        for (int j = 0; j <= ND; j++) {
            float d = (float)j / (float)ND;
            float dd = 0.6f + d * L;
            float x = ox + dx * dd, z = oz + dz * dd;
            px[i][j] = x; pz[i][j] = z;
            py[i][j] = track_height(x, z) + 0.2f;
            float edge = 1.0f - fabsf((float)i / (float)NA - 0.5f) * 2.0f;
            float fall = j == 0 ? 0.7f : (1.0f - d) * (0.6f + 0.4f * (1.0f - d));
            kk[i][j] = sat(edge * 1.6f) * fall;
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        r3d_mode(pass == 0 ? RM_MUL2 : RM_ADD);
        for (int i = 0; i < NA; i++)
            for (int j = 0; j < ND; j++) {
                uint32_t cc[4];
                float ks[4] = { kk[i][j], kk[i + 1][j], kk[i + 1][j + 1], kk[i][j + 1] };
                for (int q = 0; q < 4; q++) {
                    if (pass == 0) { int v = 128 + (int)(ks[q] * 50.0f); cc[q] = RGBA(v, v, v, 255); }
                    else cc[q] = RGBA(200, 236, 255, (int)(ks[q] * 30.0f));
                }
                r3d_tri3(px[i][j], py[i][j], pz[i][j], cc[0], px[i + 1][j], py[i + 1][j], pz[i + 1][j], cc[1], px[i + 1][j + 1], py[i + 1][j + 1], pz[i + 1][j + 1], cc[2]);
                r3d_tri3(px[i][j], py[i][j], pz[i][j], cc[0], px[i + 1][j + 1], py[i + 1][j + 1], pz[i + 1][j + 1], cc[2], px[i][j + 1], py[i][j + 1], pz[i][j + 1], cc[3]);
            }
    }
}

void car_draw_showroom(int body, uint32_t paint, uint32_t paint2, int livery, float x, float y, float z, float yaw, float t)
{
    const Model *M = &models[body];
    Xf X;
    make_xf(&X, x, y + 0.7f, z, yaw, 0, 0);
    Shade S;
    S.sun[0] = -0.45f; S.sun[1] = 0.75f; S.sun[2] = 0.48f;
    float sl = sqrtf(S.sun[0] * S.sun[0] + S.sun[1] * S.sun[1] + S.sun[2] * S.sun[2]);
    for (int k = 0; k < 3; k++) S.sun[k] /= sl;
    S.sunc[0] = 0.95f; S.sunc[1] = 0.92f; S.sunc[2] = 0.88f;
    S.amb[0] = 0.42f; S.amb[1] = 0.45f; S.amb[2] = 0.55f;
    float vx = r_cam.ex - x, vy = r_cam.ey - y, vz = r_cam.ez - z, vl = sqrtf(vx * vx + vy * vy + vz * vz);
    float hx = S.sun[0] + vx / vl, hy = S.sun[1] + vy / vl, hz = S.sun[2] + vz / vl, hl = sqrtf(hx * hx + hy * hy + hz * hz);
    S.half[0] = hx / hl; S.half[1] = hy / hl; S.half[2] = hz / hl;
    S.view[0] = vx / vl; S.view[1] = vy / vl; S.view[2] = vz / vl;
    S.night = 0; S.braking = 0; S.paint = paint; S.paint2 = paint2; S.livery = livery;
    S.view[0] = vx / vl; S.view[1] = vy / vl; S.view[2] = vz / vl;
    r3d_mode(RM_OPAQUE);
    draw_model(M, &X, &S);
    float rot = t * 3.0f;
    (void)rot;
    r3d_mode(RM_ADD);
    r3d_blob(x, 0.05f, z, M->wid * (livery == LIVERY_SIGNATURE ? 1.1f : 0.8f), M->len * 0.7f, -yaw, livery == LIVERY_SIGNATURE ? 0x2A9AFFD0u : 0x5AD8FFA0u, 0x00000000u);
    for (int s = -1; s <= 1; s += 2) {
        float e[3] = { (float)s * M->tx, M->ty, -M->len * 0.45f - 0.1f }, w[3];
        xf_pt(&X, e, w);
        r3d_glow(w[0], w[1], w[2], 0.7f, livery == LIVERY_SIGNATURE ? 0x6AC8FFC0u : 0x8AE8FFC0u);
    }
    r3d_mode(RM_OPAQUE);
}

/* ------------------------------------------------------------------ */
/* physics                                                             */
/* ------------------------------------------------------------------ */
void car_place(Car *c, float dist, float lat)
{
    float x, z;
    track_point(dist, lat, &x, &z);
    int k = track_sample_at(dist);
    const TSample *s = &trk.s[k];
    c->x = x; c->z = z; c->y = s->y;
    c->h = atan2f(s->tx, s->tz);
    c->vx = c->vz = 0; c->w = 0; c->speed = 0;
    c->air = 0; c->vy = 0; c->pitch = c->roll = c->susp = 0;
    c->sample = k;
    c->lat = lat; c->along = 0;
    c->drifting = 0; c->drift_charge = 0; c->drift_tier = 0;
    c->boost_t = c->mini_t = 0;
    c->stuck_t = c->wrong_t = 0;
    c->rpm = 0.15f; c->gear = 1;
}


/* the turbine's spin (0..1+): follows the speed, spools up with the throttle and the boost - no gears */
static void update_engine(Car *c, float dt, float vmax_eff)
{
    float v = c->speed / (vmax_eff > 1 ? vmax_eff : 1);
    float target = 0.22f + 0.7f * sat(v) + 0.12f * c->in.throttle;
    if (c->boost_t > 0) target += 0.18f;
    else if (c->mini_t > 0) target += 0.08f;
    if (c->drifting) target += 0.05f;
    target = clampf(target, 0.15f, 1.2f);
    c->rpm += (target - c->rpm) * damp(c->in.throttle > 0.5f ? 5.0f : 3.0f, dt);
    c->gear = 1;
}

static void drift_end(Car *c)
{
    if (!c->drifting) return;
    c->drifting = 0;
    c->drift_latch = c->in.drift;          /* still holding L: don't slam on the brakes */
    if (c->drift_tier > 0) {
        const float mt[4] = { 0, 0.55f, 0.95f, 1.45f }, nb[4] = { 0, 0.18f, 0.35f, 0.6f };
        c->mini_t = fmaxf(c->mini_t, mt[c->drift_tier]);
        if (c->boost_t <= 0) c->nitro = fminf(1.0f, c->nitro + nb[c->drift_tier] * 0.5f * (0.8f + 0.2f * c->st.drift));
        c->ev_boost = c->drift_tier;
    }
    c->drift_total += c->drift_pts * (1.0f + 0.5f * (float)c->drift_tier);
    if (c->drift_pts > c->drift_best) c->drift_best = c->drift_pts;
    c->drift_charge = 0; c->drift_tier = 0;
}

static void car_step(Car *c, float dt, int racing)
{
    const TrackInfo *I = trk.info;
    CarInput *in = &c->in;
    if (!racing) {
        /* on the grid: held in place by the start clamps (no creeping, and never backwards) */
        in->steer = 0; in->drift = 0; in->nitro = 0; in->hop = 0;
        c->vx = c->vz = 0; c->w = 0; c->speed = 0; c->slip = 0;
        c->steer_vis *= 0.9f;
        return;
    }

    float sh = sinf(c->h), ch = cosf(c->h);
    float fx = sh, fz = ch, rx = -ch, rz = sh;
    float vf = c->vx * fx + c->vz * fz, vl = c->vx * rx + c->vz * rz;
    float speed = sqrtf(vf * vf + vl * vl);

    /* surface */
    const TSample *s = &trk.s[c->sample];
    float wall = c->lat < 0 ? s->wl : s->wr;
    /* the whole deck races; only the strip against the energy walls drags a little */
    c->offroad = fabsf(c->lat) > s->hw + 2.6f;
    float surf = I->grip * (c->offroad ? 0.9f : 1.0f);
    float grip = c->st.grip * surf;
    int airborne = c->air > 0.05f;

    /* boost: unlimited, but it has to recharge after every use */
    if (in->nitro && c->nitro >= 1.0f && c->boost_t <= 0) {
        c->nitro = 0;
        c->boost_t = BOOST_TIME;
        c->ev_nitro = 1;
    }
    if (c->boost_t <= 0 && c->nitro < 1.0f) {
        c->nitro += dt / c->st.recharge;
        if (c->nitro >= 1.0f) { c->nitro = 1.0f; c->ev_ready = 1; }
    }
    float npow = c->st.npow;
    float vmax = c->st.vmax * (c->offroad ? 0.9f : 1.0f);
    float accel = c->st.accel * (c->offroad ? 0.9f : 1.0f);
    if (c->boost_t > 0) { vmax *= 1.22f + 0.035f * npow; accel += 18.0f + 3.2f * npow; c->boost_t -= dt; if (c->boost_t < 0) c->boost_t = 0; }
    if (c->mini_t > 0) { vmax *= 1.1f; accel += 12.0f; c->mini_t -= dt; if (c->mini_t < 0) c->mini_t = 0; }
    if (c->start_boost > 0) { accel += 14.0f; vmax *= 1.08f; c->start_boost -= dt; }
    if (c->slipstream > 0.5f) vmax *= 1.04f;

    /* drive */
    if (!airborne) {
        if (in->throttle > 0.02f && !(in->brake > 0.5f && vf < 1.0f && !c->drifting)) {
            if (vf < vmax) {
                float r = vf / vmax;
                float a = accel * (1.0f - r * r * 0.92f) * in->throttle;
                if (vf < 0) a += 10.0f;                     /* stop reversing quickly */
                vf += a * dt;
                if (vf > vmax) vf = vmax;
            } else {
                vf -= (vf - vmax) * 1.8f * dt;
            }
        }
        if (!in->drift) c->drift_latch = 0;
        if (in->brake > 0.05f && !c->drifting && !c->drift_latch) {
            if (vf > 0.5f) vf -= 24.0f * in->brake * dt;
            else if (in->throttle < 0.1f) { vf -= 9.0f * dt; if (vf < -9.0f) vf = -9.0f; }
        }
        if (in->throttle < 0.02f && in->brake < 0.05f) vf -= vf * 0.35f * dt + signf(vf) * 0.6f * dt;
        if (c->offroad) vf -= vf * 0.25f * dt;
        if (vf > vmax * 1.3f) vf = vmax * 1.3f;
        c->vx = fx * vf + rx * vl;
        c->vz = fz * vf + rz * vl;
    }

    /* drifting: L in a corner at speed (a hop lets it start slower) */
    float steer = in->steer;
    float fs = fabsf(steer);
    if (!c->drifting && !c->drift_latch && in->drift && fs > 0.3f && vf > (airborne ? 8.0f : 12.0f)) {
        c->drifting = 1;
        c->drift_dir = steer > 0 ? 1 : -1;
        c->drift_charge = 0; c->drift_tier = 0; c->drift_pts = 0; c->drift_t = 0; c->straight_t = 0;
        c->w += -(float)c->drift_dir * 0.9f;
    }
    if (c->drifting) {
        c->drift_t += dt;
        if (!in->drift || vf < 7.0f) drift_end(c);
    }

    /* steering */
    float ws_max = grip / (speed > 5.0f ? speed : 5.0f);
    if (ws_max > 2.6f) ws_max = 2.6f;
    float low = sat(speed / 3.0f);
    float wt;
    if (c->drifting) {
        float wmax = ws_max * 1.6f;
        float into = steer * (float)c->drift_dir;           /* +1 = steering into the drift */
        float k = 0.62f + 0.42f * into;                       /* 0.2 .. 1.04 */
        wt = -(float)c->drift_dir * wmax * k;
    } else {
        wt = -steer * ws_max * low * (vf < -0.5f ? -1.0f : 1.0f);
    }
    if (airborne) wt *= 0.5f;
    c->w += (wt - c->w) * damp(c->drifting ? 7.0f : 12.0f, dt);
    c->h += c->w * dt;
    c->h = wrap_angle(c->h);

    /* lateral grip */
    sh = sinf(c->h); ch = cosf(c->h);
    fx = sh; fz = ch; rx = -ch; rz = sh;
    float vx = c->vx, vz = c->vz;
    if (!airborne) {
        /* drive speed is kept along the new heading partially (the car "carves") */
        float sp0 = sqrtf(vx * vx + vz * vz);
        vf = vx * fx + vz * fz; vl = vx * rx + vz * rz;
        float lg = c->drifting ? 2.6f : 13.0f;
        lg *= surf;
        if (I->surface == SURF_SNOW && !c->drifting) lg *= 0.8f;
        vl *= expf(-lg * dt);
        vx = fx * vf + rx * vl; vz = fz * vf + rz * vl;
        if (c->drifting && vf > 0) {
            /* a drift keeps its momentum: the slide scrubs only a little speed */
            float sp1 = sqrtf(vx * vx + vz * vz);
            float keep = sp0 * (1.0f - 0.05f * dt);
            if (sp1 > 1e-3f && sp1 < keep) { vx *= keep / sp1; vz *= keep / sp1; }
        }
    }
    c->vx = vx; c->vz = vz;
    c->speed = sqrtf(vx * vx + vz * vz);
    c->slip = atan2f(vl, fabsf(vf) + 0.5f);

    /* drift charge, tiers and points */
    if (c->drifting) {
        float as = fabsf(sinf(c->slip));
        c->drift_charge += dt * (0.55f + 1.6f * as) * c->st.drift;
        int tier = c->drift_charge > 3.3f ? 3 : (c->drift_charge > 1.9f ? 2 : (c->drift_charge > 0.85f ? 1 : 0));
        if (tier > c->drift_tier) { c->drift_tier = tier; c->ev_tier = tier; }
        c->drift_pts += c->speed * (0.2f + as) * dt * 6.0f;
        if (as < 0.06f) { c->straight_t += dt; if (c->straight_t > 0.5f) drift_end(c); } else c->straight_t = 0;
    }

    /* hop */
    if (in->hop && !airborne && racing) { c->vy = 3.6f; c->air = 0.06f; }
    if (c->air > 0 || c->vy > 0) {
        c->air += c->vy * dt;
        c->vy -= 22.0f * dt;
        if (c->air <= 0) { c->air = 0; if (c->vy < -2.0f) c->ev_land = 1; c->vy = 0; }
    }

    /* move */
    c->x += c->vx * dt;
    c->z += c->vz * dt;

    /* track position */
    int prev = c->sample;
    c->sample = track_locate(c->sample, c->x, c->z, &c->lat, &c->along);
    s = &trk.s[c->sample];
    wall = c->lat < 0 ? s->wl : s->wr;
    /* barriers */
    float lim = wall - 1.25f;
    c->impact = 0;
    if (fabsf(c->lat) > lim) {
        float sg = c->lat > 0 ? 1.0f : -1.0f;
        float push = fabsf(c->lat) - lim;
        c->x -= s->nx * sg * push; c->z -= s->nz * sg * push;
        c->lat = sg * lim;
        float vn = (c->vx * s->nx + c->vz * s->nz) * sg;
        if (vn > 0) {
            /* the energy wall pushes back: a bounce that keeps most of the speed */
            c->vx -= s->nx * sg * vn * 1.45f; c->vz -= s->nz * sg * vn * 1.45f;
            float fr = 1.0f - fminf(0.2f, vn * 0.018f);
            c->vx *= fr; c->vz *= fr;
            c->impact = vn;
            if (vn > 2.5f && c->wall_t <= 0) { c->ev_impact = 1; c->ev_impact_v = vn; c->wall_t = 0.25f; }
            /* turn the nose back along the barrier */
            float th = atan2f(s->tx, s->tz);
            float dv = wrap_angle(th - c->h);
            if (fabsf(dv) < 1.4f) c->h += dv * fminf(1.0f, vn * 0.06f);
            if (c->drifting && vn > 6.0f) drift_end(c);
        }
        c->scrape = fminf(1.0f, c->speed / 20.0f);
    } else {
        c->scrape *= 0.8f;
    }
    if (c->wall_t > 0) c->wall_t -= dt;

    /* boost pads */
    if (c->pad_t > 0) c->pad_t -= dt;
    if (c->air <= 0.05f && c->pad_t <= 0 && track_is_boost(c->sample) && fabsf(c->lat - track_pad_lat(c->sample)) < 2.8f) {
        c->mini_t = fmaxf(c->mini_t, 1.0f);
        c->pad_t = 0.6f;
        c->ev_pad = 1;
    }

    /* ground */
    {
        const TSample *b = &trk.s[(c->sample + (c->along >= 0 ? 1 : trk.n - 1)) % trk.n];
        float t = fabsf(c->along) / (trk.length / (float)trk.n);
        float gy = lerpf(s->y, b->y, clampf(t, 0, 1));
        if (c->offroad && fabsf(c->lat) > s->hw + 1.2f) gy -= 0.03f;
        c->y = gy;
        /* pitch from the slope along the track, plus squat and dive; roll from cornering */
        float slope = (b->y - s->y) / (trk.length / (float)trk.n) * (c->along >= 0 ? 1.0f : -1.0f);
        float dirk = fx * s->tx + fz * s->tz;
        float pt = atanf(slope * dirk) - (in->throttle > 0.5f && c->speed < c->st.vmax * 0.7f ? 0.025f : 0.0f) + (in->brake > 0.5f && vf > 3.0f ? 0.045f : 0.0f);
        c->pitch += (pt - c->pitch) * damp(8.0f, dt);
        float rt = clampf(c->w * c->speed * 0.0042f, -0.1f, 0.1f) + (c->drifting ? (float)c->drift_dir * 0.03f : 0.0f);
        c->roll += (rt - c->roll) * damp(7.0f, dt);
        float bump = c->offroad ? 0.04f * sinf(c->progress * 2.7f) : 0.0f;
        c->susp += (bump - c->susp) * damp(10.0f, dt);
    }

    /* progress and laps */
    {
        int n = trk.n;
        if (prev > n * 3 / 4 && c->sample < n / 4) {
            if (c->cp) { c->lap++; c->cp = 0; c->ev_horn = 0; }
        } else if (prev < n / 4 && c->sample > n * 3 / 4) {
            c->lap--; c->cp = 1;
        }
        if (c->sample > n * 2 / 5 && c->sample < n * 3 / 5) c->cp = 1;
        c->progress = (float)(c->lap - 1) * trk.length + s->dist + c->along;
        /* wrong way */
        float dirk = fx * s->tx + fz * s->tz;
        if (dirk < -0.3f && c->speed > 3.0f) c->wrong_t += dt; else c->wrong_t = 0;
    }

    /* visuals */
    c->steer_vis += (in->steer - c->steer_vis) * damp(12.0f, dt);
    c->wheel_rot += vf * dt / 0.5f;
    if (c->wheel_rot > 1000.0f) c->wheel_rot -= 6.2831853f * 159.0f;
    if (c->wheel_rot < -1000.0f) c->wheel_rot += 6.2831853f * 159.0f;
    update_engine(c, dt, c->st.vmax);

    /* stuck */
    if (racing && c->speed < 2.0f && in->throttle > 0.5f) c->stuck_t += dt; else c->stuck_t = 0;
}

static void collide(Car *a, Car *b)
{
    const float R = 1.25f, OFF = 1.15f;
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2) {
            float ax = a->x + sinf(a->h) * OFF * (float)i, az = a->z + cosf(a->h) * OFF * (float)i;
            float bx = b->x + sinf(b->h) * OFF * (float)j, bz = b->z + cosf(b->h) * OFF * (float)j;
            float dx = bx - ax, dz = bz - az, d2 = dx * dx + dz * dz;
            if (d2 >= 4 * R * R || d2 < 1e-6f) continue;
            float d = sqrtf(d2), nx = dx / d, nz = dz / d, pen = 2 * R - d;
            a->x -= nx * pen * 0.5f; a->z -= nz * pen * 0.5f;
            b->x += nx * pen * 0.5f; b->z += nz * pen * 0.5f;
            float rv = (b->vx - a->vx) * nx + (b->vz - a->vz) * nz;
            if (rv < 0) {
                float jm = -rv * 0.65f;
                a->vx -= nx * jm; a->vz -= nz * jm;
                b->vx += nx * jm; b->vz += nz * jm;
                /* off-centre hits twist the cars a little */
                a->w += (float)i * jm * 0.05f * ((nx * cosf(a->h) - nz * sinf(a->h)) > 0 ? 1.0f : -1.0f);
                b->w -= (float)j * jm * 0.05f * ((nx * cosf(b->h) - nz * sinf(b->h)) > 0 ? 1.0f : -1.0f);
                if (-rv > 3.0f) {
                    if (a->wall_t <= 0) { a->ev_impact = 1; a->ev_impact_v = -rv; a->wall_t = 0.2f; }
                    if (b->wall_t <= 0) { b->wall_t = 0.2f; }
                }
            }
        }
}

void cars_step(float dt, int racing)
{
    for (int i = 0; i < ncars; i++) {
        Car *c = &cars[i];
        if (c->finished && c->is_player == 0 && !racing) {}
        car_step(c, dt, racing || c->finished);
    }
    for (int i = 0; i < ncars; i++)
        for (int j = i + 1; j < ncars; j++) collide(&cars[i], &cars[j]);
    /* slipstream: right behind another car */
    for (int i = 0; i < ncars; i++) {
        Car *c = &cars[i];
        float sx = sinf(c->h), sz = cosf(c->h);
        int in_tow = 0;
        for (int j = 0; j < ncars && !in_tow; j++) {
            if (j == i) continue;
            float dx = cars[j].x - c->x, dz = cars[j].z - c->z;
            float along = dx * sx + dz * sz, side = fabsf(dx * sz - dz * sx);
            if (along > 4.0f && along < 20.0f && side < 2.2f && c->speed > 18.0f) in_tow = 1;
        }
        if (in_tow) { c->slipstream += dt; if (c->boost_t <= 0) c->nitro = fminf(1.0f, c->nitro + 0.12f * dt); }
        else c->slipstream = 0;
    }
}

/* ------------------------------------------------------------------ */
/* AI                                                                  */
/* ------------------------------------------------------------------ */
void car_ai_input(Car *c, float dt)
{
    int n = trk.n;
    float step = trk.length / (float)n;
    float speed = c->speed;
    /* target speed: the slowest point of the line over the next stretch, scaled for this car's grip */
    float gk = sqrtf(c->st.grip * trk.info->grip / 18.0f);
    float vt = 1e9f;
    int look = 6 + (int)(speed * 0.9f / step);
    for (int k = 1; k <= look; k++) {
        const TSample *s = &trk.s[(c->sample + k) % n];
        float v = s->vline * gk;
        /* allow for the distance still available to brake */
        float d = (float)k * step;
        float vb = sqrtf(v * v + 2.0f * 13.0f * d);
        if (vb < vt) vt = vb;
    }
    {
        const TSample *s = &trk.s[c->sample];
        float v = s->vline * gk;
        if (v < vt) vt = v;
    }
    vt *= c->skill;
    /* lookahead point on the racing line (plus this driver's lane and any avoidance) */
    float la = 4.5f + speed * 0.3f;
    int kk = (c->sample + (int)(la / step) + 1) % n;
    const TSample *t = &trk.s[kk];
    float lane = t->line + c->lane + c->ai_avoid;
    float lim = t->hw - 1.6f;
    lane = clampf(lane, -lim, lim);
    float tx = t->x + t->nx * lane, tz = t->z + t->nz * lane;
    float ht = atan2f(tx - c->x, tz - c->z);
    float err = wrap_angle(ht - c->h);
    float steer = clampf(-err * 2.6f, -1.0f, 1.0f);
    /* avoid the car in front */
    float avoid_target = 0;
    for (int j = 0; j < ncars; j++) {
        const Car *o = &cars[j];
        if (o == c) continue;
        float dp = o->progress - c->progress;
        if (dp < 1.0f || dp > 14.0f) continue;
        float dl = o->lat - c->lat;
        if (fabsf(dl) > 2.8f) continue;
        if (o->speed > speed + 1.0f) continue;
        avoid_target = dl > 0 ? -3.0f : 3.0f;
        if (fabsf(c->lat + avoid_target) > trk.s[c->sample].hw - 1.5f) avoid_target = -avoid_target;
    }
    c->ai_avoid += (avoid_target - c->ai_avoid) * damp(2.5f, dt);
    c->in.steer = steer;
    if (speed < vt) { c->in.throttle = 1.0f; c->in.brake = 0; }
    else if (speed > vt + 2.5f) { c->in.throttle = 0; c->in.brake = clampf((speed - vt) / 6.0f, 0.2f, 1.0f); }
    else { c->in.throttle = 0.4f; c->in.brake = 0; }
    /* drifts through the tight corners (for show and for the mini-turbo) */
    const TSample *here = &trk.s[(c->sample + 4) % n];
    float ac = fabsf(here->lcurv);
    if (c->drifting) {
        c->in.drift = ac > 0.012f && c->drift_t < 2.6f;
        c->in.steer = clampf(steer * 0.7f + (float)c->drift_dir * 0.35f, -1, 1);
        if (c->drift_tier >= 2 && ac < 0.02f) c->in.drift = 0;
    } else {
        c->in.drift = ac > 0.026f && speed > 18.0f && fabsf(steer) > 0.35f && c->ai_drift_t <= 0;
        if (c->in.drift) c->ai_drift_t = 2.5f;
    }
    if (c->ai_drift_t > 0) c->ai_drift_t -= dt;
    /* nitro on the straights */
    c->in.nitro = 0;
    c->ai_nitro_t -= dt;
    if (c->nitro >= 1.0f && c->ai_nitro_t <= 0 && vt > speed + 8.0f && fabsf(steer) < 0.2f) {
        c->in.nitro = 1; c->ai_nitro_t = 1.0f + 6.0f * (1.0f - c->skill);
    }
    c->in.hop = 0;
}
