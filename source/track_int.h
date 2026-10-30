/* RACING LEGENDS - internals shared by track.c and scenery.c */
#ifndef TRACK_INT_H
#define TRACK_INT_H

#include "track.h"
#include "r3d.h"
#include "util.h"

#define GRID 3.0f
#define GMAX 360                   /* nodes per side at most */

extern int    g_w, g_h;            /* terrain grid size (nodes) */
extern float  g_x0, g_z0;
extern float *g_hgt;               /* node heights */
extern float *g_dst;               /* |distance| from the centre line */
extern int16_t *g_smp;             /* nearest sample */
extern float  g_wallmax;           /* the widest wall offset on the track */

/* clearance from the track at (x, z): distance beyond the nearest wall (negative = inside the walls) */
float track_clearance(float x, float z);
float track_dist(float x, float z);         /* distance from the centre line (from the grid) */
int   track_nearest(float x, float z);      /* nearest sample (from the grid) */
float terrain_height(float x, float z);     /* the terrain alone (bilinear on the grid) */

/* lit, flat-shaded static triangle: normal = (v1 - v0) x (v2 - v0) must point out of the surface */
void lit_tri(int mode, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t alb);
void lit_quad(int mode, float x0, float y0, float z0, float x1, float y1, float z1,
              float x2, float y2, float z2, float x3, float y3, float z3, uint32_t alb);
void emis_tri(int mode, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t c);
void emis_quad(int mode, float x0, float y0, float z0, float x1, float y1, float z1,
               float x2, float y2, float z2, float x3, float y3, float z3, uint32_t c);
/* an axis-rotated box: centre (x, z) on the ground at y, half sizes, yaw */
void lit_box(float x, float y, float z, float hx, float hy, float hz, float yaw, uint32_t top, uint32_t side);
/* a vertical n-sided prism / cone frustum */
void lit_tri_n(int mode, const float *p0, const float *n0, const float *p1, const float *n1, const float *p2, const float *n2, uint32_t alb);
void lit_blob(float x, float y, float z, float rx, float ry, float rz, uint32_t c, float wob, uint32_t seed);
void lit_prism(float x, float y0, float z, float r0, float r1, float h, int n, float rot, uint32_t c, int cap);
/* shadow of a set of 3D points (projected along the sun onto the ground) */
void shadow_hull(const float *pts, int n, float gy);
void ao_blob(float x, float z, float r, float strength);

/* lights (baked + glows) */
void add_light(float x, float y, float z, float r, uint32_t c, float glow);

void scenery_plan(void);            /* choose every prop and add the lights (before the ground is baked) */
void scenery_build(void);
float scenery_ao(float x, float z);         /* 0.45..1: how much nearby props shade the ground */           /* the props' meshes and shadows */

#endif
