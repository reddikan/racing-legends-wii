/*
 * RACING LEGENDS - Nintendo Wii platform layer (devkitPPC + libogc + libfat).
 *
 *   video : 3D through GX - the track's static chunks are GPU display lists (in MEM2), craft / particles /
 *           glows are sent each frame, the HUD is a 2D layer on top (text from a Bebas Neue atlas). Depth
 *           buffered, alpha / additive / 2x-multiply blending, a detail texture on the ground, then the film
 *           look from copies of the frame: bloom, vignette, tilt-shift, soft focus, halation, RGB split,
 *           lifted blacks and grain. 60 fps, 16:9 anamorphic or letterboxed 4:3
 *   input : Wii Classic Controller (in Wii Remote 1); a GameCube controller works too
 *   audio : ASND - ten looping voices (engines, tyres, nitro, wind...) whose pitch and volume follow the
 *           game every frame, six voices for one-shot effects
 *   save  : sd:/apps/racinglegends/save.dat
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <sys/stat.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <asndlib.h>
#include <fat.h>

#include "game.h"
#include "r3d.h"
#include "ui.h"
#include "audio.h"
#include "font_bebas.h"

#define FIFO_SIZE (1024 * 1024)
#define SAVE_DIR  "sd:/apps/racinglegends"
#define SAVE_PATH "sd:/apps/racinglegends/save.dat"

/* CPU-only working buffers (and the display lists) go to MEM2 */
void *big_alloc(unsigned long bytes)
{
    u32 lo = ((u32)SYS_GetArena2Lo() + 31) & ~31u;
    SYS_SetArena2Lo((void *)(lo + ((bytes + 31) & ~31ul)));
    return (void *)lo;
}

static GXRModeObj *rmode;
static void       *xfb[2];
static int         fbi;
static int         wide = 1;
static volatile int quit;

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */
static void set_view(void)
{
    float w = (float)rmode->fbWidth, h = (float)rmode->efbHeight;
    if (wide) {
        GX_SetViewport(0, 0, w, h, 0, 1);
        GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    } else {
        float bh = h * 0.75f, top = (h - bh) * 0.5f;
        GX_SetViewport(0, top, w, bh, 0, 1);
        GX_SetScissor(0, (u32)top, rmode->fbWidth, (u32)bh);
    }
}

static void vtx_plain(void)
{
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_InvVtxCache();
}

static void video_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    if ((rmode->viTVMode >> 2) == VI_PAL) rmode = &TVEurgb60Hz480IntDf;     /* always 60 Hz */
    rmode->viWidth   = 678;
    rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - rmode->viWidth) / 2;

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    void *fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GXColor black = { 0, 0, 0, 255 };
    GX_SetCopyClear(black, 0x00FFFFFF);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbh = GX_SetDispCopyYScale(yscale);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbh);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDither(GX_ENABLE);
    GX_SetZCompLoc(GX_TRUE);
    set_view();

    vtx_plain();
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_InvalidateTexAll();

    /* matrix 1: the 2D layer (virtual 854 x 480, y down) */
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX1);
}

/* ------------------------------------------------------------------ */
/* static meshes: GPU display lists in MEM2                            */
/* ------------------------------------------------------------------ */
#define DL_POOL (15u * 1024u * 1024u)
#define MAX_MESH 2048
typedef struct { void *dl; u32 len; const RV *imm; int n; } Mesh;
static Mesh meshes[MAX_MESH];
static int nmesh;
static u8 *dl_pool;
static u32 dl_used;

void plat_mesh_reset(void)
{
    GX_DrawDone();              /* the GPU must be idle before the old lists are overwritten */
    nmesh = 0;
    dl_used = 0;
}

/* The lists are written byte for byte as the GPU reads them (the same stream GX_Begin / GX_Position3f32 /
 * GX_Color1u32 would send): 0x90 | format = triangles, a 16-bit count, then x y z (f32) and RGBA8 per
 * vertex, padded with NOPs to 32 bytes. Building them directly avoids redirecting the CPU FIFO. */
int plat_mesh_create(const RV *v, int n)
{
    if (nmesh >= MAX_MESH || n < 3) return -1;
    if (!dl_pool) dl_pool = (u8 *)big_alloc(DL_POOL);
    n -= n % 3;
    u32 batches = (u32)(n / 65520 + 1);
    u32 size = ((u32)n * 16u + batches * 3u + 31u) & ~31u;
    Mesh *m = &meshes[nmesh];
    m->imm = NULL; m->dl = NULL; m->len = 0; m->n = n;
    if (dl_used + size <= DL_POOL) {
        u8 *buf = dl_pool + dl_used, *w = buf;
        int i = 0;
        while (i < n) {
            int k = n - i;
            if (k > 65520) k = 65520;
            *w++ = (u8)(GX_TRIANGLES | (GX_VTXFMT0 & 7));
            *w++ = (u8)(k >> 8);
            *w++ = (u8)(k & 255);
            for (int j = 0; j < k; j++) {
                const RV *p = &v[i + j];
                memcpy(w, &p->x, 4); memcpy(w + 4, &p->y, 4); memcpy(w + 8, &p->z, 4);   /* big-endian floats */
                w[12] = (u8)(p->c >> 24); w[13] = (u8)(p->c >> 16); w[14] = (u8)(p->c >> 8); w[15] = (u8)p->c;
                w += 16;
            }
            i += k;
        }
        while ((u32)(w - buf) < size) *w++ = 0;       /* GX_NOP */
        DCFlushRange(buf, size);
        m->dl = buf; m->len = size;
        dl_used += size;
        return nmesh++;
    }
    /* no room: keep a copy and draw it directly */
    RV *copy = (RV *)big_alloc(sizeof(RV) * (unsigned long)n);
    memcpy(copy, v, sizeof(RV) * (size_t)n);
    m->imm = copy;
    return nmesh++;
}

/* ------------------------------------------------------------------ */
/* textures: the font atlas, the ground detail, film grain, and the   */
/* copies of the frame the post-processing works from                  */
/* ------------------------------------------------------------------ */
static GXTexObj to_font, to_detail, to_grain;
static void *tex_full, *tex_half, *tex_q, *tex_bq;
static GXTexObj to_full, to_half, to_q, to_bq;
static int cur_2d = -1, cur_mode = -1, cur_tev = -1;
static int post_bloomed, post_grunged, have_q;
static u32 frame_no;

/* a linear 8-bit image into the GPU's tiled I8 layout (8 x 4 texel blocks) */
static void *make_i8(const u8 *src, int w, int h)
{
    u8 *dst = memalign(32, (size_t)(w * h));
    int k = 0;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 8)
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 8; x++) dst[k++] = src[(by + y) * w + bx + x];
    DCFlushRange(dst, (u32)(w * h));
    return dst;
}

static void textures_init(void)
{
    void *f = make_i8(FONT_TEX, FONT_TEX_W, FONT_TEX_H);
    GX_InitTexObj(&to_font, f, FONT_TEX_W, FONT_TEX_H, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&to_font, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    static u8 raw[R_DETAIL_SIZE * R_DETAIL_SIZE];
    r3d_make_detail(raw);
    void *d = make_i8(raw, R_DETAIL_SIZE, R_DETAIL_SIZE);
    GX_InitTexObj(&to_detail, d, R_DETAIL_SIZE, R_DETAIL_SIZE, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GX_InitTexObjLOD(&to_detail, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    static u8 gr[R_GRAIN_SIZE * R_GRAIN_SIZE];
    r3d_make_grain(gr);
    void *gt = make_i8(gr, R_GRAIN_SIZE, R_GRAIN_SIZE);
    GX_InitTexObj(&to_grain, gt, R_GRAIN_SIZE, R_GRAIN_SIZE, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GX_InitTexObjLOD(&to_grain, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);

    tex_full = memalign(32, GX_GetTexBufferSize(640, 528, GX_TF_RGBA8, GX_FALSE, 0));
    tex_half = memalign(32, GX_GetTexBufferSize(320, 264, GX_TF_RGB565, GX_FALSE, 0));
    tex_q    = memalign(32, GX_GetTexBufferSize(160, 132, GX_TF_RGB565, GX_FALSE, 0));
    tex_bq   = memalign(32, GX_GetTexBufferSize(160, 132, GX_TF_RGB565, GX_FALSE, 0));

    Mtx m;
    guMtxIdentity(m);
    GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
    /* the ground's detail: texture coordinates straight from world x and z */
    float k = R_DETAIL_TEXELS_PER_M / (float)R_DETAIL_SIZE;
    Mtx gm = { { k, 0, 0, 0 }, { 0, 0, k, 0 }, { 0, 0, 0, 1 } };
    GX_LoadTexMtxImm(gm, GX_TEXMTX1, GX_MTX2x4);
}

/* ---- TEV setups ---- */
enum { TEV_PLAIN, TEV_GROUND, TEV_TEXT, TEV_TEXMOD, TEV_THRESH };

static void vtx_tex(void)
{
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_InvVtxCache();
}

static void set_tev(int t)
{
    if (t == cur_tev) return;
    cur_tev = t;
    GX_SetNumTevStages(1);
    switch (t) {
    case TEV_PLAIN:
        vtx_plain();
        break;
    case TEV_GROUND:
        /* vertex colour x detail x 2 (128 in the texture = unchanged) */
        vtx_plain();
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX1);
        GX_LoadTexObj(&to_detail, GX_TEXMAP0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        break;
    case TEV_TEXT:
        /* the atlas is coverage: colour from the vertex, alpha = vertex alpha x coverage */
        vtx_tex();
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
        GX_LoadTexObj(&to_font, GX_TEXMAP0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        break;
    case TEV_TEXMOD:
        /* texture x vertex colour (post-processing quads); the texture is loaded by the caller */
        vtx_tex();
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        break;
    case TEV_THRESH:
        /* bright pass: (texture - K0) x 2, then x vertex colour */
        vtx_tex();
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
        GX_SetNumTevStages(2);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_KONST, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        break;
    }
}

static void load_3d(void);
static void load_2d(void);

/* ---- frame copies ---- */
static u32 efb_top(void) { if (wide) return 0; u32 h = rmode->efbHeight, bh = (h * 3 / 4) & ~3u; return ((h - bh) / 2) & ~1u; }
static u32 efb_h(void) { u32 h = rmode->efbHeight; return wide ? h : ((h * 3 / 4) & ~3u); }

static void copy_frame(void *dst, GXTexObj *to, int shrink, u8 fmt, int quadrant)
{
    u32 w = rmode->fbWidth, h = efb_h(), top = efb_top();
    if (quadrant) { w /= 2; h /= 2; }
    GX_SetTexCopySrc(0, (u16)top, (u16)w, (u16)h);
    u32 dw = shrink ? w / 2 : w, dh = shrink ? h / 2 : h;
    GX_SetTexCopyDst((u16)dw, (u16)dh, fmt, shrink ? GX_TRUE : GX_FALSE);
    GX_CopyTex(dst, GX_FALSE);
    GX_PixModeSync();
    GX_InvalidateTexAll();
    GX_InitTexObj(to, dst, (u16)dw, (u16)dh, fmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(to, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

/* a textured quad in virtual screen coordinates */
static void tq(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 c)
{
    GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
    GX_Position3f32(x0, y0, 0); GX_Color1u32(c); GX_TexCoord2f32(u0, v0);
    GX_Position3f32(x1, y0, 0); GX_Color1u32(c); GX_TexCoord2f32(u1, v0);
    GX_Position3f32(x1, y1, 0); GX_Color1u32(c); GX_TexCoord2f32(u1, v1);
    GX_Position3f32(x0, y1, 0); GX_Color1u32(c); GX_TexCoord2f32(u0, v1);
    GX_End();
}

static void post_begin(void)
{
    if (cur_2d != 1) load_2d();
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    cur_mode = -1;
}

static void blend_alpha(void) { GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); }
static void blend_add(void)   { GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR); }
static void blend_none(void)  { GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR); }

/* the frame, shrunk to a quarter (tex_q) through the top-left quadrant of the EFB; the quadrant is put back
 * from tex_full, which must hold the current frame */
static void make_quarter(int bright, void *dst, GXTexObj *to)
{
    float hw = VW * 0.5f, hh = VH * 0.5f;
    if (bright) {
        GXColor k = { 0xD0, 0xD0, 0xD0, 0xFF };          /* threshold ~0.82 */
        GX_SetTevKColor(GX_KCOLOR0, k);
        set_tev(TEV_THRESH);
    } else set_tev(TEV_TEXMOD);
    GX_LoadTexObj(&to_half, GX_TEXMAP0);
    blend_none();
    tq(0, 0, hw, hh, 0, 0, 1, 1, 0xFFFFFFFFu);
    copy_frame(dst, to, 1, GX_TF_RGB565, 1);
    /* put the quadrant back */
    set_tev(TEV_TEXMOD);
    GX_LoadTexObj(&to_full, GX_TEXMAP0);
    tq(0, 0, hw, hh, 0, 0, 0.5f, 0.5f, 0xFFFFFFFFu);
}

/* bloom and the vignette: once a frame, before the tilt-shift and the HUD */
static void post_bloom(void)
{
    post_bloomed = 1;
    post_begin();
    copy_frame(tex_full, &to_full, 0, GX_TF_RGBA8, 0);
    copy_frame(tex_half, &to_half, 1, GX_TF_RGB565, 0);
    make_quarter(1, tex_bq, &to_bq);
    /* the bright parts, blurred (five taps of the quarter-size copy) and added back */
    set_tev(TEV_TEXMOD);
    GX_LoadTexObj(&to_bq, GX_TEXMAP0);
    blend_add();
    const float o = 1.6f / 160.0f, p = 1.6f / 120.0f;
    const float du[5] = { 0, -o, o, -o, o }, dv[5] = { 0, -p, -p, p, p };
    for (int i = 0; i < 5; i++) tq(0, 0, VW, VH, du[i], dv[i], 1 + du[i], 1 + dv[i], 0xFFFFFF33u);
    /* vignette: multiply by a soft falloff */
    set_tev(TEV_PLAIN);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_CLEAR);
    const int MX = 12, MY = 8;
    for (int j = 0; j < MY; j++) {
        GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)((MX + 1) * 2));
        for (int i = 0; i <= MX; i++)
            for (int r = 0; r < 2; r++) {
                float x = VW * (float)i / MX, y = VH * (float)(j + r) / MY;
                float dx = (x / VW - 0.5f) * 2, dy = (y / VH - 0.5f) * 2;
                float v = 1.0f - 0.22f * (dx * dx * 0.7f + dy * dy);
                u8 c = (u8)(v * 255.0f);
                GX_Position3f32(x, y, 0); GX_Color4u8(c, c, c, 255);
            }
        GX_End();
    }
}

/* the miniature look: sharp around the craft, two levels of blur growing towards the top and bottom */
static void post_tiltshift(void)
{
    if (!post_bloomed) post_bloom();
    post_begin();
    copy_frame(tex_full, &to_full, 0, GX_TF_RGBA8, 0);
    copy_frame(tex_half, &to_half, 1, GX_TF_RGB565, 0);
    make_quarter(0, tex_q, &to_q);
    have_q = 1;
    set_tev(TEV_TEXMOD);
    blend_alpha();
    const int MY = 24;
    for (int pass = 0; pass < 2; pass++) {
        GX_LoadTexObj(pass ? &to_q : &to_half, GX_TEXMAP0);
        GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT1, (u16)((MY + 1) * 2));
        for (int j = 0; j <= MY; j++) {
            float t = (float)j / (float)MY, y = VH * t, k = 0;
            if (t < 0.5f) k = r_post_top * (0.5f - t) / 0.5f;
            if (t > 0.84f) k = r_post_bot * (t - 0.84f) / 0.16f;
            k = k > 1 ? 1 : k;
            k = k * k * (3 - 2 * k);
            float a = pass ? (k - 0.5f) * 2.0f : k * 2.0f;
            a = a < 0 ? 0 : (a > 1 ? 1 : a);
            u8 al = (u8)(a * 255.0f);
            GX_Position3f32(0, y, 0); GX_Color4u8(255, 255, 255, al); GX_TexCoord2f32(0, t);
            GX_Position3f32(VW, y, 0); GX_Color4u8(255, 255, 255, al); GX_TexCoord2f32(1, t);
        }
        GX_End();
    }
}

/* the film: soft focus, halation, the RGB split, lifted blacks and grain (before the HUD) */
static void post_grunge(void)
{
    if (!post_bloomed) post_bloom();
    post_grunged = 1;
    post_begin();
    copy_frame(tex_full, &to_full, 0, GX_TF_RGBA8, 0);
    copy_frame(tex_half, &to_half, 1, GX_TF_RGB565, 0);
    if (!have_q) { make_quarter(0, tex_q, &to_q); have_q = 1; }
    /* soft focus: the half-size copy over the frame */
    set_tev(TEV_TEXMOD);
    GX_LoadTexObj(&to_half, GX_TEXMAP0);
    blend_alpha();
    tq(0, 0, VW, VH, 0, 0, 1, 1, 0xFFFFFF8Cu);
    /* halation: the bright neon bleeds a warm glow (the quarter-size copy, thresholded, tinted) */
    {
        GXColor k = { 0x8C, 0x8C, 0x8C, 0xFF };          /* threshold ~0.55 */
        GX_SetTevKColor(GX_KCOLOR0, k);
        set_tev(TEV_THRESH);
        GX_LoadTexObj(&to_q, GX_TEXMAP0);
        blend_add();
        const float o = 2.5f / 160.0f, p = 2.5f / 120.0f;
        tq(0, 0, VW, VH, -o, -p, 1 - o, 1 - p, 0xA06A8480u);
        tq(0, 0, VW, VH, o, p, 1 + o, 1 + p, 0xA06A8480u);
    }
    /* RGB split: rebuild the frame channel by channel, red pushed out, blue pulled in (more at the edges) */
    copy_frame(tex_full, &to_full, 0, GX_TF_RGBA8, 0);
    set_tev(TEV_PLAIN);
    blend_none();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(0, 0, 0); GX_Color1u32(0x000000FFu);
    GX_Position3f32(VW, 0, 0); GX_Color1u32(0x000000FFu);
    GX_Position3f32(VW, VH, 0); GX_Color1u32(0x000000FFu);
    GX_Position3f32(0, VH, 0); GX_Color1u32(0x000000FFu);
    GX_End();
    set_tev(TEV_TEXMOD);
    GX_LoadTexObj(&to_full, GX_TEXMAP0);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
    const int MX = 16, MY = 10;
    const u32 chan[3] = { 0xFF0000FFu, 0x00FF00FFu, 0x0000FFFFu };
    const float dir[3] = { 1.0f, 0.0f, -1.0f };
    for (int ch = 0; ch < 3; ch++)
        for (int j = 0; j < MY; j++) {
            GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT1, (u16)((MX + 1) * 2));
            for (int i = 0; i <= MX; i++)
                for (int r = 0; r < 2; r++) {
                    float u = (float)i / MX, v = (float)(j + r) / MY;
                    float dx = (u - 0.5f) * 2, dy = (v - 0.5f) * 2, r2 = dx * dx + dy * dy;
                    float k = (1.2f + 4.5f * r2) * r_fx_split * dir[ch];
                    /* sample from closer to the centre for red: the red image appears pushed outwards */
                    float su = u - dx * k / VW, sv = v - dy * k * 0.5f / VH;
                    GX_Position3f32(u * VW, v * VH, 0); GX_Color1u32(chan[ch]); GX_TexCoord2f32(su, sv);
                }
            GX_End();
        }
    /* lifted blacks: a faded, filmic floor */
    set_tev(TEV_PLAIN);
    blend_alpha();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(0, 0, 0); GX_Color1u32(0xFFFFFF09u);
    GX_Position3f32(VW, 0, 0); GX_Color1u32(0xFFFFFF09u);
    GX_Position3f32(VW, VH, 0); GX_Color1u32(0xFFFFFF09u);
    GX_Position3f32(0, VH, 0); GX_Color1u32(0xFFFFFF09u);
    GX_End();
    /* grain: dst x (1 + k (n - 0.5)), a new offset every frame */
    set_tev(TEV_TEXMOD);
    GX_LoadTexObj(&to_grain, GX_TEXMAP0);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_SRCALPHA, GX_LO_CLEAR);
    u32 hsh = frame_no * 2654435761u;
    float gu = (float)(hsh & 127) / 128.0f, gv = (float)((hsh >> 7) & 127) / 128.0f;
    float nu = (640.0f / (float)R_GRAIN_SIZE), nv = ((float)efb_h() / (float)R_GRAIN_SIZE);
    u8 kk = 56;                                         /* ~0.22 */
    tq(0, 0, VW, VH, gu, gv, gu + nu, gv + nv, ((u32)kk << 24) | ((u32)kk << 16) | ((u32)kk << 8) | (u32)(255 - kk / 2));
}

/* ------------------------------------------------------------------ */
/* drawing the command list                                            */
/* ------------------------------------------------------------------ */
static void load_3d(void)
{
    Mtx44 proj;
    guPerspective(proj, r_cam.fovy, VW / VH, r_cam.znear, r_cam.zfar);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
    Mtx view;
    guVector eye = { r_cam.ex, r_cam.ey, r_cam.ez }, up = { 0, 1, 0 }, at = { r_cam.tx, r_cam.ty, r_cam.tz };
    guLookAt(view, &eye, &up, &at);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    cur_2d = 0;
}

static void load_2d(void)
{
    Mtx44 proj;
    guOrtho(proj, 0, VH, 0, VW, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetCurrentMtx(GX_PNMTX1);
    cur_2d = 1;
}

static void set_mode(int mode, int is2d)
{
    set_tev(mode == RM_GROUND && !is2d ? TEV_GROUND : TEV_PLAIN);
    if (mode == cur_mode && is2d == cur_2d) return;
    if (is2d != cur_2d) { if (is2d) load_2d(); else load_3d(); }
    cur_mode = mode;
    switch (mode) {
    case RM_OPAQUE: case RM_GROUND: GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR); break;
    case RM_ADD:    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR); break;
    case RM_MUL2:   GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_SRCCLR, GX_LO_CLEAR); break;
    default:        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); break;
    }
    if (is2d) GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    else switch (mode) {
    case RM_OPAQUE: case RM_GROUND: GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE); break;
    case RM_SHADOW: GX_SetZMode(GX_TRUE, GX_LESS, GX_TRUE); break;
    default:        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE); break;
    }
}

static void send_tris(const RV *v, int n)
{
    int i = 0;
    while (i + 2 < n) {
        int k = n - i;
        if (k > 65520) k = 65520;
        k -= k % 3;
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)k);
        for (int j = 0; j < k; j++) {
            const RV *p = &v[i + j];
            GX_Position3f32(p->x, p->y, p->z);
            GX_Color1u32(p->c);
        }
        GX_End();
        i += k;
    }
}

static void send_text(const TV *v, int n)
{
    if (cur_2d != 1) load_2d();
    set_tev(TEV_TEXT);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    blend_alpha();
    cur_mode = -1;
    int i = 0;
    while (i + 2 < n) {
        int k = n - i;
        if (k > 65520) k = 65520;
        k -= k % 3;
        GX_Begin(GX_TRIANGLES, GX_VTXFMT1, (u16)k);
        for (int j = 0; j < k; j++) {
            const TV *p = &v[i + j];
            GX_Position3f32(p->x, p->y, 0);
            GX_Color1u32(p->c);
            GX_TexCoord2f32(p->u, p->v);
        }
        GX_End();
        i += k;
    }
}

static void draw_list(void)
{
    cur_2d = -1; cur_mode = -1; cur_tev = -1;
    post_bloomed = post_grunged = have_q = 0;
    frame_no++;
    int any3d = 0;
    for (int ci = 0; ci < r_ncmd; ci++) {
        const RCmd *c = &r_cmd[ci];
        if (c->type == RC_MESH || c->type == RC_TRIS) any3d = 1;
        /* the film look goes on before the first 2D layer (it applies to the 3D view only) */
        if ((c->type == RC_2D || c->type == RC_TEXT) && any3d && !post_grunged) post_grunge();
        switch (c->type) {
        case RC_POST:
            post_tiltshift();
            break;
        case RC_MESH:
            if (c->mesh >= nmesh) break;
            set_mode(c->mode, 0);
            if (meshes[c->mesh].dl) GX_CallDispList(meshes[c->mesh].dl, meshes[c->mesh].len);
            else if (meshes[c->mesh].imm) send_tris(meshes[c->mesh].imm, meshes[c->mesh].n);
            break;
        case RC_TEXT:
            if (c->count >= 3) send_text(&r_tv[c->start], c->count);
            break;
        default:
            if (c->count < 3) break;
            set_mode(c->mode, c->type == RC_2D);
            send_tris(&r_v[c->start], c->count);
            break;
        }
    }
    if (any3d && !post_grunged) post_grunge();
    set_tev(TEV_PLAIN);
}

static int frame_pending;

static void show_previous(void)
{
    if (!frame_pending) { VIDEO_WaitVSync(); return; }
    GX_WaitDrawDone();
    VIDEO_SetNextFramebuffer(xfb[fbi ^ 1]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    frame_pending = 0;
}

static void present(void)
{
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GXColor cc = { (u8)(r_clear >> 24), (u8)(r_clear >> 16), (u8)(r_clear >> 8), 255 };
    GX_SetCopyClear(cc, 0x00FFFFFF);
    GX_CopyDisp(xfb[fbi], GX_TRUE);
    GX_SetDrawDone();
    GX_Flush();
    frame_pending = 1;
    fbi ^= 1;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
#define X_HOME (1u << 20)

static float stick_axis(int pos, int center, int lo, int hi)
{
    float d = (float)(pos - center);
    float range = d >= 0 ? (float)(hi - center) : (float)(center - lo);
    if (range < 4.0f) return 0.0f;
    float v = d / range;
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

static void read_stick(const joystick_t *js, float *ox, float *oy)
{
    float x = stick_axis(js->pos.x, js->center.x, js->min.x, js->max.x);
    float y = -stick_axis(js->pos.y, js->center.y, js->min.y, js->max.y);
    if (x == 0.0f && y == 0.0f && js->mag > 0.0f && js->mag <= 1.5f) {
        float a = js->ang * 3.14159265f / 180.0f;
        x = sinf(a) * js->mag; y = -cosf(a) * js->mag;
    }
    float m = sqrtf(x * x + y * y);
    if (m < 0.15f || !isfinite(m)) { x = y = 0.0f; }
    else { float k = (m > 1.0f ? 1.0f : m); k = (k - 0.15f) / 0.85f / m; x *= k; y *= k; }
    *ox = x; *oy = -y;          /* game: +y up */
}

static u32 read_input(Input *in)
{
    WPAD_ScanPads();
    PAD_ScanPads();
    u32 h = WPAD_ButtonsHeld(0), b = 0;
    if (h & WPAD_CLASSIC_BUTTON_UP)     b |= BTN_UP;
    if (h & WPAD_CLASSIC_BUTTON_DOWN)   b |= BTN_DOWN;
    if (h & WPAD_CLASSIC_BUTTON_LEFT)   b |= BTN_LEFT;
    if (h & WPAD_CLASSIC_BUTTON_RIGHT)  b |= BTN_RIGHT;
    if (h & WPAD_CLASSIC_BUTTON_A)      b |= BTN_A;
    if (h & WPAD_CLASSIC_BUTTON_B)      b |= BTN_B;
    if (h & WPAD_CLASSIC_BUTTON_X)      b |= BTN_X;
    if (h & WPAD_CLASSIC_BUTTON_Y)      b |= BTN_Y;
    if (h & WPAD_CLASSIC_BUTTON_ZL)     b |= BTN_ZL;
    if (h & WPAD_CLASSIC_BUTTON_ZR)     b |= BTN_ZR;
    if (h & WPAD_CLASSIC_BUTTON_FULL_L) b |= BTN_L;
    if (h & WPAD_CLASSIC_BUTTON_FULL_R) b |= BTN_R;
    if (h & WPAD_CLASSIC_BUTTON_PLUS)   b |= BTN_PLUS;
    if (h & WPAD_CLASSIC_BUTTON_MINUS)  b |= BTN_MINUS;
    if ((h & WPAD_CLASSIC_BUTTON_HOME) || (h & WPAD_BUTTON_HOME)) b |= X_HOME;
    /* a plain Wii Remote can work the menus */
    if (h & WPAD_BUTTON_PLUS) b |= BTN_PLUS;
    if (h & WPAD_BUTTON_A) b |= BTN_A;

    in->sx = in->sy = in->lt = in->rt = 0.0f;
    WPADData *wd = WPAD_Data(0);
    if (wd && wd->exp.type == WPAD_EXP_CLASSIC) {
        read_stick(&wd->exp.classic.ljs, &in->sx, &in->sy);
        float r = wd->exp.classic.r_shoulder, l = wd->exp.classic.l_shoulder;
        if (r > 0.08f) in->rt = r > 1.0f ? 1.0f : r;
        if (l > 0.08f) in->lt = l > 1.0f ? 1.0f : l;
        if (r > 0.45f) b |= BTN_R;
        if (l > 0.45f) b |= BTN_L;
    }
    /* GameCube controller in port 1 */
    u16 gh = PAD_ButtonsHeld(0);
    if (gh) {
        if (gh & PAD_BUTTON_UP) b |= BTN_UP;
        if (gh & PAD_BUTTON_DOWN) b |= BTN_DOWN;
        if (gh & PAD_BUTTON_LEFT) b |= BTN_LEFT;
        if (gh & PAD_BUTTON_RIGHT) b |= BTN_RIGHT;
        if (gh & PAD_BUTTON_A) b |= BTN_A;
        if (gh & PAD_BUTTON_B) b |= BTN_B;
        if (gh & PAD_BUTTON_X) b |= BTN_X;
        if (gh & PAD_BUTTON_Y) b |= BTN_Y;
        if (gh & PAD_BUTTON_START) b |= BTN_PLUS;
        if (gh & PAD_TRIGGER_Z) b |= BTN_MINUS;
        if (gh & PAD_TRIGGER_R) b |= BTN_R;
        if (gh & PAD_TRIGGER_L) b |= BTN_L;
    }
    {
        float gx = (float)PAD_StickX(0) / 80.0f, gy = (float)PAD_StickY(0) / 80.0f;
        if (fabsf(gx) > 0.15f || fabsf(gy) > 0.15f) { in->sx = gx > 1 ? 1 : (gx < -1 ? -1 : gx); in->sy = gy > 1 ? 1 : (gy < -1 ? -1 : gy); }
        float tr = (float)PAD_TriggerR(0) / 200.0f, tl = (float)PAD_TriggerL(0) / 200.0f;
        if (tr > 0.1f && tr > in->rt) in->rt = tr > 1 ? 1 : tr;
        if (tl > 0.1f && tl > in->lt) in->lt = tl > 1 ? 1 : tl;
        if (tr > 0.5f) b |= BTN_R;
        if (tl > 0.5f) b |= BTN_L;
    }
    /* the stick also drives menu navigation */
    static int nav_held;
    u32 sn = 0;
    if (in->sy > 0.6f) sn |= BTN_UP;
    if (in->sy < -0.6f) sn |= BTN_DOWN;
    if (in->sx < -0.6f) sn |= BTN_LEFT;
    if (in->sx > 0.6f) sn |= BTN_RIGHT;
    (void)nav_held;
    return b | (sn << 24);
}

/* ------------------------------------------------------------------ */
/* audio                                                               */
/* ------------------------------------------------------------------ */
#define FIRST_SFX_VOICE NUM_LOOPS
static int loop_snd[NUM_LOOPS];
static u64 voice_start[16];
static int voice_vol[16];

static int clamp_pitch(float rate)
{
    int p = (int)(rate * (float)AUDIO_RATE);
    if (p < 2000) p = 2000;
    if (p > MAX_PITCH - 1000) p = MAX_PITCH - 1000;
    return p;
}

static void audio_frame(void)
{
    LoopState L[NUM_LOOPS];
    game_loops(L);
    for (int i = 0; i < NUM_LOOPS; i++) {
        float vol = L[i].snd >= 0 ? L[i].vol * audio_gain(L[i].snd) : 0.0f;
        if (vol > 1) vol = 1;
        int vl = (int)(vol * 200.0f * (L[i].pan > 0 ? 1.0f - L[i].pan * 0.6f : 1.0f));
        int vr = (int)(vol * 200.0f * (L[i].pan < 0 ? 1.0f + L[i].pan * 0.6f : 1.0f));
        if (L[i].snd < 0 || vol < 0.003f) {
            if (loop_snd[i] >= 0) ASND_ChangeVolumeVoice(i, 0, 0);
            continue;
        }
        if (L[i].snd != loop_snd[i] || ASND_StatusVoice(i) == SND_UNUSED) {
            ASND_StopVoice(i);
            ASND_SetInfiniteVoice(i, VOICE_MONO_16BIT, clamp_pitch(L[i].rate), 0, (void *)audio_data(L[i].snd), audio_len(L[i].snd) * 2, vl, vr);
            loop_snd[i] = L[i].snd;
        } else {
            ASND_ChangePitchVoice(i, clamp_pitch(L[i].rate));
            ASND_ChangeVolumeVoice(i, vl, vr);
        }
    }
    int id;
    float rate, vol, pan;
    u64 now = gettime();
    while (game_pop_sound(&id, &rate, &vol, &pan)) {
        if (id < 0 || id >= SND_COUNT) continue;
        float base = 220.0f * (vol > 1.0f ? 1.0f : vol) * audio_gain(id);
        s32 v = -1;
        for (s32 k = FIRST_SFX_VOICE; k < 16; k++) if (ASND_StatusVoice(k) == SND_UNUSED) { v = k; break; }
        if (v < 0) {
            int best = -1, bv = 1 << 30;
            for (s32 k = FIRST_SFX_VOICE; k < 16; k++) {
                if (diff_usec(voice_start[k], now) < 50000) continue;
                if (voice_vol[k] < bv) { bv = voice_vol[k]; best = k; }
            }
            if (best < 0 || (float)bv > base) continue;
            ASND_StopVoice(best);
            v = best;
        }
        int vl = (int)(base * (pan > 0 ? 1.0f - pan * 0.6f : 1.0f));
        int vr = (int)(base * (pan < 0 ? 1.0f + pan * 0.6f : 1.0f));
        ASND_SetVoice(v, VOICE_MONO_16BIT, clamp_pitch(rate), 0, (void *)audio_data(id), audio_len(id) * 2, vl, vr, NULL);
        voice_start[v] = now;
        voice_vol[v] = (int)base;
    }
}

/* ------------------------------------------------------------------ */
/* saving                                                              */
/* ------------------------------------------------------------------ */
static int sd_ok;

static void save_load(void)
{
    sd_ok = fatInitDefault();
    game_storage(sd_ok);
    if (!sd_ok) return;
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return;
    static char buf[SAVE_MAX];
    int n = (int)fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n > 0) game_load(buf, n);
}

static void save_write(void)
{
    static char buf[SAVE_MAX];
    int n = game_save_request(buf, sizeof buf);
    if (n <= 0 || !sd_ok) return;
    mkdir("sd:/apps", 0777);
    mkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_PATH, "wb");
    int ok = 0;
    if (f) { ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n; ok = (fclose(f) == 0) && ok; }
    game_save_result(ok);
}

/* a frame while the sounds and the first track are being built */
static void show_loading(void)
{
    r3d_begin(0x05070CFFu);
    r3d_camera(0, 10, -10, 0, 0, 0, 40);
    ui_begin(RM_ALPHA);
    ui_rect_v(0, 0, VW, VH, 0x0C1020FFu, 0x05070CFFu);
    ui_text_weight(1.9f);
    ui_text_c("RACING", VW * 0.5f, VH * 0.5f - 70, 40, 0xFFFFFFFFu);
    ui_text_weight(2.0f);
    ui_text_c("LEGENDS", VW * 0.5f, VH * 0.5f - 20, 52, 0xFFD23AFFu);
    ui_text_weight(1.0f);
    ui_text_c("WARMING UP THE ENGINES...", VW * 0.5f, VH * 0.5f + 60, 12, 0xA8B2C8FFu);
    for (int k = 0; k < 2; k++) {
        draw_list();
        present();
        show_previous();
    }
}

static void on_reset(u32 irq, void *ctx) { (void)irq; (void)ctx; quit = 1; }
static void on_power(void) { quit = 1; }

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    video_init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC);
    PAD_Init();
    ASND_Init();
    ASND_Pause(0);
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
    for (int i = 0; i < NUM_LOOPS; i++) loop_snd[i] = -1;

    textures_init();
    show_loading();
    audio_init();
    DCFlushRange((void *)audio_pool(), (u32)audio_pool_bytes());
    game_init();
    save_load();
    wide = game_wants_wide();
    set_view();

    const float dt = 1.0f / 60.0f;
    u32 prev = 0;
    u64 last = gettime();

    while (!quit) {
        Input in;
        u32 raw = read_input(&in);
        u32 held = raw & 0xFFFFFFu, stick = raw >> 24;
        u32 pressed = held & ~prev;
        static u32 prev_stick;
        u32 stick_pressed = stick & ~prev_stick;
        prev_stick = stick;
        prev = held;
        if (held & X_HOME) break;
        if (game_quit_requested()) break;

        u64 now = gettime();
        float elapsed = (float)diff_usec(last, now) * 1e-6f;
        last = now;
        int steps = (int)(elapsed / dt + 0.5f);
        if (steps < 1) steps = 1;
        if (steps > 2) steps = 2;
        for (int s = 0; s < steps; s++) {
            in.held = held & 0xFFFFu;
            in.pressed = s == 0 ? ((pressed | stick_pressed) & 0xFFFFu) : 0;
            game_update(&in, dt);
        }
        if (game_wants_wide() != wide) { wide = game_wants_wide(); set_view(); }

        audio_frame();
        save_write();

        game_render();
        show_previous();
        draw_list();
        present();
    }

    ASND_End();
    return 0;
}
