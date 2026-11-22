#include "level.h"
#include <math.h>
#include <string.h>

/* ---------- deterministic RNG so level N is always the same course ---------- */
static uint32_t rng_state;
static void rseed(uint32_t s) { rng_state = s * 2654435761u + 0x9E3779B9u; if (!rng_state) rng_state = 1; }
static uint32_t rnext(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
static float rf(float a, float b) { return a + (b - a) * ((rnext() & 0xFFFFFF) / 16777216.0f); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* ---------- generation ---------- */
static void plat_from_center(Plat *p, float cx, float top, float cz, float fx, float fz, float len, float wid, int kind)
{
    /* len runs along the heading, wid across it */
    float hx = fx != 0 ? len * 0.5f : wid * 0.5f;
    float hz = fz != 0 ? len * 0.5f : wid * 0.5f;
    p->x0 = cx - hx; p->x1 = cx + hx;
    p->z0 = cz - hz; p->z1 = cz + hz;
    p->y1 = top; p->y0 = top - PLAT_THICK;
    p->kind = kind; p->fx = fx; p->fz = fz;
}

static int overlaps_any(const Level *L, const Plat *n, int skip_from)
{
    for (int i = 0; i < skip_from; i++) {
        const Plat *p = &L->plat[i];
        if (n->x0 - 1.0f < p->x1 && n->x1 + 1.0f > p->x0 &&
            n->z0 - 1.0f < p->z1 && n->z1 + 1.0f > p->z0) return 1;
    }
    return 0;
}

static void add_way(Level *L, float x, float z) { if (L->nway < MAX_WAY) { L->way[L->nway].x = x; L->way[L->nway].z = z; L->nway++; } }

static void add_haz(Level *L, int type, float x0, float z0, float x1, float z1, float y)
{
    if (L->nhaz >= MAX_HAZ) return;
    Hazard *h = &L->haz[L->nhaz++];
    h->type = type; h->x0 = x0; h->z0 = z0; h->x1 = x1; h->z1 = z1; h->y = y;
}

/* point at (along, across) in a platform's own frame -> world */
static void plat_local(const Plat *p, float along, float across, float *wx, float *wz)
{
    float cx = (p->x0 + p->x1) * 0.5f, cz = (p->z0 + p->z1) * 0.5f;
    /* right vector = (-fz, fx) rotated so "across" is perpendicular */
    float rx = -p->fz, rz = p->fx;
    *wx = cx + p->fx * along + rx * across;
    *wz = cz + p->fz * along + rz * across;
}

static float plat_len(const Plat *p) { return p->fx != 0 ? p->x1 - p->x0 : p->z1 - p->z0; }
static float plat_wid(const Plat *p) { return p->fx != 0 ? p->z1 - p->z0 : p->x1 - p->x0; }

void level_build(Level *L, int num)
{
    memset(L, 0, sizeof *L);
    float d = (num - 1) / (float)(NUM_LEVELS - 1);           /* 0 .. 1 difficulty */
    int count = 6 + (int)(num * 0.12f);                       /* 6 .. 24 platforms + finish */
    level_build_ex(L, num, (uint32_t)num * 7919u + 13u, count, d, 0);
}

/* race-only tweaks (all off for the 150 single-player levels) */
static float race_sad_widen = 0.0f, race_sad_speed = 1.0f;
/* lanes with 3 sad faces (level 110 on): wider platform so the faces take longer to cross it, and slower faces */
#define SAD3_WIDEN 1.2f
#define SAD3_SPEED 0.65f
static int race_cp_after_sad = 0;
void level_race_options(float sad_widen, float sad_speed, int cp_after_first_sad)
{
    race_sad_widen = sad_widen; race_sad_speed = sad_speed; race_cp_after_sad = cp_after_first_sad;
}

void level_build_ex(Level *L, int num, uint32_t seed, int count, float d, int cp_every)
{
    int sad_cp_done = 0;
    memset(L, 0, sizeof *L);
    L->num = num;
    rseed(seed);
    if (count > MAX_PLATS - 2) count = MAX_PLATS - 2;

    /* start pad */
    plat_from_center(&L->plat[0], 0, 0, 0, 0, -1, 4.0f, 4.0f, PLAT_START);
    L->nplat = 1;
    L->spawn_x = 0; L->spawn_y = 0; L->spawn_z = 0.8f;
    add_way(L, 0, 0);

    int heading = 0;      /* 0 = -Z, 1 = -X, 2 = +X */
    int prev_hz = -1;     /* half-spike and sad face platforms must be left straight ahead */
    int en_start = 0;      /* index of the previous platform's first sad face */
    float minY = 0;

    for (int i = 1; i <= count; i++) {
        int last = (i == count);
        const Plat *prev = &L->plat[i - 1];

        /* pick heading: mostly forward, sometimes turn (never reverse) */
        int nh = heading;
        if (!last && i > 1 && prev_hz != 3 && prev_hz != 2 && rf(0, 1) < lerpf(0.12f, 0.35f, d)) {
            if (heading == 0) nh = rf(0, 1) < 0.5f ? 1 : 2;
            else nh = 0;
        }

        /* hazard type for this platform */
        int hz = -1;           /* -1 none, 0 spike strip, 1 lava strip, 2 sad faces, 3 half spikes */
        if (!last && i > 1) {
            float r = rf(0, 1), acc = 0;
            float p_spk = num >= 4 ? lerpf(0.15f, 0.25f, d) : 0;
            float p_lav = num >= 12 ? lerpf(0.10f, 0.20f, d) : 0;
            float p_sad = num >= 8 ? lerpf(0.15f, 0.24f, d) : 0;
            float p_half = (num >= 20 && nh == heading && prev_hz < 0) ? lerpf(0.08f, 0.15f, d) : 0;
            if (r < (acc += p_spk)) hz = 0;
            else if (r < (acc += p_lav)) hz = 1;
            else if (r < (acc += p_sad)) hz = 2;
            else if (r < (acc += p_half)) hz = 3;
        }

        float size = lerpf(3.2f, 1.5f, d);
        float len = clampf(size + rf(-0.5f, 0.6f), 1.2f, 4.0f);
        float wid = clampf(size + rf(-0.5f, 0.6f), 1.2f, 4.0f);
        if (hz == 0 || hz == 1) { len = rf(4.6f, 5.6f); wid = clampf(wid, 1.6f, 3.0f); }
        int nsad = 1 + (num >= 50) + (num >= 110);
        if (hz == 2) { len = rf(4.2f, 4.8f) + 2.2f * (nsad - 1) + (nsad > 1 ? 1.0f : 0.0f); wid = rf(3.2f, 4.2f) + (nsad == 3 ? SAD3_WIDEN + race_sad_widen * 0.5f : race_sad_widen); }
        if (hz == 3) { len = rf(2.4f, 3.2f); wid = rf(3.0f, 3.6f); }
        if (last) { len = wid = 3.4f; }
        int is_cp = cp_every && !last && (i % cp_every == 0 || (race_cp_after_sad && prev_hz == 2 && !sad_cp_done));
        if (is_cp && prev_hz == 2) sad_cp_done = 1;
        if (is_cp) { hz = -1; len = wid = 3.4f; }       /* checkpoint pads are big and safe */

        float dy = rf(-0.8f, lerpf(0.4f, 1.2f, d));
        float gap = lerpf(0.8f, 3.0f, d) + rf(0, lerpf(0.6f, 0.9f, d));
        if (dy > 0.6f && gap > 3.0f) gap = 3.0f;
        {   /* small take-off pad = less run-up, so shorten the jump */
            float pr = fminf(prev->x1 - prev->x0, prev->z1 - prev->z0);
            if (pr < 2.0f && gap > 2.8f) gap = 2.8f;
        }
        if (gap > 3.8f) gap = 3.8f;
        if (gap < 0.6f) gap = 0.6f;

        Plat np;
        for (int attempt = 0; attempt < 3; attempt++) {
            float fx = nh == 1 ? -1.0f : nh == 2 ? 1.0f : 0.0f;
            float fz = nh == 0 ? -1.0f : 0.0f;
            /* previous platform's half extent along the new heading, and across it */
            float pcx = (prev->x0 + prev->x1) * 0.5f, pcz = (prev->z0 + prev->z1) * 0.5f;
            float phf = fx != 0 ? (prev->x1 - prev->x0) * 0.5f : (prev->z1 - prev->z0) * 0.5f;
            float phr = fx != 0 ? (prev->z1 - prev->z0) * 0.5f : (prev->x1 - prev->x0) * 0.5f;
            float maxlat = phr + wid * 0.5f - 0.9f;
            float lat = clampf(rf(-1, 1) * lerpf(0.0f, 1.4f, d), -maxlat, maxlat);
            if (maxlat < 0) lat = 0;
            float along = phf + gap + len * 0.5f;
            float cx = pcx + fx * along + (-fz) * lat;
            float cz = pcz + fz * along + (fx) * lat;
            plat_from_center(&np, cx, prev->y1 + dy, cz, fx, fz, len, wid, last ? PLAT_FINISH : PLAT_NORMAL);
            if (!overlaps_any(L, &np, i - 1)) break;
            nh = 0;   /* fall back to straight ahead */
            gap = fminf(gap + 0.3f, 2.8f);
        }
        if (nh != heading) {
            /* a forced turn can't leave through half-spikes, and can't enter a half-spike pad sideways */
            if (prev_hz == 3 && L->nhaz > 0) L->nhaz--;
            if (prev_hz == 2) L->nen = en_start;   /* no turning out through sad face lanes */
            if (hz == 3) hz = -1;
            /* route point on the previous pad lined up with the new one (used by the level checker) */
            float pcx = (prev->x0 + prev->x1) * 0.5f, pcz = (prev->z0 + prev->z1) * 0.5f;
            float ncx = (np.x0 + np.x1) * 0.5f, ncz = (np.z0 + np.z1) * 0.5f;
            float pL2 = plat_len(prev) * 0.5f, pW2 = plat_wid(prev) * 0.5f;
            float along = (ncx - pcx) * prev->fx + (ncz - pcz) * prev->fz;
            along = clampf(along, -pL2 + 0.6f, pL2 - 0.6f);
            if (prev_hz == 0 || prev_hz == 1) along = fmaxf(along, fminf(1.3f, pL2 - 0.5f));
            float across = (ncx - pcx) * -prev->fz + (ncz - pcz) * prev->fx;
            float wx, wz;
            plat_local(prev, along, (across > 0 ? 1.0f : -1.0f) * (pW2 - 0.7f > 0 ? pW2 - 0.7f : 0), &wx, &wz);
            add_way(L, wx, wz);
        }
        heading = nh;
        prev_hz = hz;
        en_start = L->nen;
        np.checkpoint = is_cp;
        L->plat[i] = np;
        L->nplat = i + 1;
        Plat *p = &L->plat[i];
        if (p->y1 < minY) minY = p->y1;

        float L2 = plat_len(p) * 0.5f, W2 = plat_wid(p) * 0.5f;
        float wx, wz;

        if (hz == 0 || hz == 1) {
            /* strip right across the platform: must be jumped */
            float depth = hz == 0 ? 1.0f : 1.4f;
            float ax, az, bx, bz;
            plat_local(p, -depth * 0.5f, -W2, &ax, &az);
            plat_local(p, depth * 0.5f, W2, &bx, &bz);
            add_haz(L, hz == 0 ? HZ_SPIKES : HZ_LAVA, fminf(ax, bx), fminf(az, bz), fmaxf(ax, bx), fmaxf(az, bz), p->y1);
            plat_local(p, -L2 + 0.9f, 0, &wx, &wz); add_way(L, wx, wz);
            plat_local(p, L2 - 0.9f, 0, &wx, &wz); add_way(L, wx, wz);
        } else if (hz == 3) {
            /* spikes cover one side; walk down the other */
            float side = rf(0, 1) < 0.5f ? -1.0f : 1.0f;
            float cut = W2 * 0.1f * -side;       /* spikes reach just past the middle */
            float ax, az, bx, bz;
            plat_local(p, -L2, side * W2, &ax, &az);
            plat_local(p, L2, cut, &bx, &bz);
            add_haz(L, HZ_SPIKES, fminf(ax, bx), fminf(az, bz), fmaxf(ax, bx), fmaxf(az, bz), p->y1);
            float lane = -side * (W2 * 0.55f);
            plat_local(p, -L2 + 0.7f, lane, &wx, &wz); add_way(L, wx, wz);
            plat_local(p, L2 - 0.7f, lane, &wx, &wz); add_way(L, wx, wz);
        } else if (hz == 2) {
            int n = nsad;
            float spd = lerpf(1.6f, 3.2f, d);
            for (int k = 0; k < n && L->nen < MAX_ENEMIES; k++) {
                Enemy *e = &L->en[L->nen++];
                float along = n == 1 ? 0 : lerpf(-L2 + 2.4f, L2 - 2.4f, k / (float)(n - 1));
                float ax, az, bx, bz;
                float pw = W2 - race_sad_widen * 0.5f;      /* faces keep their old patrol width */
                plat_local(p, along, -pw + ENEMY_R + 0.05f, &ax, &az);
                plat_local(p, along, pw - ENEMY_R - 0.05f, &bx, &bz);
                e->axis = fabsf(ax - bx) > fabsf(az - bz) ? 0 : 1;
                e->a = e->axis == 0 ? fminf(ax, bx) : fminf(az, bz);
                e->b = e->axis == 0 ? fmaxf(ax, bx) : fmaxf(az, bz);
                e->x = ax; e->z = az; e->y = p->y1 + ENEMY_R;
                float t = rf(0, 1);
                if (e->axis == 0) e->x = lerpf(e->a, e->b, t); else e->z = lerpf(e->a, e->b, t);
                e->dir = rf(0, 1) < 0.5f ? -1 : 1;
                e->speed = spd * rf(0.85f, 1.15f) * race_sad_speed * (n == 3 ? SAD3_SPEED : 1.0f);
                e->phase = rf(0, 6.28f);
            }
            plat_local(p, -L2 + 0.7f, 0, &wx, &wz); add_way(L, wx, wz);
            plat_local(p, L2 - 0.7f, 0, &wx, &wz); add_way(L, wx, wz);
        } else {
            plat_local(p, 0, 0, &wx, &wz); add_way(L, wx, wz);
        }
    }
    L->lava_y = minY - 3.5f;
}

/* ---------- queries ---------- */
int point_in_hazard(const Level *L, float x, float z, float feet_y)
{
    for (int i = 0; i < L->nhaz; i++) {
        const Hazard *h = &L->haz[i];
        if (x > h->x0 && x < h->x1 && z > h->z0 && z < h->z1 && feet_y < h->y + 0.6f && feet_y > h->y - 0.3f) return 1;
    }
    return 0;
}

int point_on_safe_ground(const Level *L, float x, float z, float feet_y)
{
    for (int i = 0; i < L->nplat; i++) {
        const Plat *p = &L->plat[i];
        if (x > p->x0 && x < p->x1 && z > p->z0 && z < p->z1 && feet_y >= p->y1 - 0.05f && feet_y < p->y1 + 1.5f)
            return !point_in_hazard(L, x, z, p->y1);
    }
    return 0;
}

/* ---------- simulation ---------- */
void player_reset(Player *p, const Level *L)
{
    memset(p, 0, sizeof *p);
    p->x = L->spawn_x; p->y = L->spawn_y; p->z = L->spawn_z;
    p->face = 3.14159265f;    /* screen faces -Z, toward the course */
    p->grounded = 1;
}

void enemies_update(Level *L, float dt)
{
    for (int i = 0; i < L->nen; i++) {
        Enemy *e = &L->en[i];
        float *c = e->axis == 0 ? &e->x : &e->z;
        *c += e->dir * e->speed * dt;
        if (*c < e->a) { *c = e->a; e->dir = 1; }
        if (*c > e->b) { *c = e->b; e->dir = -1; }
        e->phase += dt;
    }
}

static int box_hit(const Player *p, const Plat *b)
{
    return p->x + PLAYER_R > b->x0 && p->x - PLAYER_R < b->x1 &&
           p->z + PLAYER_R > b->z0 && p->z - PLAYER_R < b->z1 &&
           p->y + PLAYER_H > b->y0 && p->y < b->y1 - 0.001f;
}

int player_step(Player *p, Level *L, float mx, float mz, int jump_pressed, int jump_held, float dt)
{
    /* horizontal velocity eases toward the stick */
    float tx = mx * MOVE_SPEED, tz = mz * MOVE_SPEED;
    float k = p->grounded ? 20.0f : 8.0f;
    float a = 1.0f - expf(-k * dt);
    p->vx += (tx - p->vx) * a;
    p->vz += (tz - p->vz) * a;
    if (mx * mx + mz * mz > 0.04f) p->face = atan2f(mx, mz);

    /* jump with a little coyote time and buffering */
    if (jump_pressed) p->jumpbuf = 0.12f; else p->jumpbuf -= dt;
    if (p->grounded) p->coyote = 0.10f; else p->coyote -= dt;
    if (p->grounded) p->double_used = 0;
    if (p->jumpbuf > 0 && p->coyote > 0) {
        p->vy = JUMP_VEL; p->grounded = 0; p->coyote = 0; p->jumpbuf = 0; p->jumped = 1;
    } else if (jump_pressed && !p->grounded && !p->double_used) {   /* double jump: once in the air */
        p->vy = JUMP_VEL; p->double_used = 1; p->jumpbuf = 0; p->jumped = 1;
    }
    if (!jump_held && p->vy > 4.0f) p->vy -= 40.0f * dt;   /* short hop on early release */
    p->vy -= GRAVITY * dt;
    if (p->vy < -25.0f) p->vy = -25.0f;

    /* move X, then Z, then Y, pushing out of platforms */
    p->x += p->vx * dt;
    for (int i = 0; i < L->nplat; i++) {
        Plat *b = &L->plat[i];
        if (box_hit(p, b)) { if (p->vx > 0) p->x = b->x0 - PLAYER_R; else p->x = b->x1 + PLAYER_R; p->vx = 0; }
    }
    p->z += p->vz * dt;
    for (int i = 0; i < L->nplat; i++) {
        Plat *b = &L->plat[i];
        if (box_hit(p, b)) { if (p->vz > 0) p->z = b->z0 - PLAYER_R; else p->z = b->z1 + PLAYER_R; p->vz = 0; }
    }
    float oldy = p->y;
    p->y += p->vy * dt;
    p->grounded = 0;
    for (int i = 0; i < L->nplat; i++) {
        Plat *b = &L->plat[i];
        if (!box_hit(p, b)) continue;
        if (p->vy <= 0 && oldy >= b->y1 - 0.05f) {
            p->y = b->y1; p->vy = 0; p->grounded = 1; p->ground_plat = i;
        } else if (p->vy > 0) {
            p->y = b->y0 - PLAYER_H; p->vy = 0;
        }
    }
    /* standing still on a top: keep grounded */
    if (!p->grounded && p->vy <= 0) {
        for (int i = 0; i < L->nplat; i++) {
            Plat *b = &L->plat[i];
            if (p->x + PLAYER_R > b->x0 && p->x - PLAYER_R < b->x1 && p->z + PLAYER_R > b->z0 && p->z - PLAYER_R < b->z1 &&
                fabsf(p->y - b->y1) < 0.02f) { p->grounded = 1; p->ground_plat = i; p->y = b->y1; p->vy = 0; }
        }
    }

    /* hazards */
    if (p->y < L->lava_y + 0.3f) return SIM_DEAD;
    for (int i = 0; i < L->nhaz; i++) {
        Hazard *h = &L->haz[i];
        float top = h->type == HZ_SPIKES ? h->y + 0.35f : h->y + 0.05f;
        float m = h->type == HZ_SPIKES ? 0.12f : 0.15f;   /* a little forgiveness at the edges */
        if (p->x + PLAYER_R - m > h->x0 && p->x - PLAYER_R + m < h->x1 &&
            p->z + PLAYER_R - m > h->z0 && p->z - PLAYER_R + m < h->z1 &&
            p->y < top && p->y + PLAYER_H > h->y - 0.1f) return SIM_DEAD;
    }
    for (int i = 0; i < L->nen; i++) {
        Enemy *e = &L->en[i];
        float bounce = fabsf(sinf(e->phase * 5.0f)) * 0.25f;
        float ey = e->y + bounce;
        /* closest point on player box to ball centre */
        float cx = fmaxf(p->x - PLAYER_R, fminf(e->x, p->x + PLAYER_R));
        float cy = fmaxf(p->y, fminf(ey, p->y + PLAYER_H));
        float cz = fmaxf(p->z - PLAYER_R, fminf(e->z, p->z + PLAYER_R));
        float dx = e->x - cx, dy = ey - cy, dz = e->z - cz;
        if (dx * dx + dy * dy + dz * dz < (ENEMY_R * 0.85f) * (ENEMY_R * 0.85f)) return SIM_DEAD;
    }
    if (p->grounded && L->plat[p->ground_plat].kind == PLAT_FINISH) return SIM_FINISH;
    return SIM_OK;
}

/* ---------- level editor: grid -> level ---------- */
static void grid_center(int c, int r, float *x, float *z) { *x = (c - (ED_W - 1) * 0.5f) * ED_CELL; *z = -r * ED_CELL; }

/* The level is laid out exactly like the grid. Turn pieces turn the camera (game.c), like the
   turns in the normal levels, so a course can carry on to the side after one. */
int level_from_grid(Level *L, const uint8_t *g)
{
    memset(L, 0, sizeof *L);
    L->num = 0;
    int has_start = 0, has_finish = 0, order[ED_W * ED_H], n = 0;
    /* platform order: the start first, then row by row (so checkpoints come in course order), finishes last */
    for (int i = 0; i < ED_W * ED_H; i++) if (TILE_TYPE(g[i]) == T_START && !has_start) { order[n++] = i; has_start = 1; }
    for (int i = 0; i < ED_W * ED_H; i++) { int t = TILE_TYPE(g[i]); if (t != T_EMPTY && t != T_START && t != T_FINISH) order[n++] = i; }
    for (int i = 0; i < ED_W * ED_H; i++) if (TILE_TYPE(g[i]) == T_FINISH) { order[n++] = i; has_finish = 1; }
    float minY = 0;
    for (int k = 0; k < n && L->nplat < MAX_PLATS; k++) {
        int i = order[k], c = i % ED_W, r = i / ED_W, t = TILE_TYPE(g[i]);
        float cx, cz, top = TILE_H(g[i]) * ED_STEP;
        grid_center(c, r, &cx, &cz);
        Plat *p = &L->plat[L->nplat++];
        plat_from_center(p, cx, top, cz, 0, -1, ED_CELL, ED_CELL, t == T_START ? PLAT_START : t == T_FINISH ? PLAT_FINISH : PLAT_NORMAL);
        p->checkpoint = t == T_CHECK;
        p->turn = t == T_TURN_L ? -1 : t == T_TURN_R ? 1 : 0;     /* looks like a pad; only the editor shows its arrow */
        if (top < minY) minY = top;
        if (t == T_START) { L->spawn_x = cx; L->spawn_y = top; L->spawn_z = cz; }
        if (t == T_SPIKES || t == T_LAVA)
            add_haz(L, t == T_SPIKES ? HZ_SPIKES : HZ_LAVA, p->x0 + 0.05f, p->z0 + 0.05f, p->x1 - 0.05f, p->z1 - 0.05f, top);
        if (t == T_SAD && L->nen < MAX_ENEMIES) {
            /* patrols across the course on the run of pieces it's on (same height, no spikes or lava).
               On a run of pieces that has a turn piece in it (the course going off to the side) the course
               runs left / right, so it patrols up and down instead, starting off the way the arrow turns. */
            #define WALK_AT(rr, cc) (TILE_TYPE(g[(rr) * ED_W + (cc)]) != T_EMPTY && TILE_TYPE(g[(rr) * ED_W + (cc)]) != T_SPIKES && \
                                     TILE_TYPE(g[(rr) * ED_W + (cc)]) != T_LAVA && TILE_H(g[(rr) * ED_W + (cc)]) == TILE_H(g[i]))
            #define IS_TURN(rr, cc) (TILE_TYPE(g[(rr) * ED_W + (cc)]) == T_TURN_L || TILE_TYPE(g[(rr) * ED_W + (cc)]) == T_TURN_R)
            int c0 = c, c1 = c, r0 = r, r1 = r, turn = 0;
            while (c0 > 0 && WALK_AT(r, c0 - 1)) c0--;
            while (c1 < ED_W - 1 && WALK_AT(r, c1 + 1)) c1++;
            for (int cc = c0; cc <= c1 && !turn; cc++) if (IS_TURN(r, cc)) turn = TILE_TYPE(g[r * ED_W + cc]);
            Enemy *e = &L->en[L->nen++];
            if (turn) {      /* up and down: the run of pieces in its column */
                while (r0 > 0 && WALK_AT(r0 - 1, c) && !IS_TURN(r0 - 1, c)) r0--;
                while (r1 < ED_H - 1 && WALK_AT(r1 + 1, c) && !IS_TURN(r1 + 1, c)) r1++;
                float za, zb, xx;
                grid_center(c, r1, &xx, &za); grid_center(c, r0, &xx, &zb);      /* za: furthest row, zb: nearest */
                e->axis = 1;
                e->a = za - ED_CELL * 0.5f + 0.2f; e->b = zb + ED_CELL * 0.5f - 0.2f;
                e->dir = turn == T_TURN_L ? -1 : 1;
            } else {
                float xa, xb, zz;
                grid_center(c0, r, &xa, &zz); grid_center(c1, r, &xb, &zz);
                e->axis = 0;
                e->a = xa - ED_CELL * 0.5f + 0.2f; e->b = xb + ED_CELL * 0.5f - 0.2f;
                e->dir = (c & 1) ? -1 : 1;
            }
            #undef WALK_AT
            #undef IS_TURN
            e->x = cx; e->z = cz; e->y = top + ENEMY_R;
            e->speed = 2.2f; e->phase = c * 1.3f + r * 0.7f;
        }
        add_way(L, cx, cz);
    }
    L->lava_y = minY - 3.5f;
    return has_start && has_finish;
}
