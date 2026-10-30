/*
 * RACING LEGENDS - software rasterizer for the native test harness: draws the same command list the Wii's
 * GPU gets (near-plane clipping, depth buffer, the blend modes, the tilt-shift pass) into an 854 x 480
 * image, so screenshots and preview videos can be made without a Wii.
 */
#include "../source/r3d.h"
#include "../source/font_bebas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS 2
#define W (854 * SS)
#define H (480 * SS)

float  swr_fb[W * H * 3];
static float zb[W * H];

typedef struct { RV *v; int n; } SMesh;
static SMesh meshes[8192];
static int nmesh;

void *big_alloc(unsigned long bytes) { void *p = calloc(1, bytes); if (!p) { fprintf(stderr, "out of memory\n"); exit(1); } return p; }

int plat_mesh_create(const RV *v, int n)
{
    if (nmesh >= 8192) return -1;
    meshes[nmesh].v = (RV *)malloc(sizeof(RV) * (size_t)n);
    memcpy(meshes[nmesh].v, v, sizeof(RV) * (size_t)n);
    meshes[nmesh].n = n;
    return nmesh++;
}

void plat_mesh_reset(void)
{
    for (int i = 0; i < nmesh; i++) free(meshes[i].v);
    nmesh = 0;
}

int swr_mesh_tris(int id) { return (id >= 0 && id < nmesh) ? meshes[id].n / 3 : 0; }

typedef struct { float x, y, z, w; float r, g, b, a; float wx, wz; } CV;   /* clip space (+ world x, z) */

static int cur_mode, cur_2d;
#define DT 256
static float detail[DT * DT];
static int detail_ready;
static void detail_init(void)
{
    static uint8_t raw[DT * DT];
    r3d_make_detail(raw);
    for (int i = 0; i < DT * DT; i++) detail[i] = (float)raw[i] / 128.0f;
    detail_ready = 1;
}
static float detail_at(float u, float v)
{
    u *= R_DETAIL_TEXELS_PER_M; v *= R_DETAIL_TEXELS_PER_M;
    int x = (int)floorf(u), y = (int)floorf(v);
    float fx = u - x, fy = v - y;
    x &= DT - 1; y &= DT - 1;
    int x1 = (x + 1) & (DT - 1), y1 = (y + 1) & (DT - 1);
    float a = detail[y * DT + x], b = detail[y * DT + x1], c = detail[y1 * DT + x], d = detail[y1 * DT + x1];
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
long swr_tris_drawn;

static void blend_px(int i, float z, float r, float g, float b, float a)
{
    float *d = &swr_fb[i * 3];
    if (!cur_2d) {
        switch (cur_mode) {
        case RM_OPAQUE: case RM_GROUND: if (z > zb[i]) return; zb[i] = z; break;
        case RM_SHADOW: if (z >= zb[i]) return; zb[i] = z; break;
        default: if (z > zb[i]) return; break;
        }
    }
    switch (cur_mode) {
    case RM_OPAQUE: case RM_GROUND: d[0] = r; d[1] = g; d[2] = b; break;
    case RM_ADD: d[0] += r * a; d[1] += g * a; d[2] += b * a; break;
    case RM_MUL2: d[0] *= 2 * r; d[1] *= 2 * g; d[2] *= 2 * b; break;
    default: d[0] += (r - d[0]) * a; d[1] += (g - d[1]) * a; d[2] += (b - d[2]) * a; break;
    }
    if (d[0] > 1) d[0] = 1;
    if (d[1] > 1) d[1] = 1;
    if (d[2] > 1) d[2] = 1;
}

static int text_mode;
static float font_alpha(float u, float v)
{
    float fx = u * FONT_TEX_W - 0.5f, fy = v * FONT_TEX_H - 0.5f;
    int x = (int)floorf(fx), y = (int)floorf(fy);
    float ax = fx - x, ay = fy - y;
    #define T(xx, yy) ((xx) < 0 || (yy) < 0 || (xx) >= FONT_TEX_W || (yy) >= FONT_TEX_H ? 0.0f : FONT_TEX[(yy) * FONT_TEX_W + (xx)] / 255.0f)
    float a = T(x, y), b = T(x + 1, y), c = T(x, y + 1), d = T(x + 1, y + 1);
    #undef T
    return a + (b - a) * ax + (c - a) * ay + (a - b - c + d) * ax * ay;
}

/* screen-space triangle: x, y in pixels, z depth (0..1) */
typedef struct { float x, y, z, r, g, b, a; float iw, u, v; } SV;

static void raster(const SV *a, const SV *b, const SV *c)
{
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0) { const SV *t = b; b = c; c = t; area = -area; }
    int x0 = (int)floorf(fminf(a->x, fminf(b->x, c->x))), x1 = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int y0 = (int)floorf(fminf(a->y, fminf(b->y, c->y))), y1 = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    float inv = 1.0f / area;
    swr_tris_drawn++;
    for (int y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float w0 = (c->x - b->x) * (py - b->y) - (c->y - b->y) * (px - b->x);
            float w1 = (a->x - c->x) * (py - c->y) - (a->y - c->y) * (px - c->x);
            float w2 = (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            w0 *= inv; w1 *= inv; w2 *= inv;
            float z = a->z * w0 + b->z * w1 + c->z * w2;
            float rr = a->r * w0 + b->r * w1 + c->r * w2, gg = a->g * w0 + b->g * w1 + c->g * w2, bb = a->b * w0 + b->b * w1 + c->b * w2;
            if (text_mode) {
                float u = a->u * w0 + b->u * w1 + c->u * w2, v = a->v * w0 + b->v * w1 + c->v * w2;
                blend_px(y * W + x, 0, rr, gg, bb, (a->a * w0 + b->a * w1 + c->a * w2) * font_alpha(u, v));
                continue;
            }
            if (cur_mode == RM_GROUND && !cur_2d) {
                float iw = a->iw * w0 + b->iw * w1 + c->iw * w2;
                float u = (a->u * w0 + b->u * w1 + c->u * w2) / iw, v = (a->v * w0 + b->v * w1 + c->v * w2) / iw;
                float k = detail_at(u, v);
                rr *= k; gg *= k; bb *= k;
            }
            blend_px(y * W + x, z, rr, gg, bb, a->a * w0 + b->a * w1 + c->a * w2);
        }
    }
}

static void to_screen(const CV *c, SV *s)
{
    float iw = 1.0f / c->w;
    s->x = (c->x * iw * 0.5f + 0.5f) * W;
    s->y = (0.5f - c->y * iw * 0.5f) * H;
    s->z = c->z * iw * 0.5f + 0.5f;
    s->r = c->r; s->g = c->g; s->b = c->b; s->a = c->a;
    s->iw = iw; s->u = c->wx * iw; s->v = c->wz * iw;
}

static CV lerp_cv(const CV *a, const CV *b, float t)
{
    CV r;
    r.x = a->x + (b->x - a->x) * t; r.y = a->y + (b->y - a->y) * t; r.z = a->z + (b->z - a->z) * t; r.w = a->w + (b->w - a->w) * t;
    r.r = a->r + (b->r - a->r) * t; r.g = a->g + (b->g - a->g) * t; r.b = a->b + (b->b - a->b) * t; r.a = a->a + (b->a - a->a) * t;
    r.wx = a->wx + (b->wx - a->wx) * t; r.wz = a->wz + (b->wz - a->wz) * t;
    return r;
}

static void tri3d(const RV *v)
{
    const float *m = r_cam.vp;
    CV c[3];
    for (int k = 0; k < 3; k++) {
        float x = v[k].x, y = v[k].y, z = v[k].z;
        c[k].x = m[0] * x + m[1] * y + m[2] * z + m[3];
        c[k].y = m[4] * x + m[5] * y + m[6] * z + m[7];
        c[k].z = m[8] * x + m[9] * y + m[10] * z + m[11];
        c[k].w = m[12] * x + m[13] * y + m[14] * z + m[15];
        c[k].wx = x; c[k].wz = z;
        uint32_t col = v[k].c;
        c[k].r = (float)(col >> 24) / 255.0f; c[k].g = (float)((col >> 16) & 255) / 255.0f;
        c[k].b = (float)((col >> 8) & 255) / 255.0f; c[k].a = (float)(col & 255) / 255.0f;
    }
    /* trivial rejects */
    int ol = 0, orr = 0, ot = 0, ob = 0;
    for (int k = 0; k < 3; k++) {
        if (c[k].x < -c[k].w) ol++;
        if (c[k].x > c[k].w) orr++;
        if (c[k].y < -c[k].w) ob++;
        if (c[k].y > c[k].w) ot++;
    }
    if (ol == 3 || orr == 3 || ot == 3 || ob == 3) return;
    /* clip against the near plane z > -w */
    CV in[3], out[4];
    for (int k = 0; k < 3; k++) in[k] = c[k];
    int no = 0;
    for (int k = 0; k < 3; k++) {
        const CV *p = &in[k], *q = &in[(k + 1) % 3];
        float dp = p->z + p->w, dq = q->z + q->w;
        if (dp >= 0) out[no++] = *p;
        if ((dp >= 0) != (dq >= 0)) out[no++] = lerp_cv(p, q, dp / (dp - dq));
    }
    if (no < 3) return;
    SV s[4];
    for (int k = 0; k < no; k++) to_screen(&out[k], &s[k]);
    raster(&s[0], &s[1], &s[2]);
    if (no == 4) raster(&s[0], &s[2], &s[3]);
}

static void tri_text(const TV *v)
{
    SV s[3];
    for (int k = 0; k < 3; k++) {
        s[k].x = v[k].x * SS; s[k].y = v[k].y * SS; s[k].z = 0; s[k].iw = 1; s[k].u = v[k].u; s[k].v = v[k].v;
        uint32_t col = v[k].c;
        s[k].r = (float)(col >> 24) / 255.0f; s[k].g = (float)((col >> 16) & 255) / 255.0f;
        s[k].b = (float)((col >> 8) & 255) / 255.0f; s[k].a = (float)(col & 255) / 255.0f;
    }
    text_mode = 1;
    raster(&s[0], &s[1], &s[2]);
    text_mode = 0;
}

static void tri2d(const RV *v)
{
    SV s[3];
    for (int k = 0; k < 3; k++) {
        s[k].x = v[k].x * SS; s[k].y = v[k].y * SS; s[k].z = 0; s[k].iw = 1; s[k].u = s[k].v = 0;
        uint32_t col = v[k].c;
        s[k].r = (float)(col >> 24) / 255.0f; s[k].g = (float)((col >> 16) & 255) / 255.0f;
        s[k].b = (float)((col >> 8) & 255) / 255.0f; s[k].a = (float)(col & 255) / 255.0f;
    }
    raster(&s[0], &s[1], &s[2]);
}

/* tilt-shift: blur the top and bottom of the frame (a separable box blur, blended by row) */
static void bloom(void)
{
    if (getenv("NOPOST")) return;
    /* bright parts, blurred wide at quarter resolution, added back (an EFB copy + TEV threshold on the Wii) */
    const int Q = 4, w = W / Q, h = H / Q;
    static float lo[(854 * SS / 4) * (480 * SS / 4) * 3], t2[(854 * SS / 4) * (480 * SS / 4) * 3];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int ch = 0; ch < 3; ch++) {
                float acc = 0;
                for (int j = 0; j < Q; j++) for (int i = 0; i < Q; i++) acc += swr_fb[((y * Q + j) * W + x * Q + i) * 3 + ch];
                acc /= (float)(Q * Q);
                float l = acc - 0.82f;
                lo[(y * w + x) * 3 + ch] = l > 0 ? l * 1.8f : 0;
            }
    for (int pass = 0; pass < 2; pass++) {
        const int R = 6;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int ch = 0; ch < 3; ch++) {
                    float a = 0; int n = 0;
                    for (int k = -R; k <= R; k++) {
                        int xx = pass ? x : x + k, yy = pass ? y + k : y;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        a += lo[(yy * w + xx) * 3 + ch]; n++;
                    }
                    t2[(y * w + x) * 3 + ch] = a / (float)n;
                }
        memcpy(lo, t2, sizeof(float) * (size_t)(w * h * 3));
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float fx = (float)x / Q - 0.5f, fy = (float)y / Q - 0.5f;
            int ix = (int)fx, iy = (int)fy;
            if (ix < 0) ix = 0; if (iy < 0) iy = 0; if (ix > w - 2) ix = w - 2; if (iy > h - 2) iy = h - 2;
            float ux = fx - ix, uy = fy - iy;
            ux = ux < 0 ? 0 : (ux > 1 ? 1 : ux); uy = uy < 0 ? 0 : (uy > 1 ? 1 : uy);
            float vx = ((float)x / W - 0.5f) * 2, vy = ((float)y / H - 0.5f) * 2;
            float vig = 1.0f - 0.22f * (vx * vx * 0.7f + vy * vy) ;
            for (int ch = 0; ch < 3; ch++) {
                float a = lo[(iy * w + ix) * 3 + ch], b = lo[(iy * w + ix + 1) * 3 + ch], c = lo[((iy + 1) * w + ix) * 3 + ch], d = lo[((iy + 1) * w + ix + 1) * 3 + ch];
                float bl = a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
                float *p = &swr_fb[(y * W + x) * 3 + ch];
                float v = *p * vig + bl * 0.8f;
                *p = v > 1 ? 1 : v;
            }
            float *px = &swr_fb[(y * W + x) * 3];
            float l = px[0] * 0.3f + px[1] * 0.55f + px[2] * 0.15f;
            for (int ch = 0; ch < 3; ch++) {
                float v = l + (px[ch] - l) * 1.1f;                       /* saturation */
                v = v + 0.12f * v * (1.0f - v) * (v - 0.35f) * 4.0f;     /* soft contrast */
                if (ch == 0) v += 0.02f * l; if (ch == 2) v += 0.025f * (1.0f - l) - 0.01f;   /* warm highlights, cool shadows */
                px[ch] = v < 0 ? 0 : (v > 1 ? 1 : v);
            }
        }
}

/* separable box blur with running sums (radius r), src -> dst */
static void box(const float *src, float *dst, float *tmp, int r)
{
    for (int y = 0; y < H; y++) {
        const float *row = &src[y * W * 3];
        float *o = &tmp[y * W * 3];
        for (int ch = 0; ch < 3; ch++) {
            float acc = 0; int n = 0;
            for (int x = -r; x <= r; x++) if (x >= 0 && x < W) { acc += row[x * 3 + ch]; n++; }
            for (int x = 0; x < W; x++) {
                o[x * 3 + ch] = acc / (float)n;
                int xo = x - r, xi = x + r + 1;
                if (xo >= 0) { acc -= row[xo * 3 + ch]; n--; }
                if (xi < W) { acc += row[xi * 3 + ch]; n++; }
            }
        }
    }
    for (int x = 0; x < W; x++)
        for (int ch = 0; ch < 3; ch++) {
            float acc = 0; int n = 0;
            for (int y = -r; y <= r; y++) if (y >= 0 && y < H) { acc += tmp[(y * W + x) * 3 + ch]; n++; }
            for (int y = 0; y < H; y++) {
                dst[(y * W + x) * 3 + ch] = acc / (float)n;
                int yo = y - r, yi = y + r + 1;
                if (yo >= 0) { acc -= tmp[(yo * W + x) * 3 + ch]; n--; }
                if (yi < H) { acc += tmp[(yi * W + x) * 3 + ch]; n++; }
            }
        }
}

/* the miniature look: a sharp band where the car is, blur growing towards the top and bottom (two blur levels,
 * as two EFB copies at decreasing resolution would give on the Wii) */
static void post(void)
{
    if (getenv("NOPOST")) return;
    if (r_post_top <= 0 && r_post_bot <= 0) return;
    static float b1[W * H * 3], b2[W * H * 3], tmp[W * H * 3];
    box(swr_fb, b1, tmp, 3 * SS);
    box(b1, b2, tmp, 6 * SS);
    for (int y = 0; y < H; y++) {
        float t = (float)y / (float)H, k = 0;
        if (t < 0.5f) k = r_post_top * (0.5f - t) / 0.5f;
        if (t > 0.84f) k = r_post_bot * (t - 0.84f) / 0.16f;
        k = k > 1 ? 1 : k;
        k = k * k * (3 - 2 * k);
        if (k <= 0) continue;
        for (int x = 0; x < W * 3; x++) {
            int i = y * W * 3 + x;
            float a = swr_fb[i], m;
            if (k < 0.5f) m = a + (b1[i] - a) * (k * 2.0f);
            else m = b1[i] + (b2[i] - b1[i]) * ((k - 0.5f) * 2.0f);
            swr_fb[i] = m;
        }
    }
}


/* the grungy film look: soft focus with halation, RGB split towards the edges, film grain */
static unsigned grain_seed = 12345;
static void grunge(void)
{
    if (getenv("NOPOST")) return;
    static float soft[W * H * 3], tmp[W * H * 3], wide[W * H * 3], src[W * H * 3];
    memcpy(src, swr_fb, sizeof src);
    box(src, soft, tmp, 2 * SS);                 /* soft focus */
    box(soft, wide, tmp, 10 * SS);               /* halation */
    float cx = W * 0.5f, cy = H * 0.5f;
    grain_seed = grain_seed * 1103515245u + 12345u;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            float dx = ((float)x - cx) / cx, dy = ((float)y - cy) / cy;
            float r2 = dx * dx + dy * dy;
            /* RGB split: red pushed out, blue pulled in, more at the edges */
            float k = (1.2f + 4.5f * r2) * SS * r_fx_split;
            int xr = (int)((float)x + dx * k), yr = (int)((float)y + dy * k * 0.5f);
            int xb = (int)((float)x - dx * k), yb = (int)((float)y - dy * k * 0.5f);
            xr = xr < 0 ? 0 : (xr >= W ? W - 1 : xr); yr = yr < 0 ? 0 : (yr >= H ? H - 1 : yr);
            xb = xb < 0 ? 0 : (xb >= W ? W - 1 : xb); yb = yb < 0 ? 0 : (yb >= H ? H - 1 : yb);
            float c[3];
            int ir = (yr * W + xr) * 3, ib = (yb * W + xb) * 3;
            c[0] = src[ir] * 0.45f + soft[ir] * 0.55f;
            c[1] = src[i + 1] * 0.45f + soft[i + 1] * 0.55f;
            c[2] = src[ib + 2] * 0.45f + soft[ib + 2] * 0.55f;
            /* halation: the bright neon bleeds a warm glow */
            for (int ch = 0; ch < 3; ch++) {
                float h = wide[i + ch];
                float hl = h > 0.55f ? (h - 0.55f) * (ch == 0 ? 0.32f : (ch == 1 ? 0.2f : 0.26f)) : 0.0f;
                c[ch] += hl;
            }
            /* grain: stronger in the mid-tones, slightly coloured */
            unsigned hsh = (unsigned)(x / SS) * 73856093u ^ (unsigned)(y / SS) * 19349663u ^ grain_seed;
            hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
            float n = (float)(hsh & 1023) / 1023.0f - 0.5f;
            float n2 = (float)((hsh >> 10) & 1023) / 1023.0f - 0.5f;
            float l = c[0] * 0.3f + c[1] * 0.55f + c[2] * 0.15f;
            float amt = 0.16f * (0.35f + l * (1.0f - l) * 2.6f);
            c[0] += (n + n2 * 0.25f) * amt; c[1] += n * amt; c[2] += (n - n2 * 0.25f) * amt;
            /* lifted blacks: a faded, filmic floor */
            for (int ch = 0; ch < 3; ch++) {
                float v = 0.035f + c[ch] * 0.965f;
                swr_fb[i + ch] = v < 0 ? 0 : (v > 1 ? 1 : v);
            }
        }
}

void swr_render(void)
{
    float cr = (float)(r_clear >> 24) / 255.0f, cg = (float)((r_clear >> 16) & 255) / 255.0f, cb = (float)((r_clear >> 8) & 255) / 255.0f;
    for (int i = 0; i < W * H; i++) { swr_fb[i * 3] = cr; swr_fb[i * 3 + 1] = cg; swr_fb[i * 3 + 2] = cb; zb[i] = 1.0f; }
    swr_tris_drawn = 0;
    if (!detail_ready) detail_init();
    int fx_done = 0, gr_done = 0;
    for (int ci = 0; ci < r_ncmd; ci++) {
        const RCmd *c = &r_cmd[ci];
        cur_mode = c->mode;
        if (!fx_done && (c->type == RC_POST || c->type == RC_2D)) { bloom(); fx_done = 1; }
        if (c->type == RC_POST) { post(); continue; }
        if ((c->type == RC_2D || c->type == RC_TEXT) && !gr_done) { if (!fx_done) { bloom(); fx_done = 1; } grunge(); gr_done = 1; }
        if (c->type == RC_TEXT) {
            cur_2d = 1; cur_mode = RM_ALPHA;
            for (int i = c->start; i + 2 < c->start + c->count; i += 3) tri_text(&r_tv[i]);
            continue;
        }
        if (c->type == RC_MESH) {
            cur_2d = 0;
            if (c->mesh >= nmesh) continue;
            const SMesh *m = &meshes[c->mesh];
            for (int i = 0; i + 2 < m->n; i += 3) tri3d(&m->v[i]);
            continue;
        }
        cur_2d = c->type == RC_2D;
        for (int i = c->start; i + 2 < c->start + c->count; i += 3) {
            if (cur_2d) tri2d(&r_v[i]); else tri3d(&r_v[i]);
        }
    }
    if (!fx_done) bloom();
    if (!gr_done) grunge();
}

void swr_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const int ow = W / SS, oh = H / SS;
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    static unsigned char row[854 * 3];
    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow; x++)
            for (int ch = 0; ch < 3; ch++) {
                float v = 0;
                for (int j = 0; j < SS; j++) for (int i = 0; i < SS; i++) v += swr_fb[((y * SS + j) * W + x * SS + i) * 3 + ch];
                v /= (float)(SS * SS);
                row[x * 3 + ch] = (unsigned char)(v <= 0 ? 0 : (v >= 1 ? 255 : (int)(v * 255.0f + 0.5f)));
            }
        fwrite(row, 1, (size_t)ow * 3, f);
    }
    fclose(f);
}
