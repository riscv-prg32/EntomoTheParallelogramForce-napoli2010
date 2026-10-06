/*
 * Host implementation of the PRG32 calls used by Entomo.
 *
 * A test includes this header and then src/entomo.c, so the unmodified
 * cartridge source runs on the workstation against a model of the three
 * kinds of PRG32 display:
 *
 *   host_indexed = 0   RGB565 surface (QEMU firmware): sprites and text
 *                      write colours, palette changes do not touch pixels
 *                      already drawn;
 *   host_indexed = 1   the ILI9341 backend: an 8-bit framebuffer over a
 *                      256-entry palette; every RGB565 colour is quantised
 *                      to the firmware's named colours or 6x6x6 cube;
 *   host_indexed = 2   PRG32-QT: an 8-bit framebuffer over an RGB332
 *                      palette; a colour takes the nearest entry of the
 *                      palette as it is now, and a snapshot row is a game
 *                      row (no status band above it).
 *
 * Declarations come from the real PRG32 headers, so a signature that
 * differs from the firmware is a compile error. The font is the firmware
 * font. Adapted from tests/host/host_prg32.h of Drumfight Napoli 97 (PRG32
 * project family, MIT).
 */
#ifndef ENTOMO_HOST_PRG32_H
#define ENTOMO_HOST_PRG32_H

#include <stdio.h>
#include <string.h>

#define ENTOMO_HOST 1
#include "prg32.h"

#include "font8.h"

#define HOST_MAX_NOTES 16384

typedef struct {
    uint32_t frame;
    uint8_t channel, instrument, note, volume;
    int8_t pan;
} host_note_t;

static int host_indexed;
static uint16_t host_rgb[PRG32_GAME_H][PRG32_GAME_W];
static uint8_t host_idx[PRG32_GAME_H][PRG32_GAME_W];
static uint16_t host_palette[256];
static uint32_t host_ms, host_frame_no, host_input;
static uint32_t host_features = PRG32_FEATURE_AUDIO | PRG32_FEATURE_AUDIO_PLUS | PRG32_FEATURE_SPRITES;
static host_note_t host_notes[HOST_MAX_NOTES];
static int host_note_count, host_note_offs;
static int host_track = -1, host_track_changes;
static int host_bad_chars;
static char host_band[2][64];
static uint32_t host_score_last;
static int host_score_count, host_scoreboard_shown;
static long host_pixels;

uint32_t entomo_host_features(void) { return host_features; }

static uint8_t host_quantise(uint16_t color) {
    if (host_indexed == 2) {
        int best = 1 << 30, index = 0, r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
        for (int i = 0; i < 256; ++i) {
            uint16_t p = host_palette[i];
            int dr = r - ((p >> 11) & 31), dg = g - ((p >> 5) & 63), db = b - (p & 31);
            int d = dr * dr + dg * dg + db * db;
            if (d < best) {
                best = d;
                index = i;
                if (!d) break;
            }
        }
        return (uint8_t)index;
    }
    static const uint16_t named[8] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f};
    for (int i = 0; i < 8; ++i) {
        if (color == named[i]) return (uint8_t)i;
    }
    unsigned r = ((color >> 11) & 31u) * 5u / 31u, g = ((color >> 5) & 63u) * 5u / 63u, b = (color & 31u) * 5u / 31u;
    return (uint8_t)(16u + r * 36u + g * 6u + b);
}

static void host_display_reset(int indexed) {
    static const uint16_t named[8] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f};
    host_indexed = indexed;
    memset(host_rgb, 0, sizeof(host_rgb));
    memset(host_idx, 0, sizeof(host_idx));
    memset(host_palette, 0, sizeof(host_palette));
    for (int i = 0; i < 8; ++i) host_palette[i] = named[i];
    for (int i = 0; i < 216; ++i) {
        unsigned r = (unsigned)i / 36u, g = (unsigned)i / 6u % 6u, b = (unsigned)i % 6u;
        host_palette[16 + i] = (uint16_t)(((r * 31u / 5u) << 11) | ((g * 63u / 5u) << 5) | (b * 31u / 5u));
    }
    if (indexed == 2) {
        for (unsigned i = 0; i < 256; ++i) {
            unsigned r = (i >> 5) & 7u, g = (i >> 2) & 7u, b = i & 3u;
            host_palette[i] = (uint16_t)(((r * 31u / 7u) << 11) | ((g * 63u / 7u) << 5) | (b * 31u / 3u));
        }
        host_palette[0] = 0;
        host_palette[255] = 0xffff;
    }
}

/* What the LCD shows at (x, y). */
static uint16_t host_pixel(int x, int y) { return host_indexed ? host_palette[host_idx[y][x]] : host_rgb[y][x]; }

static void host_plot(int x, int y, uint16_t color) {
    if ((unsigned)x >= PRG32_GAME_W || (unsigned)y >= PRG32_GAME_H) return;
    host_rgb[y][x] = color;
    host_idx[y][x] = host_quantise(color);
    ++host_pixels;
}

uint32_t prg32_ticks_ms(void) { return host_ms; }
uint32_t prg32_input_read(void) { return host_input; }

void prg32_gfx_pixel(int x, int y, uint16_t color) { host_plot(x, y, color); }

void prg32_gfx_rect(int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) host_plot(px, py, color);
    }
}

/* PRG32-QT and PRG32-iOS draw text with a reduced font: capitals, digits
 * and these signs. Anything else would show as '?' there. */
static int host_portable_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr(" ?!.,:-+/", c));
}

void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    for (; *s; ++s, x += 8) {
        unsigned ch = (unsigned char)*s;
        if (!host_portable_char(*s)) {
            ++host_bad_chars;
            printf("non-portable character '%c' drawn at %d,%d\n", *s, x, y);
        }
        if (x + 8 > PRG32_GAME_W) {
            ++host_bad_chars;
            printf("text runs off the screen at %d,%d\n", x, y);
        }
        if (ch < 32 || ch > 126) ch = '?';
        for (int row = 0; row < 8; ++row) {
            unsigned bits = host_font8[ch - 32][row];
            for (int col = 0; col < 8; ++col) host_plot(x + col, y + row, (bits & (0x80u >> col)) ? fg : bg);
        }
    }
}

void prg32_palette_set(uint8_t index, uint16_t rgb565) { host_palette[index] = rgb565; }
uint16_t prg32_palette_get(uint8_t index) { return host_palette[index]; }

void prg32_gfx_pixel_indexed(int x, int y, uint8_t index) {
    if ((unsigned)x >= PRG32_GAME_W || (unsigned)y >= PRG32_GAME_H) return;
    host_rgb[y][x] = host_palette[index];
    host_idx[y][x] = index;
}

int prg32_gfx_snapshot_row_rgb565(int y, uint16_t *out, size_t pixels) {
    if (!out || pixels < PRG32_LCD_W || (unsigned)y >= PRG32_LCD_H) return -1;
    int row = host_indexed == 2 ? y : y - 20;
    for (int x = 0; x < PRG32_LCD_W; ++x) out[x] = (row >= 0 && row < PRG32_GAME_H) ? host_pixel(x, row) : 0;
    return PRG32_LCD_W;
}

static int host_sprite_errors;
void prg32_sprite_draw_indexed(int x, int y, const prg32_indexed_sprite_t *s, uint32_t frame) {
    (void)frame;
    if (!s || !s->pixels || !s->palette || s->bits_per_pixel != 8 || !s->width || !s->height) {
        ++host_sprite_errors;
        return;
    }
    for (int r = 0; r < s->height; ++r) {
        for (int c = 0; c < s->width; ++c) {
            uint8_t v = s->pixels[r * s->width + c];
            if (v >= s->palette_count) {
                ++host_sprite_errors;
                continue;
            }
            host_plot(x + c, y + r, s->palette[v]);
        }
    }
}

void prg32_band_set_mode(uint8_t band, prg32_band_mode_t mode) { (void)band; (void)mode; }
void prg32_band_set_colors(uint8_t band, uint16_t fg, uint16_t bg) { (void)band; (void)fg; (void)bg; }
void prg32_band_set_text(uint8_t band, const char *text) { snprintf(host_band[band & 1], sizeof(host_band[0]), "%s", text); }

void prg32_audio_play_track(uint16_t track_id) {
    host_track = track_id;
    ++host_track_changes;
}

void prg32_audio_note_on_pan(uint8_t channel, uint8_t instrument, uint8_t note, uint8_t volume, int8_t pan) {
    if (host_note_count < HOST_MAX_NOTES) {
        host_note_t *n = &host_notes[host_note_count++];
        n->frame = host_frame_no;
        n->channel = channel;
        n->instrument = instrument;
        n->note = note;
        n->volume = volume;
        n->pan = pan;
    }
}

void prg32_audio_note_off(uint8_t channel) {
    (void)channel;
    ++host_note_offs;
}

int prg32_score_submit_current_player(const char *game, uint32_t score) {
    (void)game;
    host_score_last = score;
    ++host_score_count;
    return 0;
}

int prg32_scoreboard_show(const char *game, const char *title) {
    (void)game;
    (void)title;
    ++host_scoreboard_shown;
    return 0;
}

static int host_save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", PRG32_GAME_W, PRG32_GAME_H);
    for (int y = 0; y < PRG32_GAME_H; ++y) {
        for (int x = 0; x < PRG32_GAME_W; ++x) {
            uint16_t c = host_pixel(x, y);
            unsigned r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
            fputc((int)((r << 3) | (r >> 2)), f);
            fputc((int)((g << 2) | (g >> 4)), f);
            fputc((int)((b << 3) | (b >> 2)), f);
        }
    }
    fclose(f);
    return 0;
}

#endif
