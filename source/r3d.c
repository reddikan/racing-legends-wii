/*
 * RACING LEGENDS - command list, camera maths and the static chunk grid. See r3d.h.
 */
#include "r3d.h"
#include "ui.h"

#include <math.h>
#include <string.h>

static int sm_vis;

RV   *r_v;
int   r_nv;
RCmd  r_cmd[R_MAX_CMD];
int   r_ncmd;
uint32_t r_clear;
RCam  r_cam;
float r_post_top, r_post_bot;

static int cur_mode = -1, cur_2d = 0;

uint32_t col_mix(uint32_t a, uint32_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
        r |= (uint32_t)(int)(x + (y - x) * t + 0.5f) << s;
    }
    return r;
}

uint32_t col_scale(uint32_t c, float k)
{
    return RGBA((int)((float)(c >> 24) * k), (int)((float)((c >> 16) & 255) * k), (int)((float)((c >> 8) & 255) * k), (int)(c & 255));
}

uint32_t col_mul(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) r |= ((((a >> s) & 255) * ((b >> s) & 255) + 127) / 255) << s;
    return r;
}

uint32_t col_hsv(float h, float s, float v, int a)
{
    h -= floorf(h);
    float h6 = h * 6.0f;
    int   i = (int)h6;
    float f = h6 - (float)i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return RGBA((int)(r * 255), (int)(g * 255), (int)(b * 255), a);
}

/* ------------------------------------------------------------------ */
static void new_seg(int type, int mode)
{
    if (r_ncmd > 0) {
        RCmd *l = &r_cmd[r_ncmd - 1];
        if ((l->type == RC_TRIS || l->type == RC_2D) && l->count == 0) { l->type = (uint8_t)type; l->mode = (uint8_t)mode; return; }
    }
    if (r_ncmd >= R_MAX_CMD) return;
    RCmd *c = &r_cmd[r_ncmd++];
    c->type = (uint8_t)type; c->mode = (uint8_t)mode; c->mesh = 0; c->start = r_nv; c->count = 0;
}

void r3d_begin(uint32_t clear)
{
    if (!r_v) r_v = (RV *)big_alloc(sizeof(RV) * R_MAX_VTX);
    r_nv = 0; r_ncmd = 0; r_clear = clear;
    cur_mode = -1; cur_2d = 0;
    r_post_top = r_post_bot = 0.0f;
    r3d_mode(RM_OPAQUE);
}

void r3d_mode(int mode)
{
    if (mode == cur_mode && !cur_2d && r_ncmd > 0 && r_cmd[r_ncmd - 1].type == RC_TRIS) return;
    cur_mode = mode; cur_2d = 0;
    new_seg(RC_TRIS, mode);
}

void ui_begin(int mode)
{
    if (mode == cur_mode && cur_2d && r_ncmd > 0 && r_cmd[r_ncmd - 1].type == RC_2D) return;
    cur_mode = mode; cur_2d = 1;
    new_seg(RC_2D, mode);
}

void r3d_mesh(int id, int mode)
{
    if (id < 0 || r_ncmd >= R_MAX_CMD) return;
    if (r_ncmd > 0 && (r_cmd[r_ncmd - 1].type == RC_TRIS || r_cmd[r_ncmd - 1].type == RC_2D) && r_cmd[r_ncmd - 1].count == 0) r_ncmd--;
    RCmd *c = &r_cmd[r_ncmd++];
    c->type = RC_MESH; c->mode = (uint8_t)mode; c->mesh = (uint16_t)id; c->start = 0; c->count = 0;
    cur_mode = -1;
}

void r3d_post(void)
{
    if (r_ncmd > 0 && (r_cmd[r_ncmd - 1].type == RC_TRIS || r_cmd[r_ncmd - 1].type == RC_2D) && r_cmd[r_ncmd - 1].count == 0) r_ncmd--;
    if (r_ncmd >= R_MAX_CMD) return;
    RCmd *c = &r_cmd[r_ncmd++];
    c->type = RC_POST; c->mode = 0; c->mesh = 0; c->start = 0; c->count = 0;
    cur_mode = -1;
}

static inline void put(float x, float y, float z, uint32_t c)
{
    RV *v = &r_v[r_nv++];
    v->x = x; v->y = y; v->z = z; v->c = c;
}

void r3d_tri3(float x0, float y0, float z0, uint32_t c0, float x1, float y1, float z1, uint32_t c1,
              float x2, float y2, float z2, uint32_t c2)
{
    if (r_nv + 3 > R_MAX_VTX || r_ncmd == 0) return;
    if (cur_mode != RM_OPAQUE && ((c0 | c1 | c2) & 255) == 0) return;
    put(x0, y0, z0, c0); put(x1, y1, z1, c1); put(x2, y2, z2, c2);
    r_cmd[r_ncmd - 1].count += 3;
}

void r3d_tri(const RV *a, const RV *b, const RV *c)
{
    r3d_tri3(a->x, a->y, a->z, a->c, b->x, b->y, b->z, b->c, c->x, c->y, c->z, c->c);
}

void r3d_quad(const RV *a, const RV *b, const RV *c, const RV *d)
{
    r3d_tri(a, b, c);
    r3d_tri(a, c, d);
}

/* ------------------------------------------------------------------ */
/* camera                                                              */
/* ------------------------------------------------------------------ */
void r3d_camera(float ex, float ey, float ez, float tx, float ty, float tz, float fovy)
{
    RCam *c = &r_cam;
    c->ex = ex; c->ey = ey; c->ez = ez; c->tx = tx; c->ty = ty; c->tz = tz;
    c->fovy = fovy; c->znear = 4.0f; c->zfar = 600.0f;
    /* view basis: f = forward, s = right, u = up (right handed, looking down -z in view space) */
    float fx = tx - ex, fy = ty - ey, fz = tz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    /* s = f x up(0,1,0) */
    float sx = -fz, sy = 0.0f, sz = fx;
    float sl = sqrtf(sx * sx + sz * sz);
    if (sl < 1e-5f) { sx = 1; sz = 0; sl = 1; }
    sx /= sl; sz /= sl;
    /* u = s x f */
    float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    float V[16] = {
        sx, sy, sz, -(sx * ex + sy * ey + sz * ez),
        ux, uy, uz, -(ux * ex + uy * ey + uz * ez),
        -fx, -fy, -fz, (fx * ex + fy * ey + fz * ez),
        0, 0, 0, 1
    };
    float t = 1.0f / tanf(fovy * 0.5f * 3.14159265f / 180.0f), asp = VW / VH;
    float n = c->znear, f = c->zfar;
    float P[16] = {
        t / asp, 0, 0, 0,
        0, t, 0, 0,
        0, 0, (f + n) / (n - f), 2 * f * n / (n - f),
        0, 0, -1, 0
    };
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += P[i * 4 + k] * V[k * 4 + j];
            c->vp[i * 4 + j] = s;
        }
    float gl = sqrtf(fx * fx + fz * fz);
    if (gl < 1e-5f) { c->fx = 0; c->fz = 1; } else { c->fx = fx / gl; c->fz = fz / gl; }
    c->rx = sx; c->rz = sz;
}

int r3d_project(float x, float y, float z, float *sx, float *sy)
{
    const float *m = r_cam.vp;
    float cx = m[0] * x + m[1] * y + m[2] * z + m[3];
    float cy = m[4] * x + m[5] * y + m[6] * z + m[7];
    float cw = m[12] * x + m[13] * y + m[14] * z + m[15];
    if (cw < 0.1f) return 0;
    *sx = (cx / cw * 0.5f + 0.5f) * VW;
    *sy = (0.5f - cy / cw * 0.5f) * VH;
    return 1;
}

/* camera-facing soft disc: centre c fading to transparent at the rim, built in view space */
void r3d_glow(float x, float y, float z, float r, uint32_t c)
{
    /* billboard axes from the camera */
    float fx = r_cam.tx - r_cam.ex, fy = r_cam.ty - r_cam.ey, fz = r_cam.tz - r_cam.ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = -fz, sz = fx, sl = sqrtf(sx * sx + sz * sz); sx /= sl; sz /= sl;
    float ux = -sz * fy, uy = sz * fx - sx * fz, uz = sx * fy;
    /* pull towards the camera a little so it isn't swallowed by the geometry it sits on */
    x -= fx * r * 0.6f; y -= fy * r * 0.6f; z -= fz * r * 0.6f;
    const int N = 10;
    uint32_t co = c & 0xFFFFFF00u;
    float px = x + sx * r, py = y, pz = z + sz * r;
    for (int i = 1; i <= N; i++) {
        float a = (float)i / (float)N * 6.2831853f, ca = cosf(a), sa = sinf(a);
        float qx = x + (sx * ca + ux * sa) * r, qy = y + uy * sa * r, qz = z + (sz * ca + uz * sa) * r;
        r3d_tri3(x, y, z, c, px, py, pz, co, qx, qy, qz, co);
        px = qx; py = qy; pz = qz;
    }
}

void r3d_blob(float x, float y, float z, float rx, float rz, float ang, uint32_t cin, uint32_t cout)
{
    const int N = 14;
    float ca = cosf(ang), sa = sinf(ang);
    float px = 0, pz = 0;
    for (int i = 0; i <= N; i++) {
        float a = (float)i / (float)N * 6.2831853f;
        float lx = cosf(a) * rx, lz = sinf(a) * rz;
        float qx = x + lx * ca - lz * sa, qz = z + lx * sa + lz * ca;
        if (i) r3d_tri3(x, y, z, cin, px, y, pz, cout, qx, y, qz, cout);
        px = qx; pz = qz;
    }
}

/* ------------------------------------------------------------------ */
/* static chunk grid                                                   */
/* ------------------------------------------------------------------ */
#define SM_MAXG 40                     /* up to 40 x 40 chunks (1.9 km square) */
#define SM_POOL_V 960000               /* build buffer: vertices of every chunk (320k triangles) */
#define SM_CHUNK_V 120000              /* most vertices one chunk + mode can hold */

typedef struct { int n, cap, off; } SmBin;
static RV    *sm_pool;                 /* the triangles as they arrive (unsorted) */
static uint16_t *sm_tag;               /* chunk + mode of each triangle */
static int    sm_ntri;
static float  sm_x0, sm_z0;
static int    sm_gw, sm_gh;
static int    sm_mesh[SM_MAXG * SM_MAXG][RM_COUNT];
static int    sm_ntris_of[SM_MAX_MESH];
static float  sm_ylo[SM_MAXG * SM_MAXG], sm_yhi[SM_MAXG * SM_MAXG];

void sm_reset(float minx, float minz, float maxx, float maxz)
{
    if (!sm_pool) {
        sm_pool = (RV *)big_alloc(sizeof(RV) * SM_POOL_V);
        sm_tag = (uint16_t *)big_alloc(sizeof(uint16_t) * (SM_POOL_V / 3 + 1));
    }
    plat_mesh_reset();
    sm_ntri = 0;
    sm_x0 = minx; sm_z0 = minz;
    sm_gw = (int)((maxx - minx) / SM_CELL) + 1;
    sm_gh = (int)((maxz - minz) / SM_CELL) + 1;
    if (sm_gw > SM_MAXG) sm_gw = SM_MAXG;
    if (sm_gh > SM_MAXG) sm_gh = SM_MAXG;
    for (int i = 0; i < SM_MAXG * SM_MAXG; i++) {
        for (int m = 0; m < RM_COUNT; m++) sm_mesh[i][m] = -1;
        sm_ylo[i] = 1e9f; sm_yhi[i] = -1e9f;
    }
}

void sm_tri(int mode, const RV *a, const RV *b, const RV *c)
{
    if ((sm_ntri + 1) * 3 > SM_POOL_V) return;
    float cx = (a->x + b->x + c->x) * (1.0f / 3.0f), cz = (a->z + b->z + c->z) * (1.0f / 3.0f);
    int gx = (int)((cx - sm_x0) / SM_CELL), gz = (int)((cz - sm_z0) / SM_CELL);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx >= sm_gw) gx = sm_gw - 1;
    if (gz >= sm_gh) gz = sm_gh - 1;
    int cell = gz * SM_MAXG + gx;
    RV *p = &sm_pool[sm_ntri * 3];
    p[0] = *a; p[1] = *b; p[2] = *c;
    sm_tag[sm_ntri] = (uint16_t)(cell * RM_COUNT + mode);
    sm_ntri++;
    for (int k = 0; k < 3; k++) {
        if (p[k].y < sm_ylo[cell]) sm_ylo[cell] = p[k].y;
        if (p[k].y > sm_yhi[cell]) sm_yhi[cell] = p[k].y;
    }
}

void sm_quad(int mode, const RV *a, const RV *b, const RV *c, const RV *d)
{
    sm_tri(mode, a, b, c);
    sm_tri(mode, a, c, d);
}

int sm_tri_count(void) { return sm_ntri; }

void sm_finish(void)
{
    /* counting sort of triangle indices by tag, then one mesh per (chunk, mode) gathered into a scratch buffer */
    int ntag = SM_MAXG * SM_MAXG * RM_COUNT;
    static int *cnt, *order, *fill;
    static RV *tmp;
    if (!cnt) {
        cnt = (int *)big_alloc(sizeof(int) * (unsigned long)(ntag + 1));
        fill = (int *)big_alloc(sizeof(int) * (unsigned long)ntag);
        order = (int *)big_alloc(sizeof(int) * (unsigned long)(SM_POOL_V / 3 + 1));
        tmp = (RV *)big_alloc(sizeof(RV) * SM_CHUNK_V);
    }
    memset(cnt, 0, sizeof(int) * (size_t)(ntag + 1));
    for (int i = 0; i < sm_ntri; i++) cnt[sm_tag[i] + 1]++;
    for (int i = 0; i < ntag; i++) cnt[i + 1] += cnt[i];
    memcpy(fill, cnt, sizeof(int) * (size_t)ntag);
    for (int i = 0; i < sm_ntri; i++) order[fill[sm_tag[i]]++] = i;
    for (int t = 0; t < ntag; t++) {
        int n = cnt[t + 1] - cnt[t];
        if (n <= 0) continue;
        if (n * 3 > SM_CHUNK_V) n = SM_CHUNK_V / 3;
        for (int k = 0; k < n; k++) {
            int i = order[cnt[t] + k];
            tmp[k * 3] = sm_pool[i * 3]; tmp[k * 3 + 1] = sm_pool[i * 3 + 1]; tmp[k * 3 + 2] = sm_pool[i * 3 + 2];
        }
        int cell = t / RM_COUNT, mode = t % RM_COUNT;
        int id = plat_mesh_create(tmp, n * 3);
        sm_mesh[cell][mode] = id;
        if (id >= 0 && id < SM_MAX_MESH) sm_ntris_of[id] = n;
    }
}

/* does the chunk (cell) intersect the view? project its box corners and test against the screen */
static int cell_visible(int gx, int gz, float margin)
{
    int cell = gz * SM_MAXG + gx;
    float x0 = sm_x0 + (float)gx * SM_CELL - margin, x1 = x0 + SM_CELL + 2 * margin;
    float z0 = sm_z0 + (float)gz * SM_CELL - margin, z1 = z0 + SM_CELL + 2 * margin;
    float y0 = sm_ylo[cell], y1 = sm_yhi[cell];
    if (y0 > y1) return 0;
    const float *m = r_cam.vp;
    int out_l = 0, out_r = 0, out_t = 0, out_b = 0, behind = 0;
    for (int k = 0; k < 8; k++) {
        float x = (k & 1) ? x1 : x0, y = (k & 2) ? y1 : y0, z = (k & 4) ? z1 : z0;
        float cx = m[0] * x + m[1] * y + m[2] * z + m[3];
        float cy = m[4] * x + m[5] * y + m[6] * z + m[7];
        float cw = m[12] * x + m[13] * y + m[14] * z + m[15];
        if (cw < 0.1f) { behind++; continue; }
        if (cx < -cw) out_l++;
        if (cx > cw) out_r++;
        if (cy < -cw) out_b++;
        if (cy > cw) out_t++;
    }
    if (behind == 8) return 0;
    int vis = 8 - behind;
    if (behind == 0 && (out_l == vis || out_r == vis || out_t == vis || out_b == vis)) return 0;
    if (behind > 0 && (out_l == vis || out_r == vis) && vis > 0) return 0;
    return 1;
}

static uint8_t sm_seen[SM_MAXG * SM_MAXG];

void sm_cull(float margin)
{
    sm_vis = 0;
    for (int gz = 0; gz < sm_gh; gz++)
        for (int gx = 0; gx < sm_gw; gx++) {
            int cell = gz * SM_MAXG + gx, any = 0;
            for (int m = 0; m < RM_COUNT; m++) any |= sm_mesh[cell][m] >= 0;
            sm_seen[cell] = (uint8_t)(any && cell_visible(gx, gz, margin));
            if (sm_seen[cell])
                for (int m = 0; m < RM_COUNT; m++)
                    if (sm_mesh[cell][m] >= 0 && sm_mesh[cell][m] < SM_MAX_MESH) sm_vis += sm_ntris_of[sm_mesh[cell][m]];
        }
}

void sm_draw_mode(int mode, float margin)
{
    (void)margin;
    for (int gz = 0; gz < sm_gh; gz++)
        for (int gx = 0; gx < sm_gw; gx++) {
            int cell = gz * SM_MAXG + gx;
            if (sm_mesh[cell][mode] < 0 || !sm_seen[cell]) continue;
            r3d_mesh(sm_mesh[cell][mode], mode);
        }
}

void sm_draw(float margin)
{
    sm_cull(margin);
    for (int m = 0; m < RM_COUNT; m++) sm_draw_mode(m, margin);
}

int sm_visible_tris(void) { return sm_vis; }
