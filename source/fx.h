/* RACING LEGENDS - particles (smoke, dust, sparks, flames, confetti) and skid marks */
#ifndef FX_H
#define FX_H

#include <stdint.h>

enum { PT_SMOKE, PT_DUST, PT_SNOW, PT_SPARK, PT_FLAME, PT_CONFETTI, PT_GLINT };

void fx_reset(void);
void fx_spawn(int type, float x, float y, float z, float vx, float vy, float vz, float life, float size, uint32_t c);
void fx_update(float dt);
void fx_draw_alpha(void);           /* smoke, dust, confetti */
void fx_draw_add(void);             /* sparks, flames */
/* skid marks: call every frame per wheel pair while sliding; id identifies the wheel (car*4 + wheel) */
void skid_add(int id, float x, float y, float z, float hx, float hz, float w, float strength);
void skid_break(int id);
void skid_draw(void);

#endif
