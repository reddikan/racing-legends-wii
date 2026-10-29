/*
 * RACING LEGENDS - the game: menus, the garage, races. Platform independent; main.c (Wii) or the test
 * harness drives it.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_A = 1 << 4, BTN_B = 1 << 5, BTN_X = 1 << 6, BTN_Y = 1 << 7,
    BTN_L = 1 << 8, BTN_R = 1 << 9, BTN_ZL = 1 << 10, BTN_ZR = 1 << 11,
    BTN_PLUS = 1 << 12, BTN_MINUS = 1 << 13,
};

typedef struct {
    uint32_t held, pressed;
    float sx, sy;          /* left stick, -1..1 (+x right, +y up) */
    float lt, rt;          /* analog triggers 0..1 (0 if not available) */
} Input;

/* sounds */
enum {
    /* loops */
    SND_ENG_A_ON, SND_ENG_A_OFF, SND_ENG_B_ON, SND_ENG_B_OFF, SND_ENG_C_ON, SND_ENG_C_OFF,
    SND_SKID, SND_GRAVEL, SND_NITRO_LOOP, SND_WIND, SND_SCRAPE,
    /* one-shots */
    SND_IMPACT_S, SND_IMPACT_L, SND_NITRO_START, SND_BOOST, SND_TIER, SND_SHIFT, SND_BACKFIRE, SND_LAND, SND_HOP,
    SND_HORN, SND_BEEP, SND_GO, SND_LAP, SND_FINAL, SND_FINISH, SND_CROWD,
    SND_UI_MOVE, SND_UI_OK, SND_UI_BACK, SND_UI_BUY, SND_UI_ERR,
    SND_COUNT
};
#define SND_FIRST_ONESHOT SND_IMPACT_S

/* the looping voices the platform keeps running */
enum { LOOP_ENG_ON, LOOP_ENG_OFF, LOOP_AI0, LOOP_AI1, LOOP_AI2, LOOP_SKID, LOOP_GRAVEL, LOOP_NITRO, LOOP_WIND, LOOP_SCRAPE, NUM_LOOPS };
typedef struct { int snd; float rate, vol, pan; } LoopState;     /* rate: playback speed ratio; snd < 0 = silent */

void game_init(void);
void game_update(const Input *in, float dt);
void game_render(void);
int  game_pop_sound(int *snd, float *rate, float *vol, float *pan);
void game_loops(LoopState *out);
int  game_wants_wide(void);                  /* 1 = 16:9, 0 = 4:3 */
int  game_quit_requested(void);

/* saving: the platform asks for the bytes to write, and hands back what it read */
#define SAVE_MAX 512
void game_storage(int available);
int  game_save_request(char *buf, int max);  /* > 0: write these bytes */
void game_save_result(int ok);
void game_load(const char *buf, int n);

#endif
