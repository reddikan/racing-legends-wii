/*
 * RACING LEGENDS - a small platform-independent 3D renderer.
 *
 * Everything is vertex-coloured triangles (lighting is baked into the colours or computed on the CPU).
 * The game fills a command list every frame:
 *   - static meshes (the track, the terrain, the scenery), built once per track and binned into a grid of
 *     chunks; the Wii turns each chunk into a GPU display list, so drawing the world costs the CPU nothing
 *   - dynamic triangles (cars, particles, glows), written into a vertex buffer every frame
 *   - a 2D layer on top (the HUD and the menus) in an 854 x 480 virtual screen
 * On the Wii, main.c sends the list to GX; the native test harness rasterizes it in software.
 */
#ifndef R3D_H
#define R3D_H

#include <stdint.h>

#define VW 854.0f
#define VH 480.0f

/* draw modes */
enum {
    RM_OPAQUE,     /* depth test + write, no blending */
    RM_SHADOW,     /* alpha blend, depth test LESS + write: overlapping shadows darken only once */
    RM_ALPHA,      /* alpha blend, depth test, no depth write */
    RM_ADD,        /* additive (src * alpha + dst), depth test, no write */
    RM_MUL2,       /* dst = 2 * src * dst (lighting), depth test, no write */
    RM_GROUND,     /* opaque, plus a world-mapped detail texture */
    RM_COUNT
};

typedef struct { float x, y, z; uint32_t c; } RV;      /* c = 0xRRGGBBAA */

enum { RC_TRIS, RC_MESH, RC_2D, RC_POST, RC_TEXT };
typedef struct { uint8_t type, mode; uint16_t mesh; int start, count; } RCmd;

#define R_MAX_VTX  120000
#define R_MAX_CMD  4096

extern RV   *r_v;            /* dynamic vertices */
typedef struct { float x, y, u, v; uint32_t c; } TV;     /* textured 2D vertex (the font atlas) */
#define R_MAX_TVTX 60000
extern TV   *r_tv;
extern int   r_ntv;
void r3d_text_tri(const TV *a, const TV *b, const TV *c);
extern int   r_nv;
extern RCmd  r_cmd[R_MAX_CMD];
extern int   r_ncmd;
extern uint32_t r_clear;

/* the camera (the platforms build their own matrices from these) */
typedef struct {
    float ex, ey, ez;        /* eye */
    float tx, ty, tz;        /* target */
    float fovy;              /* degrees */
    float znear, zfar;
    /* derived by r3d_camera: the combined view-projection (row major, column vectors) */
    float vp[16];
    float fx, fz, rx, rz;    /* ground-plane forward/right of the view, for culling */
} RCam;
extern RCam r_cam;

/* tilt-shift (miniature) post effect: blur strength at the top / bottom of the screen */
extern float r_post_top, r_post_bot;
/* the film look: RGB split strength (1 = normal), set per frame */
extern float r_fx_split;

/* textures shared by the platforms: the ground's detail (asphalt grain and mottling; 128 = no change, the
 * renderer doubles it) and the film grain */
#define R_DETAIL_SIZE 256
#define R_DETAIL_TEXELS_PER_M 1.6f
#define R_GRAIN_SIZE 128
void r3d_make_detail(uint8_t *out);           /* R_DETAIL_SIZE^2 bytes */
void r3d_make_grain(uint8_t *out);            /* R_GRAIN_SIZE^2 bytes, mean 128 */

/* colour helpers */
static inline uint32_t RGBA(int r, int g, int b, int a)
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    a = a < 0 ? 0 : (a > 255 ? 255 : a);
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}
static inline uint32_t col_a(uint32_t c, float a)
{
    int al = (int)((float)(c & 255) * a);
    al = al < 0 ? 0 : (al > 255 ? 255 : al);
    return (c & 0xFFFFFF00u) | (uint32_t)al;
}
static inline uint32_t col_seta(uint32_t c, int a) { a = a < 0 ? 0 : (a > 255 ? 255 : a); return (c & 0xFFFFFF00u) | (uint32_t)a; }
uint32_t col_mix(uint32_t a, uint32_t b, float t);
uint32_t col_scale(uint32_t c, float k);              /* rgb * k (alpha kept) */
uint32_t col_mul(uint32_t a, uint32_t b);             /* per channel product */
uint32_t col_hsv(float h, float s, float v, int a);

/* frame */
void r3d_begin(uint32_t clear);
void r3d_camera(float ex, float ey, float ez, float tx, float ty, float tz, float fovy);
int  r3d_project(float x, float y, float z, float *sx, float *sy);   /* world -> virtual screen; 0 if behind */
void r3d_mode(int mode);
void r3d_tri(const RV *a, const RV *b, const RV *c);
void r3d_tri3(float x0, float y0, float z0, uint32_t c0, float x1, float y1, float z1, uint32_t c1,
              float x2, float y2, float z2, uint32_t c2);
void r3d_quad(const RV *a, const RV *b, const RV *c, const RV *d);
void r3d_mesh(int id, int mode);
void r3d_post(void);                                   /* apply the tilt-shift to what's been drawn */
/* a camera-facing glow (additive): a soft disc of radius r in world units */
void r3d_glow(float x, float y, float z, float r, uint32_t c);
/* a ground-hugging soft blob in the current mode (shadows, light pools) */
void r3d_blob(float x, float y, float z, float rx, float rz, float ang, uint32_t cin, uint32_t cout);

/* ---- static meshes, binned into a grid of chunks ---- */
#define SM_CELL 48.0f
#define SM_MAX_MESH 2048
void sm_reset(float minx, float minz, float maxx, float maxz);
void sm_tri(int mode, const RV *a, const RV *b, const RV *c);
void sm_quad(int mode, const RV *a, const RV *b, const RV *c, const RV *d);
void sm_finish(void);                     /* upload every chunk (calls plat_mesh_create) */
void sm_cull(float margin);              /* decide which chunks are on screen (once per frame) */
void sm_draw(float margin);               /* queue the chunks the camera can see, mode by mode */
void sm_draw_mode(int mode, float margin);
int  sm_tri_count(void);
int  sm_visible_tris(void);               /* static triangles queued this frame */

/* implemented by the platform */
int  plat_mesh_create(const RV *v, int nverts);       /* returns a mesh id */
void plat_mesh_reset(void);
void *big_alloc(unsigned long bytes);                  /* CPU-only working memory */

#endif
