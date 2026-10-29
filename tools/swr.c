/*
 * RACING LEGENDS - software rasterizer for the native test harness: draws the same command list the Wii's
 * GPU gets (near-plane clipping, depth buffer, the blend modes, the tilt-shift pass) into an 854 x 480
 * image, so screenshots and preview videos can be made without a Wii.
 */
#include "../source/r3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 854
#define H 480

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

typedef struct { float x, y, z, w; float r, g, b, a; } CV;   /* clip space */

static int cur_mode, cur_2d;
long swr_tris_drawn;

static void blend_px(int i, float z, float r, float g, float b, float a)
{
    float *d = &swr_fb[i * 3];
    if (!cur_2d) {
        switch (cur_mode) {
        case RM_OPAQUE: if (z > zb[i]) return; zb[i] = z; break;
        case RM_SHADOW: if (z >= zb[i]) return; zb[i] = z; break;
        default: if (z > zb[i]) return; break;
        }
    }
    switch (cur_mode) {
    case RM_OPAQUE: d[0] = r; d[1] = g; d[2] = b; break;
    case RM_ADD: d[0] += r * a; d[1] += g * a; d[2] += b * a; break;
    case RM_MUL2: d[0] *= 2 * r; d[1] *= 2 * g; d[2] *= 2 * b; break;
    default: d[0] += (r - d[0]) * a; d[1] += (g - d[1]) * a; d[2] += (b - d[2]) * a; break;
    }
    if (d[0] > 1) d[0] = 1;
    if (d[1] > 1) d[1] = 1;
    if (d[2] > 1) d[2] = 1;
}

/* screen-space triangle: x, y in pixels, z depth (0..1) */
typedef struct { float x, y, z, r, g, b, a; } SV;

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
            blend_px(y * W + x, z,
                     a->r * w0 + b->r * w1 + c->r * w2, a->g * w0 + b->g * w1 + c->g * w2,
                     a->b * w0 + b->b * w1 + c->b * w2, a->a * w0 + b->a * w1 + c->a * w2);
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
}

static CV lerp_cv(const CV *a, const CV *b, float t)
{
    CV r;
    r.x = a->x + (b->x - a->x) * t; r.y = a->y + (b->y - a->y) * t; r.z = a->z + (b->z - a->z) * t; r.w = a->w + (b->w - a->w) * t;
    r.r = a->r + (b->r - a->r) * t; r.g = a->g + (b->g - a->g) * t; r.b = a->b + (b->b - a->b) * t; r.a = a->a + (b->a - a->a) * t;
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

static void tri2d(const RV *v)
{
    SV s[3];
    for (int k = 0; k < 3; k++) {
        s[k].x = v[k].x; s[k].y = v[k].y; s[k].z = 0;
        uint32_t col = v[k].c;
        s[k].r = (float)(col >> 24) / 255.0f; s[k].g = (float)((col >> 16) & 255) / 255.0f;
        s[k].b = (float)((col >> 8) & 255) / 255.0f; s[k].a = (float)(col & 255) / 255.0f;
    }
    raster(&s[0], &s[1], &s[2]);
}

/* tilt-shift: blur the top and bottom of the frame (a separable box blur, blended by row) */
static void post(void)
{
    if (r_post_top <= 0 && r_post_bot <= 0) return;
    static float tmp[W * H * 3], blr[W * H * 3];
    const int R = 3;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int ch = 0; ch < 3; ch++) {
                float s = 0; int n = 0;
                for (int k = -R; k <= R; k++) { int xx = x + k; if (xx < 0 || xx >= W) continue; s += swr_fb[(y * W + xx) * 3 + ch]; n++; }
                tmp[(y * W + x) * 3 + ch] = s / (float)n;
            }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int ch = 0; ch < 3; ch++) {
                float s = 0; int n = 0;
                for (int k = -R; k <= R; k++) { int yy = y + k; if (yy < 0 || yy >= H) continue; s += tmp[(yy * W + x) * 3 + ch]; n++; }
                blr[(y * W + x) * 3 + ch] = s / (float)n;
            }
    for (int y = 0; y < H; y++) {
        float t = (float)y / (float)H;
        float k = 0;
        if (t < 0.32f) k = r_post_top * (1.0f - t / 0.32f);
        if (t > 0.78f) k = r_post_bot * ((t - 0.78f) / 0.22f);
        k = k > 1 ? 1 : k;
        if (k <= 0) continue;
        for (int x = 0; x < W * 3; x++) swr_fb[y * W * 3 + x] += (blr[y * W * 3 + x] - swr_fb[y * W * 3 + x]) * k;
    }
}

void swr_render(void)
{
    float cr = (float)(r_clear >> 24) / 255.0f, cg = (float)((r_clear >> 16) & 255) / 255.0f, cb = (float)((r_clear >> 8) & 255) / 255.0f;
    for (int i = 0; i < W * H; i++) { swr_fb[i * 3] = cr; swr_fb[i * 3 + 1] = cg; swr_fb[i * 3 + 2] = cb; zb[i] = 1.0f; }
    swr_tris_drawn = 0;
    for (int ci = 0; ci < r_ncmd; ci++) {
        const RCmd *c = &r_cmd[ci];
        cur_mode = c->mode;
        if (c->type == RC_POST) { post(); continue; }
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
}

void swr_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    static unsigned char row[W * 3];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W * 3; x++) {
            float v = swr_fb[y * W * 3 + x];
            row[x] = (unsigned char)(v <= 0 ? 0 : (v >= 1 ? 255 : (int)(v * 255.0f + 0.5f)));
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}
