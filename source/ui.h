/*
 * RACING LEGENDS - 2D drawing for the HUD and the menus (854 x 480 virtual screen, y down) and the
 * italic, chamfered "racing" stroke font.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "r3d.h"

void ui_begin(int mode);                         /* switch to 2D with blend mode RM_ALPHA or RM_ADD */

void ui_tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2);
void ui_quad(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
             float x2, float y2, uint32_t c2, float x3, float y3, uint32_t c3);
void ui_rect(float x, float y, float w, float h, uint32_t c);
void ui_rect_v(float x, float y, float w, float h, uint32_t top, uint32_t bot);
void ui_rect_h(float x, float y, float w, float h, uint32_t left, uint32_t right);
void ui_rrect(float x, float y, float w, float h, float r, uint32_t c);
void ui_rrect_v(float x, float y, float w, float h, float r, uint32_t top, uint32_t bot);
/* slanted (parallelogram) bar, the racing-game panel shape: skew = horizontal shift of the top edge */
void ui_slant(float x, float y, float w, float h, float skew, uint32_t c);
void ui_slant_h(float x, float y, float w, float h, float skew, uint32_t left, uint32_t right);
void ui_circle(float cx, float cy, float r, uint32_t c);
void ui_glow(float cx, float cy, float r, uint32_t c);
void ui_ring(float cx, float cy, float r, float th, uint32_t c);
void ui_arc(float cx, float cy, float r0, float r1, float a0, float a1, int n, uint32_t c0, uint32_t c1);
void ui_line(float x0, float y0, float x1, float y1, float w, uint32_t c);

/* text: size = cap height in pixels */
float ui_text_w(const char *s, float size);
void  ui_text(const char *s, float x, float y, float size, uint32_t c);          /* top-left */
void  ui_text_c(const char *s, float cx, float y, float size, uint32_t c);       /* centred */
void  ui_text_r(const char *s, float rx, float y, float size, uint32_t c);       /* right aligned */
/* with a soft dark drop shadow */
void  ui_text_s(const char *s, float x, float y, float size, uint32_t c);
void  ui_text_sc(const char *s, float cx, float y, float size, uint32_t c);
void  ui_text_sr(const char *s, float rx, float y, float size, uint32_t c);
/* stroke weight multiplier for the next calls (1 = regular, ~1.4 = heavy) */
void  ui_text_weight(float w);

#endif
