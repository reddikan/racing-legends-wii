/* RACING LEGENDS - state shared by game.c, race.c, hud.c and menu.c */
#ifndef GAME_INT_H
#define GAME_INT_H

#include "game.h"
#include "car.h"
#include "track.h"
#include "r3d.h"
#include "ui.h"
#include "fx.h"
#include "util.h"

#define SAVE_MAGIC 0x524C4732u   /* "RLG2" */
#define SAVE_MAGIC_V1 0x524C4731u
/* the speed readout: game metres per second -> the mph shown (the world is drawn at toy scale, so speeds read high) */
#define MPS_TO_MPH 2.796f

typedef struct {
    uint32_t magic;
    int32_t  credits;
    uint8_t  lv[NUM_UPGRADES];
    uint8_t  body, paint, paint2, livery;
    uint8_t  unlocked;                 /* tracks open: 1..5 */
    uint8_t  best_place[NUM_TRACKS];   /* 0 = never raced */
    float    best_lap[NUM_TRACKS];
    float    best_race[NUM_TRACKS];
    uint8_t  laps, cam, units, wide, tilt, sfx_vol;
    int32_t  races, wins, podiums;
    int32_t  best_drift;
    uint8_t  owned_body;               /* bit mask of bodies bought */
    uint8_t  seen_help;
    uint8_t  secret;                   /* FUTURE 88 unlocked by the code */
    uint8_t  pad[1];
} Save;

extern Save sv;

enum { ST_TITLE, ST_TRACKS, ST_GARAGE, ST_OPTIONS, ST_LOADING, ST_RACE, ST_RESULTS, ST_HELP };
enum { RP_INTRO, RP_COUNT, RP_RACE, RP_DONE };
enum { CAM_FOLLOW, CAM_FIXED, CAM_CLOSE, NUM_CAMS };

typedef struct { char text[32]; uint32_t col; float t, size; float y; } Popup;
#define MAX_POPUPS 6

typedef struct {
    int state;
    float t, fade;
    int quit;
    /* menus */
    int cursor, sub, tab, confirm;
    int code_pos;          /* progress through the secret code */
    float unlock_t;        /* the FUTURE 88 unlock card (> 0 while showing) */
    int track_sel;
    float menu_anim;
    /* race */
    int track, laps, attract;
    int phase;
    float phase_t, race_t;
    int paused, pause_sel;
    int final_lap_shown;
    int count_last;
    float thr_press_t;
    int order[MAX_CARS];
    /* camera */
    float cx, cy, cz, cyaw, cdist, cheight, cfov, shake, look_x, look_z;
    float intro_a;
    /* results */
    int res_place, res_credits, res_base, res_drift, res_clean, res_unlock, res_best;
    float res_time;
    /* popups */
    Popup pop[MAX_POPUPS];
    float nitro_flash, tier_flash; int tier_col;
    float boost_anim;      /* 1 -> 0 after each boost */
    float mph88_t;         /* > 0 while the 88 mph animation plays */
    int armed88; float since88;
    float wrong_t;
    /* audio */
    float eng_vol;
    int loading_track, loading_next, loading_frames;
    /* save */
    int storage, dirty, save_ok, save_msg_t;
} G;

extern G g;

/* sounds */
void snd(int id, float vol, float rate, float pan);

/* race.c */
void race_setup(int track, int laps, int attract);
void race_update(const Input *in, float dt);
void race_render(void);
void race_camera(float dt, int snap);
void race_draw_world(int hud);
int  race_reward(int place);
void race_loops(LoopState *out);

/* hud.c */
void hud_draw(void);
void hud_popup(const char *text, uint32_t col, float size);
void hud_update(float dt);
float hud_speed_units(float mps);
const char *hud_time(float t, char *buf);
void hud_minimap(float x, float y, float w, float h, int dots);

/* menu.c */
void menu_update(const Input *in, float dt);
void menu_render(void);
void menu_enter(int state);
int  upgrade_cost(int up, int level);
void draw_background_panel(void);
void track_preview_path(int id, float *xy, int max, int *n);

/* shared drawing bits */
void draw_logo(float cx, float y, float scale, float t);
void ui_panel(float x, float y, float w, float h);
void save_mark_dirty(void);

#endif
