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

const char *BODY_NAME[NUM_BODIES] = { "HORNET RS", "THUNDER V8", "BAJA BUGGY", "APEX GT" };
const uint32_t PAINT[NUM_PAINTS] = {
    0x1E5ADCFFu, 0xE8302AFFu, 0xF2C42AFFu, 0x22A85AFFu, 0xF4F4F2FFu, 0x26262CFFu,
    0xF2782AFFu, 0x8A3AD8FFu, 0x2AC8E0FFu, 0xF25AA8FFu, 0x9AA2AEFFu, 0x6A2A1EFFu,
};
const char *PAINT_NAME[NUM_PAINTS] = { "RALLY BLUE", "RACING RED", "SUNBURST", "EMERALD", "PEARL WHITE", "MIDNIGHT",
                                       "BLAZE ORANGE", "ULTRAVIOLET", "ICE CYAN", "BUBBLEGUM", "GUNMETAL", "MAHOGANY" };

void car_stats_from_levels(const uint8_t *lv, CarStats *st)
{
    st->vmax  = 36.0f + 2.9f * (float)lv[UP_ENGINE];
    st->accel = 12.5f + 1.7f * (float)lv[UP_TURBO];
    st->grip  = 15.0f + 1.25f * (float)lv[UP_TIRES];
    st->tank  = 2.0f + (float)(lv[UP_TANK] > 3 ? 3 : lv[UP_TANK]);
    st->npow  = (float)lv[UP_NITRO];
    st->drift = 1.0f + 0.22f * (float)lv[UP_DRIFT];
}

/* ------------------------------------------------------------------ */
/* models                                                              */
/* ------------------------------------------------------------------ */
enum { M_PAINT, M_PAINT_LO, M_PAINT_ROOF, M_STRIPE, M_GLASS, M_DARK, M_CHROME, M_HEAD, M_TAIL, M_TIRE, M_RIM, M_SEAT, M_HELMET, M_AMBER };

typedef struct { float p[3][3]; float n[3]; uint8_t m; } MTri;
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
    float B[4][3] = { { -wb, yb, zb0 }, { wb, yb, zb0 }, { wb, yb, zb1 }, { -wb, yb, zb1 } };
    float T[4][3] = { { -wt, yt, zt0 }, { wt, yt, zt0 }, { wt, yt, zt1 }, { -wt, yt, zt1 } };
    float c[3] = { 0, (yb + yt) * 0.5f, (zb0 + zb1 + zt0 + zt1) * 0.25f };
    if (mtop >= 0) mquad(T[0], T[1], T[2], T[3], mtop, c);
    if (mfront >= 0) mquad(B[3], B[2], T[2], T[3], mfront, c);
    if (mback >= 0) mquad(B[0], B[1], T[1], T[0], mback, c);
    if (mside >= 0) { mquad(B[1], B[2], T[2], T[1], mside, c); mquad(B[0], B[3], T[3], T[0], mside, c); }
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

static void build_hornet(void)
{
    Model *m = cur;
    fbox(1.16f, 0.36f, -2.25f, 2.25f, 1.25f, 0.92f, -2.38f, 2.32f, -1, M_PAINT_LO, M_PAINT_LO, M_PAINT_LO);
    fbox(1.25f, 0.92f, -2.38f, 2.32f, 1.12f, 1.12f, -2.22f, 2.05f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(1.08f, 1.12f, -2.0f, 0.7f, 0.86f, 1.86f, -1.92f, -0.2f, M_PAINT_ROOF, M_GLASS, M_GLASS, M_GLASS);
    stripes(1.125f, 0.75f, 2.05f, 0.2f);
    stripes(1.865f, -1.92f, -0.2f, 0.2f);
    /* roof spoiler and rally light pod */
    fbox(0.9f, 1.86f, -2.05f, -1.75f, 0.92f, 1.98f, -2.15f, -1.85f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(0, 1.12f, 1.24f, 1.85f, 0.7f, 0.12f, M_DARK, M_DARK);
    for (int i = -1; i <= 1; i += 2) for (int k = 0; k < 2; k++) {
        float x = (float)i * (0.18f + 0.34f * (float)k);
        panel(x - 0.13f, 1.0f, 2.0f, x + 0.13f, 1.0f, 2.0f, x + 0.13f, 1.22f, 1.98f, x - 0.13f, 1.22f, 1.98f, M_AMBER);
    }
    /* bumpers */
    mbox(0, 0.32f, 0.6f, 2.36f, 1.2f, 0.12f, M_DARK, M_DARK);
    mbox(0, 0.32f, 0.6f, -2.42f, 1.2f, 0.1f, M_DARK, M_DARK);
    /* lights */
    for (int s = -1; s <= 1; s += 2) {
        float x0 = (float)s * 0.62f, x1 = (float)s * 1.05f;
        panel(x0, 0.7f, 2.33f, x1, 0.7f, 2.33f, x1, 0.88f, 2.31f, x0, 0.88f, 2.31f, M_HEAD);
        panel(x0, 0.7f, -2.39f, x1, 0.7f, -2.39f, x1, 0.86f, -2.39f, x0, 0.86f, -2.39f, M_TAIL);
    }
    m->wx = 1.1f; m->wz = 1.55f; m->wy = 0.48f; m->wr = 0.48f; m->ww = 0.4f;
    m->len = 4.8f; m->wid = 2.5f; m->hgt = 1.9f;
    m->hx = 0.84f; m->hy = 0.8f; m->hz = 2.36f; m->tx = 0.84f; m->ty = 0.78f;
}

static void build_thunder(void)
{
    Model *m = cur;
    fbox(1.2f, 0.34f, -2.55f, 2.55f, 1.3f, 0.88f, -2.62f, 2.62f, -1, M_PAINT_LO, M_PAINT_LO, M_PAINT_LO);
    fbox(1.3f, 0.88f, -2.62f, 2.62f, 1.2f, 1.05f, -2.5f, 2.45f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(1.12f, 1.05f, -1.65f, 0.55f, 0.9f, 1.6f, -1.25f, -0.15f, M_PAINT_ROOF, M_GLASS, M_GLASS, M_GLASS);
    stripes(1.055f, 0.6f, 2.45f, 0.24f);
    stripes(1.605f, -1.25f, -0.15f, 0.24f);
    stripes(1.055f, -2.5f, -1.7f, 0.24f);
    /* hood scoop and ducktail */
    fbox(0.42f, 1.05f, 0.9f, 1.9f, 0.32f, 1.25f, 1.05f, 1.75f, M_DARK, M_DARK, M_DARK, M_DARK);
    fbox(1.15f, 1.05f, -2.55f, -2.2f, 1.18f, 1.22f, -2.62f, -2.3f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(0, 0.3f, 0.55f, 2.66f, 1.25f, 0.1f, M_CHROME, M_CHROME);
    mbox(0, 0.3f, 0.55f, -2.68f, 1.25f, 0.1f, M_CHROME, M_CHROME);
    panel(-0.55f, 0.6f, 2.63f, 0.55f, 0.6f, 2.63f, 0.55f, 0.82f, 2.62f, -0.55f, 0.82f, 2.62f, M_DARK);
    for (int s = -1; s <= 1; s += 2) {
        float x0 = (float)s * 0.7f, x1 = (float)s * 1.1f;
        panel(x0, 0.62f, 2.63f, x1, 0.62f, 2.63f, x1, 0.82f, 2.62f, x0, 0.82f, 2.62f, M_HEAD);
        panel(x0, 0.68f, -2.63f, x1, 0.68f, -2.63f, x1, 0.84f, -2.63f, x0, 0.84f, -2.63f, M_TAIL);
    }
    m->wx = 1.14f; m->wz = 1.7f; m->wy = 0.52f; m->wr = 0.52f; m->ww = 0.46f;
    m->len = 5.3f; m->wid = 2.6f; m->hgt = 1.6f;
    m->hx = 0.9f; m->hy = 0.72f; m->hz = 2.64f; m->tx = 0.9f; m->ty = 0.76f;
}

static void build_baja(void)
{
    Model *m = cur;
    /* tub and nose */
    fbox(0.88f, 0.55f, -1.85f, 1.7f, 0.98f, 0.98f, -1.95f, 1.75f, -1, M_PAINT_LO, M_PAINT_LO, M_PAINT_LO);
    fbox(0.98f, 0.98f, -1.95f, 1.75f, 0.9f, 1.08f, -1.85f, 1.6f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(0.8f, 0.55f, 1.7f, 2.45f, 0.68f, 0.92f, 1.6f, 2.25f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    stripes(1.085f, 0.4f, 1.6f, 0.18f);
    /* cockpit: seats and the driver's helmet */
    mbox(0, 1.08f, 1.25f, -0.4f, 0.75f, 0.75f, M_SEAT, M_SEAT);
    fbox(0.26f, 1.25f, -0.62f, -0.18f, 0.22f, 1.75f, -0.6f, -0.2f, M_HELMET, M_HELMET, M_HELMET, M_HELMET);
    /* roll cage */
    float px[4] = { -0.85f, 0.85f, -0.85f, 0.85f }, pz[4] = { 0.35f, 0.35f, -1.25f, -1.25f };
    for (int i = 0; i < 4; i++) mbox(px[i], 1.0f, 2.05f, pz[i], 0.07f, 0.07f, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(0, 2.0f, 2.12f, 0.35f, 0.92f, 0.07f, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(0, 2.0f, 2.12f, -1.25f, 0.92f, 0.07f, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(-0.85f, 2.0f, 2.12f, -0.45f, 0.07f, 0.85f, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(0.85f, 2.0f, 2.12f, -0.45f, 0.07f, 0.85f, M_PAINT_ROOF, M_PAINT_ROOF);
    /* engine and spare wheel at the back */
    mbox(0, 0.9f, 1.4f, -2.2f, 0.6f, 0.45f, M_DARK, M_DARK);
    fbox(0.5f, 1.15f, -2.6f, -2.0f, 0.5f, 1.45f, -2.6f, -2.0f, M_TIRE, M_TIRE, M_TIRE, M_TIRE);
    mbox(0, 1.45f, 1.5f, -2.3f, 0.28f, 0.2f, M_RIM, M_RIM);
    /* light bar on the cage */
    for (int i = -2; i <= 2; i++) panel((float)i * 0.32f - 0.12f, 2.12f, 0.43f, (float)i * 0.32f + 0.12f, 2.12f, 0.43f,
                                         (float)i * 0.32f + 0.12f, 2.3f, 0.43f, (float)i * 0.32f - 0.12f, 2.3f, 0.43f, M_AMBER);
    for (int s = -1; s <= 1; s += 2) {
        float x0 = (float)s * 0.3f, x1 = (float)s * 0.62f;
        panel(x0, 0.68f, 2.46f, x1, 0.68f, 2.46f, x1, 0.84f, 2.42f, x0, 0.84f, 2.42f, M_HEAD);
        panel(x0, 0.75f, -1.96f, x1, 0.75f, -1.96f, x1, 0.9f, -1.96f, x0, 0.9f, -1.96f, M_TAIL);
    }
    m->wx = 1.28f; m->wz = 1.45f; m->wy = 0.62f; m->wr = 0.62f; m->ww = 0.55f;
    m->len = 5.0f; m->wid = 2.9f; m->hgt = 2.3f;
    m->hx = 0.46f; m->hy = 0.76f; m->hz = 2.45f; m->tx = 0.46f; m->ty = 0.82f;
}

static void build_apex(void)
{
    Model *m = cur;
    fbox(1.2f, 0.3f, -2.45f, 2.5f, 1.32f, 0.76f, -2.52f, 2.58f, -1, M_PAINT_LO, M_PAINT_LO, M_PAINT_LO);
    fbox(1.32f, 0.76f, -2.52f, 2.58f, 1.16f, 0.94f, -2.36f, 2.3f, M_PAINT, M_PAINT, M_PAINT, M_PAINT);
    fbox(1.02f, 0.94f, -1.45f, 0.75f, 0.76f, 1.42f, -1.0f, -0.05f, M_PAINT_ROOF, M_GLASS, M_GLASS, M_GLASS);
    stripes(0.945f, 0.8f, 2.3f, 0.2f);
    stripes(1.425f, -1.0f, -0.05f, 0.2f);
    stripes(0.945f, -2.36f, -1.5f, 0.2f);
    /* side intakes */
    for (int s = -1; s <= 1; s += 2) {
        float x = (float)s * 1.29f;
        panel(x, 0.45f, -0.9f, x, 0.45f, 0.1f, x, 0.7f, 0.0f, x, 0.7f, -0.8f, M_DARK);
    }
    /* rear wing */
    mbox(-0.7f, 0.94f, 1.38f, -2.15f, 0.06f, 0.1f, M_DARK, M_DARK);
    mbox(0.7f, 0.94f, 1.38f, -2.15f, 0.06f, 0.1f, M_DARK, M_DARK);
    fbox(1.3f, 1.36f, -2.45f, -1.95f, 1.3f, 1.46f, -2.5f, -2.05f, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF, M_PAINT_ROOF);
    mbox(-1.3f, 1.25f, 1.55f, -2.25f, 0.04f, 0.3f, M_DARK, M_DARK);
    mbox(1.3f, 1.25f, 1.55f, -2.25f, 0.04f, 0.3f, M_DARK, M_DARK);
    mbox(0, 0.26f, 0.48f, 2.55f, 1.25f, 0.1f, M_DARK, M_DARK);
    for (int s = -1; s <= 1; s += 2) {
        float x0 = (float)s * 0.6f, x1 = (float)s * 1.1f;
        panel(x0, 0.62f, 2.56f, x1, 0.62f, 2.56f, x1, 0.74f, 2.5f, x0, 0.74f, 2.5f, M_HEAD);
        panel(x0, 0.6f, -2.53f, x1, 0.6f, -2.53f, x1, 0.72f, -2.53f, x0, 0.72f, -2.53f, M_TAIL);
    }
    m->wx = 1.14f; m->wz = 1.6f; m->wy = 0.48f; m->wr = 0.48f; m->ww = 0.44f;
    m->len = 5.1f; m->wid = 2.64f; m->hgt = 1.5f;
    m->hx = 0.85f; m->hy = 0.68f; m->hz = 2.56f; m->tx = 0.85f; m->ty = 0.66f;
}

void cars_models_init(void)
{
    void (*b[NUM_BODIES])(void) = { build_hornet, build_thunder, build_baja, build_apex };
    for (int i = 0; i < NUM_BODIES; i++) { cur = &models[i]; cur->n = 0; b[i](); }
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
    float sun[3], sunc[3], amb[3], half[3];
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
        float k = (shiny == 2 ? 0.6f : 0.42f) * (S->night ? 0.5f : 1.0f);
        r += sp * k * S->sunc[0] + sp * 0.1f; g += sp * k * S->sunc[1] + sp * 0.1f; b += sp * k * S->sunc[2] + sp * 0.1f;
        /* a cool sky reflection on upward faces (the "toy car lacquer" look) */
        float up = n[1] > 0 ? n[1] : 0;
        r += up * 0.06f; g += up * 0.07f; b += up * 0.09f;
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
        float n[3], a[3], b[3], c[3];
        xf_dir(X, t->n, n);
        uint32_t col = shade(S, n, t->m);
        if (!col) continue;
        xf_pt(X, t->p[0], a); xf_pt(X, t->p[1], b); xf_pt(X, t->p[2], c);
        r3d_tri3(a[0], a[1], a[2], col, b[0], b[1], b[2], col, c[0], c[1], c[2], col);
    }
}

/* one wheel: an octagonal drum on the local x axis; the rim spins with `rot`, `steer` turns it */
static void draw_wheel(const Model *M, const Xf *X, const Shade *S, float sx, float sz, float rot, float steer, float drop)
{
    const int N = 8;
    float r = M->wr, hw = M->ww * 0.5f, cy = M->wy - drop;
    float cs = cosf(steer), ss = sinf(steer);
    float side = sx > 0 ? 1.0f : -1.0f;
    float rim[N][3], tin[N][3], tout[N][3], hub[3];
    for (int i = 0; i < N; i++) {
        float a = rot + (float)i / (float)N * TAU;
        float ly = cosf(a) * r, lz = sinf(a) * r;
        /* local wheel frame: x = axle, then steered about y */
        float pi[3] = { -hw * side, ly, lz }, po[3] = { hw * side, ly, lz }, pr[3] = { hw * side + 0.02f * side, ly * 0.62f, lz * 0.62f };
        float *outs[3] = { tin[i], tout[i], rim[i] };
        float *ins[3] = { pi, po, pr };
        for (int k = 0; k < 3; k++) {
            float x = ins[k][0], z = ins[k][2];
            float q[3] = { sx + x * cs + z * ss, cy + ins[k][1], sz - x * ss + z * cs };
            xf_pt(X, q, outs[k]);
        }
    }
    float hl[3] = { sx + hw * side * cs + 0.04f * side, cy, sz - hw * side * ss };
    xf_pt(X, hl, hub);
    float up[3] = { 0, 1, 0 }, nn[3];
    xf_dir(X, up, nn);
    for (int i = 0; i < N; i++) {
        int j = (i + 1) % N;
        /* tread: normal points out from the axle */
        float mid[3] = { (tout[i][0] + tout[j][0]) * 0.5f, (tout[i][1] + tout[j][1]) * 0.5f, (tout[i][2] + tout[j][2]) * 0.5f };
        float axis[3] = { hub[0], hub[1], hub[2] };
        float n[3] = { mid[0] - axis[0], mid[1] - axis[1], mid[2] - axis[2] };
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-6f;
        n[0] /= l; n[1] /= l; n[2] /= l;
        uint32_t ct = shade(S, n, M_TIRE);
        r3d_tri3(tin[i][0], tin[i][1], tin[i][2], ct, tin[j][0], tin[j][1], tin[j][2], ct, tout[j][0], tout[j][1], tout[j][2], ct);
        r3d_tri3(tin[i][0], tin[i][1], tin[i][2], ct, tout[j][0], tout[j][1], tout[j][2], ct, tout[i][0], tout[i][1], tout[i][2], ct);
        /* outer side wall and the rim with alternating spokes */
        float sn[3] = { X->r[0] * side, X->r[1] * side, X->r[2] * side };
        uint32_t cw = shade(S, sn, M_TIRE);
        r3d_tri3(tout[i][0], tout[i][1], tout[i][2], cw, tout[j][0], tout[j][1], tout[j][2], cw, rim[j][0], rim[j][1], rim[j][2], cw);
        r3d_tri3(tout[i][0], tout[i][1], tout[i][2], cw, rim[j][0], rim[j][1], rim[j][2], cw, rim[i][0], rim[i][1], rim[i][2], cw);
        uint32_t cr = shade(S, sn, M_RIM);
        if (i & 1) cr = col_scale(cr, 0.55f);
        r3d_tri3(hub[0], hub[1], hub[2], cr, rim[i][0], rim[i][1], rim[i][2], cr, rim[j][0], rim[j][1], rim[j][2], cr);
    }
    (void)nn;
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
    make_xf(&X, c->x, c->y + c->air + c->susp, c->z, c->h, c->pitch, c->roll);
    Shade S;
    setup_shade(&S, c->x, c->y, c->z, night);
    S.paint = c->paint; S.paint2 = c->paint2; S.livery = c->livery;
    S.braking = c->in.brake > 0.2f && c->speed > 1.0f;
    Xf G;
    make_xf(&G, c->x, gy, c->z, c->h, 0, 0);
    if (shadow > 0) draw_shadow(M, &G, gy, night, shadow);
    r3d_mode(RM_OPAQUE);
    draw_model(M, &X, &S);
    float st = c->steer_vis * 0.45f;
    draw_wheel(M, &X, &S, -M->wx, M->wz, c->wheel_rot, -st, c->susp * 0.5f);
    draw_wheel(M, &X, &S, M->wx, M->wz, c->wheel_rot, -st, c->susp * 0.5f);
    draw_wheel(M, &X, &S, -M->wx, -M->wz, c->wheel_rot, 0, c->susp * 0.5f);
    draw_wheel(M, &X, &S, M->wx, -M->wz, c->wheel_rot, 0, c->susp * 0.5f);
    /* glows: headlights at night, brake lights, nitro exhaust */
    r3d_mode(RM_ADD);
    float hp[2][3], tp[2][3];
    for (int s = 0; s < 2; s++) {
        float sx = s ? M->hx : -M->hx;
        float a[3] = { sx, M->hy, M->hz + 0.05f }, b[3] = { sx, M->ty, -M->hz - 0.05f };
        xf_pt(&X, a, hp[s]); xf_pt(&X, b, tp[s]);
        if (night) r3d_glow(hp[s][0], hp[s][1], hp[s][2], 1.3f, 0xFFF4D8B0u);
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
                    if (pass == 0) { int v = 128 + (int)(ks[q] * 127.0f); cc[q] = RGBA(v, v, v - 6, 255); }
                    else cc[q] = RGBA(255, 236, 200, (int)(ks[q] * 70.0f));
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
    make_xf(&X, x, y, z, yaw, 0, 0);
    Shade S;
    S.sun[0] = -0.45f; S.sun[1] = 0.75f; S.sun[2] = 0.48f;
    float sl = sqrtf(S.sun[0] * S.sun[0] + S.sun[1] * S.sun[1] + S.sun[2] * S.sun[2]);
    for (int k = 0; k < 3; k++) S.sun[k] /= sl;
    S.sunc[0] = 0.95f; S.sunc[1] = 0.92f; S.sunc[2] = 0.88f;
    S.amb[0] = 0.42f; S.amb[1] = 0.45f; S.amb[2] = 0.55f;
    float vx = r_cam.ex - x, vy = r_cam.ey - y, vz = r_cam.ez - z, vl = sqrtf(vx * vx + vy * vy + vz * vz);
    float hx = S.sun[0] + vx / vl, hy = S.sun[1] + vy / vl, hz = S.sun[2] + vz / vl, hl = sqrtf(hx * hx + hy * hy + hz * hz);
    S.half[0] = hx / hl; S.half[1] = hy / hl; S.half[2] = hz / hl;
    S.night = 0; S.braking = 0; S.paint = paint; S.paint2 = paint2; S.livery = livery;
    r3d_mode(RM_OPAQUE);
    draw_model(M, &X, &S);
    float rot = t * 3.0f;
    draw_wheel(M, &X, &S, -M->wx, M->wz, rot, 0, 0);
    draw_wheel(M, &X, &S, M->wx, M->wz, rot, 0, 0);
    draw_wheel(M, &X, &S, -M->wx, -M->wz, rot, 0, 0);
    draw_wheel(M, &X, &S, M->wx, -M->wz, rot, 0, 0);
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

#define GEARS 5
static const float GEAR_TOP[GEARS] = { 0.24f, 0.42f, 0.62f, 0.82f, 1.06f };

static void update_engine(Car *c, float dt, float vmax_eff)
{
    float v = c->speed / (vmax_eff > 1 ? vmax_eff : 1);
    /* automatic gearbox: shift up near the top of each gear, down when it falls well below */
    int g = c->gear;
    if (c->shift_t > 0) c->shift_t -= dt;
    if (g < GEARS && v > GEAR_TOP[g - 1] * 0.97f && c->shift_t <= 0) { c->gear++; c->shift_t = 0.25f; c->ev_shift = 1; if (c->in.throttle > 0.5f && (c->number & 1)) c->ev_backfire = 1; }
    else if (g > 1 && v < GEAR_TOP[g - 2] * 0.72f) { c->gear--; c->shift_t = 0.15f; }
    float lo = g > 1 ? GEAR_TOP[g - 2] * 0.55f : 0.0f, hi = GEAR_TOP[g - 1];
    float target = 0.18f + 0.82f * sat((v - lo) / (hi - lo));
    if (c->drifting) target += 0.12f;                      /* wheels spinning up in the slide */
    if (c->in.throttle > 0.1f && c->speed < 4.0f) target = fmaxf(target, 0.35f + 0.4f * c->in.throttle);
    if (c->in.throttle < 0.1f) target *= 0.85f;
    target = clampf(target, 0.12f, 1.08f);
    c->rpm += (target - c->rpm) * damp(c->shift_t > 0 ? 18.0f : 9.0f, dt);
}

static void drift_end(Car *c)
{
    if (!c->drifting) return;
    c->drifting = 0;
    c->drift_latch = c->in.drift;          /* still holding L: don't slam on the brakes */
    if (c->drift_tier > 0) {
        const float mt[4] = { 0, 0.55f, 0.95f, 1.45f }, nb[4] = { 0, 0.18f, 0.35f, 0.6f };
        c->mini_t = fmaxf(c->mini_t, mt[c->drift_tier]);
        c->nitro = fminf(c->st.tank, c->nitro + nb[c->drift_tier] * (0.8f + 0.2f * c->st.drift));
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
    if (!racing) { in->throttle = 0; in->brake = 1; in->steer = 0; in->drift = 0; in->nitro = 0; in->hop = 0; }

    float sh = sinf(c->h), ch = cosf(c->h);
    float fx = sh, fz = ch, rx = -ch, rz = sh;
    float vf = c->vx * fx + c->vz * fz, vl = c->vx * rx + c->vz * rz;
    float speed = sqrtf(vf * vf + vl * vl);

    /* surface */
    const TSample *s = &trk.s[c->sample];
    float wall = c->lat < 0 ? s->wl : s->wr;
    c->offroad = fabsf(c->lat) > s->hw + 1.2f;           /* kerbs count as track */
    float surf = I->grip * (c->offroad ? 0.8f : 1.0f);
    float grip = c->st.grip * surf;
    int airborne = c->air > 0.05f;

    /* nitro */
    if (in->nitro && c->nitro >= 1.0f && c->boost_t < 1.6f) {
        c->nitro -= 1.0f;
        c->boost_t += 1.5f;
        c->ev_nitro = 1;
    }
    float npow = c->st.npow;
    float vmax = c->st.vmax * (c->offroad ? 0.66f : 1.0f);
    float accel = c->st.accel * (c->offroad ? 0.75f : 1.0f);
    if (c->boost_t > 0) { vmax *= 1.2f + 0.035f * npow; accel += 16.0f + 3.2f * npow; c->boost_t -= dt; if (c->boost_t < 0) c->boost_t = 0; }
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
        if (c->offroad) vf -= vf * 0.6f * dt;
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
            c->vx -= s->nx * sg * vn * 1.3f; c->vz -= s->nz * sg * vn * 1.3f;
            float fr = 1.0f - fminf(0.35f, vn * 0.03f);
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
        if (in_tow) { c->slipstream += dt; c->nitro = fminf(c->st.tank, c->nitro + 0.06f * dt); }
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
        c->in.nitro = 1; c->ai_nitro_t = 4.0f + 3.0f * (1.0f - c->skill);
    }
    c->in.hop = 0;
}
