/* Level generation, physics and hazards. No graphics code here, so the
   level checker (tools/levelcheck) can run all 150 levels headless. */
#pragma once
#include <stdint.h>

#define NUM_LEVELS   150
#define MAX_PLATS    128   /* the level editor can place up to this many pieces */
#define MAX_HAZ      128
#define MAX_ENEMIES  96
#define MAX_WAY      120

#define PLAYER_R     0.40f   /* half width of M.I.K.E's collision box */
#define PLAYER_H     0.70f
#define GRAVITY      30.0f
#define JUMP_VEL     11.0f
#define MOVE_SPEED   6.5f
#define ENEMY_R      0.45f
#define PLAT_THICK   0.6f

enum { PLAT_NORMAL, PLAT_START, PLAT_FINISH };
enum { HZ_SPIKES, HZ_LAVA };

typedef struct { float x0, y0, z0, x1, y1, z1; int kind; float fx, fz; int checkpoint, turn; } Plat;  /* fx,fz = course heading; turn: -1 / 1 = turns the screen left / right */
typedef struct { int type; float x0, z0, x1, z1, y; } Hazard;
typedef struct { float x, y, z; int axis; float a, b, dir, speed, phase; } Enemy;
typedef struct { float x, z; } Way;

typedef struct {
    int num;                       /* 1..150 */
    int nplat, nhaz, nen, nway;
    Plat plat[MAX_PLATS];
    Hazard haz[MAX_HAZ];
    Enemy en[MAX_ENEMIES];
    Way way[MAX_WAY];
    float lava_y;
    float spawn_x, spawn_y, spawn_z;
} Level;

typedef struct {
    float x, y, z, vx, vy, vz;
    float face;          /* facing angle (radians) */
    int grounded, ground_plat;
    float coyote, jumpbuf;
    int jumped;          /* set by player_step on the step a jump starts (for the jump sound) */
    int double_used;     /* the one extra jump in the air has been used (back on landing) */
} Player;

enum { SIM_OK, SIM_DEAD, SIM_FINISH };

void level_build(Level *L, int num);

/* ---------- level editor grid ---------- */
#define ED_W     7          /* columns */
#define ED_H     40         /* rows, the course runs from row 0 forward */
#define ED_CELL  2.0f       /* each grid square, in world units */
#define ED_STEP  0.6f       /* one height step */
#define ED_MAXH  4
enum { T_EMPTY, T_PAD, T_SPIKES, T_LAVA, T_SAD, T_CHECK, T_START, T_FINISH, T_TURN_L, T_TURN_R, T_COUNT };
/* a tile byte: bits 0-2 type, 3-5 height, 6 type bit 3 (the screen turn pieces) */
#define TILE_TYPE(t)   (((t) & 7) | (((t) >> 3) & 8))
#define TILE_H(t)      (((t) >> 3) & 7)
#define TILE(type, h)  ((uint8_t)(((type) & 7) | ((h) << 3) | (((type) & 8) << 3)))
/* builds a level from an editor grid (ED_W * ED_H tiles, row-major from row 0).
   Returns 1 when it can be played (it has a start and a finish). */
int level_from_grid(Level *L, const uint8_t *grid);
/* custom course: num gates which hazards appear (as for level num), d = difficulty 0..1,
   count = platforms before the finish, cp_every = checkpoint pad every N platforms (0 = none) */
void level_build_ex(Level *L, int num, uint32_t seed, int count, float d, int cp_every);
/* race-only: widen sad face pads (patrol stays the same width), scale their speed,
   and put a checkpoint right after the first sad face pad. Call before level_build_ex. */
void level_race_options(float sad_widen, float sad_speed, int cp_after_first_sad);
void player_reset(Player *p, const Level *L);
void enemies_update(Level *L, float dt);
/* mx,mz: desired move direction in world space (length 0..1). jump_pressed: edge-triggered */
int  player_step(Player *p, Level *L, float mx, float mz, int jump_pressed, int jump_held, float dt);
int  point_on_safe_ground(const Level *L, float x, float z, float feet_y);
int  point_in_hazard(const Level *L, float x, float z, float feet_y);
