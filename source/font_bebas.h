/* the HUD / menu font: Bebas Neue (SIL Open Font License 1.1) baked into an alpha atlas */
#ifndef FONT_BEBAS_H
#define FONT_BEBAS_H
#define FONT_TEX_W 1024
#define FONT_TEX_H 512
typedef struct { short x0, y0, x1, y1, ox, oy; float adv; } FontGlyph;   /* atlas rect, offset from the pen at the baseline, advance (px at 64 px) */
extern const float FONT_CAP_H;
extern const FontGlyph FONT_GLYPH[2][95];
extern const unsigned char FONT_TEX[FONT_TEX_W * FONT_TEX_H];
#endif
