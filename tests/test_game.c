/*
 * Gameplay tests: the unmodified cartridge source on the host harness.
 *
 *   test_game            run the checks
 *   test_game DIR        also write one PPM screenshot per scene into DIR
 */
#include "host_prg32.h"

#include "../src/entomo.c"

static int failures;
static const char *shots;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            ++failures;                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);         \
            printf("\n");                \
        }                                \
    } while (0)

static void step(uint32_t input, int frames) {
    for (int i = 0; i < frames; ++i) {
        host_input = input;
        host_ms += 33;
        ++host_frame_no;
        entomo_update();
        entomo_draw();
    }
}

static void shot(const char *name) {
    char path[512];
    if (!shots) return;
    snprintf(path, sizeof(path), "%s/%s.ppm", shots, name);
    host_save_ppm(path);
}

static void boot(int indexed) {
    host_display_reset(indexed);
    host_ms = 1000;
    host_input = 0;
    host_note_count = 0;
    entomo_init();
}

static void start_game(void) {
    step(0, 3);
    step(PRG32_BTN_A, 2);
    step(0, 2);
}

/* Put the hero on a tile of the active layer. */
static void warp(int tx, int ty) {
    hero_x = tx * 16 + 8;
    hero_y = ty * 16 + 12;
    find_targets();
}

static int task_index(int want_layer, int kind, int zone) {
    for (int i = 0; i < TASK_COUNT; ++i) {
        if ((TASKS[i][2] & 1) == want_layer && (TASKS[i][2] >> 1) == kind && zone_of(TASKS[i][0], TASKS[i][1]) == zone &&
            task_state[i] != TASK_DONE)
            return i;
    }
    return -1;
}

static void test_tables(void) {
    int links = 0;
    for (int y = 0; y < MAP_N; ++y) {
        for (int x = 0; x < MAP_N; ++x) {
            uint8_t s = map_at(napoli_surface_map, x, y), u = map_at(napoli_subterranean_map, x, y);
            CHECK((s == T_LINK) == (u == T_LINK), "shaft at %d,%d is not in both layers", x, y);
            links += s == T_LINK;
        }
    }
    CHECK(links == NODE_COUNT, "%d shafts, expected %d", links, NODE_COUNT);
    for (int i = 0; i < NODE_COUNT; ++i)
        CHECK(map_at(napoli_surface_map, PLACES[i][0], PLACES[i][1]) == T_LINK, "place %d is not a shaft", i);
    for (int i = 0; i < TASK_COUNT; ++i) {
        int l = TASKS[i][2] & 1;
        uint8_t t = map_at(l ? napoli_subterranean_map : napoli_surface_map, TASKS[i][0], TASKS[i][1]);
        CHECK(((l ? WALK_UNDER : WALK_SURFACE) >> t) & 1u, "task %d stands on a solid tile", i);
    }
    for (int i = 0; i < SPAWN_COUNT; ++i)
        CHECK((WALK_SURFACE >> map_at(napoli_surface_map, SPAWNS[i][0], SPAWNS[i][1])) & 1u, "actor %d in a wall", i);
    CHECK((WALK_SURFACE >> map_at(napoli_surface_map, START_TX, START_TY)) & 1u, "start in a wall");
    printf("tables: %d shafts, %d tasks, %d actors\n", links, TASK_COUNT, SPAWN_COUNT);
}

static void test_title_and_walk(void) {
    boot(0);
    step(0, 20);
    CHECK(state == ST_TITLE, "boots to the title");
    CHECK(host_track == TRK_TITLE, "title music");
    shot("title");
    start_game();
    CHECK(state == ST_PLAY && host_track == TRK_SURFACE, "A starts the patrol");
    step(0, 10);
    shot("galleria");
    int x0 = hero_x, y0 = hero_y;
    step(PRG32_BTN_DOWN, 20);
    step(PRG32_BTN_RIGHT, 20);
    step(PRG32_BTN_UP, 20);
    step(PRG32_BTN_LEFT, 20);
    CHECK(hero_x != x0 || hero_y != y0, "the hero moves");
    CHECK(box_free(hero_x, hero_y), "the hero never ends inside a wall");
    /* Walk at random for a while: never inside a wall, never off the map. */
    for (int i = 0; i < 600; ++i) {
        static const uint32_t dirs[4] = {PRG32_BTN_LEFT, PRG32_BTN_RIGHT, PRG32_BTN_UP, PRG32_BTN_DOWN};
        step(dirs[(i / 25) & 3] | dirs[(i / 40 + 1) & 3], 1);
        CHECK(box_free(hero_x, hero_y), "hero in a wall at frame %d", i);
    }
    for (int i = 0; i < ACTOR_MAX; ++i) {
        uint8_t t = map_at(napoli_surface_map, actors[i].x >> 4, actors[i].y >> 4);
        CHECK((WALK_SURFACE >> t) & 1u, "actor %d walked into a wall", i);
    }
    warp(48, 60);
    step(0, 4);
    shot("quartieri");
    warp(118, 112);
    step(0, 4);
    shot("beverello");
    step(PRG32_BTN_SELECT, 1);
    step(0, 3);
    CHECK(state == ST_MAP, "START away from a manhole opens the atlas");
    shot("map_surface");
    step(PRG32_BTN_SELECT, 1);
    step(0, 2);
    CHECK(state == ST_PLAY, "START closes the atlas");
}

static void test_layers(void) {
    boot(0);
    start_game();
    int sx = PLACES[0][0], sy = PLACES[0][1];
    warp(sx, sy);
    int wx = hero_x, wy = hero_y;
    step(PRG32_BTN_SELECT, 1);
    step(0, 3);
    CHECK(layer == 1 && map_cur == napoli_subterranean_map, "SELECT on a manhole swaps the map pointer");
    CHECK(hero_x == wx && hero_y == wy, "coordinates are continuous across the layers");
    CHECK(host_track == TRK_UNDER, "underground music");
    CHECK(box_free(hero_x, hero_y), "the foot of the shaft is free");
    shot("underground");
    step(0, 30);
    int before = host_note_count, blips = 0, pans[3] = {0, 0, 0};
    sfx(I_BLIP, 80, 200, hero_x + 100);
    step(0, 20);
    for (int n = before; n < host_note_count; ++n) {
        if (host_notes[n].instrument == I_BLIP && blips < 3) pans[blips++] = host_notes[n].pan;
    }
    CHECK(blips == 3, "a sound below ground has two echoes (%d notes)", blips);
    CHECK(pans[0] > 0 && pans[1] < 0 && pans[2] > 0, "the echoes answer from alternate sides");
    step(PRG32_BTN_DOWN, 12);
    step(PRG32_BTN_SELECT, 1);
    step(0, 3);
    CHECK(state == ST_MAP, "START off the staircase opens the atlas") ;
    shot("map_underground");
    step(PRG32_BTN_SELECT, 1);
    warp(sx, sy);
    step(0, 2);
    step(PRG32_BTN_SELECT, 1);
    step(0, 2);
    CHECK(layer == 0 && map_cur == napoli_surface_map, "and back up");
    /* A big cistern, for the screenshot. */
    warp(PLACES[8][0], PLACES[8][1]);
    step(PRG32_BTN_SELECT, 1);
    step(0, 2);
    step(PRG32_BTN_DOWN, 30);
    shot("ricovero");
}

static void test_shield(void) {
    boot(0);
    start_game();
    actor_t *a = &actors[0];
    warp(a->x >> 4, a->y >> 4);
    hero_x = a->x - 20;
    hero_y = a->y;
    hero_safe = 0;
    a->dx = 1;
    a->dy = 0;
    a->state = A_WALK;
    a->vx = 20;
    a->vy = 0;
    int minutes_before = clock_frames;
    uint32_t score_before = score;
    step(PRG32_BTN_B, 1);
    CHECK(shield_on, "B raises the shield");
    CHECK(a->state == A_BOUNCE, "the shield bounces the actor");
    CHECK(a->vx == -40 || a->vx == -24 * 0 - 40, "velocity is inverted and doubled (vx=%d)", a->vx);
    CHECK(score == score_before + 25, "a bounce scores");
    CHECK(clock_frames - minutes_before < 5, "a bounce costs no time");
    step(PRG32_BTN_B, 4);
    shot("shield");
    step(PRG32_BTN_B, 400);
    CHECK(!shield_on && forza < FORZA_MIN_START * 3, "the shield runs out (forza=%d)", forza);
    step(0, 300);
    CHECK(forza > FORZA_MIN_START, "and recharges");
    /* Without the shield a contact costs time, nothing else. */
    a->state = A_WALK;
    a->dx = a->dy = 0;
    hero_x = a->x;
    hero_y = a->y;
    hero_safe = 0;
    int before = clock_frames;
    step(0, 1);
    CHECK(clock_frames - before >= CONTACT_MINUTES * FRAMES_PER_MINUTE, "a contact costs time");
    CHECK(state == ST_PLAY, "and the game goes on");
}

static void clean(int i) {
    layer = TASKS[i][2] & 1;
    map_cur = layer ? napoli_subterranean_map : napoli_surface_map;
    warp(TASKS[i][0], TASKS[i][1]);
    for (int k = 0; k < ACTOR_MAX; ++k) { /* keep the crowd away */
        if (iabs(actors[k].x - hero_x) + iabs(actors[k].y - hero_y) < 64) actors[k].state = A_CITIZEN;
    }
    step(PRG32_BTN_A, 60);
}

static void test_decoro(void) {
    boot(0);
    start_game();
    int i = task_index(0, KIND_GRAFFITI, 0);
    CHECK(i >= 0, "a graffiti in zone 0");
    layer = 0;
    warp(TASKS[i][0], TASKS[i][1]);
    step(0, 2);
    shot("graffiti");
    step(PRG32_BTN_A, 12);
    CHECK(task_state[i] > 0 && task_state[i] != TASK_DONE, "A scrubs (progress %d)", task_state[i]);
    shot("scrubbing");
    step(PRG32_BTN_A, 40);
    CHECK(task_state[i] == TASK_DONE, "the wall is clean");
    CHECK(zone_done[0] == 1 && score >= 100, "decoro and score go up");
    int scrubs = 0;
    for (int n = 0; n < host_note_count; ++n) scrubs += host_notes[n].instrument == I_SCRUB;
    CHECK(scrubs >= 8, "scrubbing is audible (%d)", scrubs);

    /* Finish zone 0: its troublemakers become citizens. */
    int walkers = 0;
    for (int k = 0; k < ACTOR_MAX; ++k) walkers += actors[k].zone == 0;
    for (int k = 0; k < TASK_COUNT; ++k) {
        if (zone_of(TASKS[k][0], TASKS[k][1]) == 0 && task_state[k] != TASK_DONE) clean(k);
    }
    CHECK(zone_complete[0], "zone 0 complete (%d/%d)", zone_done[0], zone_total[0]);
    int citizens = 0;
    for (int k = 0; k < ACTOR_MAX; ++k) citizens += actors[k].zone == 0 && actors[k].state == A_CITIZEN;
    CHECK(citizens == walkers && walkers > 0, "all %d troublemakers of the rione became citizens (%d)", walkers, citizens);
    layer = 0;
    map_cur = napoli_surface_map;
    for (int k = 0; k < ACTOR_MAX; ++k) {
        if (actors[k].zone == 0) {
            warp(actors[k].x >> 4, (actors[k].y >> 4));
            hero_x += 20;
            break;
        }
    }
    step(0, 3);
    shot("citizens");
    for (int k = 0; k < TASK_COUNT; ++k) {
        if (task_state[k] != TASK_DONE) clean(k);
    }
    step(0, 3);
    CHECK(state == ST_END && won, "all four rioni: the night is won");
    CHECK(host_track == TRK_ENDING, "ending music");
    step(0, 20);
    shot("ending");
    step(PRG32_BTN_A, 1);
    step(0, 2);
    CHECK(host_score_count == 1 && host_scoreboard_shown == 1 && host_score_last == score, "the score is saved once");
    CHECK(state == ST_TITLE, "back to the title");
}

static void test_dawn(void) {
    boot(0);
    start_game();
    clock_frames = NIGHT_MINUTES * FRAMES_PER_MINUTE - 3;
    step(0, 6);
    CHECK(state == ST_END && !won, "dawn ends the patrol");
    shot("dawn");
}

/* The frame used as the Cartridge Store screenshot: Via Speranzella, a
 * graffiti on the wall, the shield up and a teppistello bouncing off it. */
static void test_store_frame(void) {
    boot(0);
    start_game();
    int i = task_index(0, KIND_GRAFFITI, 0);
    warp(TASKS[i][0], TASKS[i][1]);
    hero_x += 34;
    for (int k = 0; k < ACTOR_MAX; ++k) actors[k].state = A_CITIZEN;
    actor_t *a = &actors[0];
    a->state = A_WALK;
    a->x = (int16_t)(hero_x + 22);
    a->y = (int16_t)hero_y;
    a->dx = -1;
    a->dy = 0;
    a->vx = -20;
    hero_safe = 0;
    msg_timer = 0;
    step(PRG32_BTN_B, 7);
    CHECK(shield_on && a->state == A_BOUNCE, "the store frame shows a bounce");
    shot("store");
}

/* Every kind of display must show exactly the same colours. */
static void test_palette(void) {
    static uint16_t ref[PRG32_GAME_H][PRG32_GAME_W];
    static const char *const names[3] = {"RGB565 surface", "indexed, system cube", "indexed, nearest entry"};
    for (int kind = 0; kind < 3; ++kind) {
        int differ = 0;
        boot(kind);
        CHECK(display == kind, "display kind detected (%d, expected %d)", display, kind);
        start_game();
        rng = 1234;
        warp(48, 60);
        step(PRG32_BTN_B, 6);
        for (int y = 0; y < PRG32_GAME_H; ++y) {
            for (int x = 0; x < PRG32_GAME_W; ++x) {
                if (!kind) ref[y][x] = host_pixel(x, y);
                else differ += ref[y][x] != host_pixel(x, y);
            }
        }
        CHECK(differ == 0, "%d pixels differ on the %s display", differ, names[kind]);
        if (kind == 1) shot("indexed_lcd");
    }
}

/* ---------------------------------------------------------------- */
/* A whole night, played by a bot through the controller alone       */
/* ---------------------------------------------------------------- */
static int bot_queue[2 * MAP_N * MAP_N];
static int bot_prev[2 * MAP_N * MAP_N];

static int bot_state(int l, int tx, int ty) { return (l * MAP_N + ty) * MAP_N + tx; }

static int bot_goal(int l, int tx, int ty) {
    for (int i = 0; i < TASK_COUNT; ++i) {
        if (task_state[i] != TASK_DONE && (TASKS[i][2] & 1) == l && TASKS[i][0] == tx && TASKS[i][1] == ty) return 1;
    }
    return 0;
}

/* Breadth-first search over both layers (the shafts join them) to the
 * nearest unfinished task. Returns the first state on the way, the start
 * state when the hero already stands on a task, or -1. */
static int bot_plan(void) {
    int start = bot_state(layer, hero_x >> 4, (hero_y - 3) >> 4), head = 0, tail = 0, found = -1;
    memset(bot_prev, 0xff, sizeof(bot_prev));
    bot_prev[start] = start;
    bot_queue[tail++] = start;
    while (head < tail) {
        int cur = bot_queue[head++], l = cur / (MAP_N * MAP_N), ty = cur / MAP_N % MAP_N, tx = cur % MAP_N;
        const uint8_t *m = l ? napoli_subterranean_map : napoli_surface_map;
        uint32_t walk = l ? WALK_UNDER : WALK_SURFACE;
        if (bot_goal(l, tx, ty)) {
            found = cur;
            break;
        }
        static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
        for (int d = 0; d < 5; ++d) {
            int next;
            if (d < 4) {
                if (!((walk >> map_at(m, tx + DX[d], ty + DY[d])) & 1u)) continue;
                next = bot_state(l, tx + DX[d], ty + DY[d]);
            } else {
                if (map_at(m, tx, ty) != T_LINK) continue;
                next = bot_state(1 - l, tx, ty);
            }
            if (bot_prev[next] >= 0) continue;
            bot_prev[next] = cur;
            bot_queue[tail++] = next;
        }
    }
    if (found < 0) return -1;
    while (bot_prev[found] != start) found = bot_prev[found];
    return found;
}

static void test_bot(void) {
    boot(0);
    start_game();
    int frames = 0, stuck = 0, last_x = 0, last_y = 0, switches = 0, bounces_score = 0;
    uint32_t select_prev = 0;
    while (state == ST_PLAY && frames < 40000) {
        uint32_t in = 0;
        int here = bot_state(layer, hero_x >> 4, (hero_y - 3) >> 4);
        int next = bot_plan();
        if (next < 0) break;
        int danger = 0;
        for (int i = 0; i < ACTOR_MAX && !layer; ++i) {
            if (actors[i].state != A_CITIZEN && iabs(actors[i].x - hero_x) < 44 && iabs(actors[i].y - hero_y) < 40) danger = 1;
        }
        if (danger && (shield_on || forza >= FORZA_MIN_START)) in |= PRG32_BTN_B;
        if (next == here) {
            int gx = (hero_x & ~15) + 8, gy = (hero_y & ~15) + 12;
            if (iabs(hero_x - gx) > 4) in |= hero_x < gx ? PRG32_BTN_RIGHT : PRG32_BTN_LEFT;
            else if (!danger) in |= PRG32_BTN_A;
            (void)gy;
        } else if (next / (MAP_N * MAP_N) != layer) {
            if (!select_prev) {
                in |= PRG32_BTN_SELECT;
                ++switches;
            }
        } else {
            int gx = (next % MAP_N) * 16 + 8, gy = (next / MAP_N % MAP_N) * 16 + 12;
            if (hero_x < gx - 2) in |= PRG32_BTN_RIGHT;
            if (hero_x > gx + 2) in |= PRG32_BTN_LEFT;
            if (hero_y < gy - 2) in |= PRG32_BTN_DOWN;
            if (hero_y > gy + 2) in |= PRG32_BTN_UP;
        }
        select_prev = in & PRG32_BTN_SELECT;
        step(in, 1);
        if (state == ST_MAP) { /* a misjudged START opened the atlas: close it */
            step(0, 1);
            step(PRG32_BTN_SELECT, 1);
            step(0, 1);
            ++bounces_score;
        }
        ++frames;
        if (hero_x == last_x && hero_y == last_y && !(in & (PRG32_BTN_A | PRG32_BTN_SELECT))) ++stuck;
        else stuck = 0;
        last_x = hero_x;
        last_y = hero_y;
        if (stuck > 90) {
            CHECK(0, "the bot is stuck at %d,%d layer %d", hero_x >> 4, hero_y >> 4, layer);
            break;
        }
        CHECK(box_free(hero_x, hero_y), "bot inside a wall");
        if (shots && frames == 2400) shot("patrol");
    }
    int clock = (22 * 60 + minutes) % (24 * 60);
    printf("bot: %s at %02d:%02d after %d frames, score %u, %d layer changes, %d atlas slips\n",
           won ? "all four rioni clean" : "night not finished", clock / 60, clock % 60, frames, (unsigned)score, switches, bounces_score);
    CHECK(state == ST_END && won, "a careful player can finish the night");
    CHECK(minutes < NIGHT_MINUTES * 3 / 4, "with time to spare (%d of %d minutes)", minutes, NIGHT_MINUTES);
}

int main(int argc, char **argv) {
    shots = argc > 1 ? argv[1] : NULL;
    test_tables();
    test_title_and_walk();
    test_layers();
    test_shield();
    test_decoro();
    test_dawn();
    test_store_frame();
    test_palette();
    test_bot();
    CHECK(host_bad_chars == 0, "%d non-portable or clipped characters", host_bad_chars);
    CHECK(host_sprite_errors == 0, "%d invalid strip draws", host_sprite_errors);
    printf("game: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
