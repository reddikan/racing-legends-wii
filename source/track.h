/*
 * RACING LEGENDS - the five circuits: the centre line, the racing line, the terrain, the scenery and the
 * baked lighting, all generated when a track is loaded.
 */
#ifndef TRACK_H
#define TRACK_H

#include <stdint.h>

#define NUM_TRACKS 5
enum { TH_FOREST, TH_CANYON, TH_SNOW, TH_HARBOR, TH_LANTERN };
enum { SURF_ASPHALT, SURF_DIRT, SURF_SNOW };

typedef struct {
    float x, y, z;          /* centre line */
    float tx, tz;           /* unit tangent */
    float nx, nz;           /* unit normal, pointing right of the direction of travel */
    float hw;               /* road half width */
    float wl, wr;           /* walls: distance from the centre on the left / right */
    float curv;             /* signed curvature (1/m), + = turning right */
    float dist;             /* distance from the start line */
    float line;             /* racing line: lateral offset */
    float lcurv;            /* curvature of the racing line */
    float vline;            /* fastest sensible speed along the racing line at full grip (m/s) */
    uint8_t kerb;           /* bit 0: kerb on the left, bit 1: on the right */
} TSample;

typedef struct {
    const char *name, *place;
    int theme, night, surface;
    int stars;               /* difficulty, 1..5 */
    float sun[3];            /* direction *towards* the sun */
    uint32_t sun_col, sky_col, gnd_col;   /* light colours */
    uint32_t clear;
    float grip;              /* surface grip multiplier */
} TrackInfo;

/* a light the track bakes into its vertex colours and the renderer draws a glow for */
typedef struct { float x, y, z, r; uint32_t c; float glow; } TLight;
#define MAX_LIGHTS 400

typedef struct {
    int id;
    const TrackInfo *info;
    TSample *s;
    int n;
    float length;
    float minx, minz, maxx, maxz;
    float water;             /* water level (-1000 = none) */
    TLight light[MAX_LIGHTS];
    int nlight;
} Track;

extern Track trk;
extern const TrackInfo TRACK_INFO[NUM_TRACKS];

void  track_load(int id);                              /* builds everything (a second or two) */
float track_height(float x, float z);                  /* the ground: terrain or road */
/* nearest centre-line sample, searching around `hint` (or everywhere if hint < 0);
 * lat = signed lateral offset (+ right), along = metres past the sample */
int   track_locate(int hint, float x, float z, float *lat, float *along);
void  track_point(float dist, float lat, float *x, float *z);     /* position from distance + offset */
int   track_sample_at(float dist);
int   track_is_boost(int sample);                   /* a boost pad on this sample? */
float track_pad_lat(int sample);                    /* lateral centre of the pads there */
void  track_draw_world(void);                          /* queue the static world (culled) */
void  track_draw_glows(float t);                        /* lamps, neon, water sparkle (dynamic) */
/* the lit colour of a surface with albedo `c`, normal n, at position p (baked lighting) */
uint32_t track_light(uint32_t c, float nx, float ny, float nz, float px, float py, float pz);
/* for the minimap: the centre line projected into a box */
void  track_map_xform(float x, float z, float bx, float by, float bw, float bh, float *ox, float *oy);
#endif
