/*
 * Entomo: The Parallelogram Force - a PRG32 cartridge.
 *
 * A tribute to Entomo, the masked civic hero of late-2000s Naples. The hero
 * restores decoro (erases graffiti, gathers rubbish and broken glass) across
 * four rioni of the real city and in the cavities beneath them, and keeps
 * troublemakers away with the Parallelogram Force shield. Nobody is hit and
 * nobody is arrested: there is no health anywhere in this file.
 *
 * One source file per build (the PRG32 builder takes one). The generated
 * tables live in gen_assets.h (tools/build_assets.py).
 *
 * Rendering: the cartridge composes the 304x176 playfield itself, three
 * layers deep, sixteen scanlines at a time, into `strip` and hands each
 * strip to the firmware as one 8-bit indexed sprite:
 *
 *   backdrop   0.25x  Vesuvius across the gulf / cavern pillars; it shows
 *                     through the "open" tiles (sea, pits)
 *   playfield  1.00x  tiles of the active map, tasks, actors, hero, shield
 *   foreground 1.50x  washing lines / stalactites, drawn over everything
 *
 * Portable-cartridge rules kept here: no libc, no floats, no pointers in
 * initialised data, no 64-bit division.
 */
#include "prg32.h"

#include "gen_assets.h"

#ifdef ENTOMO_HOST
uint32_t entomo_host_features(void);
#define host_features() entomo_host_features()
#else
extern const prg32_abi_table_t *__prg32_abi;
static uint32_t host_features(void) { return __prg32_abi ? __prg32_abi->provided_features : 0u; }
#endif

/* ------------------------------------------------------------------ */
/* Layout and tuning                                                   */
/* ------------------------------------------------------------------ */
#define SCR_W 320
#define SCR_H 200
#define VIEW_X 8
#define VIEW_Y 16
#define VIEW_W 304
#define VIEW_H 176
#define STRIP_H 16
#define TILE 16
#define WORLD (MAP_N * TILE)

#define P0 0  /* surface tiles */
#define P1 16 /* underground tiles */
#define P2 32 /* hero and effects */
#define P3 48 /* actors and objects */
#define P4 64 /* backdrop, foreground, ledger */
#define P5 80 /* underground tiles, dimmed */

#define C_PAPER (P4 + 9)
#define C_PAPER_DARK (P4 + 10)
#define C_INK (P4 + 11)
#define C_MAGENTA (P4 + 12)
#define C_NEON (P2 + 10)
#define C_NAVY (P2 + 1)
#define C_WHITE (P2 + 13)

/* Map tile types (nibbles). Type 2 links the two layers. */
#define T_LINK 2
#define T_SOLID 4
#define T_OPEN 8
#define T_TORCH 9
#define WALK_SURFACE 0x24fu /* street, pedestrian, tombino, piazza, grass, galleria */
#define WALK_UNDER 0x44fu   /* gallery, cistern, stairs, water, rails, shelter */

#define FRAMES_PER_MINUTE 56 /* 30 fps: the 22:00-06:00 night lasts 15 minutes */
#define NIGHT_MINUTES 480
#define CONTACT_MINUTES 8

#define FORZA_MAX 960
#define FORZA_DRAIN 5
#define FORZA_GAIN 2
#define FORZA_MIN_START 90

#define ACTOR_MAX SPAWN_COUNT
#define A_WALK 0
#define A_BOUNCE 1
#define A_ALIGN 2
#define A_CITIZEN 3

#define KIND_GRAFFITI 0
#define KIND_TRASH 1
#define KIND_GLASS 2
#define TASK_DONE 255

/* Audio: the tracker plays instrument n on channel n (0..3). */
#define CH_HUM 4
#define CH_SFX 5
#define I_SCRUB 8
#define I_BLIP 9
#define I_HUM 10
#define I_BOING 11
#define I_CLANK 12
#define I_DRIP 13
#define TRK_TITLE 0
#define TRK_SURFACE 1
#define TRK_UNDER 2
#define TRK_JINGLE 3
#define TRK_ENDING 4

enum { ST_TITLE, ST_PLAY, ST_MAP, ST_END };

typedef struct {
    int16_t x, y;   /* feet centre, world pixels */
    int16_t vx, vy; /* velocity, 1/16 pixel per frame */
    int16_t ax, ay; /* sub-pixel accumulators */
    int8_t dx, dy;  /* walking direction */
    uint8_t kind, state, timer, zone, anim;
} actor_t;

typedef struct {
    uint8_t delay, instrument, note, volume;
    int8_t pan;
    uint8_t channel;
} sfx_t;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static uint16_t strip_words[SCR_W * STRIP_H / 2]; /* 16-bit aligned: also the snapshot row */
#define strip ((uint8_t *)strip_words)
static uint16_t pal_draw[PAL_COUNT];
static prg32_indexed_sprite_t strip_spr;
static int clip_x, clip_y, clip_w, clip_h;

static uint8_t display;    /* DISPLAY_RGB, DISPLAY_CUBE or DISPLAY_NEAREST */
static uint8_t has_audio;
static uint8_t state, frame_dirty;
static uint32_t frame, rng;
static uint32_t pad, pad_prev;

static const uint8_t *map_cur; /* napoli_surface_map or napoli_subterranean_map */
static uint8_t layer;          /* 0 surface, 1 subterranean */
static int cam_x, cam_y;
static int hero_x, hero_y;
static uint8_t hero_dir, hero_step, hero_safe, shield_on;
static int forza;
static uint32_t score;
static int clock_frames, minutes;
static uint8_t task_state[TASK_COUNT];
static uint8_t zone_done[ZONE_COUNT], zone_total[ZONE_COUNT], zone_complete[ZONE_COUNT];
static actor_t actors[ACTOR_MAX];
static int target_task, link_x, link_y;
static int fx_x, fx_y, fx_timer;
static char msg[36];
static int msg_timer;
static uint8_t place_name, place_valid;
static uint8_t light[13][21];
static int light_tx, light_ty;
static sfx_t sfx_queue[8];
static uint8_t music_now, music_resume;
static int jingle_timer;
static uint8_t won, score_sent;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static uint32_t hash2(int x, int y) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
    h ^= h >> 13;
    return h * 0x5bd1e995u;
}

static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int iabs(int v) { return v < 0 ? -v : v; }

static char *put_str(char *d, const char *s) {
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

static char *put_num(char *d, uint32_t v, int digits) {
    for (int i = digits - 1; i >= 0; --i) {
        d[i] = (char)('0' + v % 10u);
        v /= 10u;
    }
    d[digits] = 0;
    return d + digits;
}

static uint16_t col(int i) { return pal_draw[i]; }

static void text(int x, int y, const char *s, int fg, int bg) { prg32_gfx_text8(x, y, s, col(fg), col(bg)); }

/* Give palette entry i the colour rgb on every kind of host. */
static void set_colour(int i, uint16_t rgb) {
    if (display) prg32_palette_set(PAL_CELL[i], rgb); /* indexed: recolours the screen */
    if (display != 1) pal_draw[i] = rgb;              /* all but DISPLAY_CUBE draw true colours */
}

/* ------------------------------------------------------------------ */
/* Map                                                                 */
/* ------------------------------------------------------------------ */
static uint8_t map_at(const uint8_t *m, int tx, int ty) {
    if ((unsigned)tx >= MAP_N || (unsigned)ty >= MAP_N) return T_SOLID;
    uint8_t b = m[ty * (MAP_N / 2) + (tx >> 1)];
    return (tx & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 15u);
}

static int walkable_type(uint8_t t) { return (int)(((layer ? WALK_UNDER : WALK_SURFACE) >> t) & 1u); }

static int walkable_px(int x, int y) { return walkable_type(map_at(map_cur, x >> 4, y >> 4)); }

/* The 11x8 box around the feet of the hero or of an actor. */
static int box_free(int x, int y) {
    return walkable_px(x - 5, y - 7) && walkable_px(x + 5, y - 7) && walkable_px(x - 5, y) && walkable_px(x + 5, y);
}

static int zone_of(int tx, int ty) {
    for (int z = 0; z < ZONE_COUNT; ++z) {
        if (tx >= ZONES[z][0] && ty >= ZONES[z][1] && tx < ZONES[z][2] && ty < ZONES[z][3]) return z;
    }
    return 0;
}

/* Which picture a map tile gets: the oblique view is derived from the
 * neighbours (a block shows its south face where a street runs below). */
static uint8_t tile_gfx(int tx, int ty, uint8_t t) {
    uint32_t h = hash2(tx, ty);
    if (!layer) {
        if (t == 0) return (h & 7u) ? GFX_STREET_A : GFX_STREET_B;
        if (t == 1) return GFX_PEDESTRIAN;
        if (t == T_LINK) return GFX_TOMBINO;
        if (t == 3) return GFX_PIAZZA;
        if (t == 4 || t == 5) {
            uint8_t below = map_at(map_cur, tx, ty + 1);
            int face = ty + 1 < MAP_N && below != 4 && below != 5 && below != 7;
            if (t == 5) return face ? GFX_PUBLIC_FACADE : GFX_PUBLIC_ROOF;
            if (face) {
                uint32_t k = (h >> 8) & 7u;
                if (k < 3) return GFX_FACADE_WINDOW;
                if (k < 5) return GFX_FACADE_BALCONY;
                return k < 7 ? GFX_FACADE_DOOR : GFX_FACADE_SHOP;
            }
            uint32_t k = hash2(tx >> 1, ty >> 1) & 3u;
            return k < 2 ? GFX_ROOF_A : (k == 2 ? GFX_ROOF_B : GFX_ROOF_C);
        }
        if (t == 6) return GFX_GRASS;
        if (t == 7) return GFX_TREE;
        if (t == 9) return GFX_GALLERIA;
        return GFX_STREET_A;
    }
    if (t == 0) return GFX_U_FLOOR;
    if (t == 1) return GFX_U_CISTERN;
    if (t == T_LINK) return GFX_U_STAIRS;
    if (t == 3) return GFX_U_WATER;
    if (t == 4) {
        if (walkable_type(map_at(map_cur, tx, ty + 1))) return GFX_U_ROCKFACE;
        return (h & 1u) ? GFX_U_ROCK_A : GFX_U_ROCK_B;
    }
    if (t == 5) return GFX_U_MASONRY;
    if (t == 6) return GFX_U_RAILS;
    if (t == 7) return GFX_U_PILLAR;
    if (t == T_TORCH) return GFX_U_TORCH;
    return GFX_U_SHELTER;
}

/* ------------------------------------------------------------------ */
/* Strip renderer                                                      */
/* ------------------------------------------------------------------ */
static void strip_begin(int x, int y, int w, int h) {
    clip_x = x;
    clip_y = y;
    clip_w = w;
    clip_h = h;
}

static void strip_end(void) {
    strip_spr.pixels = strip;
    strip_spr.palette = pal_draw;
    strip_spr.width = (uint16_t)clip_w;
    strip_spr.height = (uint16_t)clip_h;
    strip_spr.frame_count = 1;
    strip_spr.palette_count = PAL_COUNT;
    strip_spr.bits_per_pixel = PRG32_SPRITE_BPP_8;
    strip_spr.transparent_index = -1;
    prg32_sprite_draw_indexed(clip_x, clip_y, &strip_spr, 0);
}

static void s_px(int x, int y, uint8_t c) {
    x -= clip_x;
    y -= clip_y;
    if ((unsigned)x < (unsigned)clip_w && (unsigned)y < (unsigned)clip_h) strip[y * clip_w + x] = c;
}

static void s_rect(int x, int y, int w, int h, uint8_t c) {
    int x0 = x - clip_x, y0 = y - clip_y, x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > clip_w) x1 = clip_w;
    if (y1 > clip_h) y1 = clip_h;
    for (int r = y0; r < y1; ++r) {
        uint8_t *d = strip + r * clip_w;
        for (int i = x0; i < x1; ++i) d[i] = c;
    }
}

/* Opaque 16x16 tile, 4 bits per pixel. */
static void s_tile(int x, int y, const uint8_t *src, uint8_t base) {
    int x0 = x - clip_x, y0 = y - clip_y;
    int r0 = y0 < 0 ? -y0 : 0, r1 = y0 + 16 > clip_h ? clip_h - y0 : 16;
    int c0 = x0 < 0 ? -x0 : 0, c1 = x0 + 16 > clip_w ? clip_w - x0 : 16;
    if (c0 >= c1) return;
    for (int r = r0; r < r1; ++r) {
        const uint8_t *s = src + r * 8;
        uint8_t *d = strip + (y0 + r) * clip_w + x0;
        if (c0 == 0 && c1 == 16) {
            for (int i = 0; i < 8; ++i) {
                uint8_t b = s[i];
                d[0] = (uint8_t)(base + (b >> 4));
                d[1] = (uint8_t)(base + (b & 15u));
                d += 2;
            }
        } else {
            for (int c = c0; c < c1; ++c) {
                uint8_t b = s[c >> 1];
                d[c] = (uint8_t)(base + ((c & 1) ? (b & 15u) : (b >> 4)));
            }
        }
    }
}

/* Sprite with colour 0 transparent. scale is 1 or 2; `fade` (0..32) drops
 * that share of the pixels in a fixed dither, which is how graffiti wears
 * off while it is scrubbed. */
static void s_sprite(int x, int y, const uint8_t *src, int w, int h, uint8_t base, int flip, int scale, int fade) {
    int x0 = x - clip_x, y0 = y - clip_y;
    int r0 = y0 < 0 ? -y0 : 0, r1 = y0 + h * scale > clip_h ? clip_h - y0 : h * scale;
    int c0 = x0 < 0 ? -x0 : 0, c1 = x0 + w * scale > clip_w ? clip_w - x0 : w * scale;
    if (c0 >= c1) return;
    int row_bytes = w >> 1;
    for (int r = r0; r < r1; ++r) {
        int sr = scale == 2 ? r >> 1 : r;
        const uint8_t *s = src + sr * row_bytes;
        uint8_t *d = strip + (y0 + r) * clip_w + x0;
        for (int c = c0; c < c1; ++c) {
            int sc = scale == 2 ? c >> 1 : c;
            if (flip) sc = w - 1 - sc;
            uint8_t b = s[sc >> 1];
            uint8_t v = (sc & 1) ? (uint8_t)(b & 15u) : (uint8_t)(b >> 4);
            if (v && (!fade || ((unsigned)(sc * 7 + sr * 13) & 31u) >= (unsigned)fade)) d[c] = (uint8_t)(base + v);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Backdrop (0.25x) and foreground (1.5x)                              */
/* ------------------------------------------------------------------ */
#define HORIZON 540
/* Profile of Somma-Vesuvius seen from the Beverello pier, 8 px per step. */
static const uint8_t VESUVIO[33] = {0,  2,  4,  7,  10, 14, 19, 25, 32, 40, 47, 52, 55, 53, 49, 46, 48,
                                    53, 58, 62, 63, 60, 54, 46, 38, 31, 25, 19, 14, 10, 6,  3,  0};
static int bg_ox, bg_oy;

static uint8_t bg_surface(int bx, int by) {
    if (by >= HORIZON) { /* the gulf, with moonlit ripples */
        int d = by - HORIZON;
        if ((d & 1) && (((unsigned)(bx + d * 5) + (frame >> 2)) & 31u) < (unsigned)(3 + (d >> 3))) return P4 + 8;
        return P4 + 7;
    }
    int rise = HORIZON - by;
    int i = bx - 512;
    if (i >= 0 && i < 256) {
        int a = VESUVIO[i >> 3], b = VESUVIO[(i >> 3) + 1];
        if (rise <= a + ((b - a) * (i & 7)) / 8) return P4 + 5;
    }
    if (rise <= 5 + (int)((hash2(bx >> 4, 3) >> 9) & 3u)) return P4 + 6; /* the far coast */
    int dither = ((bx ^ by) & 1) * 6;
    if (rise < 18 + dither) return P4 + 4;
    if (rise < 46 + dither) return P4 + 3;
    if (rise < 96 + dither) return P4 + 2;
    if ((hash2(bx, by) & 255u) == 0) return P4 + 13; /* stars */
    return P4 + 1;
}

static uint8_t bg_under(int bx, int by) {
    int m = bx % 88;
    if (m < 18) { /* tuff pillars left by the quarrymen */
        if ((by % 44) < 2) return P1 + 4;
        return m < 5 ? P1 + 6 : (m < 13 ? P1 + 5 : P1 + 4);
    }
    if (m > 50 && m < 54 && (by % 132) > 60 && (by % 132) < 66) return P1 + 8; /* a far lamp */
    if ((hash2(bx >> 1, by >> 1) & 63u) == 0) return P1 + 2;
    return (by % 44) < 22 && ((bx + by) & 3) == 0 ? P1 + 1 : P1 + 0;
}

static void s_backdrop(int x, int y, int w, int h) {
    int x0 = x - clip_x, y0 = y - clip_y, x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > clip_w) x1 = clip_w;
    if (y1 > clip_h) y1 = clip_h;
    for (int r = y0; r < y1; ++r) {
        uint8_t *d = strip + r * clip_w;
        int by = r + clip_y + bg_oy;
        for (int i = x0; i < x1; ++i) {
            int bx = i + clip_x + bg_ox;
            d[i] = layer ? bg_under(bx, by) : bg_surface(bx, by);
        }
    }
}

/* Sparse props on a 160x112 lattice in foreground space. */
static void draw_foreground(void) {
    int fx = cam_x + (cam_x >> 1), fy = cam_y + (cam_y >> 1);
    for (int cy = fy / 112; cy <= (fy + VIEW_H) / 112; ++cy) {
        for (int cx = fx / 160; cx <= (fx + VIEW_W) / 160; ++cx) {
            uint32_t h = hash2(cx + layer * 57, cy);
            if ((h >> 4) & 3u) continue;
            int x = VIEW_X + cx * 160 + (int)((h >> 8) % 64u) - fx;
            int y = VIEW_Y + cy * 112 + (int)((h >> 16) % 64u) - fy;
            if (y >= clip_y + clip_h || y + 48 <= clip_y) continue;
            if (layer) s_sprite(x, y, SPR_STALACTITE, 16, 24, P4, 0, 2, 0);
            else s_sprite(x, y, SPR_LAUNDRY, 48, 12, P4, (int)(h & 1u), 2, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sound                                                               */
/* ------------------------------------------------------------------ */
static void music(int track) {
    music_now = (uint8_t)track;
    if (has_audio) prg32_audio_play_track((uint16_t)track);
}

static void sfx_at(int delay, int instrument, int note, int volume, int pan, int channel) {
    for (int i = 0; i < 8; ++i) {
        if (!sfx_queue[i].volume) {
            sfx_queue[i].delay = (uint8_t)delay;
            sfx_queue[i].instrument = (uint8_t)instrument;
            sfx_queue[i].note = (uint8_t)note;
            sfx_queue[i].volume = (uint8_t)volume;
            sfx_queue[i].pan = (int8_t)(pan ? pan : 1); /* pan 0 means "instrument default" */
            sfx_queue[i].channel = (uint8_t)channel;
            return;
        }
    }
}

/* A sound at a world x: panned to where it is on screen and, below ground,
 * followed by two fainter echoes thrown to the opposite sides. */
static void sfx(int instrument, int note, int volume, int world_x) {
    int pan = (world_x - cam_x - VIEW_W / 2) * 60 / (VIEW_W / 2);
    if (pan > 60) pan = 60;
    if (pan < -60) pan = -60;
    sfx_at(0, instrument, note, volume, pan, CH_SFX);
    if (layer) {
        sfx_at(6, instrument, note, volume / 2, pan > 0 ? -50 : 50, 6);
        sfx_at(13, instrument, note - 12, volume / 4, pan > 0 ? 40 : -40, 7);
    }
}

static void sfx_tick(void) {
    for (int i = 0; i < 8; ++i) {
        sfx_t *s = &sfx_queue[i];
        if (!s->volume) continue;
        if (s->delay) {
            --s->delay;
            continue;
        }
        if (has_audio) prg32_audio_note_on_pan(s->channel, s->instrument, s->note, s->volume, s->pan);
        s->volume = 0;
    }
    if (jingle_timer && --jingle_timer == 0) music(music_resume);
}

static void jingle(void) {
    music_resume = layer ? TRK_UNDER : TRK_SURFACE;
    music(TRK_JINGLE);
    jingle_timer = 75;
}

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */
/* PRG32 hosts keep the screen in three different ways, and the cartridge
 * wants its 96 exact colours on all of them:
 *
 *   DISPLAY_RGB      an RGB565 surface (QEMU firmware): colours are drawn
 *                    as they are, the palette calls change nothing;
 *   DISPLAY_CUBE     the ILI9341 indexed framebuffer: a colour is quantised
 *                    by formula to a cell of the 6x6x6 system cube. Each
 *                    game colour gets a private cell: it is drawn with a
 *                    proxy colour that lands in the cell (PAL_PROXY) and the
 *                    cell is redefined with the true colour;
 *   DISPLAY_NEAREST  an indexed framebuffer that looks up the nearest
 *                    palette entry (PRG32-QT): the same cells are
 *                    redefined, and drawing the true colour finds them.
 *
 * The probe draws one pixel with a true colour and one with its proxy,
 * changes the palette cell behind them and reads the row back. */
#define DISPLAY_RGB 0
#define DISPLAY_CUBE 1
#define DISPLAY_NEAREST 2

static int detect_display(void) {
    const uint16_t probe = 0x2967; /* in no default palette; its cube cell is not PAL_CELL[1] */
    uint8_t cell = PAL_CELL[1];
    uint16_t before = prg32_palette_get(cell);
    int kind = DISPLAY_RGB;
    prg32_palette_set(cell, probe);
    for (int row = 0; row <= 20; row += 20) { /* row 20 of the LCD is row 0 of the game on the firmware */
        prg32_gfx_pixel(0, row, probe);
        prg32_gfx_pixel(1, row, PAL_PROXY[1]);
    }
    prg32_palette_set(cell, 0xffff);
    if (prg32_gfx_snapshot_row_rgb565(20, strip_words, SCR_W) >= 2) {
        if (strip_words[0] == 0xffff) kind = DISPLAY_NEAREST;
        else if (strip_words[1] == 0xffff) kind = DISPLAY_CUBE;
    }
    prg32_palette_set(cell, before);
    return kind;
}

static void palette_init(void) {
    display = (uint8_t)detect_display();
    for (int i = 0; i < PAL_COUNT; ++i) {
        pal_draw[i] = display == DISPLAY_CUBE ? PAL_PROXY[i] : PAL_RGB[i];
        if (display != DISPLAY_RGB) prg32_palette_set(PAL_CELL[i], PAL_RGB[i]);
    }
}

static uint16_t mix565(uint16_t a, uint16_t b, int t) { /* t = 0..8 */
    int r = ((a >> 11) & 31) * (8 - t) + ((b >> 11) & 31) * t;
    int g = ((a >> 5) & 63) * (8 - t) + ((b >> 5) & 63) * t;
    int bl = (a & 31) * (8 - t) + (b & 31) * t;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 5) | (bl >> 3));
}

/* Colour cycling: the neon of the shield breathes, torches flicker, the
 * cistern water glints. Three palette writes every third frame. */
static void palette_animate(void) {
    if (frame % 3u) return;
    int t = (int)((frame / 3u) & 15u);
    if (t > 8) t = 16 - t;
    set_colour(C_NEON, mix565(PAL_RGB[P2 + 12], PAL_RGB[P2 + 11], t));
    uint32_t f = hash2((int)(frame / 3u), 7) & 3u;
    set_colour(P1 + 8, PAL_RGB[P1 + (f == 0 ? 10 : 8)]);
    set_colour(P1 + 12, mix565(PAL_RGB[P1 + 12], PAL_RGB[P1 + 3], t));
}

/* ------------------------------------------------------------------ */
/* Ledger chrome and HUD                                               */
/* ------------------------------------------------------------------ */
/* The yellowed, gridded paper of a street-atlas index page. */
static uint8_t paper(int x, int y) {
    if ((x & 15) == 7 || (y & 15) == 7) return C_PAPER_DARK;
    if ((hash2(x, y) & 31u) == 0) return C_PAPER_DARK;
    return C_PAPER;
}

static void draw_paper(int y0, int h) {
    strip_begin(0, y0, SCR_W, h);
    for (int r = 0; r < h; ++r) {
        for (int x = 0; x < SCR_W; ++x) strip[r * SCR_W + x] = paper(x, y0 + r);
    }
}

static void draw_frame_chrome(void) {
    for (int y = 0; y < SCR_H; y += STRIP_H) {
        int h = SCR_H - y < STRIP_H ? SCR_H - y : STRIP_H;
        draw_paper(y, h);
        s_rect(VIEW_X - 2, VIEW_Y - 2, VIEW_W + 4, 1, C_MAGENTA);
        s_rect(VIEW_X - 2, VIEW_Y + VIEW_H + 1, VIEW_W + 4, 1, C_MAGENTA);
        s_rect(VIEW_X - 2, VIEW_Y - 2, 1, VIEW_H + 4, C_MAGENTA);
        s_rect(VIEW_X + VIEW_W + 1, VIEW_Y - 2, 1, VIEW_H + 4, C_MAGENTA);
        s_rect(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, C_INK);
        strip_end();
    }
}

static void hud_place(char *line) {
    int tx = hero_x >> 4, ty = hero_y >> 4;
    char *p = put_str(line, "TAV ");
    p = put_num(p, (uint32_t)(tx < 64 ? (ty < 64 ? 35 : 37) : (ty < 64 ? 36 : 38)), 2);
    *p++ = ' ';
    *p++ = (char)('A' + ((tx & 63) >> 4));
    *p++ = (char)('0' + (ty < 64 ? 2 + (ty >> 4) : 1 + ((ty - 64) >> 4)));
    *p++ = ' ';
    if (layer) p = put_str(p, "SOTTO ");
    if ((frame & 7u) == 0 || !place_valid) {
        int best = 10;
        place_valid = 1;
        place_name = 255;
        for (int i = 0; i < PLACE_COUNT; ++i) {
            int d = iabs(PLACES[i][0] - tx) + iabs(PLACES[i][1] - ty);
            if (d < best) {
                best = d;
                place_name = PLACES[i][2];
            }
        }
    }
    put_str(p, place_name == 255 ? ZONE_NAMES[zone_of(tx, ty)] : PLACE_NAMES[place_name]);
}

static void pad_line(char *line, int width) {
    int n = 0;
    while (line[n]) ++n;
    while (n < width) line[n++] = ' ';
    line[width] = 0;
}

static void draw_hud(void) {
    char line[48];
    if (msg_timer) put_str(line, msg);
    else hud_place(line);
    pad_line(line, 32);
    text(8, 3, line, msg_timer ? C_MAGENTA : C_INK, C_PAPER);

    int clock = (22 * 60 + minutes) % (24 * 60);
    char *p = put_num(line, (uint32_t)(clock / 60), 2);
    *p++ = ':';
    put_num(p, (uint32_t)(clock % 60), 2);
    text(272, 3, line, minutes > NIGHT_MINUTES - 60 ? C_MAGENTA : C_INK, C_PAPER);

    int z = zone_of(hero_x >> 4, hero_y >> 4);
    p = put_str(line, "DECORO ");
    p = put_num(p, (uint32_t)(zone_done[z] * 100 / zone_total[z]), 3);
    put_str(p, "/100");
    text(8, 192, line, zone_complete[z] ? C_MAGENTA : C_INK, C_PAPER);
    text(128, 192, "FORZA", C_INK, C_PAPER);
    int bar = forza * 40 / FORZA_MAX;
    prg32_gfx_rect(172, 193, bar, 6, col(forza < FORZA_MIN_START ? C_MAGENTA : P2 + 12));
    prg32_gfx_rect(172 + bar, 193, 40 - bar, 6, col(C_PAPER_DARK));
    put_num(line, score, 6);
    text(264, 192, line, C_INK, C_PAPER);
}

static void say(const char *a, const char *b, int frames) {
    char *p = put_str(msg, a);
    if (b) put_str(p, b);
    msg_timer = frames;
}

/* ------------------------------------------------------------------ */
/* Game logic                                                          */
/* ------------------------------------------------------------------ */
static int task_need(int kind) { return kind == KIND_GRAFFITI ? 36 : (kind == KIND_TRASH ? 20 : 28); }

static void find_targets(void) {
    int best = 1 << 30;
    target_task = -1;
    for (int i = 0; i < TASK_COUNT; ++i) {
        if (task_state[i] == TASK_DONE || (TASKS[i][2] & 1u) != layer) continue;
        int d = iabs(TASKS[i][0] * 16 + 8 - hero_x) + iabs(TASKS[i][1] * 16 + 8 - hero_y);
        if (d < best) {
            best = d;
            target_task = i;
        }
    }
    best = 1 << 30;
    for (int i = 0; i < NODE_COUNT; ++i) { /* the first places are the shafts */
        int d = iabs(PLACES[i][0] * 16 + 8 - hero_x) + iabs(PLACES[i][1] * 16 + 8 - hero_y);
        if (d < best) {
            best = d;
            link_x = PLACES[i][0] * 16 + 8;
            link_y = PLACES[i][1] * 16 + 8;
        }
    }
}

static void game_reset(void) {
    layer = 0;
    map_cur = napoli_surface_map;
    hero_x = START_TX * 16 + 8;
    hero_y = START_TY * 16 + 12;
    hero_dir = 0;
    hero_safe = 60;
    shield_on = 0;
    forza = FORZA_MAX;
    score = 0;
    clock_frames = 0;
    minutes = 0;
    won = 0;
    score_sent = 0;
    msg_timer = 0;
    fx_timer = 0;
    jingle_timer = 0;
    place_valid = 0;
    rng = prg32_ticks_ms() | 1u;
    for (int z = 0; z < ZONE_COUNT; ++z) zone_done[z] = zone_total[z] = zone_complete[z] = 0;
    for (int i = 0; i < TASK_COUNT; ++i) {
        task_state[i] = 0;
        ++zone_total[zone_of(TASKS[i][0], TASKS[i][1])];
    }
    for (int i = 0; i < ACTOR_MAX; ++i) {
        actor_t *a = &actors[i];
        a->x = (int16_t)(SPAWNS[i][0] * 16 + 8);
        a->y = (int16_t)(SPAWNS[i][1] * 16 + 12);
        a->kind = SPAWNS[i][2];
        a->zone = (uint8_t)zone_of(SPAWNS[i][0], SPAWNS[i][1]);
        a->state = A_WALK;
        a->dx = a->dy = 0;
        a->vx = a->vy = a->ax = a->ay = 0;
        a->timer = 0;
        a->anim = (uint8_t)i;
    }
    find_targets();
}

static void complete_zone(int z) {
    zone_complete[z] = 1;
    score += 1000;
    /* Nobody is arrested: the troublemakers of the rione settle down with
     * a book or a flower pot and never collide again. */
    for (int i = 0; i < ACTOR_MAX; ++i) {
        if (actors[i].zone == z) actors[i].state = A_CITIZEN;
    }
    say(ZONE_NAMES[z], " SHINES!", 110);
    jingle();
    int all = 1;
    for (int k = 0; k < ZONE_COUNT; ++k) all &= zone_complete[k];
    if (all) won = 1;
}

static void hero_step_axis(int dx, int dy, int speed) {
    for (int n = speed; n > 0; --n) { /* one pixel at a time */
        if (box_free(hero_x + dx, hero_y + dy)) {
            hero_x += dx;
            hero_y += dy;
            continue;
        }
        /* Blocked: slide along the wall towards the nearest opening, so a
         * one-tile vicolo can be entered without pixel-perfect aiming. */
        for (int k = 1; k <= 9; ++k) {
            int sx = dy ? k : 0, sy = dx ? k : 0;
            if (box_free(hero_x + dx - sx, hero_y + dy - sy) && box_free(hero_x - (sx ? 1 : 0), hero_y - (sy ? 1 : 0))) {
                hero_x -= sx ? 1 : 0;
                hero_y -= sy ? 1 : 0;
                break;
            }
            if (box_free(hero_x + dx + sx, hero_y + dy + sy) && box_free(hero_x + (sx ? 1 : 0), hero_y + (sy ? 1 : 0))) {
                hero_x += sx ? 1 : 0;
                hero_y += sy ? 1 : 0;
                break;
            }
        }
    }
}

static void update_hero(void) {
    int dx = 0, dy = 0;
    if (pad & PRG32_BTN_LEFT) dx = -1;
    if (pad & PRG32_BTN_RIGHT) dx = 1;
    if (pad & PRG32_BTN_UP) dy = -1;
    if (pad & PRG32_BTN_DOWN) dy = 1;

    /* B: the Parallelogram Force. It needs a reserve to start. */
    int want = (pad & PRG32_BTN_B) != 0;
    if (want && !shield_on && forza >= FORZA_MIN_START) {
        shield_on = 1;
        if (has_audio) prg32_audio_note_on_pan(CH_HUM, I_HUM, 45, 150, 1);
    }
    if (shield_on && (!want || forza <= 0)) {
        shield_on = 0;
        if (has_audio) prg32_audio_note_off(CH_HUM);
        if (forza <= 0) say("FORZA SPENT: RECHARGING", 0, 60);
    }
    forza += shield_on ? -FORZA_DRAIN : FORZA_GAIN;
    if (forza > FORZA_MAX) forza = FORZA_MAX;
    if (forza < 0) forza = 0;

    /* A: restorative direct action on the task at hand. */
    int busy = 0;
    if ((pad & PRG32_BTN_A) && target_task >= 0 && !shield_on) {
        int i = target_task, kind = TASKS[i][2] >> 1;
        int tx = TASKS[i][0] * 16 + 8, ty = TASKS[i][1] * 16 + 8;
        if (iabs(tx - hero_x) < 15 && iabs(ty - (hero_y - 4)) < 15) {
            busy = 1;
            hero_dir = kind == KIND_GRAFFITI ? 1 : 0;
            if ((frame & 3u) == 0) sfx(I_SCRUB, 70 + (int)(rnd() % 12u), 110, tx);
            if (++task_state[i] >= task_need(kind)) {
                int z = zone_of(TASKS[i][0], TASKS[i][1]);
                task_state[i] = TASK_DONE;
                score += kind == KIND_GRAFFITI ? 100u : (kind == KIND_TRASH ? 60u : 80u);
                fx_x = TASKS[i][0] * 16;
                fx_y = (TASKS[i][1] - (kind == KIND_GRAFFITI ? 1 : 0)) * 16;
                fx_timer = 24;
                sfx_at(0, I_BLIP, 76, 170, 1, CH_SFX);
                sfx_at(3, I_BLIP, 83, 170, layer ? -40 : 1, CH_SFX);
                forza += 120;
                if (++zone_done[z] >= zone_total[z]) complete_zone(z);
                else if (!msg_timer) say(kind == KIND_GRAFFITI ? "WALL CLEAN" : (kind == KIND_TRASH ? "RUBBISH GATHERED" : "GLASS CLEARED"), 0, 40);
                find_targets();
            }
        }
    }
    if (!busy && (dx || dy)) {
        if (dx) {
            hero_step_axis(dx, 0, shield_on ? 2 : 3);
            hero_dir = dx < 0 ? 2 : 3;
        }
        if (dy) {
            hero_step_axis(0, dy, shield_on ? 2 : 3);
            if (!dx) hero_dir = dy < 0 ? 1 : 0;
        }
        ++hero_step;
    } else {
        hero_step = 0;
    }
    if (hero_safe) --hero_safe;

    /* SELECT on a manhole or a staircase: swap the map pointer. The world
     * coordinates do not change, the two layers are one above the other. */
    if ((pad & ~pad_prev) & PRG32_BTN_SELECT) {
        if (map_at(map_cur, hero_x >> 4, (hero_y - 3) >> 4) == T_LINK) {
            /* Step onto the middle of the shaft first: the walls around its
             * two ends differ, the tile itself is free in both layers. */
            hero_x = (hero_x & ~15) + 8;
            hero_y = (((hero_y - 3) >> 4) << 4) + 12;
            layer ^= 1u;
            map_cur = layer ? napoli_subterranean_map : napoli_surface_map;
            place_valid = 0;
            sfx(I_CLANK, 40, 220, hero_x);
            say(layer ? "DOWN INTO THE CAVITA" : "BACK TO THE STREETS", 0, 50);
            if (shield_on && has_audio) prg32_audio_note_off(CH_HUM);
            shield_on = 0;
            jingle_timer = 0;
            music(layer ? TRK_UNDER : TRK_SURFACE);
            find_targets();
        } else {
            state = ST_MAP;
            frame_dirty = 1;
            if (shield_on && has_audio) prg32_audio_note_off(CH_HUM);
            shield_on = 0;
        }
    }
}

static void actor_choose(actor_t *a) {
    static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
    int tx = a->x >> 4, ty = a->y >> 4;
    int options[4], n = 0, back = -1;
    for (int d = 0; d < 4; ++d) {
        uint8_t t = map_at(napoli_surface_map, tx + DX[d], ty + DY[d]);
        if (!((WALK_SURFACE >> t) & 1u)) continue;
        if (DX[d] == -a->dx && DY[d] == -a->dy && (a->dx || a->dy)) back = d;
        else options[n++] = d;
    }
    int pick = back;
    if (n) {
        pick = options[rnd() % (uint32_t)n];
        /* A guardiamacchine walks up to whoever comes near his patch. */
        if (a->kind == 1 && !layer && iabs(hero_x - a->x) + iabs(hero_y - a->y) < 112) {
            int best = 1 << 30;
            for (int i = 0; i < n; ++i) {
                int d = options[i];
                int dist = iabs(hero_x - (a->x + DX[d] * 16)) + iabs(hero_y - (a->y + DY[d] * 16));
                if (dist < best) {
                    best = dist;
                    pick = d;
                }
            }
        }
    }
    if (pick < 0) {
        a->dx = a->dy = 0;
        return;
    }
    a->dx = DX[pick];
    a->dy = DY[pick];
}

static void actor_free_step(actor_t *a, int dx, int dy) {
    uint8_t save = layer; /* actors live on the surface */
    const uint8_t *m = map_cur;
    layer = 0;
    map_cur = napoli_surface_map;
    if (box_free(a->x + dx, a->y + dy)) {
        a->x = (int16_t)(a->x + dx);
        a->y = (int16_t)(a->y + dy);
    } else {
        a->vx = a->vy = 0;
    }
    layer = save;
    map_cur = m;
}

static void update_actors(void) {
    for (int i = 0; i < ACTOR_MAX; ++i) {
        actor_t *a = &actors[i];
        ++a->anim;
        if (a->state == A_CITIZEN) continue;
        if (a->state == A_WALK) {
            int speed = a->kind ? 12 : 20; /* 1/16 px per frame */
            a->vx = (int16_t)(a->dx * speed);
            a->vy = (int16_t)(a->dy * speed);
            a->ax = (int16_t)(a->ax + speed);
            while (a->ax >= 16) {
                a->ax = (int16_t)(a->ax - 16);
                if ((a->x & 15) == 8 && (a->y & 15) == 12) actor_choose(a);
                a->x = (int16_t)(a->x + a->dx);
                a->y = (int16_t)(a->y + a->dy);
            }
        } else if (a->state == A_BOUNCE) {
            a->ax = (int16_t)(a->ax + a->vx);
            a->ay = (int16_t)(a->ay + a->vy);
            while (a->ax >= 16) { a->ax = (int16_t)(a->ax - 16); actor_free_step(a, 1, 0); }
            while (a->ax <= -16) { a->ax = (int16_t)(a->ax + 16); actor_free_step(a, -1, 0); }
            while (a->ay >= 16) { a->ay = (int16_t)(a->ay - 16); actor_free_step(a, 0, 1); }
            while (a->ay <= -16) { a->ay = (int16_t)(a->ay + 16); actor_free_step(a, 0, -1); }
            if (--a->timer == 0) a->state = A_ALIGN;
        } else { /* A_ALIGN: back to the centre of the tile, then walk on */
            int gx = (a->x & ~15) + 8, gy = (a->y & ~15) + 12;
            if (a->x != gx) a->x = (int16_t)(a->x + (gx > a->x ? 1 : -1));
            else if (a->y != gy) a->y = (int16_t)(a->y + (gy > a->y ? 1 : -1));
            else {
                a->state = A_WALK;
                a->dx = a->dy = 0;
                a->ax = a->ay = 0;
            }
        }
        if (layer || a->state != A_WALK) continue;
        int ddx = iabs(a->x - hero_x), ddy = iabs(a->y - hero_y);
        if (shield_on && ddx < 28 && ddy < 22) {
            /* The non-violent physics engine: contact with the shield
             * inverts and doubles the actor's velocity - an elastic
             * bounce. No health exists to be depleted. */
            a->vx = (int16_t)(-a->vx * 2);
            a->vy = (int16_t)(-a->vy * 2);
            if (!a->vx && !a->vy) { /* it was standing still: push it away */
                a->vx = (int16_t)(a->x < hero_x ? -40 : 40);
                a->vy = (int16_t)(a->y < hero_y ? -24 : 24);
            }
            a->ax = a->ay = 0;
            a->state = A_BOUNCE;
            a->timer = 18;
            score += 25;
            sfx(I_BOING, 52, 200, a->x);
            sfx_at(3, I_BOING, 64, 160, a->x < hero_x ? -50 : 50, CH_SFX);
        } else if (!shield_on && !hero_safe && ddx < 11 && ddy < 11) {
            /* Without the shield a troublemaker only wastes the hero's
             * time: the clock jumps ahead while they argue. */
            clock_frames += CONTACT_MINUTES * FRAMES_PER_MINUTE;
            hero_safe = 75;
            a->dx = (int8_t)-a->dx;
            a->dy = (int8_t)-a->dy;
            say(a->kind ? "GUARDIAMACCHINE: TIME LOST" : "TEPPISTELLO: TIME LOST", 0, 70);
            sfx(I_CLANK, 52, 180, a->x);
        }
    }
}

static void update_play(void) {
    update_hero();
    if (state != ST_PLAY) return;
    update_actors();
    if ((frame & 7u) == 0) find_targets();
    if (msg_timer) --msg_timer;
    if (fx_timer) --fx_timer;
    if (layer && (frame % 97u) == 0) /* water drips somewhere in the dark */
        sfx(I_DRIP, 84 + (int)(rnd() % 10u), 90, cam_x + (int)(rnd() % (uint32_t)VIEW_W));
    ++clock_frames;
    minutes = clock_frames / FRAMES_PER_MINUTE;
    if (won || minutes >= NIGHT_MINUTES) {
        if (minutes > NIGHT_MINUTES) minutes = NIGHT_MINUTES;
        if (won) score += (uint32_t)(NIGHT_MINUTES - minutes) * 10u;
        if (shield_on && has_audio) prg32_audio_note_off(CH_HUM);
        shield_on = 0;
        state = ST_END;
        frame_dirty = 1;
        jingle_timer = 0;
        music(TRK_ENDING);
    }
}

/* ------------------------------------------------------------------ */
/* Drawing: the playfield                                              */
/* ------------------------------------------------------------------ */
/* Below ground only the hero's lamp and the wall torches give light:
 * 2 lit, 1 dim, 0 black. One value per visible tile. */
static void compute_light(void) {
    light_tx = cam_x >> 4;
    light_ty = cam_y >> 4;
    int htx = hero_x >> 4, hty = (hero_y - 4) >> 4;
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c < 21; ++c) {
            int tx = light_tx + c, ty = light_ty + r;
            int dx = tx - htx, dy = ty - hty, d2 = dx * dx + dy * dy;
            uint8_t l = d2 <= 13 ? 2 : (d2 <= 40 ? 1 : 0);
            if (l < 2) {
                for (int j = -2; j <= 2 && l < 2; ++j) {
                    for (int i = -2; i <= 2; ++i) {
                        if (map_at(map_cur, tx + i, ty + j) != T_TORCH) continue;
                        uint8_t t = (iabs(i) <= 1 && iabs(j) <= 1) ? 2 : 1;
                        if (t > l) l = t;
                    }
                }
            }
            light[r][c] = l;
        }
    }
}

static uint8_t light_at(int tx, int ty) {
    int c = tx - light_tx, r = ty - light_ty;
    if (!layer) return 2;
    if ((unsigned)c >= 21u || (unsigned)r >= 13u) return 0;
    return light[r][c];
}

static void draw_tiles(void) {
    int wy = cam_y + clip_y - VIEW_Y;
    for (int ty = wy >> 4; ty <= (wy + clip_h - 1) >> 4; ++ty) {
        int sy = VIEW_Y + ty * 16 - cam_y;
        for (int tx = cam_x >> 4; tx <= (cam_x + VIEW_W - 1) >> 4; ++tx) {
            int sx = VIEW_X + tx * 16 - cam_x;
            uint8_t t = map_at(map_cur, tx, ty);
            uint8_t l = light_at(tx, ty);
            if (!l) s_rect(sx, sy, 16, 16, P1 + 0);
            else if (t == T_OPEN) s_backdrop(sx, sy, 16, 16);
            else s_tile(sx, sy, TILE_GFX[tile_gfx(tx, ty, t)], (uint8_t)(layer ? (l == 2 ? P1 : P5) : P0));
        }
    }
}

static void draw_tasks(void) {
    for (int i = 0; i < TASK_COUNT; ++i) {
        if ((TASKS[i][2] & 1u) != layer) continue;
        int kind = TASKS[i][2] >> 1, tx = TASKS[i][0], ty = TASKS[i][1];
        if (kind == KIND_GRAFFITI) --ty; /* painted on the wall above */
        int sx = VIEW_X + tx * 16 - cam_x, sy = VIEW_Y + ty * 16 - cam_y;
        if (sy >= clip_y + clip_h || sy + 16 <= clip_y || sx <= VIEW_X - 16 || sx >= VIEW_X + VIEW_W) continue;
        if (!light_at(tx, TASKS[i][1])) continue;
        if (task_state[i] == TASK_DONE) {
            if (kind == KIND_TRASH) s_sprite(sx, sy, SPR_OBJ[OBJ_FLOWERS], 16, 16, P3, 0, 1, 0);
            continue;
        }
        int obj = kind == KIND_GRAFFITI ? ((i & 1) ? OBJ_GRAFFITI_B : OBJ_GRAFFITI_A) : (kind == KIND_TRASH ? OBJ_TRASH : OBJ_GLASS);
        s_sprite(sx, sy, SPR_OBJ[obj], 16, 16, P3, 0, 1, task_state[i] * 30 / task_need(kind));
    }
    if (fx_timer && (fx_timer & 2)) s_sprite(VIEW_X + fx_x - cam_x, VIEW_Y + fx_y - cam_y, SPR_OBJ[OBJ_SPARKLE], 16, 16, P3, 0, 1, 0);
}

static void draw_actors(void) {
    if (layer) return;
    for (int i = 0; i < ACTOR_MAX; ++i) {
        const actor_t *a = &actors[i];
        int sx = VIEW_X + a->x - 8 - cam_x, sy = VIEW_Y + a->y - 22 - cam_y;
        if (sy >= clip_y + clip_h || sy + 24 <= clip_y || sx <= VIEW_X - 16 || sx >= VIEW_X + VIEW_W) continue;
        int f;
        if (a->state == A_CITIZEN) f = 4 + a->kind;
        else f = a->kind * 2 + (a->state == A_BOUNCE ? 1 : ((a->anim >> 3) & 1));
        s_sprite(sx, sy + (a->state == A_BOUNCE ? -(int)(a->timer & 3u) : 0), SPR_NPC[f], 16, 24, P3, a->dx < 0, 1, 0);
    }
}

static void draw_hero(void) {
    int sx = VIEW_X + hero_x - 8 - cam_x, sy = VIEW_Y + hero_y - 22 - cam_y;
    if (hero_safe && (frame & 2u) && state == ST_PLAY) return;
    int f = hero_dir == 0 ? 0 : (hero_dir == 1 ? 2 : 4);
    s_sprite(sx, sy, SPR_HERO[f + ((hero_step >> 2) & 1)], 16, 24, P2, hero_dir == 2, 1, 0);
}

/* The Parallelogram Force: a neon wireframe box sheared along the 2:1
 * isometric diagonal. Edges are solid, the mesh inside is drawn on every
 * other pixel so the street shows through it. */
static void draw_shield(void) {
    if (!shield_on) return;
    int cx = VIEW_X + hero_x - cam_x, cy = VIEW_Y + hero_y - 11 - cam_y;
    int phase = (int)(frame >> 1) & 7;
    for (int y = -18; y <= 18; ++y) {
        int sy = cy + y;
        if (sy < clip_y || sy >= clip_y + clip_h) continue;
        int shear = -y / 2;
        for (int x = -24; x <= 24; ++x) {
            int edge = y <= -17 || y >= 17 || x <= -23 || x >= 23;
            int mesh = ((y + 18) % 9 == 0) || (((x + 24 + phase) & 7) == 0);
            if (edge) s_px(cx + x + shear, sy, (x + y + (int)frame) & 8 ? P2 + 11 : C_NEON);
            else if (mesh && ((x + y) & 1)) s_px(cx + x + shear, sy, C_NEON);
        }
    }
}

/* A blinking lozenge on the nearest job (or, when the layer is clean, on
 * the nearest way to the other layer); it rides the edge when off screen. */
static void draw_marker(void) {
    int wx, wy;
    uint8_t c = C_NEON;
    if (target_task >= 0) {
        wx = TASKS[target_task][0] * 16 + 8;
        wy = TASKS[target_task][1] * 16 - 6;
    } else {
        wx = link_x;
        wy = link_y - 12;
        c = P2 + 14;
    }
    int sx = VIEW_X + wx - cam_x, sy = VIEW_Y + wy - cam_y;
    int inside = sx >= VIEW_X + 6 && sx < VIEW_X + VIEW_W - 6 && sy >= VIEW_Y + 6 && sy < VIEW_Y + VIEW_H - 6;
    if (inside) sy -= (int)((frame >> 2) & 3u);
    else if (frame & 8u) return;
    if (sx < VIEW_X + 6) sx = VIEW_X + 6;
    if (sx > VIEW_X + VIEW_W - 7) sx = VIEW_X + VIEW_W - 7;
    if (sy < VIEW_Y + 6) sy = VIEW_Y + 6;
    if (sy > VIEW_Y + VIEW_H - 7) sy = VIEW_Y + VIEW_H - 7;
    for (int y = -4; y <= 4; ++y) {
        int w = 4 - iabs(y);
        s_rect(sx - w, sy + y, w * 2 + 1, 1, C_NAVY);
        if (w) s_rect(sx - w + 1, sy + y, w * 2 - 1, 1, c);
    }
}

static void draw_play(void) {
    cam_x = hero_x - VIEW_W / 2;
    cam_y = hero_y - 8 - VIEW_H / 2;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    if (cam_x > WORLD - VIEW_W) cam_x = WORLD - VIEW_W;
    if (cam_y > WORLD - VIEW_H) cam_y = WORLD - VIEW_H;
    bg_ox = (cam_x >> 2) - VIEW_X;
    bg_oy = (cam_y >> 2) - VIEW_Y;
    if (layer) compute_light();
    for (int s = 0; s < VIEW_H / STRIP_H; ++s) {
        strip_begin(VIEW_X, VIEW_Y + s * STRIP_H, VIEW_W, STRIP_H);
        draw_tiles();
        draw_tasks();
        draw_actors();
        draw_hero();
        draw_shield();
        draw_foreground();
        draw_marker();
        strip_end();
    }
}

/* ------------------------------------------------------------------ */
/* Drawing: the Tuttocitta' overlay                                    */
/* ------------------------------------------------------------------ */
#define MAP_PX 192 /* 128 tiles at 1.5 pixels each */

static uint8_t map_colour(int mx, int my) {
    int tx = mx * 2 / 3, ty = my * 2 / 3;
    if (mx % 24 == 0 || my % 24 == 0) return C_MAGENTA; /* the atlas squares */
    uint8_t t = map_at(map_cur, tx, ty);
    if (!layer) {
        if (t == 4) return C_PAPER;
        if (t == 5) return C_PAPER_DARK;
        if (t == 1 || t == 9) return P0 + 13;
        if (t == 6 || t == 7) return P0 + 12;
        if (t == T_OPEN) return P4 + 8;
        if (t == T_LINK) return P4 + 15;
        return P4 + 13;
    }
    if (t == 4 || t == T_TORCH) return C_INK;
    if (t == 5 || t == 7) return P1 + 13;
    if (t == 3) return P4 + 8;
    if (t == T_OPEN) return P1 + 0;
    if (t == T_LINK) return P4 + 15;
    return t == 6 ? P1 + 14 : C_PAPER;
}

static void map_dot(int tx, int ty, int r, uint8_t c) {
    int x = 4 + tx * 3 / 2, y = 4 + ty * 3 / 2;
    s_rect(x - r, y - r, r * 2 + 1, r * 2 + 1, c);
}

static void draw_map(void) {
    char line[24];
    for (int y = 0; y < SCR_H; y += STRIP_H) {
        int h = SCR_H - y < STRIP_H ? SCR_H - y : STRIP_H;
        draw_paper(y, h);
        for (int r = 0; r < h; ++r) {
            int my = y + r - 4;
            if ((unsigned)my >= MAP_PX) continue;
            uint8_t *d = strip + r * SCR_W + 4;
            for (int mx = 0; mx < MAP_PX; ++mx) d[mx] = map_colour(mx, my);
        }
        s_rect(3, 3, MAP_PX + 2, 1, C_INK);
        s_rect(3, 4 + MAP_PX, MAP_PX + 2, 1, C_INK);
        s_rect(3, 3, 1, MAP_PX + 2, C_INK);
        s_rect(4 + MAP_PX, 3, 1, MAP_PX + 2, C_INK);
        for (int i = 0; i < NODE_COUNT; ++i) map_dot(PLACES[i][0], PLACES[i][1], 1, P4 + 15);
        for (int i = 0; i < TASK_COUNT; ++i) {
            if (task_state[i] != TASK_DONE && (TASKS[i][2] & 1u) == layer && (frame & 8u)) map_dot(TASKS[i][0], TASKS[i][1], 1, P4 + 14);
        }
        if (!layer) {
            for (int i = 0; i < ACTOR_MAX; ++i) {
                if (actors[i].state != A_CITIZEN) map_dot(actors[i].x >> 4, actors[i].y >> 4, 0, C_INK);
            }
        }
        map_dot(hero_x >> 4, hero_y >> 4, 2, (frame & 4u) ? C_NEON : C_NAVY);
        strip_end();
    }
    text(204, 6, "TUTTOCITTA", C_MAGENTA, C_PAPER);
    text(204, 16, "NAPOLI", C_INK, C_PAPER);
    text(204, 26, "TAV 35-38", C_INK, C_PAPER);
    text(204, 40, layer ? "SOTTOSUOLO" : "SUPERFICIE", C_INK, C_PAPER_DARK);
    for (int z = 0; z < ZONE_COUNT; ++z) {
        text(204, 56 + z * 22, ZONE_NAMES[z], zone_complete[z] ? C_MAGENTA : C_INK, C_PAPER);
        char *p = put_str(line, " ");
        p = put_num(p, zone_done[z], 2);
        *p++ = '/';
        put_num(p, zone_total[z], 2);
        text(204, 66 + z * 22, line, C_INK, C_PAPER);
    }
    text(204, 148, "RED: TO CLEAN", P4 + 14, C_PAPER);
    text(204, 158, "BLUE: TOMBINO", P4 + 15, C_PAPER);
    text(204, 168, "GREEN: ENTOMO", P2 + 12, C_PAPER);
    text(204, 184, "START: CLOSE", C_INK, C_PAPER);
}

/* ------------------------------------------------------------------ */
/* Drawing: title and ending                                           */
/* ------------------------------------------------------------------ */
/* 5x7 capitals for the logo; each cell becomes a sheared block. */
static const uint8_t LOGO[5][7] = {
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, /* E */
    {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}, /* N */
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* O */
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
};

static void draw_logo(int x, int y, uint8_t c) {
    static const uint8_t WORD[6] = {0, 1, 2, 3, 4, 3};
    for (int row = 0; row < 7; ++row) {
        int sy = y + row * 6;
        if (sy >= clip_y + clip_h || sy + 6 <= clip_y) continue;
        for (int i = 0; i < 6; ++i) {
            uint8_t bits = LOGO[WORD[i]][row];
            for (int b = 0; b < 5; ++b) {
                if (!(bits & (0x10u >> b))) continue;
                for (int k = 0; k < 6; ++k) /* one parallelogram, slope 2:1 */
                    s_rect(x + i * 40 + b * 7 + (41 - row * 6 - k) / 2, sy + k, 7, 1, c);
            }
        }
    }
}

static void draw_title(void) {
    uint8_t save = layer;
    layer = 0;
    bg_ox = 480;
    bg_oy = 400;
    for (int y = 0; y < SCR_H; y += STRIP_H) {
        int h = SCR_H - y < STRIP_H ? SCR_H - y : STRIP_H;
        strip_begin(0, y, SCR_W, h);
        s_backdrop(0, y, SCR_W, h);
        s_rect(0, 66, SCR_W, 12, C_NAVY);
        draw_logo(40 + 2, 14 + 2, C_NAVY);
        draw_logo(40, 14, C_NEON);
        s_sprite(8, 72, SPR_BUST, 48, 64, P2, 0, 2, 0);
        for (int r = 0; r < h; ++r) { /* a card torn from the atlas index */
            int sy = y + r;
            if (sy < 88 || sy >= 192) continue;
            for (int x = 120; x < 312; ++x) strip[r * SCR_W + x] = paper(x, sy);
        }
        strip_end();
    }
    layer = save;
    text(68, 68, "THE PARALLELOGRAM FORCE", C_NEON, C_NAVY);
    text(128, 94, "NAPLES, 2010. A MASKED", C_INK, C_PAPER);
    text(128, 104, "HERO RESTORES DECORO.", C_INK, C_PAPER);
    text(128, 114, "NO FIGHTS, NO ARRESTS.", C_MAGENTA, C_PAPER);
    text(128, 130, "A: CLEAN  B: SHIELD", C_INK, C_PAPER);
    text(128, 140, "START: MAP / TOMBINO", C_INK, C_PAPER);
    text(128, 150, "4 RIONI BEFORE DAWN", C_INK, C_PAPER);
    text(128, 172, (frame & 16u) ? "PRESS A TO PATROL" : "                 ", C_MAGENTA, C_PAPER);
}

static void draw_end(void) {
    char line[40];
    if (frame_dirty) {
        for (int y = 0; y < SCR_H; y += STRIP_H) {
            int h = SCR_H - y < STRIP_H ? SCR_H - y : STRIP_H;
            draw_paper(y, h);
            s_rect(0, 10, SCR_W, 24, C_NAVY);
            s_sprite(248, 60, SPR_BUST, 48, 64, P2, 0, 1, 0);
            strip_end();
        }
        text(won ? 68 : 60, 18, won ? "NAPOLI SHINES AT DAWN!" : "DAWN: THE PATROL IS OVER", C_NEON, C_NAVY);
        for (int z = 0; z < ZONE_COUNT; ++z) {
            char *p = put_str(line, ZONE_NAMES[z]);
            pad_line(line, 16);
            p = put_num(line + 16, (uint32_t)(zone_done[z] * 100 / zone_total[z]), 3);
            put_str(p, "/100");
            text(24, 52 + z * 14, line, zone_complete[z] ? C_MAGENTA : C_INK, C_PAPER);
        }
        char *p = put_str(line, "SCORE ");
        put_num(p, score, 6);
        text(24, 116, line, C_INK, C_PAPER);
        text(24, 136, won ? "THE TEPPISTELLI NOW READ BOOKS" : "EVERY CLEAN WALL STAYS CLEAN.", C_INK, C_PAPER);
        text(24, 146, won ? "AND PLANT FLOWERS. GRAZIE!" : "TRY AGAIN TOMORROW NIGHT.", C_INK, C_PAPER);
    }
    text(24, 172, (frame & 16u) ? "A: SAVE SCORE" : "             ", C_MAGENTA, C_PAPER);
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */
static void bands(const char *top, const char *bottom) {
    prg32_band_set_mode(PRG32_BAND_TOP, PRG32_BAND_MODE_CUSTOM);
    prg32_band_set_mode(PRG32_BAND_BOTTOM, PRG32_BAND_MODE_CUSTOM);
    /* Through col(), like every other colour: on the indexed LCD a raw
     * RGB value could land in a palette cell the game has redefined. */
    prg32_band_set_colors(PRG32_BAND_TOP, col(P2 + 11), col(C_NAVY));
    prg32_band_set_colors(PRG32_BAND_BOTTOM, col(C_PAPER), col(C_NAVY));
    prg32_band_set_text(PRG32_BAND_TOP, top);
    prg32_band_set_text(PRG32_BAND_BOTTOM, bottom);
}

void entomo_init(void) {
    has_audio = (host_features() & PRG32_FEATURE_AUDIO) != 0;
    palette_init();
    bands("ENTOMO: THE PARALLELOGRAM FORCE", "NAPOLI 2010 - PRG32");
    frame = 0;
    pad = pad_prev = 0;
    for (int i = 0; i < 8; ++i) sfx_queue[i].volume = 0;
    game_reset();
    state = ST_TITLE;
    frame_dirty = 1;
    music(TRK_TITLE);
}

void entomo_update(void) {
    pad_prev = pad;
    pad = prg32_input_read();
    uint32_t pressed = pad & ~pad_prev;
    ++frame;
    sfx_tick();
    if (state == ST_TITLE) {
        if (pressed & PRG32_BTN_A) {
            game_reset();
            state = ST_PLAY;
            frame_dirty = 1;
            music(TRK_SURFACE);
            say("GALLERIA UMBERTO I, 22:00", 0, 90);
        }
    } else if (state == ST_PLAY) {
        update_play();
    } else if (state == ST_MAP) {
        if (pressed & (PRG32_BTN_SELECT | PRG32_BTN_B)) {
            state = ST_PLAY;
            frame_dirty = 1;
        }
    } else if (pressed & PRG32_BTN_A) {
        if (!score_sent) {
            score_sent = 1;
            prg32_score_submit_current_player("entomo", score);
            prg32_scoreboard_show("entomo", "ENTOMO - DECORO");
        }
        state = ST_TITLE;
        frame_dirty = 1;
        music(TRK_TITLE);
    }
}

void entomo_draw(void) {
    palette_animate();
    if (state == ST_TITLE) {
        draw_title();
    } else if (state == ST_PLAY) {
        if (frame_dirty) draw_frame_chrome();
        draw_play();
        if (frame_dirty || (frame & 3u) == 0) draw_hud();
    } else if (state == ST_MAP) {
        draw_map();
    } else {
        draw_end();
    }
    frame_dirty = 0;
}
