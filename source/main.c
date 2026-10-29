/*
 * RACING LEGENDS - Nintendo Wii platform layer (devkitPPC + libogc + libfat).
 *
 *   video : 3D through GX - the track's static chunks are GPU display lists, cars / particles / glows are
 *           sent each frame, the HUD is a 2D layer on top. Depth buffered, alpha / additive / 2x-multiply
 *           blending, a tilt-shift blur pass, 60 fps, 16:9 anamorphic or letterboxed 4:3
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
#define DL_POOL (20u * 1024u * 1024u)
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

int plat_mesh_create(const RV *v, int n)
{
    if (nmesh >= MAX_MESH || n < 3) return -1;
    if (!dl_pool) dl_pool = (u8 *)big_alloc(DL_POOL);
    n -= n % 3;
    /* 16 bytes per vertex, a 3-byte header per GX_Begin batch, padding */
    u32 batches = (u32)(n / 65520 + 1);
    u32 size = ((u32)n * 16u + batches * 3u + 64u + 31u) & ~31u;
    Mesh *m = &meshes[nmesh];
    m->imm = NULL; m->dl = NULL; m->len = 0; m->n = n;
    if (dl_used + size <= DL_POOL) {
        u8 *buf = dl_pool + dl_used;
        DCInvalidateRange(buf, size);
        GX_BeginDispList(buf, size);
        int i = 0;
        while (i < n) {
            int k = n - i;
            if (k > 65520) k = 65520;
            GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)k);
            for (int j = 0; j < k; j++) {
                const RV *p = &v[i + j];
                GX_Position3f32(p->x, p->y, p->z);
                GX_Color1u32(p->c);
            }
            GX_End();
            i += k;
        }
        u32 len = GX_EndDispList();
        if (len > 0) {
            m->dl = buf; m->len = len;
            dl_used += (len + 31u) & ~31u;
            return nmesh++;
        }
    }
    /* no room (or the list failed): keep a copy and draw it directly */
    RV *copy = (RV *)big_alloc(sizeof(RV) * (unsigned long)n);
    memcpy(copy, v, sizeof(RV) * (size_t)n);
    m->imm = copy;
    return nmesh++;
}

/* ------------------------------------------------------------------ */
/* tilt-shift: a half-size copy of the frame blended over its top and bottom */
/* ------------------------------------------------------------------ */
static void *tex_b;
static GXTexObj to_b;
static int cur_2d = -1, cur_mode = -1;

static void post_init(void)
{
    tex_b = memalign(32, GX_GetTexBufferSize(320, 240, GX_TF_RGB565, GX_FALSE, 0));
    Mtx m;
    guMtxIdentity(m);
    GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
}

static void load_2d(void);

static void post_apply(void)
{
    u32 w = rmode->fbWidth, h = rmode->efbHeight, top = 0;
    if (!wide) { u32 bh = (h * 3 / 4) & ~3u; top = ((h - bh) / 2) & ~1u; h = bh; }
    GX_SetTexCopySrc(0, top, w, h);
    GX_SetTexCopyDst(w / 2, h / 2, GX_TF_RGB565, GX_TRUE);
    GX_CopyTex(tex_b, GX_FALSE);
    GX_PixModeSync();
    GX_InvalidateTexAll();
    GX_InitTexObj(&to_b, tex_b, (u16)(w / 2), (u16)(h / 2), GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&to_b, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    load_2d();
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_InvVtxCache();
    GX_LoadTexObj(&to_b, GX_TEXMAP0);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    /* strips down the screen; alpha follows the blur strength for that height */
    const int MY = 16;
    for (int pass = 0; pass < 2; pass++) {
        float o = pass ? -1.5f : 1.5f, ou = o / VW, ov = o / VH, ka = pass ? 0.55f : 0.8f;
        GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT1, (u16)((MY + 1) * 2));
        for (int j = 0; j <= MY; j++) {
            float t = (float)j / (float)MY, y = VH * t;
            float k = 0;
            if (t < 0.32f) k = r_post_top * (1.0f - t / 0.32f);
            if (t > 0.78f) k = r_post_bot * ((t - 0.78f) / 0.22f);
            k = k > 1 ? 1 : k;
            u8 a = (u8)(k * ka * 255.0f);
            GX_Position3f32(0, y, 0); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0 + ou, t + ov);
            GX_Position3f32(VW, y, 0); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(1 + ou, t + ov);
        }
        GX_End();
    }
    vtx_plain();
    cur_mode = -1;
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
    if (mode == cur_mode && is2d == cur_2d) return;
    if (is2d != cur_2d) { if (is2d) load_2d(); else load_3d(); }
    cur_mode = mode;
    switch (mode) {
    case RM_OPAQUE: GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR); break;
    case RM_ADD:    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR); break;
    case RM_MUL2:   GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_SRCCLR, GX_LO_CLEAR); break;
    default:        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); break;
    }
    if (is2d) GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    else switch (mode) {
    case RM_OPAQUE: GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE); break;
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

static void draw_list(void)
{
    cur_2d = -1; cur_mode = -1;
    for (int ci = 0; ci < r_ncmd; ci++) {
        const RCmd *c = &r_cmd[ci];
        switch (c->type) {
        case RC_POST:
            post_apply();
            break;
        case RC_MESH:
            if (c->mesh >= nmesh) break;
            set_mode(c->mode, 0);
            if (meshes[c->mesh].dl) GX_CallDispList(meshes[c->mesh].dl, meshes[c->mesh].len);
            else if (meshes[c->mesh].imm) send_tris(meshes[c->mesh].imm, meshes[c->mesh].n);
            break;
        default:
            if (c->count < 3) break;
            set_mode(c->mode, c->type == RC_2D);
            send_tris(&r_v[c->start], c->count);
            break;
        }
    }
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

    post_init();
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
