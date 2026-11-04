// Second screen for Super Mario 64 (sm64ex-alo) on the Anbernic RG DS under bananaOS.
// The bottom panel shows the level map with Mario and his heading, and the HUD (lives, coins, stars, health, red coins) and the
// name of the music, in the layout of the Nintendo 3DS port "Ultimate" (Epic0522/Super-Mario-64-3ds-port---Ultimate), whose
// pictures are read from minimap.pack (see tools/make_minimap_pack.py). Without the pack, or without the second panel,
// nothing happens.   GPL-3.0-or-later
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sm64.h"
#include "types.h"
#include "seq_ids.h"
#include "level_table.h"
#include "game/area.h"
#include "game/level_update.h"
#include "game/ingame_menu.h"
#include "game/save_file.h"
#include "game/game_init.h"
#include "audio/external.h"

int banana_bottom_open(int *w, int *h);
void banana_bottom_present(const uint8_t *rgba, int w, int h);

#define SW 320
#define SH 240
#define MAP_X 40 // 320 - 240 = 80, the map sits in the middle
#define MAP_SIZE 240
#define MAP_OFFSET 8192.0f

struct bsprite { char name[33]; int w, h; uint32_t *px; };
struct bmap { int level, area; uint32_t color; uint32_t *px; };

static struct {
    int state; // 0 = not tried, 1 = running, -1 = off
    struct bmap *maps; int nmaps;
    struct bsprite *spr; int nspr;
    uint32_t fb[SW * SH];
    long last_ms;
} U;

static struct bsprite *spr_find(const char *name)
{
    for (int i = 0; i < U.nspr; i++) if (!strcmp(U.spr[i].name, name)) return &U.spr[i];
    return NULL;
}

static long now_ms(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static int load_pack(void)
{
    const char *path = getenv("SM64_MINIMAP_PACK");
    if (!path) path = "minimap.pack";
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "BANANA-UI: no %s, second screen off\n", path); return 0; }
    uint8_t hd[12];
    if (fread(hd, 1, 12, f) != 12 || memcmp(hd, "BMMP", 4)) { fclose(f); return 0; }
    uint32_t nm, ns; memcpy(&nm, hd + 4, 4); memcpy(&ns, hd + 8, 4);
    U.maps = calloc(nm, sizeof *U.maps); U.spr = calloc(ns, sizeof *U.spr);
    for (uint32_t i = 0; i < nm; i++) {
        uint8_t h[6];
        if (fread(h, 1, 6, f) != 6) { fclose(f); return 0; }
        U.maps[i].level = h[0]; U.maps[i].area = h[1]; memcpy(&U.maps[i].color, h + 2, 4);
        U.maps[i].px = malloc(256 * 256 * 4);
        if (fread(U.maps[i].px, 4, 256 * 256, f) != 256 * 256) { fclose(f); return 0; }
        U.nmaps++;
    }
    for (uint32_t i = 0; i < ns; i++) {
        uint8_t h[36]; uint16_t w, hh;
        if (fread(h, 1, 36, f) != 36) break;
        memcpy(U.spr[i].name, h, 32); U.spr[i].name[32] = 0;
        memcpy(&w, h + 32, 2); memcpy(&hh, h + 34, 2);
        U.spr[i].w = w; U.spr[i].h = hh; U.spr[i].px = malloc((size_t)w * hh * 4);
        if (fread(U.spr[i].px, 4, (size_t)w * hh, f) != (size_t)w * hh) break;
        U.nspr++;
    }
    fclose(f);
    fprintf(stderr, "BANANA-UI: %d maps, %d sprites\n", U.nmaps, U.nspr);
    return 1;
}

// pixels are bytes R,G,B,A
static inline void put(int x, int y, uint32_t px)
{
    if ((unsigned)x >= SW || (unsigned)y >= SH) return;
    uint32_t a = px >> 24;
    if (a == 0) return;
    if (a == 255) { U.fb[y * SW + x] = px; return; }
    uint32_t d = U.fb[y * SW + x];
    uint32_t r = ((px & 255) * a + (d & 255) * (255 - a)) / 255;
    uint32_t g = (((px >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
    uint32_t b = (((px >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
    U.fb[y * SW + x] = 0xFF000000u | (b << 16) | (g << 8) | r;
}

static void blit(const struct bsprite *s, int x, int y)
{
    if (!s) return;
    for (int j = 0; j < s->h; j++) for (int i = 0; i < s->w; i++) put(x + i, y + j, s->px[j * s->w + i]);
}

// bottom-left anchored like the 3DS code (y counts from the bottom of the 240 px)
static void blit_bl(const struct bsprite *s, int x, int ybottom_up) { if (s) blit(s, x, SH - ybottom_up - s->h); }

static void blit_map(const struct bmap *m)
{
    for (int y = 0; y < MAP_SIZE; y++)
        for (int x = 0; x < MAP_SIZE; x++)
            put(MAP_X + x, y, m->px[(y * 256 / MAP_SIZE) * 256 + (x * 256 / MAP_SIZE)]);
}

static void disc(int cx, int cy, int r, uint32_t c)
{
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= r * r) put(cx + x, cy + y, c);
}

static void line(int x0, int y0, int x1, int y1, uint32_t c)
{
    int n = (int)fmaxf(fabsf((float)(x1 - x0)), fabsf((float)(y1 - y0)));
    for (int i = 0; i <= n; i++) {
        float t = n ? (float)i / n : 0;
        int x = (int)(x0 + (x1 - x0) * t), y = (int)(y0 + (y1 - y0) * t);
        put(x, y, c); put(x + 1, y, c);
    }
}

#define RGBA(r, g, b) (0xFF000000u | ((b) << 16) | ((g) << 8) | (r))

static int level_index_for(s16 lvl, int *area)
{
    switch (lvl) {
        case LEVEL_BBH: return 1;
        case LEVEL_CCM: return 2;
        case LEVEL_CASTLE: return 3;
        case LEVEL_HMC: return 4;
        case LEVEL_SSL: return 5;
        case LEVEL_BOB: return 6;
        case LEVEL_SL: return 7;
        case LEVEL_WDW: return 8;
        case LEVEL_JRB: return 9;
        case LEVEL_THI: return 10;
        case LEVEL_TTC: return 11;
        case LEVEL_RR: return 12;
        case LEVEL_CASTLE_GROUNDS:
            if (save_file_get_flags() & SAVE_FLAG_MOAT_DRAINED) *area = 1; // drained moat is a save flag, not an area
            return 13;
        case LEVEL_BITDW: return 14;
        case LEVEL_VCUTM: return 15;
        case LEVEL_BITFS: return 16;
        case LEVEL_SA: return 17;
        case LEVEL_BITS: return 18;
        case LEVEL_LLL: return 19;
        case LEVEL_DDD: return 20;
        case LEVEL_WF: return 21;
        case LEVEL_CASTLE_COURTYARD: return 22;
        case LEVEL_PSS: return 23;
        case LEVEL_COTMC: return 24;
        case LEVEL_TOTWC: return 25;
        case LEVEL_BOWSER_1: return 26;
        case LEVEL_WMOTR: return 27;
        case LEVEL_BOWSER_2: return 28;
        case LEVEL_BOWSER_3: return 29;
        case LEVEL_TTM: return 30;
        default: return -1;
    }
}

static const struct bmap *find_map(int level, int area)
{
    for (int i = 0; i < U.nmaps; i++) if (U.maps[i].level == level && U.maps[i].area == area) return &U.maps[i];
    return NULL;
}

static void draw_number(int value, int x, int y)
{
    char n[8], name[16];
    if (value < 0) value = 0;
    if (value > 999) value = 999;
    snprintf(n, sizeof n, "%d", value);
    for (char *p = n; *p; p++, x += 12) { snprintf(name, sizeof name, "hud_digit_%c", *p); blit(spr_find(name), x, y); }
}

static void draw_counter_right(const char *icon, int value, int right, int ytop)
{
    int digits = value >= 100 ? 3 : value >= 10 ? 2 : 1;
    int dx = right - 16 - (digits - 1) * 12, xx = dx - 14, ix = xx - 16;
    blit(spr_find(icon), ix, ytop);
    blit(spr_find("hud_x"), xx, ytop);
    draw_number(value, dx, ytop);
}

static const struct { int seq; const char *name; int w; } MUSIC[] = {
    { SEQ_SOUND_PLAYER, "music_silence", 64 }, { SEQ_EVENT_CUTSCENE_COLLECT_STAR, "music_victory", 128 },
    { SEQ_MENU_TITLE_SCREEN, "music_title_theme", 128 }, { SEQ_LEVEL_GRASS, "music_main_theme", 256 },
    { SEQ_LEVEL_INSIDE_CASTLE, "music_inside_castle", 256 }, { SEQ_LEVEL_WATER, "music_dire_dire_docks", 128 },
    { SEQ_LEVEL_HOT, "music_lethal_lava_land", 128 }, { SEQ_LEVEL_BOSS_KOOPA, "music_bowser_battle", 128 },
    { SEQ_LEVEL_SNOW, "music_snow_mountain", 128 }, { SEQ_LEVEL_SLIDE, "music_slider", 64 },
    { SEQ_LEVEL_SPOOKY, "music_haunted_house", 128 }, { SEQ_EVENT_PIRANHA_PLANT, "music_piranha_lullaby", 256 },
    { SEQ_LEVEL_UNDERGROUND, "music_hazy_maze_cave", 128 }, { SEQ_MENU_STAR_SELECT, "music_star_select", 128 },
    { SEQ_EVENT_POWERUP, "music_powerful_mario", 128 }, { SEQ_EVENT_METAL_CAP, "music_metal_mario", 128 },
    { SEQ_EVENT_KOOPA_MESSAGE, "music_koopa_message", 128 }, { SEQ_LEVEL_KOOPA_ROAD, "music_koopas_road", 128 },
    { SEQ_EVENT_HIGH_SCORE, "music_high_score", 128 }, { SEQ_EVENT_MERRY_GO_ROUND, "music_merry_go_round", 128 },
    { SEQ_EVENT_RACE, "music_race_fanfare", 128 }, { SEQ_EVENT_CUTSCENE_STAR_SPAWN, "music_star_spawn", 128 },
    { SEQ_EVENT_BOSS, "music_boss_battle", 128 }, { SEQ_EVENT_CUTSCENE_COLLECT_KEY, "music_key_clear", 128 },
    { SEQ_EVENT_ENDLESS_STAIRS, "music_endless_stairs", 128 }, { SEQ_LEVEL_BOSS_KOOPA_FINAL, "music_final_bowser", 128 },
    { SEQ_EVENT_CUTSCENE_CREDITS, "music_staff_roll", 128 }, { SEQ_EVENT_SOLVE_PUZZLE, "music_puzzle_solved", 128 },
    { SEQ_EVENT_TOAD_MESSAGE, "music_toad_message", 128 }, { SEQ_EVENT_PEACH_MESSAGE, "music_peach_message", 128 },
    { SEQ_EVENT_CUTSCENE_INTRO, "music_opening", 128 }, { SEQ_EVENT_CUTSCENE_VICTORY, "music_victory", 128 },
    { SEQ_EVENT_CUTSCENE_ENDING, "music_ending", 128 }, { SEQ_MENU_FILE_SELECT, "music_file_select", 128 },
    { SEQ_EVENT_CUTSCENE_LAKITU, "music_lakitu", 128 },
};

static void draw_frame(void)
{
    int area = gCurrAreaIndex > 0 ? gCurrAreaIndex - 1 : 0;
    int li = level_index_for(gCurrLevelNum, &area);
    const struct bmap *m = li >= 0 ? find_map(li, area) : NULL;
    if (li >= 0 && !m) m = find_map(0, 0);
    uint32_t bg = 0xFF000000u;
    if (m) { uint32_t c = m->color; bg = RGBA((c >> 16) & 255, (c >> 8) & 255, c & 255); }
    for (int i = 0; i < SW * SH; i++) U.fb[i] = bg;
    if (m) blit_map(m);

    if (m && gMarioState) {
        float fx = (gMarioState->pos[0] + MAP_OFFSET) * (MAP_SIZE / (2 * MAP_OFFSET));
        float fy = (gMarioState->pos[2] + MAP_OFFSET) * (MAP_SIZE / (2 * MAP_OFFSET));
        fx = fminf((float)MAP_SIZE, fmaxf(0.0f, fx)); fy = fminf((float)MAP_SIZE, fmaxf(0.0f, fy));
        float a = (float)gMarioState->faceAngle[1] * (6.2831853f / 65536.0f);
        int cx = MAP_X + (int)fx, cy = (int)fy;
        line(cx, cy, cx + (int)(sinf(a) * 13), cy + (int)(cosf(a) * 13), RGBA(255, 255, 255));
        disc(cx, cy, 5, RGBA(255, 255, 255));
        disc(cx, cy, 4, RGBA(220, 30, 30));
    }

    s16 flags = gHudDisplay.flags;
    if (flags != HUD_DISPLAY_NONE) {
        if (flags & HUD_DISPLAY_FLAG_LIVES) {
            disc(16, SH - 218 - 8, 7, RGBA(255, 255, 255)); disc(16, SH - 218 - 8, 6, RGBA(220, 30, 30));
            blit_bl(spr_find("hud_x"), 24, 218);
            draw_number(gHudDisplay.lives, 40, SH - 218 - 16);
        }
        static const char *rc[4] = { "hud_red_coin_front", "hud_red_coin_tilt_right", "hud_red_coin_side", "hud_red_coin_tilt_left" };
        int reds = gRedCoinsCollected; if (reds > 8) reds = 8;
        for (int i = 0; i < reds; i++) {
            const struct bsprite *s = spr_find(rc[(gGlobalTimer & 6) >> 1]);
            if (s) for (int j = 0; j < 16; j++) for (int k = 0; k < 16; k++)
                put(8 + i * 17 + k, SH - 3 - 16 + j, s->px[(j * s->h / 16) * s->w + (k * s->w / 16)]);
        }
        if ((flags & HUD_DISPLAY_FLAG_CAMERA_AND_POWER) && gHudDisplay.wedges > 0) {
            char n[24]; snprintf(n, sizeof n, "hud_health_%d", gHudDisplay.wedges > 8 ? 8 : gHudDisplay.wedges);
            blit_bl(spr_find(n), 4, 150);
        }
        if (flags & HUD_DISPLAY_FLAG_STAR_COUNT) draw_counter_right("hud_star", gHudDisplay.stars, 316, SH - 218 - 16);
        if (flags & HUD_DISPLAY_FLAG_COIN_COUNT) draw_counter_right("hud_coin", gHudDisplay.coins, 316, SH - 198 - 16);
    }

    u16 seqArgs = get_current_background_music();
    if (seqArgs != 0xFFFF) {
        for (unsigned i = 0; i < sizeof MUSIC / sizeof MUSIC[0]; i++)
            if (MUSIC[i].seq == (seqArgs & 0x7F)) {
                const struct bsprite *s = spr_find(MUSIC[i].name);
                if (s) blit(s, 316 - MUSIC[i].w, SH - 3 - 16);
                break;
            }
    }
}

int banana_ui_active(void) { return U.state == 1; }

void banana_ui_frame(void)
{
    if (U.state < 0) return;
    if (U.state == 0) {
        const char *e = getenv("BANANA_SECOND");
        int w, h;
        if ((e && *e == '0') || !load_pack() || !banana_bottom_open(&w, &h)) { U.state = -1; return; }
        U.state = 1;
    }
    long t = now_ms();
    if (t - U.last_ms < 33) return; // about 30 pictures a second at most
    U.last_ms = t;
    draw_frame();
    { static int n; if ((n++ % 150) == 0) { int a = gCurrAreaIndex > 0 ? gCurrAreaIndex - 1 : 0; int li = level_index_for(gCurrLevelNum, &a);
      fprintf(stderr, "BANANA-UI: frame %d level %d area %d li %d map %d px0 %08x\n", n, (int)gCurrLevelNum, (int)gCurrAreaIndex, li, li >= 0 && find_map(li, a) ? 1 : 0, (unsigned)U.fb[SW * SH / 2]); fflush(stderr); } }
    banana_bottom_present((const uint8_t *)U.fb, SW, SH);
}
