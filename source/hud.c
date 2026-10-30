/*
 * RACING LEGENDS - the race HUD: position and standings, lap and times, the speedometer and the boost gauge,
 * the minimap, name tags, the countdown, drift popups and the pause menu.
 */
#include "game_int.h"

#include <stdio.h>
#include <string.h>

#define PANEL     0x0C1020B8u
#define PANEL_HI  0x1C2440D0u
#define WHITE     0xFFFFFFFFu
#define DIM       0xB8C2D8FFu
#define YELLOW    0xFFD23AFFu
#define CYAN      0x3AD8FFFFu

static const char *ORD[9] = { "", "ST", "ND", "RD", "TH", "TH", "TH", "TH", "TH" };

float hud_speed_units(float mps) { return sv.units ? mps * MPS_TO_MPH : mps * MPS_TO_MPH * 1.609f; }

const char *hud_time(float t, char *buf)
{
    if (t <= 0) { strcpy(buf, "-:--.--"); return buf; }
    int m = (int)(t / 60.0f);
    float s = t - (float)m * 60.0f;
    int si = (int)s, cs = (int)((s - (float)si) * 100.0f);
    sprintf(buf, "%d:%02d.%02d", m, si, cs);
    return buf;
}

void hud_popup(const char *text, uint32_t col, float size)
{
    int k = 0;
    float oldest = 1e9f;
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (g.pop[i].t <= 0) { k = i; oldest = -1; break; }
        if (g.pop[i].t < oldest) { oldest = g.pop[i].t; k = i; }
    }
    Popup *p = &g.pop[k];
    snprintf(p->text, sizeof p->text, "%s", text);
    p->col = col; p->size = size; p->t = 1.6f; p->y = 0;
    /* push the others up */
    for (int i = 0; i < MAX_POPUPS; i++) if (i != k && g.pop[i].t > 0) g.pop[i].y -= size * 1.4f;
}

void hud_update(float dt)
{
    for (int i = 0; i < MAX_POPUPS; i++) if (g.pop[i].t > 0) { g.pop[i].t -= dt; g.pop[i].y -= dt * 10.0f; }
}

void ui_panel(float x, float y, float w, float h)
{
    ui_slant(x, y, w, h, 8, PANEL);
    ui_slant_h(x + 1, y, w - 2, 2, 8, 0xFFFFFF30u, 0xFFFFFF08u);
}

/* ------------------------------------------------------------------ */
static void draw_position(const Car *pl)
{
    char buf[32];
    ui_panel(16, 14, 150, 76);
    ui_text_weight(1.5f);
    sprintf(buf, "%d", pl->place);
    ui_text_s(buf, 32, 24, 50, WHITE);
    ui_text_weight(1.0f);
    float w = ui_text_w(buf, 50);
    ui_text_s(ORD[pl->place], 36 + w, 26, 16, YELLOW);
    sprintf(buf, "/%d", ncars);
    ui_text_s(buf, 38 + w, 56, 18, DIM);
    ui_text("POS", 118, 22, 11, DIM);

    /* standings */
    int shown = 0;
    float y = 98;
    for (int i = 0; i < ncars && shown < 5; i++) {
        const Car *c = &cars[g.order[i]];
        if (i >= 4 && !c->is_player) continue;
        if (i >= 4 && shown == 4) { ui_text("...", 26, y - 6, 8, DIM); }
        int me = c->is_player;
        ui_slant(16, y, 190, 22, 5, me ? 0xFFD23AE0u : PANEL);
        ui_slant(16, y, 26, 22, 5, me ? 0x1A1A20E0u : 0x2A3150E0u);
        sprintf(buf, "%d", i + 1);
        ui_text_c(buf, 31, y + 5, 12, me ? YELLOW : WHITE);
        ui_rect(48, y + 4, 4, 14, c->paint);
        ui_text(c->name, 58, y + 6, 10, me ? 0x101018FFu : WHITE);
        if (c->finished) ui_text_r("FIN", 196, y + 6, 9, me ? 0x101018FFu : 0x5AF27AFFu);
        y += 25;
        shown++;
    }
}

static void draw_laps(const Car *pl)
{
    char buf[32], t[16];
    ui_panel(VW - 196, 14, 180, 92);
    ui_text("LAP", VW - 182, 22, 11, DIM);
    int lap = pl->lap < 1 ? 1 : (pl->lap > g.laps ? g.laps : pl->lap);
    ui_text_weight(1.4f);
    sprintf(buf, "%d", lap);
    ui_text_s(buf, VW - 182, 38, 34, WHITE);
    ui_text_weight(1.0f);
    sprintf(buf, "/%d", g.laps);
    ui_text_s(buf, VW - 182 + ui_text_w("0", 34) + 6, 52, 18, DIM);
    float cur = pl->finished ? pl->last_lap : (g.phase == RP_RACE ? g.race_t - pl->lap_start : 0);
    ui_text("TIME", VW - 104, 24, 9, DIM);
    ui_text_r(hud_time(cur, t), VW - 26, 36, 13, WHITE);
    ui_text("BEST", VW - 104, 58, 9, DIM);
    ui_text_r(hud_time(pl->best_lap, t), VW - 26, 70, 13, pl->best_lap > 0 ? YELLOW : DIM);
    ui_text("TOTAL", VW - 182, 86, 8, DIM);
    ui_text_r(hud_time(g.race_t, t), VW - 26, 86, 9, DIM);
}

static void draw_speedo(const Car *pl)
{
    char buf[16];
    float cx = 92, cy = VH - 78, r = 58;
    ui_circle(cx, cy, r + 8, 0x0A0E1ACCu);
    ui_ring(cx, cy, r + 8, 2, 0xFFFFFF22u);
    float a0 = PI * 0.75f, a1 = PI * 2.25f;
    float mph = pl->speed * MPS_TO_MPH;
    float f = sat(mph / 240.0f);                         /* the dial runs 0 - 240 mph */
    ui_arc(cx, cy, r - 7, r, a0, a1, 32, 0x2A3048FFu, 0x2A3048FFu);
    /* ticks every 30 mph, and the 88 mark */
    for (int i = 0; i <= 8; i++) {
        float a = a0 + (a1 - a0) * (float)i / 8.0f, ca = cosf(a), sa = sinf(a);
        ui_line(cx + ca * (r - 14), cy + sa * (r - 14), cx + ca * (r - 9), cy + sa * (r - 9), i % 2 ? 1.5f : 2.5f, 0xFFFFFF80u);
    }
    {
        float a = a0 + (a1 - a0) * (88.0f / 240.0f), ca = cosf(a), sa = sinf(a);
        ui_line(cx + ca * (r - 16), cy + sa * (r - 16), cx + ca * (r + 3), cy + sa * (r + 3), 3.0f, mph >= 88.0f ? 0xBFF4FFFFu : 0x3AD8FFFFu);
    }
    if (f > 0.005f) {
        uint32_t ce = pl->boost_t > 0 ? 0xBFF4FFFFu : col_mix(CYAN, 0x3A7AFFFFu, sat((f - 0.55f) * 2.2f));
        ui_arc(cx, cy, r - 7, r, a0, a0 + (a1 - a0) * f, 28, CYAN, ce);
        ui_begin(RM_ADD);
        float ae = a0 + (a1 - a0) * f;
        ui_glow(cx + cosf(ae) * (r - 3.5f), cy + sinf(ae) * (r - 3.5f), 12, col_a(ce, 0.7f));
        ui_begin(RM_ALPHA);
    }
    sprintf(buf, "%d", (int)(hud_speed_units(pl->speed) + 0.5f));
    ui_text_weight(1.4f);
    ui_text_c(buf, cx, cy - 16, 26, WHITE);
    ui_text_weight(1.0f);
    ui_text_c(sv.units ? "MPH" : "KM/H", cx, cy + 16, 9, DIM);

    /* the boost gauge: fills while it recharges, glows when it's ready */
    float nx = 168, ny = VH - 48, bw = 170, bh = 18;
    ui_slant(nx, ny, bw, bh, 7, 0x0A0E1ACCu);
    if (pl->boost_t > 0) {
        float k = sat(pl->boost_t / BOOST_TIME);
        ui_slant_h(nx + 3, ny + 3, (bw - 6) * k, bh - 6, 5, 0xFFFFFFFFu, 0x6AD8FFFFu);
        ui_text_weight(1.4f);
        ui_text_s("BOOST!", nx + 4, ny - 22, 14, 0xBFF4FFFFu);
        ui_text_weight(1.0f);
    } else if (pl->nitro >= 1.0f) {
        float p = 0.75f + 0.25f * sinf(g.t * 8.0f);
        ui_slant_h(nx + 3, ny + 3, bw - 6, bh - 6, 5, col_a(0x3AB8FFFFu, p), col_a(0xBFF4FFFFu, p));
        ui_begin(RM_ADD);
        ui_glow(nx + bw * 0.5f, ny + bh * 0.5f, 70, col_a(0x3AB8FF40u, p));
        ui_begin(RM_ALPHA);
        ui_text_weight(1.4f);
        ui_text_s("BOOST READY", nx + 4, ny - 22, 14, CYAN);
        ui_text_weight(1.0f);
        ui_text_s("R", nx + bw - 6, ny - 22, 14, WHITE);
    } else {
        ui_slant_h(nx + 3, ny + 3, (bw - 6) * pl->nitro, bh - 6, 5, 0x1A4A8AFFu, 0x2A7AC8FFu);
        ui_text_s("RECHARGING", nx + 4, ny - 20, 11, DIM);
    }
}

static float map_pts[2][400];
static int map_n, map_track = -1;

void hud_minimap(float x, float y, float w, float h, int dots)
{
    if (map_track != trk.id) {
        map_n = 0;
        for (int k = 0; k < trk.n && map_n < 400; k += 4) {
            track_map_xform(trk.s[k].x, trk.s[k].z, 0, 0, 1, 1, &map_pts[0][map_n], &map_pts[1][map_n]);
            map_n++;
        }
        map_track = trk.id;
    }
    for (int pass = 0; pass < 2; pass++) {
        float th = pass ? 3.0f : 7.0f;
        uint32_t c = pass ? 0xE8ECF4FFu : 0x00000080u;
        for (int i = 0; i < map_n; i++) {
            int j = (i + 1) % map_n;
            float ax = x + map_pts[0][i] * w, ay = y + map_pts[1][i] * h, bx = x + map_pts[0][j] * w, by = y + map_pts[1][j] * h;
            ui_line(ax, ay, bx, by, th, c);
            if (!pass) ui_circle(ax, ay, th * 0.5f, c);
        }
    }
    /* start line */
    {
        float sx = x + map_pts[0][0] * w, sy = y + map_pts[1][0] * h;
        ui_rect(sx - 3, sy - 3, 6, 6, 0xFFD23AFFu);
    }
    if (!dots) return;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ncars; i++) {
            const Car *c = &cars[i];
            if ((pass == 1) != (c->is_player != 0)) continue;
            float mx, my;
            track_map_xform(c->x, c->z, 0, 0, 1, 1, &mx, &my);
            mx = x + mx * w; my = y + my * h;
            if (c->is_player) {
                ui_circle(mx, my, 6, WHITE);
                ui_circle(mx, my, 4.2f, c->paint);
            } else {
                ui_circle(mx, my, 4, 0x000000C0u);
                ui_circle(mx, my, 3, c->paint);
            }
        }
}

static void draw_minimap(void)
{
    float w = 168, h = 128, x = VW - w - 18, y = VH - h - 16;
    hud_minimap(x + 6, y + 4, w - 12, h - 8, 1);
}

static void draw_tags(void)
{
    for (int i = 0; i < ncars; i++) {
        const Car *c = &cars[i];
        if (c->is_player) continue;
        float dx = c->x - cars[0].x, dz = c->z - cars[0].z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d > 42.0f) continue;
        float sx, sy;
        if (!r3d_project(c->x, c->y + 2.8f, c->z, &sx, &sy)) continue;
        if (sx < 0 || sx > VW || sy < 0 || sy > VH) continue;
        float a = sat((42.0f - d) / 10.0f);
        char buf[24];
        sprintf(buf, "%d", c->place);
        float tw = ui_text_w(c->name, 8) + 22;
        ui_rrect(sx - tw * 0.5f, sy - 16, tw, 14, 3, col_a(0x0A0E1AD0u, a));
        ui_rrect(sx - tw * 0.5f, sy - 16, 14, 14, 3, col_a(c->paint, a));
        ui_text_c(buf, sx - tw * 0.5f + 7, sy - 13, 8, col_a(WHITE, a));
        ui_text(c->name, sx - tw * 0.5f + 18, sy - 13, 8, col_a(WHITE, a));
    }
}

static void draw_countdown(void)
{
    if (g.phase == RP_INTRO) {
        float a = smooth01(g.phase_t * 2.0f) * (1.0f - smooth01((g.phase_t - 2.7f) * 3.0f));
        float slide = (1.0f - smooth01(g.phase_t * 2.0f)) * -200.0f;
        ui_slant_h(slide + 60, 150, 520, 92, 14, col_a(0x0C1020E8u, a), col_a(0x0C102000u, a));
        ui_rect(slide + 60, 150, 6, 92, col_a(YELLOW, a));
        ui_text_weight(1.5f);
        ui_text_s(trk.info->name, slide + 84, 164, 34, col_a(WHITE, a));
        ui_text_weight(1.0f);
        char buf[48];
        sprintf(buf, "%s  -  %d LAPS", trk.info->place, g.laps);
        ui_text_s(buf, slide + 86, 212, 12, col_a(YELLOW, a));
        ui_text_c("PRESS A TO SKIP", VW * 0.5f, VH - 40, 10, col_a(DIM, a * (0.6f + 0.4f * sinf(g.t * 5))));
        return;
    }
    if (g.phase == RP_COUNT) {
        int n = 3 - (int)floorf(g.phase_t / 0.85f);
        if (n < 1) n = 1;
        float f = g.phase_t / 0.85f - floorf(g.phase_t / 0.85f);
        float sc = 1.0f + (1.0f - smooth01(f * 3.0f)) * 0.6f;
        char buf[16];
        sprintf(buf, "%d", n);
        ui_text_weight(1.8f);
        ui_text_sc(buf, VW * 0.5f, 150 - 40 * (sc - 1.0f), 90 * sc, col_a(WHITE, 1.0f - f * 0.3f));
        ui_text_weight(1.0f);
        /* start lights */
        float lx = VW * 0.5f - 90;
        ui_rrect(lx - 14, 60, 208, 52, 12, 0x101218E8u);
        for (int i = 0; i < 4; i++) {
            int on = (3 - n) >= i;
            uint32_t c = on ? 0xFF3A2AFFu : 0x3A2020FFu;
            ui_circle(lx + 10 + (float)i * 50, 86, 17, c);
            if (on) { ui_begin(RM_ADD); ui_glow(lx + 10 + (float)i * 50, 86, 34, 0xFF3A2A70u); ui_begin(RM_ALPHA); }
        }
    }
    if (g.phase == RP_RACE && g.phase_t < 0.9f) {
        float lx = VW * 0.5f - 90;
        float a = 1.0f - smooth01((g.phase_t - 0.5f) * 3.0f);
        ui_rrect(lx - 14, 60, 208, 52, 12, col_a(0x101218E8u, a));
        for (int i = 0; i < 4; i++) {
            ui_circle(lx + 10 + (float)i * 50, 86, 17, col_a(0x3AFF6AFFu, a));
            ui_begin(RM_ADD); ui_glow(lx + 10 + (float)i * 50, 86, 36, col_a(0x3AFF6A70u, a)); ui_begin(RM_ALPHA);
        }
    }
}

static void draw_popups(void)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        const Popup *p = &g.pop[i];
        if (p->t <= 0) continue;
        float age = 1.6f - p->t;
        float a = sat(p->t * 3.0f);
        float sc = 1.0f + (1.0f - smooth01(age * 6.0f)) * 0.5f;
        float y = 150 + p->y;
        ui_text_weight(p->size > 30 ? 1.7f : 1.3f);
        ui_text_sc(p->text, VW * 0.5f, y - p->size * (sc - 1.0f) * 0.5f, p->size * sc, col_a(p->col, a));
        ui_text_weight(1.0f);
    }
}

static void draw_finish(const Car *pl)
{
    float a = smooth01(g.phase_t * 2.0f);
    char buf[32];
    sprintf(buf, "%d%s PLACE", pl->place, ORD[pl->place]);
    ui_slant_h(-20, 168, VW + 40, 110, 0, col_a(0x0C1020E0u, a), col_a(0x0C1020E0u, a));
    ui_rect(0, 168, VW, 3, col_a(pl->place <= 3 ? YELLOW : DIM, a));
    ui_rect(0, 275, VW, 3, col_a(pl->place <= 3 ? YELLOW : DIM, a));
    ui_text_weight(1.8f);
    float sc = 1.0f + (1.0f - smooth01(g.phase_t * 3.0f)) * 0.8f;
    uint32_t c = pl->place == 1 ? YELLOW : (pl->place <= 3 ? WHITE : DIM);
    ui_text_sc(buf, VW * 0.5f, 186 - 20 * (sc - 1), 52 * sc, col_a(c, a));
    ui_text_weight(1.0f);
    const char *sub = pl->place == 1 ? "LEGENDARY DRIVING!" : (pl->place <= 3 ? "PODIUM FINISH!" : "UPGRADE YOUR CAR AND TRY AGAIN");
    ui_text_sc(sub, VW * 0.5f, 250, 13, col_a(DIM, a));
}

/* streaks of light rushing out from the middle of the screen */
static void speed_lines(float k, uint32_t col)
{
    if (k <= 0.01f) return;
    ui_begin(RM_ADD);
    float cx = VW * 0.5f, cy = VH * 0.56f;
    for (int i = 0; i < 44; i++) {
        uint32_t h = hash32((uint32_t)i * 2654435761u);
        float a = (float)(h & 1023) / 1024.0f * TAU;
        float speed = 1.4f + (float)((h >> 10) & 255) / 255.0f * 1.6f;
        float ph = fmodf(g.t * speed + (float)((h >> 18) & 255) / 255.0f, 1.0f);
        float r0 = 220.0f + ph * 420.0f, len = 60.0f + 140.0f * ph;
        float ca = cosf(a), sa = sinf(a) * 0.72f;
        float w = 1.2f + 2.2f * ph;
        float x0 = cx + ca * r0, y0 = cy + sa * r0, x1 = cx + ca * (r0 + len), y1 = cy + sa * (r0 + len);
        float nx = -sa * w, ny = ca * w;
        uint32_t c0 = col_a(col, 0.0f), c1 = col_a(col, k * (0.25f + 0.55f * ph));
        ui_quad(x0, y0, c0, x1 + nx, y1 + ny, c1, x1 - nx, y1 - ny, c1, x0, y0, c0);
    }
    ui_begin(RM_ALPHA);
}

/* 88 MPH: the screen edges flash blue (the rings and light streaks are in the world, race.c) */
static void draw_88(void)
{
    float t = g.mph88_t;
    if (t <= 0) return;
    ui_begin(RM_ADD);
    /* the screen edges flash blue as it hits */
    float f = (1.0f - smooth01(t * 3.0f)) * 0.8f;
    ui_rect_h(0, 0, 160, VH, col_a(0x3A8AFFFFu, f), 0x3A8AFF00u);
    ui_rect_h(VW - 160, 0, 160, VH, 0x3A8AFF00u, col_a(0x3A8AFFFFu, f));
    ui_rect_v(0, 0, VW, 90, col_a(0x6AC8FFFFu, f * 0.7f), 0x6AC8FF00u);
    ui_begin(RM_ALPHA);
}

static void draw_pause(void)
{
    ui_rect(0, 0, VW, VH, 0x05070CA8u);
    ui_text_weight(1.6f);
    ui_text_sc("PAUSED", VW * 0.5f, 120, 40, WHITE);
    ui_text_weight(1.0f);
    const char *items[3] = { "RESUME", "RESTART RACE", "QUIT TO MENU" };
    for (int i = 0; i < 3; i++) {
        int s = g.pause_sel == i;
        float y = 200 + (float)i * 46;
        ui_slant(VW * 0.5f - 150, y, 300, 36, 8, s ? 0xFFD23AE8u : 0x1A2238E0u);
        ui_text_c(items[i], VW * 0.5f + 4, y + 11, 15, s ? 0x101018FFu : WHITE);
    }
    ui_text_c("X  CHANGE CAMERA     -  RESET CAR     Y  HORN", VW * 0.5f, VH - 46, 10, DIM);
}

void hud_draw(void)
{
    const Car *pl = &cars[0];
    ui_begin(RM_ALPHA);
    if (g.phase == RP_INTRO) { draw_countdown(); return; }
    draw_tags();
    draw_position(pl);
    draw_laps(pl);
    draw_speedo(pl);
    draw_minimap();
    draw_countdown();
    draw_popups();
    if (g.wrong_t > 1.5f && ((int)(g.t * 3) & 1)) ui_text_sc("WRONG WAY!", VW * 0.5f, 210, 34, 0xFF4A3AFFu);
    if (g.phase == RP_DONE) draw_finish(pl);
    /* boosts and 88 mph: speed lines and the 88 overlay */
    {
        float k = 0;
        if (pl->boost_t > 0) k = 0.55f + 0.45f * sat(g.boost_anim);
        if (g.mph88_t > 0) k = fmaxf(k, 1.0f - smooth01(g.mph88_t / 1.3f));
        k = fmaxf(k, sat((pl->speed * MPS_TO_MPH - 115.0f) / 70.0f) * 0.4f);     /* always a hint of speed when flat out */
        speed_lines(k, pl->livery == LIVERY_SIGNATURE ? 0x6AB8FFFFu : 0xBFEFFFFFu);
        draw_88();
    }
    /* boost screen flash */
    if (g.nitro_flash > 0) {
        ui_begin(RM_ADD);
        float a = g.nitro_flash;
        ui_rect_h(0, 0, 120, VH, col_a(0x3A8AFF60u, a), 0x3A8AFF00u);
        ui_rect_h(VW - 120, 0, 120, VH, 0x3A8AFF00u, col_a(0x3A8AFF60u, a));
        ui_begin(RM_ALPHA);
    }
    if (g.paused) draw_pause();
}
