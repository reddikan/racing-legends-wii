/*
 * RACING LEGENDS - the top level: start-up, the state machine, sound events and saving.
 */
#include "game_int.h"

#include <string.h>

Save sv;
G g;

/* ------------------------------------------------------------------ */
/* sounds                                                              */
/* ------------------------------------------------------------------ */
typedef struct { int id; float rate, vol, pan; } SndEv;
#define SQ 48
static SndEv sq[SQ];
static int sq_head, sq_tail;

void snd(int id, float vol, float rate, float pan)
{
    int next = (sq_head + 1) % SQ;
    if (next == sq_tail) return;
    sq[sq_head].id = id; sq[sq_head].vol = vol; sq[sq_head].rate = rate; sq[sq_head].pan = pan;
    sq_head = next;
}

int game_pop_sound(int *id, float *rate, float *vol, float *pan)
{
    if (sq_tail == sq_head) return 0;
    *id = sq[sq_tail].id; *rate = sq[sq_tail].rate; *vol = sq[sq_tail].vol; *pan = sq[sq_tail].pan;
    sq_tail = (sq_tail + 1) % SQ;
    return 1;
}

void game_loops(LoopState *out) { race_loops(out); }

/* ------------------------------------------------------------------ */
/* saving                                                              */
/* ------------------------------------------------------------------ */
static void save_defaults(void)
{
    memset(&sv, 0, sizeof sv);
    sv.magic = SAVE_MAGIC;
    sv.credits = 300;
    sv.unlocked = 1;
    sv.laps = 3;
    sv.wide = 1;
    sv.tilt = 1;
    sv.owned_body = 1;
    sv.body = 0; sv.paint = 0; sv.paint2 = 4; sv.livery = 1;
    sv.sfx_vol = 10;
}

void save_mark_dirty(void) { g.dirty = 1; }
void game_storage(int available) { g.storage = available; }

int game_save_request(char *buf, int max)
{
    if (!g.dirty || !g.storage) return 0;
    /* don't stall the game mid-race */
    if (g.state == ST_RACE && g.phase != RP_DONE) return 0;
    g.dirty = 0;
    int n = (int)sizeof sv;
    if (n > max) return 0;
    memcpy(buf, &sv, (size_t)n);
    return n;
}

void game_save_result(int ok) { g.save_ok = ok; g.save_msg_t = 90; }

void game_load(const char *buf, int n)
{
    if (n != (int)sizeof sv) return;
    Save s;
    memcpy(&s, buf, sizeof s);
    if (s.magic != SAVE_MAGIC) return;
    sv = s;
    if (sv.unlocked < 1 || sv.unlocked > NUM_TRACKS) sv.unlocked = 1;
    if (sv.laps < 1 || sv.laps > 5) sv.laps = 3;
    if (sv.body >= NUM_BODIES) sv.body = 0;
    if (sv.paint >= NUM_PAINTS) sv.paint = 0;
    if (sv.paint2 >= NUM_PAINTS) sv.paint2 = 4;
    if (sv.livery > 3) sv.livery = 0;
    if (sv.cam >= NUM_CAMS) sv.cam = 0;
    sv.owned_body |= 1;
    for (int i = 0; i < NUM_UPGRADES; i++) if (sv.lv[i] > UP_MAX) sv.lv[i] = UP_MAX;
    if (sv.lv[UP_TANK] > 3) sv.lv[UP_TANK] = 3;
}

int game_wants_wide(void) { return sv.wide; }
int game_quit_requested(void) { return g.quit; }

/* ------------------------------------------------------------------ */
void game_init(void)
{
    memset(&g, 0, sizeof g);
    save_defaults();
    cars_models_init();
    track_load(0);
    race_setup(0, 3, 1);
    g.state = ST_TITLE;
    g.track_sel = 0;
}

void game_update(const Input *in, float dt)
{
    g.t += dt;
    if (g.save_msg_t > 0) g.save_msg_t--;
    static const Input none;
    switch (g.state) {
    case ST_RACE:
        race_update(in, dt);
        break;
    case ST_LOADING:
        g.loading_frames++;
        if (g.loading_frames >= 3) {
            track_load(g.loading_track);
            g.state = ST_RACE;
            g.t = 0;
            race_setup(g.loading_track, sv.laps, 0);
        }
        break;
    case ST_GARAGE:
        menu_update(in, dt);
        break;
    default:
        race_update(&none, dt);
        menu_update(in, dt);
        break;
    }
}

void game_render(void)
{
    if (g.state == ST_RACE) { race_render(); return; }
    menu_render();
}
