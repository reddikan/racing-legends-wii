/* quick track viewer: renders each track from a few camera positions */
#include "../source/r3d.h"
#include "../source/track.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
void swr_render(void);
void swr_save_ppm(const char *path);
extern long swr_tris_drawn;
int main(int argc, char **argv)
{
    int only = argc > 1 ? atoi(argv[1]) : -1;
    for (int t = 0; t < 5; t++) {
        if (only >= 0 && t != only) continue;
        track_load(t);
        printf("track %d: %d samples, %.0f m, %d static tris, %d lights\n", t, trk.n, trk.length, sm_tri_count(), trk.nlight);
        float ds[4] = { 0.0f, trk.length * 0.27f, trk.length * 0.55f, trk.length * 0.8f };
        for (int v = 0; v < 4; v++) {
            int k = track_sample_at(ds[v]);
            const TSample *s = &trk.s[k];
            float tx = s->x + s->tx * 8, tz = s->z + s->tz * 8, ty = s->y;
            float ex = tx - s->tx * 30, ez = tz - s->tz * 30, ey = ty + 44;
            r3d_begin(trk.info->clear);
            r3d_camera(ex, ey, ez, tx, ty, tz, 40.0f);
            track_draw_world();
            sm_draw_mode(RM_OPAQUE, 0);
            sm_draw_mode(RM_ALPHA, 0);
            sm_draw_mode(RM_SHADOW, 0);
            track_draw_glows(1.0f);
            swr_render();
            char p[128]; sprintf(p, "tools/out/view_%d_%d.ppm", t, v);
            swr_save_ppm(p);
            printf("  view %d: %d visible static tris, %ld rasterized\n", v, sm_visible_tris(), swr_tris_drawn);
        }
    }
    return 0;
}
