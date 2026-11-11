/*
 * Celeste Classic (ccleste) - PS3 frontend for PSL1GHT.
 * Game logic: celeste.c/celeste.h (ccleste by lemon32767, game by
 * Maddy Thorson & Noel Berry). This file only implements the PICO-8
 * callbacks (software renderer into a 128x128 8-bit buffer), input,
 * and presentation through RSX framebuffers.
 *
 * Audio is not implemented yet (sfx/music callbacks are no-ops).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "celeste.h"
#include "tilemap.h"   /* tilemap_data[], tile_flags[] */
#include "assets.h"    /* gfx_data[], font_data[] */

#define P8W 128
#define P8H 128

/* ------------------------------------------------------------------ */
/* Software renderer                                                  */
/* ------------------------------------------------------------------ */

static const unsigned int base_palette[16] = {
    0x000000, 0x1d2b53, 0x7e2553, 0x008751,
    0xab5236, 0x5f574f, 0xc2c3c7, 0xfff1e8,
    0xff004d, 0xffa300, 0xffec27, 0x00e436,
    0x29adff, 0x83769c, 0xff77a8, 0xffccaa
};

static unsigned char fb8[P8W * P8H];   /* stores base palette indices */
static unsigned char pmap[16];         /* pal(a,b) remap table */
static int cam_x = 0, cam_y = 0;
static unsigned short buttons_state = 0;

static void reset_palette(void)
{
    int i;
    for (i = 0; i < 16; i++) pmap[i] = (unsigned char)i;
}

static void put(int x, int y, int col)
{
    if (x >= 0 && x < P8W && y >= 0 && y < P8H)
        fb8[x + y * P8W] = pmap[col & 15];
}

static void p8_rectfill(int x0, int y0, int x1, int y1, int col)
{
    int x, y;
    if (x1 < x0 || y1 < y0) return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            put(x, y, col);
}

static void p8_line(int x0, int y0, int x1, int y1, int col)
{
    int sx, sy, dx, dy, err;
#define CLAMP(v, hi) v = v < 0 ? 0 : v >= hi ? hi - 1 : v
    CLAMP(x0, P8W); CLAMP(y0, P8H);
    CLAMP(x1, P8W); CLAMP(y1, P8H);
#undef CLAMP
    dx = abs(x1 - x0);
    dy = abs(y1 - y0);
    if (!dx && !dy) return;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;
    while (x0 != x1 || y0 != y1) {
        int e2;
        put(x0, y0, col);
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* draw an 8x8 cell of the 128x64 sprite sheet */
static void draw_sprite(int sprite, int x, int y, int flipx, int flipy)
{
    int sx = 8 * (sprite % 16), sy = 8 * (sprite / 16);
    int i, j;
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            int u = flipx ? 7 - i : i;
            int v = flipy ? 7 - j : j;
            unsigned char p = gfx_data[(sx + u) + (sy + v) * GFX_W];
            if (p) put(x + i, y + j, p);
        }
    }
}

static void p8_print(const char *str, int x, int y, int col)
{
    for (; *str; str++) {
        int c = *str & 0x7F;
        int sx = 8 * (c % 16), sy = 8 * (c / 16);
        int i, j;
        for (j = 0; j < 8; j++)
            for (i = 0; i < 8; i++)
                if (font_data[(sx + i) + (sy + j) * FONT_W])
                    put(x + i, y + j, col);
        x += 4;
    }
}

static int gettileflag(int tile, int flag)
{
    return tile < (int)(sizeof(tile_flags) / sizeof(*tile_flags)) &&
           (tile_flags[tile] & (1 << flag)) != 0;
}

static int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...)
{
    va_list args;
    int ret = 0;

    va_start(args, call);
#define INT_ARG() va_arg(args, int)
#define RET_INT(v) do { ret = (v); goto end; } while (0)

    switch (call) {
    case CELESTE_P8_MUSIC:
        (void)INT_ARG(); (void)INT_ARG(); (void)INT_ARG();
        break;
    case CELESTE_P8_SFX:
        (void)INT_ARG();
        break;
    case CELESTE_P8_SPR: {
        int sprite = INT_ARG();
        int x = INT_ARG();
        int y = INT_ARG();
        int cols = INT_ARG();
        int rows = INT_ARG();
        int flipx = INT_ARG();
        int flipy = INT_ARG();
        (void)cols; (void)rows;
        if (sprite >= 0)
            draw_sprite(sprite, x - cam_x, y - cam_y, flipx, flipy);
        break;
    }
    case CELESTE_P8_BTN: {
        int b = INT_ARG();
        RET_INT(!!(buttons_state & (1 << b)));
    }
    case CELESTE_P8_PAL: {
        int a = INT_ARG();
        int b = INT_ARG();
        if (a >= 0 && a < 16 && b >= 0 && b < 16) pmap[a] = (unsigned char)b;
        break;
    }
    case CELESTE_P8_PAL_RESET:
        reset_palette();
        break;
    case CELESTE_P8_CIRCFILL: {
        int cx = INT_ARG() - cam_x;
        int cy = INT_ARG() - cam_y;
        int r = INT_ARG();
        int col = INT_ARG();
        if (r <= 1) {
            p8_rectfill(cx - 1, cy, cx + 1, cy, col);
            p8_rectfill(cx, cy - 1, cx, cy + 1, col);
        } else if (r <= 2) {
            p8_rectfill(cx - 2, cy - 1, cx + 2, cy + 1, col);
            p8_rectfill(cx - 1, cy - 2, cx + 1, cy + 2, col);
        } else if (r <= 3) {
            p8_rectfill(cx - 3, cy - 1, cx + 3, cy + 1, col);
            p8_rectfill(cx - 1, cy - 3, cx + 1, cy + 3, col);
            p8_rectfill(cx - 2, cy - 2, cx + 2, cy + 2, col);
        } else {
            int f = 1 - r, ddFx = 1, ddFy = -2 * r, x = 0, y = r;
            p8_line(cx, cy - y, cx, cy + r, col);
            p8_line(cx + r, cy, cx - r, cy, col);
            while (x < y) {
                if (f >= 0) { y--; ddFy += 2; f += ddFy; }
                x++; ddFx += 2; f += ddFx;
                p8_line(cx + x, cy + y, cx - x, cy + y, col);
                p8_line(cx + x, cy - y, cx - x, cy - y, col);
                p8_line(cx + y, cy + x, cx - y, cy + x, col);
                p8_line(cx + y, cy - x, cx - y, cy - x, col);
            }
        }
        break;
    }
    case CELESTE_P8_PRINT: {
        const char *str = va_arg(args, const char *);
        int x = INT_ARG() - cam_x;
        int y = INT_ARG() - cam_y;
        int col = INT_ARG() % 16;
        p8_print(str, x, y, col);
        break;
    }
    case CELESTE_P8_RECTFILL: {
        int x0 = INT_ARG() - cam_x;
        int y0 = INT_ARG() - cam_y;
        int x1 = INT_ARG() - cam_x;
        int y1 = INT_ARG() - cam_y;
        int col = INT_ARG();
        p8_rectfill(x0, y0, x1, y1, col);
        break;
    }
    case CELESTE_P8_LINE: {
        int x0 = INT_ARG() - cam_x;
        int y0 = INT_ARG() - cam_y;
        int x1 = INT_ARG() - cam_x;
        int y1 = INT_ARG() - cam_y;
        int col = INT_ARG();
        p8_line(x0, y0, x1, y1, col);
        break;
    }
    case CELESTE_P8_MGET: {
        int tx = INT_ARG();
        int ty = INT_ARG();
        RET_INT(tilemap_data[tx + ty * 128]);
    }
    case CELESTE_P8_CAMERA:
        cam_x = INT_ARG();
        cam_y = INT_ARG();
        break;
    case CELESTE_P8_FGET: {
        int tile = INT_ARG();
        int flag = INT_ARG();
        RET_INT(gettileflag(tile, flag));
    }
    case CELESTE_P8_MAP: {
        int mx = INT_ARG(), my = INT_ARG();
        int tx = INT_ARG(), ty = INT_ARG();
        int mw = INT_ARG(), mh = INT_ARG();
        int mask = INT_ARG();
        int x, y;
        for (x = 0; x < mw; x++) {
            for (y = 0; y < mh; y++) {
                int tile = tilemap_data[x + mx + (y + my) * 128];
                if (mask == 0 ||
                    (mask == 4 && tile_flags[tile] == 4) ||
                    gettileflag(tile, mask != 4 ? mask - 1 : mask))
                    draw_sprite(tile, tx + x * 8 - cam_x, ty + y * 8 - cam_y, 0, 0);
            }
        }
        break;
    }
    }
end:
    va_end(args);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Game step shared by every platform                                 */
/* ------------------------------------------------------------------ */

static int paused = 0;

static void game_init(void)
{
    reset_palette();
    memset(fb8, 0, sizeof fb8);
    Celeste_P8_set_call_func(pico8emu);
    Celeste_P8_set_rndseed((unsigned)time(NULL));
    Celeste_P8_init();
}

/* runs one 30 fps game frame; draws into fb8 */
static void game_frame(void)
{
    if (paused) {
        const int x0 = P8W / 2 - 3 * 4, y0 = 8;
        p8_rectfill(x0 - 1, y0 - 1, 6 * 4 + x0 + 1, 6 + y0 + 1, 6);
        p8_rectfill(x0, y0, 6 * 4 + x0, 6 + y0, 0);
        p8_print("paused", x0 + 1, y0 + 1, 7);
    } else {
        Celeste_P8_update();
        Celeste_P8_draw();
    }
}

#ifdef __PPU__
/* ================================================================== */
/* PS3 / PSL1GHT                                                      */
/* ================================================================== */
#include <malloc.h>
#include <unistd.h>
#include <ppu-types.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)

static gcmContextData *context = NULL;
static u32 cur_fb = 0;
static u32 *fb_ptr[2];
static u32 fb_off[2];
static u32 scr_w, scr_h, scr_pitch;
static int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void do_flip(u32 id)
{
    gcmSetFlip(context, id);
    rsxFlushBuffer(context);
    gcmSetWaitFlip(context);
}

static int init_video(void)
{
    videoState state;
    videoConfiguration vconfig;
    videoResolution res;
    void *host_addr = memalign(1024 * 1024, HOST_SIZE);
    int i;

    if (!host_addr) return -1;
    rsxInit(&context, CB_SIZE, HOST_SIZE, host_addr);
    if (!context) return -1;

    if (videoGetState(0, 0, &state) != 0) return -1;
    if (state.state != 0) return -1;            /* no display connected */
    if (videoGetResolution(state.displayMode.resolution, &res) != 0) return -1;

    memset(&vconfig, 0, sizeof vconfig);
    vconfig.resolution = state.displayMode.resolution;
    vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch = res.width * sizeof(u32);
    vconfig.aspect = state.displayMode.aspect;
    if (videoConfigure(0, &vconfig, NULL, 0) != 0) return -1;
    videoGetState(0, 0, &state);

    scr_w = res.width;
    scr_h = res.height;
    scr_pitch = res.width * sizeof(u32);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (i = 0; i < 2; i++) {
        fb_ptr[i] = (u32 *)rsxMemalign(64, scr_h * scr_pitch);
        if (!fb_ptr[i]) return -1;
        if (rsxAddressToOffset(fb_ptr[i], &fb_off[i]) != 0) return -1;
        if (gcmSetDisplayBuffer(i, fb_off[i], scr_pitch, scr_w, scr_h) != 0) return -1;
        memset(fb_ptr[i], 0, scr_h * scr_pitch);
    }
    gcmResetFlipStatus();
    do_flip(1);   /* show buffer 1 first so we can draw into 0 */
    cur_fb = 0;
    return 0;
}

static void present(void)
{
    u32 scale = (scr_w < scr_h ? scr_w : scr_h) / P8H;
    u32 ox, oy, x, y, i;
    u32 pal32[16];
    u32 *dst;
    u32 *row;

    if (scale < 1) scale = 1;
    ox = (scr_w - P8W * scale) / 2;
    oy = (scr_h - P8H * scale) / 2;
    for (i = 0; i < 16; i++) pal32[i] = base_palette[i];

    wait_flip();
    dst = fb_ptr[cur_fb];
    row = (u32 *)malloc(P8W * scale * sizeof(u32));
    if (!row) return;

    for (y = 0; y < P8H; y++) {
        u32 k;
        for (x = 0; x < P8W; x++) {
            u32 c = pal32[fb8[x + y * P8W] & 15];
            for (k = 0; k < scale; k++) row[x * scale + k] = c;
        }
        for (k = 0; k < scale; k++)
            memcpy(dst + (oy + y * scale + k) * (scr_pitch / 4) + ox,
                   row, P8W * scale * sizeof(u32));
    }
    free(row);

    do_flip(cur_fb);
    cur_fb ^= 1;
}

static void read_pad(void)
{
    static padInfo pinfo;
    static padData pdata;
    static int prev_start = 0;
    int i, start = 0;

    ioPadGetInfo(&pinfo);
    buttons_state = 0;
    for (i = 0; i < MAX_PADS; i++) {
        if (!pinfo.status[i]) continue;
        ioPadGetData(i, &pdata);
        if (pdata.BTN_LEFT  || pdata.ANA_L_H < 64)  buttons_state |= 1 << 0;
        if (pdata.BTN_RIGHT || pdata.ANA_L_H > 192) buttons_state |= 1 << 1;
        if (pdata.BTN_UP    || pdata.ANA_L_V < 64)  buttons_state |= 1 << 2;
        if (pdata.BTN_DOWN  || pdata.ANA_L_V > 192) buttons_state |= 1 << 3;
        if (pdata.BTN_CROSS || pdata.BTN_SQUARE)    buttons_state |= 1 << 4; /* jump */
        if (pdata.BTN_CIRCLE || pdata.BTN_TRIANGLE) buttons_state |= 1 << 5; /* dash */
        if (pdata.BTN_START) start = 1;
        if (pdata.BTN_SELECT && pdata.BTN_START) { /* reset */
            paused = 0;
            Celeste_P8_set_rndseed((unsigned)time(NULL));
            Celeste_P8_init();
        }
        break;   /* first connected pad only */
    }
    if (start && !prev_start) paused = !paused;
    prev_start = start;
}

int main(void)
{
    int frame = 0;

    if (init_video() != 0) return 1;
    ioPadInit(7);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    game_init();

    while (running) {
        sysUtilCheckCallback();
        /* vsync is 60 Hz, the game runs at 30 Hz: logic every 2nd flip */
        if ((frame & 1) == 0) {
            read_pad();
            game_frame();
        }
        present();
        frame++;
    }

    gcmSetWaitFlip(context);
    rsxFinish(context, 1);
    ioPadEnd();
    return 0;
}

#else
/* ================================================================== */
/* Host test harness: runs the game headless and dumps PPM frames     */
/* ================================================================== */
static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", P8W, P8H);
    for (i = 0; i < P8W * P8H; i++) {
        unsigned int c = base_palette[fb8[i] & 15];
        fputc((c >> 16) & 255, f);
        fputc((c >> 8) & 255, f);
        fputc(c & 255, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 150;
    game_init();
    for (i = 0; i < n; i++) {
        buttons_state = (i > 40 && i < 120) ? (1 << 1) : 0;   /* hold right */
        if (i == 60 || i == 90) buttons_state |= 1 << 4;      /* jump */
        game_frame();
        if (i == 10)  dump_ppm("frame_title.ppm");
        if (i == n-1) dump_ppm("frame_last.ppm");
    }
    printf("ok, %d frames\n", n);
    return 0;
}
#endif
