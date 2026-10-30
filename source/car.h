/*
 * RACING LEGENDS - hover craft: the models, the drift handling, the boost, collisions and the AI pilots.
 */
#ifndef CAR_H
#define CAR_H

#include <stdint.h>

#define NUM_BODIES 4
#define BODY_FUTURE88 3              /* the hidden craft */
#define LIVERY_SIGNATURE 4           /* its liquid silver + neon blue finish */
#define NUM_PAINTS 12
#define MAX_CARS 8

enum { UP_ENGINE, UP_TURBO, UP_GRIP, UP_RECHARGE, UP_BOOST, UP_DRIFT, NUM_UPGRADES };
#define UP_MAX 5
#define BOOST_TIME 1.6f            /* seconds a boost lasts */

extern const char *BODY_NAME[NUM_BODIES];
extern const uint32_t PAINT[NUM_PAINTS];
extern const char *PAINT_NAME[NUM_PAINTS];

typedef struct {
    float vmax;        /* top speed (m/s) */
    float accel;       /* launch acceleration (m/s^2) */
    float grip;        /* lateral grip (m/s^2) */
    float recharge;    /* seconds for the boost to recharge from empty */
    float npow;        /* boost power level 0..5 */
    float drift;       /* drift-charge rate multiplier */
} CarStats;

void car_stats_from_levels(const uint8_t *lv, CarStats *st);

typedef struct {
    float throttle, brake, steer;   /* 0..1, 0..1, -1..1 (+ = right) */
    int drift;                      /* L held */
    int nitro;                      /* boost pressed (edge) */
    int hop;                        /* B pressed (edge) */
} CarInput;

typedef struct {
    /* identity */
    int body, livery, number, is_player;
    uint32_t paint, paint2;
    char name[16];
    CarStats st;
    /* state */
    float x, y, z, vx, vz, h, w;    /* position, velocity, heading (forward = (sin h, cos h)), yaw rate */
    float speed;                     /* |v| */
    float air, vy;                   /* hop height and its vertical speed */
    float pitch, roll, susp, steer_vis, wheel_rot;
    int   sample; float lat, along;
    int   lap, cp, finished, place;
    float progress;                  /* metres raced */
    float race_time, finish_time, lap_start, best_lap, last_lap;
    float nitro;                     /* boost charge 0..1: full = ready */
    float boost_t, mini_t, start_boost, pad_t;
    int   drifting, drift_dir, drift_tier, drift_latch;
    float drift_charge, drift_t, drift_pts, drift_total, drift_best, straight_t;
    float slip;                      /* slip angle (rad) */
    int   offroad;
    float wall_t, impact, scrape;    /* contact with the barriers */
    float rpm; int gear; float shift_t;
    float stuck_t, wrong_t;
    float slipstream;
    /* AI */
    int   ai; float skill, lane, ai_nitro_t, ai_avoid, ai_drift_t;
    CarInput in;
    /* events for sound / effects (set during the step, cleared by the game) */
    int ev_impact, ev_nitro, ev_boost, ev_land, ev_tier, ev_shift, ev_backfire, ev_horn, ev_pad, ev_ready;
    float ev_impact_v;
} Car;

extern Car cars[MAX_CARS];
extern int ncars;

void cars_models_init(void);                         /* build the four body meshes (once) */
void car_place(Car *c, float dist, float lat);       /* on the grid */
void car_ai_input(Car *c, float dt);                 /* fill c->in for an AI driver */
void cars_step(float dt, int racing);                /* physics for every car, collisions */
void car_draw(const Car *c, float t, int night, float shadow);
/* draw a body in the garage / title (yaw = rotation, spin = wheel angle) */
void car_draw_showroom(int body, uint32_t paint, uint32_t paint2, int livery, float x, float y, float z, float yaw, float t);
void car_headlights(const Car *c);                   /* night: light on the road ahead (MUL2 + ADD) */

#endif
