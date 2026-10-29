/* RACING LEGENDS - small maths helpers */
#ifndef UTIL_H
#define UTIL_H

#include <math.h>
#include <stdint.h>

#define PI  3.14159265f
#define TAU 6.2831853f

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sat(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float smooth01(float x) { x = sat(x); return x * x * (3 - 2 * x); }
static inline float smoothstep(float a, float b, float x) { return smooth01((x - a) / (b - a)); }
static inline float signf(float x) { return x < 0 ? -1.0f : 1.0f; }
static inline float approach(float x, float target, float rate)
{
    if (x < target) { x += rate; if (x > target) x = target; }
    else { x -= rate; if (x < target) x = target; }
    return x;
}
static inline float wrap_angle(float a)
{
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
/* frame-rate independent exponential smoothing factor */
static inline float damp(float rate, float dt) { return 1.0f - expf(-rate * dt); }

static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static inline float hash2f(int x, int y, uint32_t seed)       /* 0..1 */
{
    return (float)(hash32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ seed) >> 8) / 16777216.0f;
}
/* smooth value noise, 0..1 */
static inline float vnoise(float x, float y, uint32_t seed)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2f(ix, iy, seed), b = hash2f(ix + 1, iy, seed), c = hash2f(ix, iy + 1, seed), d = hash2f(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
static inline float fbm(float x, float y, uint32_t seed)       /* ~0..1 */
{
    return vnoise(x, y, seed) * 0.55f + vnoise(x * 2.1f, y * 2.1f, seed + 7) * 0.28f + vnoise(x * 4.3f, y * 4.3f, seed + 13) * 0.17f;
}

/* simple xorshift rng */
typedef struct { uint32_t s; } Rng;
static inline uint32_t rng_u(Rng *r) { r->s ^= r->s << 13; r->s ^= r->s >> 17; r->s ^= r->s << 5; return r->s; }
static inline float rng_f(Rng *r) { return (float)(rng_u(r) >> 8) / 16777216.0f; }          /* 0..1 */
static inline float rng_r(Rng *r, float a, float b) { return a + (b - a) * rng_f(r); }

#endif
