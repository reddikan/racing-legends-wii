/*
 * RACING LEGENDS - the menus: title, track select, the garage (upgrades, cars, paint), options, how to play,
 * loading and the results screen.
 */
#include "game_int.h"

#include <stdio.h>
#include <string.h>

#define PANEL     0x0C1020D8u
#define WHITE     0xFFFFFFFFu
#define DIM       0xA8B2C8FFu
#define YELLOW    0xFFD23AFFu
#define CYAN      0x3AD8FFFFu
#define DARKTXT   0x101018FFu

static const char *UP_NAME[NUM_UPGRADES] = { "ENGINE", "THRUSTERS", "MAG GRIP", "RECHARGE", "BOOST POWER", "DRIFT KIT" };
static const char *UP_DESC[NUM_UPGRADES] = {
    "RAISES TOP SPEED", "FASTER ACCELERATION", "MORE GRIP AND SHARPER HANDLING",
    "THE BOOST RECHARGES FASTER", "STRONGER, FASTER BOOSTS", "DRIFTS CHARGE QUICKER AND TOP UP THE BOOST" };
static const int UP_LEVELS[NUM_UPGRADES] = { 5, 5, 5, 5, 5, 5 };
static const int BODY_PRICE[NUM_BODIES] = { 0, 1500, 2500, 0 };
static const char *LIVERY_NAME[5] = { "CLEAN", "RACING STRIPES", "TWO-TONE", "CONTRAST ROOF", "LIQUID SILVER" };

int upgrade_cost(int up, int level)
{
    static const int BASE[5] = { 350, 650, 1000, 1500, 2100 };
    static const float MUL[NUM_UPGRADES] = { 1.2f, 1.0f, 1.0f, 1.1f, 0.9f, 0.8f };
    if (level >= UP_LEVELS[up]) return 0;
    return (int)((float)BASE[level] * MUL[up]) / 10 * 10;
}

/* ------------------------------------------------------------------ */
/* shared bits                                                         */
/* ------------------------------------------------------------------ */
void draw_logo(float cx, float y, float sc, float t)
{
    /* speed streaks */
    ui_begin(RM_ADD);
    for (int i = 0; i < 7; i++) {
        float yy = y + 10 * sc + (float)i * 9.0f * sc;
        float off = fmodf(t * 260.0f + (float)i * 97.0f, 700.0f) - 350.0f;
        ui_rect_h(cx - 330 * sc + off, yy, 120 * sc, 2, 0xFFFFFF00u, 0xFFFFFF40u);
    }
    ui_begin(RM_ALPHA);
    ui_text_weight(1.45f);
    ui_text_sc("RACING", cx - 8 * sc, y, 38 * sc, WHITE);
    ui_text_weight(1.55f);
    float pulse = 0.85f + 0.15f * sinf(t * 2.0f);
    ui_text_sc("LEGENDS", cx + 8 * sc, y + 58 * sc, 52 * sc, col_mix(0xFF9A2AFFu, YELLOW, pulse));
    ui_text_weight(1.0f);
    /* chequered strip */
    float w = 330 * sc, x0 = cx - w * 0.5f, yy = y + 128 * sc;
    for (int i = 0; i < 30; i++)
        for (int r = 0; r < 2; r++)
            ui_rect(x0 + (float)i * w / 30.0f, yy + (float)r * 5 * sc, w / 30.0f, 5 * sc, ((i + r) & 1) ? 0x101018E0u : 0xF4F4F4E0u);
}

void draw_background_panel(void)
{
    ui_rect_v(0, 0, VW, VH, 0x05070C60u, 0x05070CC0u);
}

static void credits_badge(float rx, float y)
{
    char buf[32];
    sprintf(buf, "%d CR", sv.credits);
    float w = ui_text_w(buf, 15) + 40;
    ui_slant(rx - w, y, w, 32, 8, 0x0C1020E0u);
    ui_circle(rx - w + 20, y + 16, 8, YELLOW);
    ui_text_c("$", rx - w + 20, y + 11, 10, DARKTXT);
    ui_text_r(buf, rx - 10, y + 9, 15, YELLOW);
}

static void hint(const char *s) { ui_text_c(s, VW * 0.5f, VH - 26, 10, DIM); }

static int stars(float x, float y, int n, int of, float s)
{
    for (int i = 0; i < of; i++) {
        float cx = x + (float)i * s * 1.3f, cy = y;
        uint32_t c = i < n ? YELLOW : 0x3A4058FFu;
        for (int k = 0; k < 5; k++) {
            float a0 = -PI * 0.5f + (float)k * TAU / 5.0f, a1 = a0 + TAU / 10.0f, a2 = a0 - TAU / 10.0f;
            ui_tri(cx, cy, c, cx + cosf(a0) * s * 0.55f, cy + sinf(a0) * s * 0.55f, c, cx + cosf(a1) * s * 0.22f, cy + sinf(a1) * s * 0.22f, c);
            ui_tri(cx, cy, c, cx + cosf(a2) * s * 0.22f, cy + sinf(a2) * s * 0.22f, c, cx + cosf(a0) * s * 0.55f, cy + sinf(a0) * s * 0.55f, c);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* track preview paths (without loading the track)                     */
/* ------------------------------------------------------------------ */
static float prev_xy[NUM_TRACKS][2][160];
static int prev_n[NUM_TRACKS], prev_ready;

void menu_enter(int state)
{
    g.state = state;
    g.t = 0;
    g.cursor = 0; g.sub = 0; g.confirm = 0;
    g.menu_anim = 0;
    if (state == ST_TRACKS) { g.cursor = g.track_sel; }
    if (state == ST_GARAGE) { g.tab = 0; }
    if (state != ST_RACE && state != ST_LOADING && state != ST_RESULTS) {
        if (!g.attract) race_setup(trk.id, 3, 1);
    }
}

/* ------------------------------------------------------------------ */
/* update                                                              */
/* ------------------------------------------------------------------ */
static int nav(const Input *in, int *v, int n, uint32_t dec, uint32_t inc)
{
    int moved = 0;
    if (in->pressed & dec) { *v = (*v + n - 1) % n; moved = 1; }
    if (in->pressed & inc) { *v = (*v + 1) % n; moved = 1; }
    if (moved) snd(SND_UI_MOVE, 0.5f, 1.0f, 0);
    return moved;
}

/* the secret code: Up Up Down Down Left Right Left Right B A */
static const uint32_t CODE[10] = { BTN_UP, BTN_UP, BTN_DOWN, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_B, BTN_A };

static int code_step(const Input *in)
{
    const uint32_t keys = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_A | BTN_B | BTN_X | BTN_Y;
    uint32_t p = in->pressed & keys;
    if (!p) return 0;
    if (p == CODE[g.code_pos]) {
        g.code_pos++;
        if (g.code_pos == 10) { g.code_pos = 0; return 1; }
    } else {
        g.code_pos = p == CODE[0] ? 1 : 0;
    }
    return 0;
}

static void title_update(const Input *in)
{
    if (g.unlock_t > 0) {
        /* the unlock card is up: A or B closes it */
        if (g.unlock_t > 0.8f && (in->pressed & (BTN_A | BTN_B))) { g.unlock_t = 0; snd(SND_UI_OK, 0.7f, 1.0f, 0); }
        return;
    }
    if (code_step(in)) {
        if (!sv.secret) { sv.secret = 1; sv.owned_body |= (uint8_t)(1 << BODY_FUTURE88); save_mark_dirty(); }
        g.unlock_t = 0.001f;
        snd(SND_UNLOCK, 1.0f, 1.0f, 0);
        return;
    }
    nav(in, &g.cursor, 4, BTN_UP, BTN_DOWN);
    if (in->pressed & BTN_A) {
        snd(SND_UI_OK, 0.7f, 1.0f, 0);
        switch (g.cursor) {
        case 0: menu_enter(ST_TRACKS); break;
        case 1: menu_enter(ST_GARAGE); break;
        case 2: menu_enter(ST_OPTIONS); break;
        default: menu_enter(ST_HELP); break;
        }
    }
}

static void tracks_update(const Input *in)
{
    if (nav(in, &g.cursor, NUM_TRACKS, BTN_UP, BTN_DOWN)) g.menu_anim = 0;
    g.track_sel = g.cursor;
    if (in->pressed & BTN_LEFT) { if (sv.laps > 1) { sv.laps--; snd(SND_UI_MOVE, 0.5f, 0.9f, 0); save_mark_dirty(); } }
    if (in->pressed & BTN_RIGHT) { if (sv.laps < 5) { sv.laps++; snd(SND_UI_MOVE, 0.5f, 1.1f, 0); save_mark_dirty(); } }
    if (in->pressed & BTN_A) {
        if (g.cursor < sv.unlocked) {
            snd(SND_UI_OK, 0.8f, 1.0f, 0);
            g.loading_track = g.cursor;
            g.state = ST_LOADING; g.t = 0; g.loading_frames = 0;
        } else snd(SND_UI_ERR, 0.7f, 1.0f, 0);
    }
    if (in->pressed & BTN_X) { snd(SND_UI_OK, 0.6f, 1.0f, 0); menu_enter(ST_GARAGE); }
    if (in->pressed & BTN_B) { snd(SND_UI_BACK, 0.6f, 1.0f, 0); menu_enter(ST_TITLE); }
}

static void garage_update(const Input *in)
{
    if (in->pressed & (BTN_R | BTN_ZR)) { g.tab = (g.tab + 1) % 3; g.cursor = g.tab == 1 ? sv.body : 0; snd(SND_UI_MOVE, 0.5f, 1.2f, 0); }
    if (in->pressed & (BTN_L | BTN_ZL)) { g.tab = (g.tab + 2) % 3; g.cursor = g.tab == 1 ? sv.body : 0; snd(SND_UI_MOVE, 0.5f, 1.2f, 0); }
    if (in->pressed & BTN_B) { snd(SND_UI_BACK, 0.6f, 1.0f, 0); menu_enter(ST_TITLE); return; }
    if (in->pressed & BTN_Y) { snd(SND_UI_OK, 0.6f, 1.0f, 0); menu_enter(ST_TRACKS); return; }
    if (g.tab == 0) {
        nav(in, &g.cursor, NUM_UPGRADES, BTN_UP, BTN_DOWN);
        if (in->pressed & BTN_A) {
            int u = g.cursor, l = sv.lv[u], c = upgrade_cost(u, l);
            if (l >= UP_LEVELS[u]) snd(SND_UI_ERR, 0.6f, 1.0f, 0);
            else if (sv.credits < c) { snd(SND_UI_ERR, 0.6f, 1.0f, 0); g.confirm = 30; }
            else { sv.credits -= c; sv.lv[u]++; snd(SND_UI_BUY, 0.8f, 1.0f, 0); save_mark_dirty(); g.menu_anim = 0; }
        }
    } else if (g.tab == 1) {
        nav(in, &g.cursor, sv.secret ? NUM_BODIES : NUM_BODIES - 1, BTN_UP, BTN_DOWN);
        int b = g.cursor;
        if ((sv.owned_body >> b) & 1) { if (sv.body != b) { sv.body = (uint8_t)b; save_mark_dirty(); } }
        else if (b == BODY_FUTURE88) { /* hidden until the code is entered */ }
        else if (in->pressed & BTN_A) {
            if (sv.credits >= BODY_PRICE[b]) { sv.credits -= BODY_PRICE[b]; sv.owned_body |= (uint8_t)(1 << b); sv.body = (uint8_t)b; snd(SND_UI_BUY, 0.8f, 1.0f, 0); save_mark_dirty(); }
            else { snd(SND_UI_ERR, 0.6f, 1.0f, 0); g.confirm = 30; }
        }
    } else {
        nav(in, &g.cursor, 3, BTN_UP, BTN_DOWN);
        if (sv.body == BODY_FUTURE88) return;
        int ch = 0;
        if (in->pressed & BTN_LEFT) ch = -1;
        if (in->pressed & BTN_RIGHT) ch = 1;
        if (ch) {
            snd(SND_UI_MOVE, 0.5f, 1.0f, 0);
            if (g.cursor == 0) sv.paint = (uint8_t)((sv.paint + NUM_PAINTS + ch) % NUM_PAINTS);
            else if (g.cursor == 1) sv.paint2 = (uint8_t)((sv.paint2 + NUM_PAINTS + ch) % NUM_PAINTS);
            else sv.livery = (uint8_t)((sv.livery + 4 + ch) % 4);
            save_mark_dirty();
        }
    }
    if (g.confirm > 0) g.confirm--;
}

static void options_update(const Input *in)
{
    nav(in, &g.cursor, 5, BTN_UP, BTN_DOWN);
    int ch = 0;
    if (in->pressed & BTN_LEFT) ch = -1;
    if (in->pressed & BTN_RIGHT) ch = 1;
    if (in->pressed & BTN_A) ch = 1;
    if (ch && g.cursor < 4) {
        snd(SND_UI_MOVE, 0.5f, 1.0f, 0);
        switch (g.cursor) {
        case 0: sv.cam = (uint8_t)((sv.cam + NUM_CAMS + ch) % NUM_CAMS); break;
        case 1: sv.units = !sv.units; break;
        case 2: sv.wide = !sv.wide; break;
        case 3: sv.tilt = !sv.tilt; break;
        }
        save_mark_dirty();
    }
    if (g.cursor == 4 && (in->pressed & BTN_A)) {
        if (g.confirm) {
            uint8_t cam = sv.cam, units = sv.units, wide = sv.wide, tilt = sv.tilt;
            memset(&sv, 0, sizeof sv);
            sv.magic = SAVE_MAGIC; sv.unlocked = 1; sv.laps = 3; sv.owned_body = 1; sv.paint = 0; sv.paint2 = 4; sv.livery = 1;
            sv.cam = cam; sv.units = units; sv.wide = wide; sv.tilt = tilt; sv.credits = 300;
            g.confirm = 0;
            snd(SND_UI_BUY, 0.7f, 0.8f, 0);
            save_mark_dirty();
        } else { g.confirm = 1; snd(SND_UI_ERR, 0.6f, 1.0f, 0); }
    }
    if (g.cursor != 4) g.confirm = 0;
    if (in->pressed & BTN_B) { snd(SND_UI_BACK, 0.6f, 1.0f, 0); menu_enter(ST_TITLE); }
}

static void results_update(const Input *in)
{
    if (g.t < 0.6f) return;
    if (in->pressed & BTN_A) { snd(SND_UI_OK, 0.7f, 1.0f, 0); menu_enter(ST_TRACKS); }
    else if (in->pressed & BTN_X) { snd(SND_UI_OK, 0.7f, 1.0f, 0); menu_enter(ST_GARAGE); }
    else if (in->pressed & BTN_Y) { snd(SND_UI_OK, 0.7f, 1.0f, 0); g.state = ST_RACE; race_setup(g.track, g.laps, 0); }
}

void menu_update(const Input *in, float dt)
{
    g.menu_anim += dt;
    if (!prev_ready) {
        for (int i = 0; i < NUM_TRACKS; i++) {
            float xy[320];
            track_preview_path(i, xy, 160, &prev_n[i]);
            for (int k = 0; k < prev_n[i]; k++) { prev_xy[i][0][k] = xy[k * 2]; prev_xy[i][1][k] = xy[k * 2 + 1]; }
        }
        prev_ready = 1;
    }
    if (g.unlock_t > 0) g.unlock_t += dt;
    switch (g.state) {
    case ST_TITLE: title_update(in); break;
    case ST_TRACKS: tracks_update(in); break;
    case ST_GARAGE: garage_update(in); break;
    case ST_OPTIONS: options_update(in); break;
    case ST_RESULTS: results_update(in); break;
    case ST_HELP: if (in->pressed & (BTN_A | BTN_B)) { snd(SND_UI_BACK, 0.6f, 1.0f, 0); menu_enter(ST_TITLE); } break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
static void menu_item(float x, float y, float w, const char *s, int sel, float t)
{
    float slide = sel ? 10.0f + 3.0f * sinf(t * 6.0f) : 0.0f;
    ui_slant(x + slide, y, w, 38, 10, sel ? 0xFFD23AF0u : 0x0C1020D0u);
    if (sel) ui_slant(x + slide - 14, y, 8, 38, 10, 0xFFD23AF0u);
    ui_text_weight(sel ? 1.4f : 1.1f);
    ui_text(s, x + slide + 22, y + 11, 16, sel ? DARKTXT : WHITE);
    ui_text_weight(1.0f);
}

/* the FUTURE 88 unlock card: a flash, the band sweeps in, the name slams down */
static void unlock_render(void)
{
    float t = g.unlock_t;
    float a = smooth01(t * 4.0f);
    ui_rect(0, 0, VW, VH, col_a(0x02040CC0u, a));
    /* the flash */
    if (t < 0.35f) {
        ui_begin(RM_ADD);
        ui_rect(0, 0, VW, VH, col_a(0xBFEFFFFFu, (0.35f - t) / 0.35f * 0.8f));
        ui_begin(RM_ALPHA);
    }
    float sweep = smooth01((t - 0.05f) * 3.0f);
    float bw = VW * sweep;
    ui_slant_h(VW * 0.5f - bw * 0.5f, 150, bw, 180, 0, 0x0A2A6AE8u, 0x1A6AD8E8u);
    ui_rect(VW * 0.5f - bw * 0.5f, 150, bw, 3, 0x3AF0FFFFu);
    ui_rect(VW * 0.5f - bw * 0.5f, 327, bw, 3, 0x3AF0FFFFu);
    /* light streaks running along the band */
    ui_begin(RM_ADD);
    for (int i = 0; i < 6; i++) {
        float y = 160.0f + (float)i * 28.0f, x = fmodf(t * (500.0f + 90.0f * (float)i) + (float)i * 211.0f, VW + 300.0f) - 150.0f;
        ui_rect_h(x - 160, y, 160, 2, 0x3AD8FF00u, col_a(0x9AE8FFFFu, 0.6f * sweep));
    }
    ui_begin(RM_ALPHA);
    if (t > 0.3f) ui_text_sc("CODE ACCEPTED  -  UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A", VW * 0.5f, 172, 14, col_a(0xBFEFFFFFu, smooth01((t - 0.3f) * 4.0f)));
    if (t > 0.45f) {
        float k = smooth01((t - 0.45f) * 5.0f), sc = 1.0f + (1.0f - k) * 1.2f;
        ui_text_weight(1.6f);
        /* chromatic copies settle into place */
        float o = (1.0f - k) * 14.0f + 2.0f * sinf(t * 30.0f) * (t < 1.2f ? 1.0f : 0.15f);
        ui_begin(RM_ADD);
        ui_text_c("FUTURE 88", VW * 0.5f - o, 202 - 30 * (sc - 1.0f), 64 * sc, col_a(0x3AF0FFFFu, 0.7f * k));
        ui_text_c("FUTURE 88", VW * 0.5f + o, 202 - 30 * (sc - 1.0f), 64 * sc, col_a(0x3A6AFFFFu, 0.7f * k));
        ui_begin(RM_ALPHA);
        ui_text_sc("FUTURE 88", VW * 0.5f, 202 - 30 * (sc - 1.0f), 64 * sc, col_a(0xFFFFFFFFu, k));
        ui_text_weight(1.0f);
    }
    if (t > 0.9f) ui_text_sc("HIDDEN CRAFT UNLOCKED  -  NOW IN YOUR GARAGE", VW * 0.5f, 290, 16, col_a(0x3AE0FFFFu, smooth01((t - 0.9f) * 4.0f)));
    if (t > 1.2f) ui_text_c("A  CONTINUE", VW * 0.5f, VH - 40, 14, col_a(0xFFD23AFFu, 0.6f + 0.4f * sinf(g.t * 5.0f)));
}

static void title_render(void)
{
    ui_rect_h(0, 0, VW * 0.55f, VH, 0x05070CD0u, 0x05070C00u);
    ui_rect_v(0, VH - 90, VW, 90, 0x05070C00u, 0x05070CC0u);
    draw_logo(230, 46, 1.0f, g.t);
    const char *items[4] = { "RACE", "GARAGE", "OPTIONS", "HOW TO PLAY" };
    for (int i = 0; i < 4; i++) menu_item(60, 232 + (float)i * 48, 250, items[i], g.cursor == i, g.t);
    credits_badge(VW - 20, 18);
    char buf[64];
    sprintf(buf, "WINS %d   PODIUMS %d   RACES %d", sv.wins, sv.podiums, sv.races);
    ui_text_r(buf, VW - 24, 60, 9, DIM);
    hint("UP / DOWN  SELECT      A  CONFIRM");
    if (!g.storage) ui_text_r("NO SD CARD: PROGRESS WON'T BE SAVED", VW - 24, VH - 48, 9, 0xFF9A6AFFu);
}

static void draw_track_map(int id, float x, float y, float w, float h, uint32_t col, float th)
{
    int n = prev_n[id];
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ax = x + prev_xy[id][0][i] * w, ay = y + prev_xy[id][1][i] * h, bx = x + prev_xy[id][0][j] * w, by = y + prev_xy[id][1][j] * h;
            ui_line(ax, ay, bx, by, pass ? th : th + 5, pass ? col : 0x00000090u);
            ui_circle(ax, ay, (pass ? th : th + 5) * 0.5f, pass ? col : 0x00000090u);
        }
    float sx = x + prev_xy[id][0][0] * w, sy = y + prev_xy[id][1][0] * h;
    ui_rect(sx - 4, sy - 4, 8, 8, 0xFFD23AFFu);
}

static void sun_moon(float x, float y, int night)
{
    if (night) {
        ui_circle(x, y, 9, 0xE8ECFFFFu);
        ui_circle(x + 5, y - 3, 8, 0x0C1020FFu);
    } else {
        ui_circle(x, y, 7, YELLOW);
        for (int i = 0; i < 8; i++) {
            float a = (float)i * TAU / 8.0f;
            ui_line(x + cosf(a) * 10, y + sinf(a) * 10, x + cosf(a) * 13, y + sinf(a) * 13, 2, YELLOW);
        }
    }
}

static void tracks_render(void)
{
    draw_background_panel();
    ui_text_weight(1.6f);
    ui_text_s("SELECT TRACK", 40, 22, 26, WHITE);
    ui_text_weight(1.0f);
    credits_badge(VW - 20, 18);
    for (int i = 0; i < NUM_TRACKS; i++) {
        const TrackInfo *I = &TRACK_INFO[i];
        int sel = g.cursor == i, locked = i >= sv.unlocked;
        float y = 76 + (float)i * 66;
        float slide = sel ? 12.0f : 0.0f;
        ui_slant(36 + slide, y, 320, 56, 10, sel ? 0xFFD23AF0u : 0x0C1020D8u);
        uint32_t tc = sel ? DARKTXT : (locked ? 0x6A7288FFu : WHITE);
        char num[4];
        sprintf(num, "%d", i + 1);
        ui_text_weight(1.5f);
        ui_text(num, 52 + slide, y + 12, 28, sel ? DARKTXT : (locked ? 0x4A5268FFu : YELLOW));
        ui_text_weight(1.1f);
        ui_text(I->name, 88 + slide, y + 12, 14, tc);
        ui_text_weight(1.0f);
        ui_text(locked ? "LOCKED" : I->place, 88 + slide, y + 34, 8, sel ? 0x3A3A44FFu : DIM);
        if (locked) {
            float lx = 330 + slide, ly = y + 28;
            ui_rrect(lx - 9, ly - 3, 18, 14, 3, tc);
            ui_ring(lx, ly - 5, 6, 3, tc);
        } else if (sv.best_place[i]) {
            char b[8];
            sprintf(b, "%d%s", sv.best_place[i], sv.best_place[i] == 1 ? "ST" : (sv.best_place[i] == 2 ? "ND" : (sv.best_place[i] == 3 ? "RD" : "TH")));
            ui_text_r(b, 344 + slide, y + 20, 12, sel ? DARKTXT : (sv.best_place[i] <= 3 ? YELLOW : DIM));
        }
    }
    /* details */
    int id = g.cursor;
    const TrackInfo *I = &TRACK_INFO[id];
    float px = 400, py = 76, pw = 420, ph = 324;
    float a = smooth01(g.menu_anim * 4.0f);
    ui_slant(px, py, pw, ph, 12, 0x0C1020E0u);
    ui_text_weight(1.5f);
    ui_text_s(I->name, px + 26, py + 18, 22, col_a(WHITE, a));
    ui_text_weight(1.0f);
    ui_text(I->place, px + 26, py + 50, 10, YELLOW);
    sun_moon(px + pw - 40, py + 30, I->night);
    ui_text("DIFFICULTY", px + 26, py + 74, 9, DIM);
    stars(px + 124, py + 78, I->stars, 5, 14);
    draw_track_map(id, px + 30 + (1 - a) * 20, py + 96, 220, 176, col_a(0xE8ECF4FFu, a), 3.5f);
    char buf[48], t[16];
    float sx = px + 280;
    ui_text("DECK", sx, py + 104, 8, DIM);
    ui_text(I->surface == SURF_ASPHALT ? "MAG DECK" : (I->surface == SURF_DIRT ? "DUST DECK" : "ICE DECK"), sx, py + 118, 12, WHITE);
    ui_text("BEST FINISH", sx, py + 144, 8, DIM);
    if (sv.best_place[id]) sprintf(buf, "%d%s", sv.best_place[id], sv.best_place[id] == 1 ? "ST" : (sv.best_place[id] == 2 ? "ND" : (sv.best_place[id] == 3 ? "RD" : "TH")));
    else strcpy(buf, "-");
    ui_text(buf, sx, py + 158, 12, WHITE);
    ui_text("BEST LAP", sx, py + 184, 8, DIM);
    ui_text(hud_time(sv.best_lap[id], t), sx, py + 198, 12, WHITE);
    ui_text("WIN PRIZE", sx, py + 224, 8, DIM);
    {
        int tr = g.track; g.track = id; int w = race_reward(1) * sv.laps / 3; g.track = tr;
        sprintf(buf, "%d CR", w);
    }
    ui_text(buf, sx, py + 238, 12, YELLOW);
    /* laps selector */
    float ly = py + ph - 38;
    ui_text("LAPS", px + 30, ly + 6, 11, DIM);
    ui_slant(px + 86, ly, 120, 28, 6, 0x1A2238FFu);
    sprintf(buf, "<  %d  >", sv.laps);
    ui_text_c(buf, px + 148, ly + 8, 12, WHITE);
    if (id >= sv.unlocked) {
        sprintf(buf, "FINISH TOP 3 AT %s TO UNLOCK", TRACK_INFO[id - 1].name);
        ui_text_c(buf, px + pw * 0.5f, ly - 26, 9, 0xFF9A6AFFu);
    } else {
        float pulse = 0.7f + 0.3f * sinf(g.t * 5.0f);
        ui_slant(px + pw - 150, ly - 4, 126, 34, 8, col_a(0xFFD23AFFu, pulse));
        ui_text_c("A  RACE!", px + pw - 84, ly + 7, 13, DARKTXT);
    }
    hint("UP / DOWN  TRACK      LEFT / RIGHT  LAPS      X  GARAGE      B  BACK");
}

/* ------------------------------------------------------------------ */
/* garage                                                              */
/* ------------------------------------------------------------------ */
static void stat_bar(float x, float y, float w, const char *name, float v, float next)
{
    ui_text(name, x, y, 9, DIM);
    ui_slant(x, y + 14, w, 10, 4, 0x1A2238FFu);
    if (next > v) ui_slant_h(x, y + 14, w * sat(next), 10, 4, 0x5AF27A90u, 0x5AF27AC0u);
    ui_slant_h(x, y + 14, w * sat(v), 10, 4, 0x3AB8FFFFu, 0x8AE8FFFFu);
}

static void car_stats_norm(const uint8_t *lv, float *o)
{
    CarStats s;
    car_stats_from_levels(lv, &s);
    o[0] = 0.25f + 0.75f * (s.vmax - 38.0f) / 15.0f;
    o[1] = 0.25f + 0.75f * (s.accel - 13.0f) / 9.0f;
    o[2] = 0.25f + 0.75f * (s.grip - 15.5f) / 6.25f;
    o[3] = 0.25f + 0.75f * ((7.0f - s.recharge) / 4.5f * 0.5f + s.npow / 5.0f * 0.5f);
    o[4] = 0.25f + 0.75f * (s.drift - 1.0f) / 1.1f;
}

static void garage_render(void)
{
    r3d_begin(0x0A0C14FFu);
    /* backdrop */
    ui_begin(RM_ALPHA);
    ui_rect_v(0, 0, VW, VH * 0.6f, 0x141A2CFFu, 0x0C1018FFu);
    ui_rect_v(0, VH * 0.6f, VW, VH * 0.4f, 0x0C1018FFu, 0x07080CFFu);
    ui_begin(RM_ADD);
    ui_glow(VW * 0.62f, VH * 0.18f, 260, 0x3A5AA840u);
    ui_begin(RM_ALPHA);
    /* the car on a turntable */
    float yaw = g.t * 0.45f + 0.6f;
    int body = g.tab == 1 ? g.cursor : sv.body;
    r3d_camera(10.5f, 7.0f, -6.5f, 1.3f, -1.9f, 4.9f, 40.0f);
    r3d_mode(RM_OPAQUE);
    /* floor disc */
    const int N = 40;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * TAU, a1 = (float)(i + 1) / N * TAU;
        uint32_t ci = 0x2A3044FFu, co = 0x161A26FFu;
        r3d_tri3(0, 0, 0, ci, cosf(a0) * 3.6f, 0, sinf(a0) * 3.6f, co, cosf(a1) * 3.6f, 0, sinf(a1) * 3.6f, co);
        r3d_tri3(cosf(a0) * 3.6f, 0, sinf(a0) * 3.6f, 0x3AB8FFC0u, cosf(a1) * 3.6f, 0, sinf(a1) * 3.6f, 0x3AB8FFC0u,
                 cosf(a1) * 3.7f, 0, sinf(a1) * 3.7f, 0x3AB8FFFFu);
        r3d_tri3(cosf(a0) * 3.6f, 0, sinf(a0) * 3.6f, 0x3AB8FFFFu, cosf(a1) * 3.7f, 0, sinf(a1) * 3.7f, 0x3AB8FFFFu,
                 cosf(a0) * 3.7f, 0, sinf(a0) * 3.7f, 0x3AB8FFFFu);
    }
    r3d_mode(RM_ALPHA);
    r3d_blob(0, 0.02f, 0, 2.0f, 3.0f, yaw, 0x000000A0u, 0x00000000u);
    car_draw_showroom(body, PAINT[sv.paint], PAINT[sv.paint2], body == BODY_FUTURE88 ? LIVERY_SIGNATURE : sv.livery, 0, 0.0f, 0, yaw, g.t);
    r3d_mode(RM_ADD);
    r3d_glow(0, 0.05f, 0, 5.0f, 0x3A8AFF30u);

    ui_begin(RM_ALPHA);
    ui_text_weight(1.6f);
    ui_text_s("GARAGE", 40, 22, 26, WHITE);
    ui_text_weight(1.0f);
    credits_badge(VW - 20, 18);
    /* tabs */
    const char *tabs[3] = { "UPGRADES", "CARS", "PAINT" };
    for (int i = 0; i < 3; i++) {
        float x = 40 + (float)i * 128;
        ui_slant(x, 62, 120, 28, 6, g.tab == i ? 0xFFD23AF0u : 0x0C1020D0u);
        ui_text_c(tabs[i], x + 63, 71, 11, g.tab == i ? DARKTXT : WHITE);
    }
    ui_text("L", 22, 70, 10, DIM);
    ui_text("R", 432, 70, 10, DIM);
    float st[5], nx[5];
    car_stats_norm(sv.lv, st);
    memcpy(nx, st, sizeof st);
    char buf[64];
    if (g.tab == 0) {
        uint8_t lv2[NUM_UPGRADES];
        memcpy(lv2, sv.lv, sizeof lv2);
        if (lv2[g.cursor] < UP_LEVELS[g.cursor]) lv2[g.cursor]++;
        car_stats_norm(lv2, nx);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            int sel = g.cursor == i;
            float y = 104 + (float)i * 50;
            ui_slant(40, y, 360, 44, 8, sel ? 0xFFD23AF0u : 0x0C1020D8u);
            ui_text(UP_NAME[i], 58, y + 8, 12, sel ? DARKTXT : WHITE);
            for (int k = 0; k < UP_LEVELS[i]; k++) {
                uint32_t pc = k < sv.lv[i] ? (sel ? DARKTXT : 0x3AB8FFFFu) : (sel ? 0x00000040u : 0x2A3150FFu);
                ui_slant(58 + (float)k * 26, y + 27, 22, 8, 3, pc);
            }
            int c = upgrade_cost(i, sv.lv[i]);
            if (sv.lv[i] >= UP_LEVELS[i]) strcpy(buf, "MAX");
            else sprintf(buf, "%d CR", c);
            ui_text_r(buf, 388, y + 15, 12, sel ? DARKTXT : (sv.lv[i] >= UP_LEVELS[i] ? 0x5AF27AFFu : (sv.credits >= c ? YELLOW : 0xFF6A5AFFu)));
        }
        ui_text(UP_DESC[g.cursor], 44, 410, 9, CYAN);
        if (g.confirm > 0) ui_text("NOT ENOUGH CREDITS - WIN RACES TO EARN MORE", 44, 426, 9, 0xFF6A5AFFu);
    } else if (g.tab == 1) {
        for (int i = 0; i < NUM_BODIES; i++) {
            int sel = g.cursor == i, own = (sv.owned_body >> i) & 1;
            float y = 104 + (float)i * 56;
            if (i == BODY_FUTURE88 && !sv.secret) {
                ui_slant(40, y, 360, 48, 8, 0x0C102080u);
                ui_text("? ? ?", 58, y + 10, 14, 0x4A5268FFu);
                ui_text("HIDDEN CRAFT", 58, y + 31, 8, 0x4A5268FFu);
                continue;
            }
            uint32_t bg = sel ? 0xFFD23AF0u : (i == BODY_FUTURE88 ? 0x0A2A5AE0u : 0x0C1020D8u);
            ui_slant(40, y, 360, 48, 8, bg);
            ui_text(BODY_NAME[i], 58, y + 10, 14, sel ? DARKTXT : (i == BODY_FUTURE88 ? 0xBFEFFFFFu : WHITE));
            if (own) ui_text(sv.body == i ? "PILOTING" : (i == BODY_FUTURE88 ? "SECRET CRAFT" : "OWNED"), 58, y + 31, 8, sel ? 0x3A3A44FFu : 0x5AF27AFFu);
            else { sprintf(buf, "%d CR", BODY_PRICE[i]); ui_text_r(buf, 388, y + 17, 12, sel ? DARKTXT : (sv.credits >= BODY_PRICE[i] ? YELLOW : 0xFF6A5AFFu)); }
        }
        ui_text("CRAFT CHANGE YOUR LOOK - SPEED COMES FROM UPGRADES", 44, 340, 9, CYAN);
        if (!((sv.owned_body >> g.cursor) & 1)) ui_text("A  BUY THIS CRAFT", 44, 358, 10, YELLOW);
        if (g.confirm > 0) ui_text("NOT ENOUGH CREDITS - WIN RACES TO EARN MORE", 44, 376, 9, 0xFF6A5AFFu);
    } else {
        const char *rows[3] = { "MAIN COLOR", "ACCENT COLOR", "LIVERY" };
        if (sv.body == BODY_FUTURE88) {
            ui_slant(40, 104, 360, 92, 8, 0x0A2A5AE0u);
            ui_text("SIGNATURE FINISH", 58, 116, 10, DIM);
            ui_text("LIQUID SILVER + NEON BLUE", 58, 138, 16, 0xBFEFFFFFu);
            ui_text("FUTURE 88 ALWAYS WEARS ITS OWN COLOURS", 58, 168, 9, CYAN);
        } else
        for (int i = 0; i < 3; i++) {
            int sel = g.cursor == i;
            float y = 104 + (float)i * 60;
            ui_slant(40, y, 360, 52, 8, sel ? 0xFFD23AF0u : 0x0C1020D8u);
            ui_text(rows[i], 58, y + 9, 10, sel ? DARKTXT : DIM);
            const char *val = i == 0 ? PAINT_NAME[sv.paint] : (i == 1 ? PAINT_NAME[sv.paint2] : LIVERY_NAME[sv.livery]);
            sprintf(buf, "<  %s  >", val);
            ui_text(buf, 58, y + 28, 12, sel ? DARKTXT : WHITE);
            if (i < 2) {
                uint32_t pc = PAINT[i == 0 ? sv.paint : sv.paint2];
                ui_rrect(350, y + 12, 32, 28, 6, 0x00000080u);
                ui_rrect(352, y + 14, 28, 24, 5, pc);
            }
        }
    }
    /* stats */
    float sx = VW - 250, sy = 322;
    ui_slant(sx - 16, sy - 12, 240, 150, 8, 0x0C1020C8u);
    const char *sn[5] = { "TOP SPEED", "ACCELERATION", "HANDLING", "BOOST", "DRIFT" };
    for (int i = 0; i < 5; i++) stat_bar(sx, sy + (float)i * 27, 200, sn[i], st[i], nx[i]);
    ui_text_r(BODY_NAME[body], VW - 30, 104, 16, WHITE);
    hint("L / R  TAB      A  BUY / SELECT      Y  RACE      B  BACK");
}

static void options_render(void)
{
    draw_background_panel();
    ui_rect(0, 0, VW, VH, 0x05070C90u);
    ui_text_weight(1.6f);
    ui_text_s("OPTIONS", 40, 22, 26, WHITE);
    ui_text_weight(1.0f);
    const char *cams[3] = { "FOLLOW", "FIXED (TOP DOWN)", "CLOSE CHASE" };
    const char *names[5] = { "CAMERA", "SPEED UNITS", "SCREEN", "TILT-SHIFT BLUR", "RESET PROGRESS" };
    const char *vals[5] = { cams[sv.cam], sv.units ? "MPH" : "KM/H", sv.wide ? "16:9 WIDE" : "4:3", sv.tilt ? "ON" : "OFF", g.confirm ? "PRESS A AGAIN TO ERASE" : "" };
    for (int i = 0; i < 5; i++) {
        int sel = g.cursor == i;
        float y = 90 + (float)i * 56;
        ui_slant(60, y, 600, 46, 10, sel ? 0xFFD23AF0u : 0x0C1020D8u);
        ui_text(names[i], 82, y + 15, 14, sel ? DARKTXT : WHITE);
        char buf[48];
        if (i < 4) sprintf(buf, "<  %s  >", vals[i]); else strcpy(buf, vals[i]);
        ui_text_r(buf, 640, y + 16, 12, sel ? DARKTXT : (i == 4 ? 0xFF6A5AFFu : YELLOW));
    }
    ui_text("X DURING A RACE ALSO SWITCHES THE CAMERA", 64, 380, 9, DIM);
    hint("UP / DOWN  SELECT      LEFT / RIGHT  CHANGE      B  BACK");
}

static void help_render(void)
{
    ui_rect(0, 0, VW, VH, 0x05070CD8u);
    ui_text_weight(1.6f);
    ui_text_s("HOW TO PLAY", 40, 22, 26, WHITE);
    ui_text_weight(1.0f);
    const char *keys[9] = { "A", "R / ZR", "L / ZL", "L + STEER", "LEFT STICK", "B", "X", "Y", "+" };
    const char *what[9] = { "ACCELERATE", "BOOST - UNLIMITED, RECHARGES AFTER EVERY USE", "BRAKE / REVERSE", "DRIFT - HOLD THROUGH THE CORNER",
                            "STEER (OR D-PAD)", "HOP (HOP + L STARTS A DRIFT)", "CHANGE CAMERA", "HORN", "PAUSE" };
    for (int i = 0; i < 9; i++) {
        float y = 76 + (float)i * 30;
        ui_slant(40, y, 150, 24, 6, 0xFFD23AE0u);
        ui_text_c(keys[i], 118, y + 7, 10, DARKTXT);
        ui_text(what[i], 210, y + 7, 11, WHITE);
    }
    float y = 76 + 9 * 30 + 6;
    ui_text("DRIFTS CHARGE UP: BLUE, ORANGE, PURPLE SPARKS. LET GO OF L FOR A MINI-TURBO.", 40, y, 9, CYAN);
    ui_text("DRIFTING AND SLIPSTREAMING BEHIND RIVALS BOTH SPEED UP THE BOOST RECHARGE.", 40, y + 16, 9, CYAN);
    ui_text("PRESS A JUST BEFORE GO FOR A ROCKET START. FINISH TOP 3 TO UNLOCK THE NEXT TRACK,", 40, y + 40, 9, YELLOW);
    ui_text("AND SPEND YOUR WINNINGS IN THE GARAGE - THE LATER TRACKS NEED A FASTER CRAFT.", 40, y + 56, 9, YELLOW);
    hint("A / B  BACK");
}

static void loading_render(void)
{
    const TrackInfo *I = &TRACK_INFO[g.loading_track];
    ui_rect_v(0, 0, VW, VH, 0x0C1020FFu, 0x05070CFFu);
    draw_track_map(g.loading_track, VW * 0.5f + 20, 110, 360, 250, 0x2A3150FFu, 6);
    ui_text_weight(1.6f);
    ui_text_s(I->name, 60, 170, 34, WHITE);
    ui_text_weight(1.0f);
    ui_text(I->place, 62, 220, 12, YELLOW);
    ui_text("LOADING...", 62, 380, 12, DIM);
    const char *tips[5] = { "HOLD L THROUGH A CORNER TO DRIFT - LET GO FOR A MINI-TURBO",
                            "PURPLE SPARKS MEAN THE BIGGEST DRIFT BOOST",
                            "BOOST WITH R - IT NEVER RUNS OUT, BUT WAIT FOR THE RECHARGE",
                            "HOP WITH B, THEN HOLD L TO START A DRIFT AT LOWER SPEED",
                            "HIT 88 MPH AND HOLD ON TIGHT..." };
    ui_text(tips[(sv.races + g.loading_track) % 5], 62, 410, 9, CYAN);
}

static void results_render(void)
{
    ui_rect(0, 0, VW, VH, 0x05070CC8u);
    char buf[64], t[16];
    ui_text_weight(1.6f);
    ui_text_s("RESULTS", 40, 22, 26, WHITE);
    ui_text_weight(1.0f);
    ui_text(trk.info->name, 230, 32, 12, YELLOW);
    float a = smooth01(g.t * 2.0f);
    for (int i = 0; i < ncars; i++) {
        const Car *c = &cars[g.order[i]];
        float y = 72 + (float)i * 40;
        float slide = (1.0f - smooth01(g.t * 3.0f - (float)i * 0.15f)) * -300.0f;
        int me = c->is_player;
        ui_slant(40 + slide, y, 440, 34, 8, me ? 0xFFD23AF0u : 0x0C1020D8u);
        sprintf(buf, "%d", i + 1);
        ui_text_weight(1.4f);
        ui_text(buf, 56 + slide, y + 8, 18, me ? DARKTXT : (i < 3 ? YELLOW : WHITE));
        ui_text_weight(1.0f);
        ui_rect(90 + slide, y + 7, 5, 20, c->paint);
        ui_text(c->name, 104 + slide, y + 11, 12, me ? DARKTXT : WHITE);
        if (c->finished) {
            if (i == 0) strcpy(buf, hud_time(c->finish_time, t));
            else { const Car *w = &cars[g.order[0]]; sprintf(buf, "+%.2f", c->finish_time - w->finish_time); }
        } else strcpy(buf, "RACING");
        ui_text_r(buf, 466 + slide, y + 11, 12, me ? DARKTXT : DIM);
    }
    float px = 510, py = 72;
    ui_slant(px, py, 320, 312, 10, col_a(0x0C1020E8u, a));
    ui_text("PRIZE MONEY", px + 24, py + 20, 10, DIM);
    sprintf(buf, "%d CR", g.res_base);
    ui_text_r(buf, px + 300, py + 20, 12, WHITE);
    ui_text("DRIFT BONUS", px + 24, py + 48, 10, DIM);
    sprintf(buf, "%d CR", g.res_drift);
    ui_text_r(buf, px + 300, py + 48, 12, WHITE);
    ui_rect(px + 24, py + 74, 276, 2, 0xFFFFFF30u);
    ui_text("EARNED", px + 24, py + 88, 12, YELLOW);
    int shown = (int)((float)g.res_credits * sat(g.t * 1.2f));
    sprintf(buf, "+%d CR", shown);
    ui_text_weight(1.4f);
    ui_text_r(buf, px + 300, py + 86, 18, YELLOW);
    ui_text_weight(1.0f);
    ui_text("BALANCE", px + 24, py + 120, 10, DIM);
    sprintf(buf, "%d CR", sv.credits);
    ui_text_r(buf, px + 300, py + 120, 12, WHITE);
    ui_text("YOUR BEST LAP", px + 24, py + 148, 10, DIM);
    ui_text_r(hud_time(cars[0].best_lap, t), px + 300, py + 148, 12, g.res_best ? 0x5AF27AFFu : WHITE);
    float yy = py + 180;
    if (g.res_best) { ui_text("NEW TRACK RECORD!", px + 24, yy, 11, 0x5AF27AFFu); yy += 24; }
    if (g.res_unlock && g.track + 1 < NUM_TRACKS) {
        float p = 0.75f + 0.25f * sinf(g.t * 6.0f);
        ui_slant(px + 16, yy - 6, 288, 46, 8, col_a(0xFFD23AFFu, p));
        ui_text("NEW TRACK UNLOCKED", px + 30, yy + 2, 10, DARKTXT);
        ui_text(TRACK_INFO[g.track + 1].name, px + 30, yy + 20, 12, DARKTXT);
        yy += 54;
    }
    if (g.res_place > 3 && g.track + 1 < NUM_TRACKS && sv.unlocked <= g.track + 1)
        ui_text("FINISH TOP 3 TO UNLOCK THE NEXT TRACK", px + 24, yy, 9, 0xFF9A6AFFu);
    hint("A  CONTINUE      X  GARAGE      Y  RACE AGAIN");
}

void menu_render(void)
{
    if (g.state == ST_GARAGE) { garage_render(); return; }
    if (g.state == ST_LOADING) { r3d_begin(0x05070CFFu); ui_begin(RM_ALPHA); loading_render(); return; }
    race_render();
    ui_begin(RM_ALPHA);
    switch (g.state) {
    case ST_TITLE: title_render(); if (g.unlock_t > 0) unlock_render(); break;
    case ST_TRACKS: tracks_render(); break;
    case ST_OPTIONS: options_render(); break;
    case ST_HELP: help_render(); break;
    case ST_RESULTS: results_render(); break;
    default: break;
    }
    if (g.save_msg_t > 0) {
        ui_text_r(g.save_ok ? "SAVED" : "SAVE FAILED", VW - 24, VH - 30, 9, g.save_ok ? 0x5AF27AFFu : 0xFF6A5AFFu);
    }
}
