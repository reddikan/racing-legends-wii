/*
 * RACING LEGENDS - particles and skid marks. See fx.h.
 */
#include "fx.h"
#include "r3d.h"
#include "util.h"

#include <string.h>

typedef struct { float x, y, z, vx, vy, vz, life, max, size, rot; uint32_t c; uint8_t type; } Part;
#define MAXP 1400
static Part parts[MAXP];
static int np;

void fx_reset(void) { np = 0; skid_break(-1); }

void fx_spawn(int type, float x, float y, float z, float vx, float vy, float vz, float life, float size, uint32_t c)
{
    if (np >= MAXP) {
        /* replace the oldest-looking slot */
        int k = (int)(hash32((uint32_t)np * 7919u + (uint32_t)(x * 10)) % MAXP);
        parts[k] = parts[--np];
    }
    Part *p = &parts[np++];
    p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz;
    p->life = life; p->max = life; p->size = size; p->c = c; p->type = (uint8_t)type;
    p->rot = (float)(np * 37 % 100) * 0.0628f;
}

void fx_update(float dt)
{
    for (int i = 0; i < np; i++) {
        Part *p = &parts[i];
        p->life -= dt;
        if (p->life <= 0) { parts[i--] = parts[--np]; continue; }
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        switch (p->type) {
        case PT_SMOKE: case PT_DUST: case PT_SNOW:
            p->vx *= 1.0f - 1.6f * dt; p->vz *= 1.0f - 1.6f * dt; p->vy = p->vy * (1.0f - 1.0f * dt) + 0.4f * dt;
            p->size += dt * (p->type == PT_SMOKE ? 2.4f : 1.8f);
            break;
        case PT_SPARK:
            p->vy -= 20.0f * dt;
            if (p->y < -100) p->life = 0;
            break;
        case PT_FLAME:
            p->vx *= 1.0f - 4.0f * dt; p->vz *= 1.0f - 4.0f * dt; p->size *= 1.0f - 1.5f * dt;
            break;
        case PT_CONFETTI:
            p->vy -= 6.0f * dt; p->vx *= 1.0f - 0.8f * dt; p->vz *= 1.0f - 0.8f * dt; p->rot += dt * 8.0f;
            break;
        default: break;
        }
    }
}

/* a soft billboard disc facing the camera */
static void puff(const Part *p, float a)
{
    float s = p->size;
    float fx = r_cam.tx - r_cam.ex, fy = r_cam.ty - r_cam.ey, fz = r_cam.tz - r_cam.ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = -fz, sz = fx, sl = sqrtf(sx * sx + sz * sz); sx /= sl; sz /= sl;
    float ux = -sz * fy, uy = sz * fx - sx * fz, uz = sx * fy;
    uint32_t ci = col_a(p->c, a), co = ci & 0xFFFFFF00u;
    const int N = 6;
    float px = p->x + sx * s, py = p->y, pz = p->z + sz * s;
    for (int i = 1; i <= N; i++) {
        float t = (float)i / (float)N * TAU + p->rot, ca = cosf(t), sa = sinf(t);
        if (i == 1) { float t0 = p->rot; px = p->x + (sx * cosf(t0) + ux * sinf(t0)) * s; py = p->y + uy * sinf(t0) * s; pz = p->z + (sz * cosf(t0) + uz * sinf(t0)) * s; }
        float qx = p->x + (sx * ca + ux * sa) * s, qy = p->y + uy * sa * s, qz = p->z + (sz * ca + uz * sa) * s;
        r3d_tri3(p->x, p->y, p->z, ci, px, py, pz, co, qx, qy, qz, co);
        px = qx; py = qy; pz = qz;
    }
}

void fx_draw_alpha(void)
{
    r3d_mode(RM_ALPHA);
    for (int i = 0; i < np; i++) {
        const Part *p = &parts[i];
        float t = p->life / p->max;
        if (p->type == PT_SMOKE || p->type == PT_DUST || p->type == PT_SNOW) {
            float a = t * (1.0f - t) * 4.0f;
            a = a > 1 ? 1 : a;
            puff(p, a * (p->type == PT_SMOKE ? 0.55f : 0.7f));
        } else if (p->type == PT_CONFETTI) {
            float s = 0.22f, c = cosf(p->rot) * s, sn = sinf(p->rot) * s;
            uint32_t col = col_a(p->c, sat(t * 3.0f));
            r3d_tri3(p->x - c, p->y - sn, p->z, col, p->x + c, p->y + sn, p->z, col, p->x, p->y + s * 0.5f, p->z + c, col);
        }
    }
}

void fx_draw_add(void)
{
    r3d_mode(RM_ADD);
    for (int i = 0; i < np; i++) {
        const Part *p = &parts[i];
        float t = p->life / p->max;
        if (p->type == PT_SPARK) {
            /* a streak along the velocity */
            float k = 0.03f;
            float ex = p->x - p->vx * k, ey = p->y - p->vy * k, ez = p->z - p->vz * k;
            float w = p->size;
            uint32_t c = col_a(p->c, t), ce = col_a(p->c, 0);
            r3d_tri3(p->x - w, p->y, p->z, c, p->x + w, p->y, p->z, c, ex, ey, ez, ce);
            r3d_tri3(p->x, p->y - w, p->z, c, p->x, p->y + w, p->z, c, ex, ey, ez, ce);
        } else if (p->type == PT_FLAME || p->type == PT_GLINT) {
            r3d_glow(p->x, p->y, p->z, p->size, col_a(p->c, t));
        }
    }
}

/* ------------------------------------------------------------------ */
/* skid marks: a ring buffer of quads                                  */
/* ------------------------------------------------------------------ */
typedef struct { float x[4], y, z[4]; float a; } Skid;
#define MAXS 900
static Skid skids[MAXS];
static int ns_head, ns_count;
#define MAXW 32
static float last_x[MAXW], last_z[MAXW], last_y[MAXW], last_hx[MAXW], last_hz[MAXW];
static uint8_t has_last[MAXW];

void skid_break(int id)
{
    if (id < 0) { memset(has_last, 0, sizeof has_last); ns_count = 0; ns_head = 0; return; }
    if (id < MAXW) has_last[id] = 0;
}

void skid_add(int id, float x, float y, float z, float hx, float hz, float w, float strength)
{
    if (id < 0 || id >= MAXW) return;
    /* hx, hz: the direction across the mark (unit) */
    if (has_last[id]) {
        float dx = x - last_x[id], dz = z - last_z[id];
        float d2 = dx * dx + dz * dz;
        if (d2 < 0.9f * 0.9f) return;
        if (d2 < 5.0f * 5.0f) {
            Skid *s = &skids[ns_head];
            ns_head = (ns_head + 1) % MAXS;
            if (ns_count < MAXS) ns_count++;
            s->x[0] = last_x[id] - last_hx[id] * w; s->z[0] = last_z[id] - last_hz[id] * w;
            s->x[1] = last_x[id] + last_hx[id] * w; s->z[1] = last_z[id] + last_hz[id] * w;
            s->x[2] = x + hx * w; s->z[2] = z + hz * w;
            s->x[3] = x - hx * w; s->z[3] = z - hz * w;
            s->y = (y + last_y[id]) * 0.5f + 0.06f;
            s->a = strength;
        }
    }
    last_x[id] = x; last_z[id] = z; last_y[id] = y; last_hx[id] = hx; last_hz[id] = hz; has_last[id] = 1;
}

void skid_draw(void)
{
    r3d_mode(RM_ALPHA);
    float cx = r_cam.tx, cz = r_cam.tz;
    for (int i = 0; i < ns_count; i++) {
        int idx = (ns_head - 1 - i + MAXS) % MAXS;
        const Skid *s = &skids[idx];
        float dx = s->x[0] - cx, dz = s->z[0] - cz;
        if (dx * dx + dz * dz > 90.0f * 90.0f) continue;
        float age = 1.0f - (float)i / (float)MAXS;
        uint32_t c = RGBA(20, 20, 24, (int)(s->a * age * 120.0f));
        RV a = { s->x[0], s->y, s->z[0], c }, b = { s->x[1], s->y, s->z[1], c }, d = { s->x[2], s->y, s->z[2], c }, e = { s->x[3], s->y, s->z[3], c };
        r3d_quad(&a, &b, &d, &e);
    }
}
