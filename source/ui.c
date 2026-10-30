/*
 * RACING LEGENDS - 2D primitives and the stroke font. See ui.h.
 */
#include "ui.h"
#include "font_bebas.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#define TAU 6.2831853f

void ui_tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2)
{
    r3d_tri3(x0, y0, 0, c0, x1, y1, 0, c1, x2, y2, 0, c2);
}

void ui_quad(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
             float x2, float y2, uint32_t c2, float x3, float y3, uint32_t c3)
{
    ui_tri(x0, y0, c0, x1, y1, c1, x2, y2, c2);
    ui_tri(x0, y0, c0, x2, y2, c2, x3, y3, c3);
}

void ui_rect(float x, float y, float w, float h, uint32_t c) { ui_quad(x, y, c, x + w, y, c, x + w, y + h, c, x, y + h, c); }
void ui_rect_v(float x, float y, float w, float h, uint32_t t, uint32_t b) { ui_quad(x, y, t, x + w, y, t, x + w, y + h, b, x, y + h, b); }
void ui_rect_h(float x, float y, float w, float h, uint32_t l, uint32_t r) { ui_quad(x, y, l, x + w, y, r, x + w, y + h, r, x, y + h, l); }

void ui_slant(float x, float y, float w, float h, float sk, uint32_t c)
{
    ui_quad(x + sk, y, c, x + w + sk, y, c, x + w, y + h, c, x, y + h, c);
}

void ui_slant_h(float x, float y, float w, float h, float sk, uint32_t l, uint32_t r)
{
    ui_quad(x + sk, y, l, x + w + sk, y, r, x + w, y + h, r, x, y + h, l);
}

static void corner(float cx, float cy, float r, float a0, uint32_t c)
{
    const int N = 5;
    float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
    for (int i = 1; i <= N; i++) {
        float a = a0 + (float)i / (float)N * (TAU * 0.25f);
        float qx = cx + cosf(a) * r, qy = cy + sinf(a) * r;
        ui_tri(cx, cy, c, px, py, c, qx, qy, c);
        px = qx; py = qy;
    }
}

void ui_rrect_v(float x, float y, float w, float h, float r, uint32_t top, uint32_t bot)
{
    if (r * 2 > h) r = h * 0.5f;
    if (r * 2 > w) r = w * 0.5f;
    uint32_t ct = col_mix(top, bot, r / h), cb = col_mix(top, bot, 1.0f - r / h);
    ui_quad(x + r, y, top, x + w - r, y, top, x + w - r, y + h, bot, x + r, y + h, bot);
    ui_quad(x, y + r, ct, x + r, y + r, ct, x + r, y + h - r, cb, x, y + h - r, cb);
    ui_quad(x + w - r, y + r, ct, x + w, y + r, ct, x + w, y + h - r, cb, x + w - r, y + h - r, cb);
    corner(x + r, y + r, r, TAU * 0.5f, ct);
    corner(x + w - r, y + r, r, TAU * 0.75f, ct);
    corner(x + w - r, y + h - r, r, 0.0f, cb);
    corner(x + r, y + h - r, r, TAU * 0.25f, cb);
}

void ui_rrect(float x, float y, float w, float h, float r, uint32_t c) { ui_rrect_v(x, y, w, h, r, c, c); }

void ui_circle(float cx, float cy, float r, uint32_t c)
{
    int n = r > 20 ? 28 : 16;
    float px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        float a = (float)i / (float)n * TAU, qx = cx + cosf(a) * r, qy = cy + sinf(a) * r;
        ui_tri(cx, cy, c, px, py, c, qx, qy, c);
        px = qx; py = qy;
    }
}

void ui_glow(float cx, float cy, float r, uint32_t c)
{
    int n = 20;
    uint32_t co = c & 0xFFFFFF00u;
    float px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        float a = (float)i / (float)n * TAU, qx = cx + cosf(a) * r, qy = cy + sinf(a) * r;
        ui_tri(cx, cy, c, px, py, co, qx, qy, co);
        px = qx; py = qy;
    }
}

void ui_arc(float cx, float cy, float r0, float r1, float a0, float a1, int n, uint32_t c0, uint32_t c1)
{
    if (n < 1) n = 1;
    float pa = a0;
    for (int i = 1; i <= n; i++) {
        float t0 = (float)(i - 1) / (float)n, t1 = (float)i / (float)n;
        float a = a0 + (a1 - a0) * t1;
        uint32_t ca = col_mix(c0, c1, t0), cb = col_mix(c0, c1, t1);
        float ca0 = cosf(pa), sa0 = sinf(pa), ca1 = cosf(a), sa1 = sinf(a);
        ui_quad(cx + ca0 * r0, cy + sa0 * r0, ca, cx + ca0 * r1, cy + sa0 * r1, ca,
                cx + ca1 * r1, cy + sa1 * r1, cb, cx + ca1 * r0, cy + sa1 * r0, cb);
        pa = a;
    }
}

void ui_ring(float cx, float cy, float r, float th, uint32_t c) { ui_arc(cx, cy, r - th * 0.5f, r + th * 0.5f, 0, TAU, 32, c, c); }

void ui_line(float x0, float y0, float x1, float y1, float w, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    ui_quad(x0 + nx, y0 + ny, c, x1 + nx, y1 + ny, c, x1 - nx, y1 - ny, c, x0 - nx, y0 - ny, c);
}

/* ------------------------------------------------------------------ */
/* the stroke font: polylines on a 4 x 6 grid (y down), ';' between strokes */
/* ------------------------------------------------------------------ */
static const char *GLYPH[128] = {
    ['A'] = "0 6 0 1.4 1.4 0 2.6 0 4 1.4 4 6;0 3.5 4 3.5",
    ['B'] = "0 6 0 0 3 0 4 1 4 2 3 3 0 3;3 3 4 4 4 5 3 6 0 6",
    ['C'] = "4 1 3 0 1 0 0 1 0 5 1 6 3 6 4 5",
    ['D'] = "0 0 0 6 2.6 6 4 4.6 4 1.4 2.6 0 0 0",
    ['E'] = "4 0 0 0 0 6 4 6;0 3 3 3",
    ['F'] = "4 0 0 0 0 6;0 3 3 3",
    ['G'] = "4 1 3 0 1 0 0 1 0 5 1 6 3 6 4 5 4 3.4 2.2 3.4",
    ['H'] = "0 0 0 6;4 0 4 6;0 3 4 3",
    ['I'] = "2 0 2 6;0.9 0 3.1 0;0.9 6 3.1 6",
    ['J'] = "4 0 4 5 3 6 1 6 0 5",
    ['K'] = "0 0 0 6;0 3.3 1.3 3.3 4 0;1.3 3.3 4 6",
    ['L'] = "0 0 0 6 4 6",
    ['M'] = "0 6 0 0 2 2.6 4 0 4 6",
    ['N'] = "0 6 0 0 4 6 4 0",
    ['O'] = "1 0 3 0 4 1 4 5 3 6 1 6 0 5 0 1 1 0",
    ['P'] = "0 6 0 0 3 0 4 1 4 2.4 3 3.4 0 3.4",
    ['Q'] = "1 0 3 0 4 1 4 5 3 6 1 6 0 5 0 1 1 0;2.6 4.6 4.2 6.2",
    ['R'] = "0 6 0 0 3 0 4 1 4 2.3 3 3.3 0 3.3;2.2 3.3 4 6",
    ['S'] = "4 1 3 0 1 0 0 1 0 2 1 3 3 3 4 4 4 5 3 6 1 6 0 5",
    ['T'] = "0 0 4 0;2 0 2 6",
    ['U'] = "0 0 0 5 1 6 3 6 4 5 4 0",
    ['V'] = "0 0 2 6 4 0",
    ['W'] = "0 0 0.9 6 2 2.4 3.1 6 4 0",
    ['X'] = "0 0 4 6;4 0 0 6",
    ['Y'] = "0 0 2 3 4 0;2 3 2 6",
    ['Z'] = "0 0 4 0 0 6 4 6",
    ['0'] = "1 0 3 0 4 1 4 5 3 6 1 6 0 5 0 1 1 0",
    ['1'] = "0.8 1.3 2.3 0 2.3 6",
    ['2'] = "0 1 1 0 3 0 4 1 4 2.3 0 6 4 6",
    ['3'] = "0 1 1 0 3 0 4 1 4 2 3 3 4 4 4 5 3 6 1 6 0 5;1.4 3 3 3",
    ['4'] = "3 6 3 0 0 4 4 4",
    ['5'] = "4 0 0 0 0 2.7 3 2.7 4 3.7 4 5 3 6 1 6 0 5",
    ['6'] = "4 1 3 0 1 0 0 1 0 5 1 6 3 6 4 5 4 4 3 3 0 3",
    ['7'] = "0 0 4 0 1.4 6",
    ['8'] = "1 0 3 0 4 1 4 2 3 3 1 3 0 2 0 1 1 0;1 3 3 3 4 4 4 5 3 6 1 6 0 5 0 4 1 3",
    ['9'] = "0 5 1 6 3 6 4 5 4 1 3 0 1 0 0 1 0 2 1 3 4 3",
    ['.'] = "2 5.7 2 5.95",
    [','] = "2.2 5.4 1.6 6.8",
    [':'] = "2 1.7 2 1.95;2 5.7 2 5.95",
    ['-'] = "0.8 3.2 3.2 3.2",
    ['+'] = "0.6 3 3.4 3;2 1.6 2 4.4",
    ['/'] = "0.4 6 3.6 0",
    ['!'] = "2 0 2 4;2 5.7 2 5.95",
    ['?'] = "0 1 1 0 3 0 4 1 4 2 2 3.4 2 4.2;2 5.7 2 5.95",
    ['\''] = "2.1 0 1.8 1.6",
    ['"'] = "1.3 0 1.3 1.5;2.7 0 2.7 1.5",
    ['%'] = "0 6 4 0;0.5 0.6 0.9 0.6;3.1 5.4 3.5 5.4",
    ['('] = "2.8 0 1.6 1.4 1.6 4.6 2.8 6",
    [')'] = "1.2 0 2.4 1.4 2.4 4.6 1.2 6",
    ['<'] = "3.2 0.6 0.6 3 3.2 5.4",
    ['>'] = "0.8 0.6 3.4 3 0.8 5.4",
    ['*'] = "1 2 3 4;3 2 1 4",
    ['='] = "0.6 2.2 3.4 2.2;0.6 3.8 3.4 3.8",
    ['_'] = "0 6 4 6",
    ['#'] = "1.3 0.5 1 5.5;3 0.5 2.7 5.5;0.2 2 3.8 2;0.2 4 3.8 4",
    ['$'] = "3.6 1.2 2.8 0.7 1 0.7 0.3 1.4 0.3 2.2 1 3 3 3 3.7 3.8 3.7 4.6 3 5.3 1 5.3 0.3 4.8;2 0 2 6",
};

#define MAXPT 2048
static float g_pts[MAXPT * 2];
static int   g_off[128], g_cnt[128];          /* point offset / count; strokes separated by NaN-ish markers */
static int   g_ready;
static float g_weight = 1.0f;

__attribute__((unused)) static void font_init(void)
{
    int np = 0;
    for (int ch = 0; ch < 128; ch++) {
        g_off[ch] = np; g_cnt[ch] = 0;
        const char *s = GLYPH[ch];
        if (!s) continue;
        while (*s) {
            if (*s == ';') { g_pts[np * 2] = -99.0f; g_pts[np * 2 + 1] = -99.0f; np++; s++; continue; }
            if (*s == ' ') { s++; continue; }
            char *e;
            float x = strtof(s, &e);
            s = e;
            while (*s == ' ') s++;
            float y = strtof(s, &e);
            s = e;
            g_pts[np * 2] = x; g_pts[np * 2 + 1] = y; np++;
        }
        g_cnt[ch] = np - g_off[ch];
    }
    g_ready = 1;
}

void ui_text_weight(float w) { g_weight = w; }

/* Bebas Neue from the atlas: size = cap height in pixels; weight > 1.25 uses the heavy cut */
#define TRACKING 0.06f
#define TEXT_SCALE 1.22f
static int wsel(void) { return g_weight > 1.25f ? 1 : 0; }

float ui_text_w(const char *s, float size)
{
    size *= TEXT_SCALE;
    float k = size / FONT_CAP_H, w = 0;
    int ws = wsel(), n = 0;
    for (; *s; s++, n++) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 126) ch = '?';
        w += FONT_GLYPH[ws][ch - 32].adv * k + size * TRACKING;
    }
    if (n) w -= size * TRACKING;
    return w;
}

static void draw_text(const char *s, float x, float y, float size, uint32_t c)
{
    y -= size * (TEXT_SCALE - 1.0f) * 0.5f;
    size *= TEXT_SCALE;
    float k = size / FONT_CAP_H, base = y + size, pen = x;
    int ws = wsel();
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 126) ch = '?';
        const FontGlyph *g = &FONT_GLYPH[ws][ch - 32];
        if (g->x1 > g->x0) {
            float x0 = pen + (float)g->ox * k, y0 = base + (float)g->oy * k;
            float x1 = x0 + (float)(g->x1 - g->x0) * k, y1 = y0 + (float)(g->y1 - g->y0) * k;
            float u0 = (float)g->x0 / FONT_TEX_W, v0 = (float)g->y0 / FONT_TEX_H, u1 = (float)g->x1 / FONT_TEX_W, v1 = (float)g->y1 / FONT_TEX_H;
            TV a = { x0, y0, u0, v0, c }, b = { x1, y0, u1, v0, c }, d = { x1, y1, u1, v1, c }, e = { x0, y1, u0, v1, c };
            r3d_text_tri(&a, &b, &d);
            r3d_text_tri(&a, &d, &e);
        }
        pen += g->adv * k + size * TRACKING;
    }
}

void ui_text(const char *s, float x, float y, float size, uint32_t c) { draw_text(s, x, y, size, c); }
void ui_text_c(const char *s, float cx, float y, float size, uint32_t c) { draw_text(s, cx - ui_text_w(s, size) * 0.5f, y, size, c); }
void ui_text_r(const char *s, float rx, float y, float size, uint32_t c) { draw_text(s, rx - ui_text_w(s, size), y, size, c); }

static void shadow(const char *s, float x, float y, float size, uint32_t c)
{
    float o = size * 0.06f + 1.0f;
    uint32_t sc = RGBA(0, 0, 0, (int)((float)(c & 255) * 0.55f));
    draw_text(s, x + o, y + o, size, sc);
    draw_text(s, x, y, size, c);
}
void ui_text_s(const char *s, float x, float y, float size, uint32_t c) { shadow(s, x, y, size, c); }
void ui_text_sc(const char *s, float cx, float y, float size, uint32_t c) { shadow(s, cx - ui_text_w(s, size) * 0.5f, y, size, c); }
void ui_text_sr(const char *s, float rx, float y, float size, uint32_t c) { shadow(s, rx - ui_text_w(s, size), y, size, c); }
