/* M.I.K.E - Multimedia Interactive Kinetic Element
   A 3D obby platformer for the Sega Dreamcast. Game states, rendering and HUD. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>        /* must come before GL/gl.h on Windows */
#endif
#include <GL/gl.h>
#include "platform.h"
#include "level.h"
#include "assets.h"
#include "vmu_emu.h"
#include "pet_vms.h"

#define PI 3.14159265f
#define SCR_W 640
#define SCR_H 480

/* ======================================================================= */
/*  Small mesh toolkit (baked lighting in vertex colours)                   */
/* ======================================================================= */
typedef struct { float x, y, z, u, v; uint8_t r, g, b, a; } V;
typedef struct { V *v; int n, cap; } Mesh;

static void m_reset(Mesh *m) { m->n = 0; }
static V *m_push(Mesh *m, int count)
{
    if (m->n + count > m->cap) {
        m->cap = (m->n + count) * 2 + 64;
        m->v = realloc(m->v, sizeof(V) * m->cap);
    }
    V *r = m->v + m->n; m->n += count; return r;
}
static void setv(V *v, float x, float y, float z, float u, float t, const uint8_t c[4])
{ v->x = x; v->y = y; v->z = z; v->u = u; v->v = t; v->r = c[0]; v->g = c[1]; v->b = c[2]; v->a = c[3]; }

/* light from the upper front-right; shadows lean purple to match the sky */
static void shade(float nx, float ny, float nz, const uint8_t base[3], uint8_t out[4])
{
    float lx = 0.42f, ly = 0.80f, lz = 0.42f;
    float d = nx * lx + ny * ly + nz * lz; if (d < 0) d = 0;
    float s = 0.50f + 0.55f * d; if (s > 1.0f) s = 1.0f;
    float tr = s * (0.92f + 0.08f * d), tg = s * (0.84f + 0.16f * d), tb = s;
    out[0] = (uint8_t)fminf(255, base[0] * tr);
    out[1] = (uint8_t)fminf(255, base[1] * tg);
    out[2] = (uint8_t)fminf(255, base[2] * tb);
    out[3] = 255;
}

/* quad p0..p3 counter-clockwise, with explicit uvs */
static void m_quad(Mesh *m, const float *p0, const float *p1, const float *p2, const float *p3,
                   float u0, float v0, float u1, float v1, const uint8_t c[4])
{
    V *v = m_push(m, 6);
    setv(&v[0], p0[0], p0[1], p0[2], u0, v1, c);
    setv(&v[1], p1[0], p1[1], p1[2], u1, v1, c);
    setv(&v[2], p2[0], p2[1], p2[2], u1, v0, c);
    setv(&v[3], p0[0], p0[1], p0[2], u0, v1, c);
    setv(&v[4], p2[0], p2[1], p2[2], u1, v0, c);
    setv(&v[5], p3[0], p3[1], p3[2], u0, v0, c);
}

/* axis aligned box. top goes to 'top', sides+bottom to 'side'. tile = texture repeats per unit (0 = stretch once) */
static void m_box(Mesh *top, Mesh *side, float x0, float y0, float z0, float x1, float y1, float z1,
                  const uint8_t base[3], float tile)
{
    uint8_t c[4];
    float a[3], b[3], cc[3], d[3];
    float tu = tile > 0 ? tile : 0;
#define P(arr, X, Y, Z) arr[0] = X, arr[1] = Y, arr[2] = Z
    /* top */
    shade(0, 1, 0, base, c);
    P(a, x0, y1, z1); P(b, x1, y1, z1); P(cc, x1, y1, z0); P(d, x0, y1, z0);
    if (tu) m_quad(top, a, b, cc, d, x0 * tu, z0 * tu, x1 * tu, z1 * tu, c);
    else m_quad(top, a, b, cc, d, 0, 0, 1, 1, c);
    float hu = tu ? tu : 1.0f / (x1 - x0);
    float hz = tu ? tu : 1.0f / (z1 - z0);
    /* front (+z) */
    shade(0, 0, 1, base, c);
    P(a, x0, y0, z1); P(b, x1, y0, z1); P(cc, x1, y1, z1); P(d, x0, y1, z1);
    m_quad(side, a, b, cc, d, x0 * hu, 0, x1 * hu, 1, c);
    /* back (-z) */
    shade(0, 0, -1, base, c);
    P(a, x1, y0, z0); P(b, x0, y0, z0); P(cc, x0, y1, z0); P(d, x1, y1, z0);
    m_quad(side, a, b, cc, d, x0 * hu, 0, x1 * hu, 1, c);
    /* right (+x) */
    shade(1, 0, 0, base, c);
    P(a, x1, y0, z1); P(b, x1, y0, z0); P(cc, x1, y1, z0); P(d, x1, y1, z1);
    m_quad(side, a, b, cc, d, z0 * hz, 0, z1 * hz, 1, c);
    /* left (-x) */
    shade(-1, 0, 0, base, c);
    P(a, x0, y0, z0); P(b, x0, y0, z1); P(cc, x0, y1, z1); P(d, x0, y1, z0);
    m_quad(side, a, b, cc, d, z0 * hz, 0, z1 * hz, 1, c);
    /* bottom */
    shade(0, -1, 0, base, c);
    P(a, x0, y0, z0); P(b, x1, y0, z0); P(cc, x1, y0, z1); P(d, x0, y0, z1);
    m_quad(side, a, b, cc, d, 0, 0, 1, 1, c);
#undef P
}

/* uv sphere, face texture centred on +Z (u = 0.25, same as the preview) */
static void m_sphere(Mesh *m, float r, int seg, int rings, const uint8_t base[3], int lit, int inside)
{
    for (int j = 0; j < rings; j++) {
        for (int i = 0; i < seg; i++) {
            float u0 = i / (float)seg, u1 = (i + 1) / (float)seg;
            float t0 = j / (float)rings, t1 = (j + 1) / (float)rings;
            float pts[4][5];
            float uu[4] = { u0, u1, u1, u0 }, tt[4] = { t1, t1, t0, t0 };
            for (int k = 0; k < 4; k++) {
                float phi = uu[k] * 2 * PI, th = tt[k] * PI;
                pts[k][0] = -cosf(phi) * sinf(th);
                pts[k][1] = cosf(th);
                pts[k][2] = sinf(phi) * sinf(th);
                pts[k][3] = uu[k]; pts[k][4] = tt[k];
            }
            int order[6] = { 0, 1, 2, 0, 2, 3 };
            if (inside) { order[1] = 2; order[2] = 1; order[4] = 3; order[5] = 2; }
            V *v = m_push(m, 6);
            for (int k = 0; k < 6; k++) {
                float *p = pts[order[k]];
                uint8_t c[4] = { base[0], base[1], base[2], 255 };
                if (lit) shade(p[0], p[1], p[2], base, c);
                setv(&v[k], p[0] * r, p[1] * r, p[2] * r, p[3], p[4], c);
            }
        }
    }
}

/* cylinder between two points (used for antennae) */
static void m_rod(Mesh *m, float ax, float ay, float az, float bx, float by, float bz, float rad, const uint8_t base[3])
{
    float dx = bx - ax, dy = by - ay, dz = bz - az;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    dx /= len; dy /= len; dz /= len;
    /* two perpendicular vectors */
    float px = dy, py = -dx, pz = 0;
    float pl = sqrtf(px * px + py * py); if (pl < 1e-4f) { px = 1; py = 0; pl = 1; }
    px /= pl; py /= pl;
    float qx = dy * pz - dz * py, qy = dz * px - dx * pz, qz = dx * py - dy * px;
    const int S = 6;
    for (int i = 0; i < S; i++) {
        float a0 = i * 2 * PI / S, a1 = (i + 1) * 2 * PI / S;
        float n0x = cosf(a0) * px + sinf(a0) * qx, n0y = cosf(a0) * py + sinf(a0) * qy, n0z = cosf(a0) * pz + sinf(a0) * qz;
        float n1x = cosf(a1) * px + sinf(a1) * qx, n1y = cosf(a1) * py + sinf(a1) * qy, n1z = cosf(a1) * pz + sinf(a1) * qz;
        uint8_t c[4]; shade((n0x + n1x) * 0.5f, (n0y + n1y) * 0.5f, (n0z + n1z) * 0.5f, base, c);
        float p0[3] = { ax + n0x * rad, ay + n0y * rad, az + n0z * rad };
        float p1[3] = { ax + n1x * rad, ay + n1y * rad, az + n1z * rad };
        float p2[3] = { bx + n1x * rad, by + n1y * rad, bz + n1z * rad };
        float p3[3] = { bx + n0x * rad, by + n0y * rad, bz + n0z * rad };
        m_quad(m, p0, p1, p2, p3, 0, 0, 1, 1, c);
    }
}

/* 4-sided pyramid spike, base centred at (x,y,z) */
static void m_spike(Mesh *m, float x, float y, float z, float r, float h)
{
    static const uint8_t metal[3] = { 205, 205, 220 }, tip[3] = { 255, 77, 94 };
    float tipy = y + h;
    float cs[4][2] = { { x - r, z }, { x, z + r }, { x + r, z }, { x, z - r } };
    for (int i = 0; i < 4; i++) {
        float *a = cs[i], *b = cs[(i + 1) & 3];
        float nx = (a[0] + b[0]) * 0.5f - x, nz = (a[1] + b[1]) * 0.5f - z;
        float nl = sqrtf(nx * nx + nz * nz + 0.09f);
        uint8_t c[4]; shade(nx / nl, 0.3f / nl, nz / nl, metal, c);
        uint8_t ct[4]; shade(nx / nl, 0.3f / nl, nz / nl, tip, ct);
        /* body up to 80%, red tip above */
        float k = 0.8f;
        float ax = a[0] + (x - a[0]) * k, az = a[1] + (z - a[1]) * k;
        float bx = b[0] + (x - b[0]) * k, bz = b[1] + (z - b[1]) * k;
        float my = y + h * k;
        float p0[3] = { a[0], y, a[1] }, p1[3] = { b[0], y, b[1] }, p2[3] = { bx, my, bz }, p3[3] = { ax, my, az };
        m_quad(m, p0, p1, p2, p3, 0, 0, 1, 1, c);
        V *v = m_push(m, 3);
        setv(&v[0], ax, my, az, 0, 0, ct);
        setv(&v[1], bx, my, bz, 0, 0, ct);
        setv(&v[2], x, tipy, z, 0, 0, ct);
    }
}

/* Dreamcast: the PowerVR draws every solid polygon before every see-through one, so a 3D model drawn
   after a see-through 2D panel still ends up underneath it (dimmed). With force_blend on, models go in
   the see-through list too, in the order they're drawn, so they land on top of the panel. */
static int force_blend;
static void draw_mesh(const Mesh *m, GLuint tex)
{
    if (!m->n) return;
    if (force_blend) glEnable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, tex);
    glVertexPointer(3, GL_FLOAT, sizeof(V), &m->v[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(V), &m->v[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(V), &m->v[0].r);
    glDrawArrays(GL_TRIANGLES, 0, m->n);
}

/* ======================================================================= */
/*  Assets                                                                  */
/* ======================================================================= */
static GLuint tex[TEX_COUNT];
static Mesh mike_body, mike_screen, mike_ant, ball, sky, ring, trophy_gold, trophy_base, plaque;
static Mesh alex_body, alex_screen, alex_ring, ball_lo;

/* ---------- skins ---------- */
#define NUM_SKINS 7
enum { FX_NONE, FX_GLITCH, FX_STATIC, FX_RAINBOW, FX_CAP, FX_PHONES, FX_CASE };
enum { UNLOCK_FREE, UNLOCK_LEVEL, UNLOCK_ENDLESS, UNLOCK_PET };
typedef struct {
    const char *name;
    uint8_t body[3], trim[3];      /* body colour; antennas / back panel (M.I.K.E) or button ring (A.L.E.X) */
    int tex, fx, unlock, need;     /* screen texture, effect, how it unlocks and the level it needs */
} Skin;
static const Skin skins[2][NUM_SKINS] = {
    {   { "CLASSIC",       { 22, 20, 28 },   { 90, 90, 90 },    TEX_FACE,        FX_NONE,    UNLOCK_FREE, 0 },
        { "RETRO CRT",     { 112, 66, 30 },  { 175, 170, 160 }, TEX_FACE_CRT,    FX_NONE,    UNLOCK_LEVEL, 25 },
        { "BLACK & WHITE", { 62, 62, 64 },   { 175, 175, 175 }, TEX_FACE_BW,     FX_NONE,    UNLOCK_LEVEL, 50 },
        { "GLITCH",        { 22, 20, 28 },   { 90, 90, 90 },    TEX_FACE_GLITCH, FX_GLITCH,  UNLOCK_LEVEL, 100 },
        { "GOLD",          { 225, 172, 30 }, { 255, 214, 40 },  TEX_FACE,        FX_NONE,    UNLOCK_LEVEL, 150 },
        { "STATIC",        { 22, 20, 28 },   { 90, 90, 90 },    TEX_SNOW,        FX_STATIC,  UNLOCK_ENDLESS, 25 },
        { "SLEEPY",        { 40, 44, 92 },   { 120, 130, 200 }, TEX_FACE_SLEEPY, FX_CAP,     UNLOCK_PET, 0 } },
    {   { "CLASSIC",       { 22, 20, 28 },   { 90, 90, 90 },    TEX_ALEX_FACE,   FX_NONE,    UNLOCK_FREE, 0 },
        { "RUGGED CASE",   { 30, 105, 235 }, { 60, 60, 60 },    TEX_ALEX_FACE,   FX_CASE,    UNLOCK_LEVEL, 25 },
        { "CRACKED",       { 22, 20, 28 },   { 90, 90, 90 },    TEX_ALEX_CRACKED, FX_NONE,   UNLOCK_LEVEL, 50 },
        { "DARK MODE",     { 12, 10, 16 },   { 150, 70, 220 },  TEX_ALEX_DARK,   FX_NONE,    UNLOCK_LEVEL, 100 },
        { "GOLD",          { 225, 172, 30 }, { 255, 214, 40 },  TEX_ALEX_FACE,   FX_NONE,    UNLOCK_LEVEL, 150 },
        { "RAINBOW",       { 22, 20, 28 },   { 90, 90, 90 },    TEX_ALEX_RAINBOW, FX_RAINBOW, UNLOCK_ENDLESS, 25 },
        { "HEADPHONES",    { 22, 20, 28 },   { 90, 90, 90 },    TEX_ALEX_FACE,   FX_PHONES,  UNLOCK_PET, 0 } },
};
static Mesh skin_body[2][NUM_SKINS], skin_trim[2][NUM_SKINS], mike_cap, alex_phones, party_hat[2], party_hat_bw;
static float mike_scr[4][3], alex_scr[4][3];     /* screen corners, for the animated skins */
static int skin_sel[2], skin_view[2];             /* chosen skin per character / the one shown (menu cursor) */
/* birthdays: M.I.K.E on November 14th (release day), A.L.E.X on April 19th - they wear a party hat all day,
   and the game says so when it starts */
static int bday_char = -1, bday_popup;
static void check_birthday(void)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) return;
    int m = tm->tm_mon + 1, d = tm->tm_mday;
#ifdef MIKE_TEST_DATE
    m = MIKE_TEST_DATE / 100; d = MIKE_TEST_DATE % 100;
#endif
    bday_char = (m == 11 && d == 14) ? 0 : (m == 4 && d == 19) ? 1 : -1;
    bday_popup = bday_char >= 0;
}
static uint32_t skins_unlocked = 0x0101;          /* bit c * 8 + n; the classic looks are always there */
/* per level */
static Mesh lv_top, lv_side, lv_fin_top, lv_fin_side, lv_spikes, lv_turn, lv_turn_arrows, scratch, lava_mesh;   /* turn arrows: editor only */

static const uint8_t WHITE[3] = { 255, 255, 255 };

/* birthday party hat (like the 2D art): a light blue cone with yellow and pink confetti squiggles and a
   yellow bobble, tilted over to the right. (bx, by, bz) is the middle of its base. */
static void make_party_hat(Mesh *m, float bx, float by, float bz, float r, float h, float tilt, int grey)
{
    static const uint8_t c_cone[3] = { 40, 185, 245 }, c_y[3] = { 255, 228, 40 }, c_p[3] = { 255, 60, 200 };
    static const uint8_t g_cone[3] = { 150, 150, 155 }, g_y[3] = { 245, 245, 245 }, g_p[3] = { 40, 40, 44 };   /* black & white skin */
    const uint8_t *cone = grey ? g_cone : c_cone, *conf[2] = { grey ? g_y : c_y, grey ? g_p : c_p };
    float ct = cosf(tilt), st = sinf(tilt);
    #define HAT_P(lx, ly, lz, out) do { (out)[0] = bx + (lx) * ct + (ly) * st; (out)[1] = by - (lx) * st + (ly) * ct; (out)[2] = bz + (lz); } while (0)
    const int S = 16;
    float tip[3]; HAT_P(0, h, 0, tip);
    for (int i = 0; i < S; i++) {
        float a0 = i * 2 * PI / S, a1 = (i + 1) * 2 * PI / S, q0[3], q1[3];
        HAT_P(cosf(a0) * r, 0, sinf(a0) * r, q0); HAT_P(cosf(a1) * r, 0, sinf(a1) * r, q1);
        uint8_t cc[4]; shade(cosf((a0 + a1) / 2) * ct, 0.45f, sinf((a0 + a1) / 2), cone, cc);
        m_quad(m, q0, q1, tip, tip, 0, 0, 1, 1, cc);
        m_quad(m, tip, tip, q1, q0, 0, 0, 1, 1, cc);
    }
    /* confetti: little zig-zag squiggles stuck on the front and sides */
    static const float spots[][3] = { { 1.75f, 0.18f, 0 }, { 1.15f, 0.30f, 1 }, { 2.25f, 0.42f, 1 }, { 1.55f, 0.55f, 0 },
                                      { 0.80f, 0.20f, 0 }, { 2.60f, 0.22f, 0 }, { 1.95f, 0.68f, 1 }, { 1.00f, 0.50f, 1 } };
    for (int k = 0; k < 8; k++) {
        float ang = spots[k][0], t = spots[k][1];
        float rr = r * (1 - t) + 0.006f, y = h * t, prev[3];
        for (int j = 0; j <= 3; j++) {
            float a = ang + (j - 1.5f) * 0.16f, yy = y + ((j & 1) ? 0.025f : -0.025f), rad = r * (1 - yy / h) + 0.008f, pt[3];
            (void)rr;
            HAT_P(cosf(a) * rad, yy, sinf(a) * rad, pt);
            if (j) m_rod(m, prev[0], prev[1], prev[2], pt[0], pt[1], pt[2], 0.009f, conf[(int)spots[k][2]]);
            memcpy(prev, pt, sizeof prev);
        }
    }
    Mesh tmp = { 0 }; m_sphere(&tmp, r * 0.28f, 8, 5, conf[0], 1, 0);
    for (int i = 0; i < tmp.n; i++) { tmp.v[i].x += tip[0]; tmp.v[i].y += tip[1]; tmp.v[i].z += tip[2]; }
    V *v = m_push(m, tmp.n); memcpy(v, tmp.v, sizeof(V) * tmp.n); free(tmp.v);
    #undef HAT_P
}

static void build_static_meshes(void)
{
    /* M.I.K.E: proportions straight from the 2D art (1000px = 1 unit), body centred on origin */
    const float W = 1.415f, H = 1.0f, D = 0.72f;
    float SW = 0.889f, SH = SW * 0.75f;
    float sx = -W / 2 + 0.103f, sy = H / 2 - 0.157f, z = D / 2 + 0.004f;
    float a[3] = { sx, sy - SH, z }, b[3] = { sx + SW, sy - SH, z }, c[3] = { sx + SW, sy, z }, d[3] = { sx, sy, z };
    uint8_t full[4] = { 255, 255, 255, 255 };
    m_quad(&mike_screen, a, b, c, d, 0, 0, 1, 1, full);
    memcpy(mike_scr[0], a, 12); memcpy(mike_scr[1], b, 12); memcpy(mike_scr[2], c, 12); memcpy(mike_scr[3], d, 12);
    float bx = -W / 2 + 0.518f, by = H / 2;
    for (int k = 0; k < NUM_SKINS; k++) {
        const Skin *sk = &skins[0][k];
        Mesh *body = &skin_body[0][k], *ant = &skin_trim[0][k];
        m_box(body, body, -W / 2, -H / 2, -D / 2, W / 2, H / 2, D / 2, sk->body, 0);
        /* back panel with vent slots so his back isn't a plain box */
        uint8_t panel[3], slot[3];
        for (int i = 0; i < 3; i++) { panel[i] = (uint8_t)((sk->body[i] + sk->trim[i]) / 2); slot[i] = (uint8_t)(sk->body[i] / 2); }
        if (k == 0) { panel[0] = 70; panel[1] = 64; panel[2] = 84; slot[0] = 12; slot[1] = 10; slot[2] = 18; }
        uint8_t pc[4], sc[4]; shade(0, 0, -1, panel, pc); shade(0, 0, -1, slot, sc);
        float bz = -D / 2 - 0.004f;
        float p0[3] = { 0.5f, -0.32f, bz }, p1[3] = { -0.5f, -0.32f, bz }, p2[3] = { -0.5f, 0.32f, bz }, p3[3] = { 0.5f, 0.32f, bz };
        m_quad(body, p0, p1, p2, p3, 0, 0, 1, 1, pc);
        for (int i = 0; i < 4; i++) {
            float y0 = -0.22f + i * 0.13f, y1 = y0 + 0.05f, z2 = bz - 0.002f;
            float s0[3] = { 0.36f, y0, z2 }, s1[3] = { -0.36f, y0, z2 }, s2[3] = { -0.36f, y1, z2 }, s3[3] = { 0.36f, y1, z2 };
            m_quad(body, s0, s1, s2, s3, 0, 0, 1, 1, sc);
        }
        m_rod(ant, bx, by - 0.02f, 0, bx - 0.188f, by + 0.378f, -0.05f, 0.016f, sk->trim);
        m_rod(ant, bx, by - 0.02f, 0, bx + 0.247f, by + 0.375f, -0.05f, 0.016f, sk->trim);
    }
    {   /* nightcap for the sleepy skin: a floppy cone leaning off the right of his head, with a bobble */
        static const uint8_t capc[3] = { 70, 110, 235 }, bob[3] = { 245, 245, 255 };
        float base[3] = { 0.3f, H / 2 - 0.02f, 0 }, tip[3] = { 0.66f, H / 2 + 0.52f, 0.05f };
        const int S = 12; float R = 0.28f;
        for (int i = 0; i < S; i++) {
            float a0 = i * 2 * PI / S, a1 = (i + 1) * 2 * PI / S;
            float q0[3] = { base[0] + cosf(a0) * R, base[1], base[2] + sinf(a0) * R * 0.9f };
            float q1[3] = { base[0] + cosf(a1) * R, base[1], base[2] + sinf(a1) * R * 0.9f };
            uint8_t cc[4]; shade(cosf((a0 + a1) / 2), 0.5f, sinf((a0 + a1) / 2), capc, cc);
            m_quad(&mike_cap, q0, q1, tip, tip, 0, 0, 1, 1, cc);
            m_quad(&mike_cap, tip, tip, q1, q0, 0, 0, 1, 1, cc);
        }
        for (int i = 0; i < S; i++)       /* white brim */
            m_rod(&mike_cap, base[0] + cosf(i * 2 * PI / S) * R, base[1], sinf(i * 2 * PI / S) * R * 0.9f,
                  base[0] + cosf((i + 1) * 2 * PI / S) * R, base[1], sinf((i + 1) * 2 * PI / S) * R * 0.9f, 0.04f, bob);
        Mesh tmp = { 0 }; m_sphere(&tmp, 0.075f, 8, 5, bob, 1, 0);
        for (int i = 0; i < tmp.n; i++) { tmp.v[i].x += tip[0]; tmp.v[i].y += tip[1]; tmp.v[i].z += tip[2]; }
        V *v = m_push(&mike_cap, tmp.n); memcpy(v, tmp.v, sizeof(V) * tmp.n); free(tmp.v);
    }

    /* M.I.K.E's birthday hat sits on the tip of his right antenna, leaning right (and a black and white one) */
    make_party_hat(&party_hat[0], bx + 0.25f, by + 0.34f, -0.05f, 0.22f, 0.56f, 0.55f, 0);
    make_party_hat(&party_hat_bw, bx + 0.25f, by + 0.34f, -0.05f, 0.22f, 0.56f, 0.55f, 1);
    mike_body = skin_body[0][0]; mike_ant = skin_trim[0][0];
    m_sphere(&ball, ENEMY_R, 12, 7, WHITE, 1, 0);
    m_sphere(&ball_lo, ENEMY_R, 8, 5, WHITE, 1, 0);      /* cheaper sad face for split screen */
    m_sphere(&sky, 90.0f, 16, 10, WHITE, 0, 1);

    /* green pickup ring around the smiley */
    static const uint8_t green[3] = { 0, 255, 26 };
    for (int i = 0; i < 16; i++) {
        float a0 = i * 2 * PI / 16, a1 = (i + 1) * 2 * PI / 16, R = 0.72f, t = 0.05f;
        uint8_t cc[4] = { green[0], green[1], green[2], 255 };
        float p0[3] = { cosf(a0) * (R - t), 0, sinf(a0) * (R - t) }, p1[3] = { cosf(a1) * (R - t), 0, sinf(a1) * (R - t) };
        float p2[3] = { cosf(a1) * (R + t), 0, sinf(a1) * (R + t) }, p3[3] = { cosf(a0) * (R + t), 0, sinf(a0) * (R + t) };
        m_quad(&ring, p0, p1, p2, p3, 0, 0, 1, 1, cc);
        m_quad(&ring, p3, p2, p1, p0, 0, 0, 1, 1, cc);
    }

    /* trophy: lathe cup + handles, base with plaque */
    static const uint8_t gold[3] = { 255, 214, 40 }, purple[3] = { 43, 16, 80 };
    float prof[][2] = { { 0, 0 }, { 0.12f, 0 }, { 0.12f, 0.06f }, { 0.06f, 0.12f }, { 0.06f, 0.45f }, { 0.15f, 0.52f },
                        { 0.42f, 0.70f }, { 0.50f, 1.05f }, { 0.52f, 1.25f }, { 0.46f, 1.25f }, { 0.40f, 0.95f } };
    int np = sizeof prof / sizeof prof[0];
    const int S = 10;
    for (int i = 0; i < S; i++) {
        float a0 = i * 2 * PI / S, a1 = (i + 1) * 2 * PI / S;
        for (int j = 0; j < np - 1; j++) {
            float r0 = prof[j][0], y0 = prof[j][1] + 0.42f, r1 = prof[j + 1][0], y1 = prof[j + 1][1] + 0.42f;
            float nx = cosf((a0 + a1) / 2), nz = sinf((a0 + a1) / 2);
            float ny = (r0 - r1); float nl = sqrtf(1 + ny * ny);
            uint8_t cc[4]; shade(nx / nl, ny / nl, nz / nl, gold, cc);
            float p0[3] = { cosf(a0) * r0, y0, sinf(a0) * r0 }, p1[3] = { cosf(a1) * r0, y0, sinf(a1) * r0 };
            float p2[3] = { cosf(a1) * r1, y1, sinf(a1) * r1 }, p3[3] = { cosf(a0) * r1, y1, sinf(a0) * r1 };
            m_quad(&trophy_gold, p0, p1, p2, p3, 0, 0, 1, 1, cc);
            m_quad(&trophy_gold, p3, p2, p1, p0, 0, 0, 1, 1, cc);
        }
    }
    for (int s = -1; s <= 1; s += 2) {   /* handles: half rings on each side */
        for (int i = 0; i < 6; i++) {
            float a0 = -PI / 2 + i * PI / 6, a1 = -PI / 2 + (i + 1) * PI / 6, R = 0.2f;
            m_rod(&trophy_gold, s * (0.48f + cosf(a0) * R), 1.4f + sinf(a0) * R, 0,
                  s * (0.48f + cosf(a1) * R), 1.4f + sinf(a1) * R, 0, 0.045f, gold);
        }
    }
    m_box(&trophy_base, &trophy_base, -0.55f, 0, -0.3f, 0.55f, 0.42f, 0.3f, purple, 0);
    float q0[3] = { -0.53f, 0.03f, 0.305f }, q1[3] = { 0.53f, 0.03f, 0.305f }, q2[3] = { 0.53f, 0.39f, 0.305f }, q3[3] = { -0.53f, 0.39f, 0.305f };
    m_quad(&plaque, q0, q1, q2, q3, 0, 0, 1, 1, full);

    /* A.L.E.X: thin tablet, proportions from his 2D art (1135 px tall = 1 unit, same height as M.I.K.E) */
    {
        const float S = 1.0f / 1135.0f, AW = 1728 * S, AH = 1.0f, AD = 0.11f, R = 0.045f;
        for (int k = 0; k < NUM_SKINS; k++) {
            const Skin *sk = &skins[1][k];
            Mesh *body = &skin_body[1][k], *rng = &skin_trim[1][k];
            float g = sk->fx == FX_CASE ? 0.035f : 0, ad = AD + (sk->fx == FX_CASE ? 0.05f : 0), rr = R + g;   /* rugged case: chunkier */
            float px[20], py[20]; int np2 = 0;
            float cxs[4] = { AW / 2 - R, AW / 2 - R, -AW / 2 + R, -AW / 2 + R }, cys[4] = { -AH / 2 + R, AH / 2 - R, AH / 2 - R, -AH / 2 + R };
            for (int c = 0; c < 4; c++)                     /* rounded corners, 4 segments each */
                for (int j = 0; j <= 4; j++) {
                    float a = (-90.0f + c * 90.0f + j * 22.5f) * PI / 180.0f;
                    px[np2] = cxs[c] + cosf(a) * rr; py[np2] = cys[c] + sinf(a) * rr; np2++;
                }
            uint8_t cf[4], cb[4]; shade(0, 0, 1, sk->body, cf); shade(0, 0, -1, sk->body, cb);
            for (int i = 0; i < np2; i++) {
                int j = (i + 1) % np2;
                V *v = m_push(body, 6);
                setv(&v[0], 0, 0, ad / 2, 0, 0, cf); setv(&v[1], px[i], py[i], ad / 2, 0, 0, cf); setv(&v[2], px[j], py[j], ad / 2, 0, 0, cf);
                setv(&v[3], 0, 0, -ad / 2, 0, 0, cb); setv(&v[4], px[j], py[j], -ad / 2, 0, 0, cb); setv(&v[5], px[i], py[i], -ad / 2, 0, 0, cb);
                float nx = (px[i] + px[j]) * 0.5f, ny = (py[i] + py[j]) * 0.5f;   /* rough outward normal */
                float nl = sqrtf(nx * nx + ny * ny); nx /= nl; ny /= nl;
                uint8_t cs[4]; shade(nx, ny, 0, sk->body, cs);
                float a[3] = { px[i], py[i], ad / 2 }, b[3] = { px[i], py[i], -ad / 2 }, cc[3] = { px[j], py[j], -ad / 2 }, d[3] = { px[j], py[j], ad / 2 };
                m_quad(body, a, b, cc, d, 0, 0, 1, 1, cs);
            }
            float rx = -AW / 2 + 1598 * S, ry = AH / 2 - 568 * S, r0 = 60 * S, r1 = 72 * S, rz = ad / 2 + 0.005f;
            uint8_t rc4[4] = { sk->trim[0], sk->trim[1], sk->trim[2], 255 };
            for (int i = 0; i < 16; i++) {                  /* home button ring */
                float a0 = i * 2 * PI / 16, a1 = (i + 1) * 2 * PI / 16;
                float p0[3] = { rx + cosf(a0) * r0, ry + sinf(a0) * r0, rz }, p1[3] = { rx + cosf(a0) * r1, ry + sinf(a0) * r1, rz };
                float p2[3] = { rx + cosf(a1) * r1, ry + sinf(a1) * r1, rz }, p3[3] = { rx + cosf(a1) * r0, ry + sinf(a1) * r0, rz };
                m_quad(rng, p0, p1, p2, p3, 0, 0, 1, 1, rc4);
            }
            if (sk->fx == FX_CASE) {         /* the screen sits in the case: a bezel strip in front, over the case edge */
                uint8_t bz[4]; shade(0, 0, 1, sk->body, bz);
                (void)bz;
            }
        }
        float SW = 1323 * S, SH = 982 * S, sx = -AW / 2 + 132 * S, sy = AH / 2 - 77 * S, z = AD / 2 + 0.004f;
        float a[3] = { sx, sy - SH, z }, b[3] = { sx + SW, sy - SH, z }, cc[3] = { sx + SW, sy, z }, d[3] = { sx, sy, z };
        m_quad(&alex_screen, a, b, cc, d, 0, 0, 1, 1, full);
        memcpy(alex_scr[0], a, 12); memcpy(alex_scr[1], b, 12); memcpy(alex_scr[2], cc, 12); memcpy(alex_scr[3], d, 12);
        alex_body = skin_body[1][0]; alex_ring = skin_trim[1][0];
        make_party_hat(&party_hat[1], AW / 2 - 0.36f, AH / 2 - 0.01f, 0, 0.2f, 0.5f, 0.45f, 0);   /* A.L.E.X's */
        /* headphones skin: band over the top, a cup on each side */
        static const uint8_t band[3] = { 255, 80, 170 }, cup[3] = { 245, 245, 250 };
        const int N = 14; float cy = 0.12f, Rb = AW / 2 + 0.03f, Ry = AH / 2 + 0.16f - cy;
        for (int i = 0; i < N; i++) {
            float a0 = PI * i / N, a1 = PI * (i + 1) / N;
            m_rod(&alex_phones, cosf(a0) * Rb, cy + sinf(a0) * Ry, 0, cosf(a1) * Rb, cy + sinf(a1) * Ry, 0, 0.03f, band);
        }
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float x0 = sgn > 0 ? AW / 2 - 0.01f : -AW / 2 - 0.09f, x1 = x0 + 0.1f;
            m_box(&alex_phones, &alex_phones, x0, cy - 0.17f, -0.13f, x1, cy + 0.15f, 0.13f, cup, 0);
        }
    }
}

/* ======================================================================= */
/*  Level geometry                                                          */
/* ======================================================================= */
static Level lv;
/* a flat arrow on top of a screen turn piece: up the middle, then off to the side it turns to */
static void turn_arrow(Mesh *m, const Plat *p)
{
    static const uint8_t w[4] = { 255, 255, 255, 255 };
    float cx = (p->x0 + p->x1) * 0.5f, cz = (p->z0 + p->z1) * 0.5f, y = p->y1 + 0.02f, s = (p->x1 - p->x0) * 0.5f, d = (float)p->turn;
    float q[3][4][2] = {     /* (x, z) offsets in units of s: shaft, bar to the side, head */
        { { -0.09f, 0.62f }, { 0.09f, 0.62f }, { 0.09f, -0.1f }, { -0.09f, -0.1f } },
        { { -0.09f * d, -0.1f }, { 0.35f * d, -0.1f }, { 0.35f * d, -0.28f }, { -0.09f * d, -0.28f } },
        { { 0.35f * d, 0.12f }, { 0.68f * d, -0.19f }, { 0.35f * d, -0.5f }, { 0.35f * d, -0.19f } } };
    for (int k = 0; k < 3; k++) {
        float v[4][3];
        for (int j = 0; j < 4; j++) { v[j][0] = cx + q[k][j][0] * s; v[j][1] = y; v[j][2] = cz + q[k][j][1] * s; }
        m_quad(m, v[0], v[1], v[2], v[3], 0, 0, 1, 1, w);
        m_quad(m, v[3], v[2], v[1], v[0], 0, 0, 1, 1, w);
    }
}
static void build_level_meshes(void)
{
    m_reset(&lv_top); m_reset(&lv_side); m_reset(&lv_fin_top); m_reset(&lv_fin_side); m_reset(&lv_spikes); m_reset(&lv_turn); m_reset(&lv_turn_arrows);
    static const uint8_t grass[3] = { 255, 255, 255 };
    for (int i = 0; i < lv.nplat; i++) {
        Plat *p = &lv.plat[i];
        static const uint8_t gold_tint[3] = { 255, 225, 70 };   /* race checkpoints */
        if (p->kind == PLAT_FINISH)
            m_box(&lv_fin_top, &lv_side, p->x0, p->y0, p->z0, p->x1, p->y1, p->z1, grass, 0.5f);
        else if (p->checkpoint)
            m_box(&lv_top, &lv_side, p->x0, p->y0, p->z0, p->x1, p->y1, p->z1, gold_tint, 1.0f);
        else if (p->turn) {        /* turn pieces look like any pad; the arrow is only drawn in the editor */
            m_box(&lv_top, &lv_side, p->x0, p->y0, p->z0, p->x1, p->y1, p->z1, grass, 1.0f);
            turn_arrow(&lv_turn_arrows, p);
        }
        else
            m_box(&lv_top, &lv_side, p->x0, p->y0, p->z0, p->x1, p->y1, p->z1, grass, 1.0f);
    }
    for (int i = 0; i < lv.nhaz; i++) {
        Hazard *h = &lv.haz[i];
        if (h->type != HZ_SPIKES) continue;
        int nx = (int)((h->x1 - h->x0) / 0.5f + 0.5f), nz = (int)((h->z1 - h->z0) / 0.5f + 0.5f);
        if (nx < 1) nx = 1;
        if (nz < 1) nz = 1;
        float sx = (h->x1 - h->x0) / nx, sz = (h->z1 - h->z0) / nz;
        for (int a = 0; a < nx; a++)
            for (int b = 0; b < nz; b++)
                m_spike(&lv_spikes, h->x0 + sx * (a + 0.5f), h->y, h->z0 + sz * (b + 0.5f), fminf(sx, sz) * 0.48f, 0.42f);
    }
}

/* ======================================================================= */
/*  Matrices / camera                                                       */
/* ======================================================================= */
static void perspective(float fovy, float aspect, float zn, float zf)
{
    float t = zn * tanf(fovy * PI / 360.0f), r = t * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-r, r, -t, t, zn, zf);
    glMatrixMode(GL_MODELVIEW);
}

static void look_at(float ex, float ey, float ez, float cx, float cy, float cz)
{
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * 0 - fz * 1, sy = fz * 0 - fx * 0, sz = fx * 1 - fy * 0;   /* f x up */
    float sl = sqrtf(sx * sx + sy * sy + sz * sz); sx /= sl; sy /= sl; sz /= sl;
    float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    float m[16] = { sx, ux, -fx, 0, sy, uy, -fy, 0, sz, uz, -fz, 0, 0, 0, 0, 1 };
    glLoadIdentity();
    glMultMatrixf(m);
    glTranslatef(-ex, -ey, -ez);
}

/* ======================================================================= */
/*  Text                                                                    */
/* ======================================================================= */
static float text_width(const char *s, float sc)
{
    float w = 0;
    for (; *s; s++) { int c = (unsigned char)*s; if (c < 32 || c > 127) c = '?'; w += (font_widths[c - 32] + 2) * sc; }
    return w - 2 * sc;
}

static void draw_text(float x, float y, float sc, uint32_t rgba, const char *s)
{
    uint8_t c[4] = { rgba >> 24, rgba >> 16, rgba >> 8, rgba };
    m_reset(&scratch);
    for (; *s; s++) {
        int ch = (unsigned char)*s; if (ch < 32 || ch > 127) ch = '?';
        int i = ch - 32;
        /* half-texel inset so nearest sampling never picks up the neighbouring glyph */
        float u0 = ((i % 16) * 16 + 0.5f) / 256.0f, v0 = ((i / 16) * 16 + 0.5f) / 128.0f;
        float u1 = u0 + 15.0f / 256.0f, v1 = v0 + 15.0f / 128.0f;
        float p0[3] = { x, y + 16 * sc, 0 }, p1[3] = { x + 16 * sc, y + 16 * sc, 0 }, p2[3] = { x + 16 * sc, y, 0 }, p3[3] = { x, y, 0 };
        m_quad(&scratch, p0, p1, p2, p3, u0, v0, u1, v1, c);
        x += (font_widths[i] + 2) * sc;
    }
    draw_mesh(&scratch, tex[TEX_FONT]);
}

/* text with a hard drop shadow, optionally centred */
static void text_fx(float x, float y, float sc, uint32_t col, uint32_t shadow, int centre, const char *s)
{
    if (centre) x = SCR_W / 2 - text_width(s, sc) / 2;
    float off = sc * 1.5f;
    if (shadow) draw_text(x + off, y + off, sc, shadow, s);
    draw_text(x, y, sc, col, s);
}

static void begin_2d(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, SCR_W, SCR_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);      /* Dreamcast: 2D would still write depth (in front of everything) without this */
    glEnable(GL_BLEND);
}
static void end_2d(void) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); }

/* ======================================================================= */
/*  Game state                                                              */
/* ======================================================================= */
enum { ST_TITLE, ST_PLAY, ST_DEAD, ST_CLEAR, ST_WIN, ST_RACE, ST_PODIUM, ST_CREDITS, ST_OVER, ST_EDIT, ST_PET };
static int state = ST_TITLE;
static float state_t, game_t;
static int score, paused;
static Player pl;
static float cam_yaw, cam_manual_t, cam_x, cam_y, cam_z, idle_t;
/* my levels: turn pieces turn the course direction the camera follows (like the turns in the normal levels).
   Each one works once per try; what was turned before the last checkpoint is kept when you go back there. */
static float cam_turn, cp_turn;
static uint8_t turn_used[MAX_PLATS], cp_turn_used[MAX_PLATS];
static int prev_a, prev_start, prev_b;
static float prev_sy;
static int pause_sel;            /* pause menu: 0 = resume, 1 = quit to title */
static float pause_prev_sy[2];
static void pause_open(const Input *ins) { paused = 1; pause_sel = 0; pause_prev_sy[0] = ins[0].sy; pause_prev_sy[1] = ins[1].sy; }

/* ---------- VMU saves: 3 files ---------- */
#define NUM_FILES 3
/* version 3: + the skins this file has (bit c * 8 + n, like skins_unlocked) */
typedef struct { char magic[4]; int version, level, score; uint32_t check; int falls; uint32_t fcheck; uint32_t skins, scheck; } SaveData;
#define SAVE_V1_SIZE 20                /* version 1 saves (before the fall counter) have no falls / fcheck */
#define SAVE_V2_SIZE 28                /* version 2 saves have no skins: they get the ones their level earns */
static SaveData files[NUM_FILES];
static int file_used[NUM_FILES];      /* 0 empty, 1 in progress, 2 all 150 cleared */
static int cur_file;                  /* file being played, -1 = none (no VMU) */
static int card_present;
static int title_mode, menu_sel, sub_sel, main_sel;   /* title_mode 4: character select */
static int player_char;                      /* 1-player hero: 0 = M.I.K.E, 1 = A.L.E.X */
static int race_swap;                        /* race: 0 = P1 M.I.K.E / P2 A.L.E.X, 1 = swapped (title_mode 5 picks it) */
static int race_cp_on = 1, race_menu_row;    /* race setup: checkpoints on/off, which row the cursor is on */
static int char_level, char_score, char_back; /* what the character select starts, and where B goes */   /* title_mode: 0 press start, 1 file select, 2 continue/start over, 3 1P/2P */
static float need_p2_t;
static float save_msg_t;
static const char *save_msg = "";

static uint32_t save_check(const SaveData *s) { return (uint32_t)(s->level * 2654435761u) ^ (uint32_t)(s->score * 40503u) ^ 0x4D494B45u; }
static uint32_t falls_check(const SaveData *s) { return (uint32_t)(s->falls * 2246822519u) ^ 0x46414C4Cu; }
static uint32_t skins_check(const SaveData *s) { return (s->skins * 3266489917u) ^ 0x534B494Eu; }
static uint32_t level_skin_bits(int beaten)      /* the level skins earned by beating this many levels */
{
    uint32_t m = 0;
    for (int c = 0; c < 2; c++) for (int k = 0; k < NUM_SKINS; k++) if (skins[c][k].unlock == UNLOCK_LEVEL && beaten >= skins[c][k].need) m |= 1u << (c * 8 + k);
    return m;
}
static int falls;                     /* falls on the file being played (levels mode) */
static int falls_saved;               /* what's on the VMU, so quitting only saves when it changed */

static void read_saves(void)
{
    card_present = plat_storage_present();
    for (int i = 0; i < NUM_FILES; i++) {
        SaveData s;
        file_used[i] = 0;
        int n = card_present ? plat_load(i, &s, sizeof s) : 0;
        if (n == SAVE_V1_SIZE) { s.falls = 0; s.fcheck = falls_check(&s); s.skins = level_skin_bits(s.level - 1); }      /* older save: no falls yet */
        else if ((n != SAVE_V2_SIZE && n != (int)sizeof s) || s.fcheck != falls_check(&s) || s.falls < 0) n = 0;
        if (n == SAVE_V2_SIZE) s.skins = level_skin_bits(s.level - 1);            /* older save: no skins yet */
        else if (n == (int)sizeof s && s.scheck != skins_check(&s)) n = 0;
        if (n && !memcmp(s.magic, "MIKE", 4) && s.version == (n == SAVE_V1_SIZE ? 1 : n == SAVE_V2_SIZE ? 2 : 3) &&
            s.check == save_check(&s) && s.level >= 1 && s.level <= NUM_LEVELS + 1) {
            files[i] = s;
            file_used[i] = s.level > NUM_LEVELS ? 2 : 1;
        }
    }
}

static void write_save(int level, int sc)
{
    if (cur_file < 0) return;
    SaveData s; memcpy(s.magic, "MIKE", 4); s.version = 3; s.level = level; s.score = sc; s.check = save_check(&s);
    s.falls = falls; s.fcheck = falls_check(&s);
    s.skins = (file_used[cur_file] ? files[cur_file].skins : 0) | level_skin_bits(level - 1); s.scheck = skins_check(&s);
    files[cur_file].skins = s.skins;
    falls_saved = falls; files[cur_file].falls = falls;
    char desc[48];
    if (level > NUM_LEVELS) snprintf(desc, sizeof desc, "FILE %d ALL CLEARED SCORE %d", cur_file + 1, sc);
    else snprintf(desc, sizeof desc, "FILE %d LEVEL %d SCORE %d", cur_file + 1, level, sc);
    int r = plat_save(cur_file, &s, sizeof s, desc);
    if (r == SAVE_OK) save_msg = "SAVED";
    else if (r == SAVE_FULL) save_msg = "VMU FULL - NOT SAVED";
    else if (r == SAVE_ERROR) save_msg = "SAVE FAILED";
    else save_msg = "";      /* no VMU: the title screen already says so */
    save_msg_t = save_msg[0] ? 1.8f : 0;
}
/* ---------- 1 player menu, level select, endless mode ---------- */
/* title_mode 6: 1 player menu, 7: level select, 8: endless high scores */
static int game_mode;                 /* 0 levels, 1 endless, 2 my levels (editor) */
static uint8_t ed_grid[ED_W * ED_H];  /* the level being edited / played in MY LEVELS */
static int ed_testing;                /* playing it from the editor: clearing / quitting goes back there */
static int ed_slot;                   /* which of the MY LEVELS slots */
static void go_title(void);
static void draw_mylevels_menu(void);
static void draw_pet_menu(void);
static int menu_row, menu_mode_sel = 1, menu_cp_sel = 1;   /* mode: 0 endless 1 levels; checkpoints: 0 on 1 off */
static int lvl_cursor = 1, lvl_unlocked = 1;
static int session_level = 1, session_score, session_falls;                /* progress when there's no VMU */
static int lives, end_cleared, end_hist[9], end_hist_n, end_rank = -1;
#define NUM_SCORES 10
typedef struct { int score, levels, ch; } ScoreEntry;
typedef struct { char magic[4]; int version, n; ScoreEntry e[NUM_SCORES]; uint32_t check; } ScoreTable;
static ScoreTable scores;

static uint32_t score_check(const ScoreTable *t)
{
    uint32_t h = 0x5C0BE5u ^ (uint32_t)t->n;
    for (int i = 0; i < NUM_SCORES; i++) h = h * 31u + (uint32_t)t->e[i].score * 7u + (uint32_t)t->e[i].levels * 13u + (uint32_t)t->e[i].ch;
    return h;
}
static void read_scores(void)
{
    ScoreTable t;
    if (plat_storage_present() && plat_load(SAVE_SCORES, &t, sizeof t) == (int)sizeof t && !memcmp(t.magic, "MKSC", 4) && t.version == 1 &&
        t.n >= 0 && t.n <= NUM_SCORES && t.check == score_check(&t)) scores = t;
    else if (memcmp(scores.magic, "MKSC", 4)) { memset(&scores, 0, sizeof scores); memcpy(scores.magic, "MKSC", 4); scores.version = 1; }
}
/* ---------- extras: skins unlocked and chosen (and, later, VMU pet rewards) ---------- */
typedef struct { char magic[4]; int version; uint32_t unlocked; int skin[2]; int pet_points, reserved[3]; uint32_t check; } Extras;
static float unlock_msg_t;
static char unlock_msg[48];
static uint32_t extras_check(const Extras *e)
{ return (e->unlocked * 2654435761u) ^ (uint32_t)(e->skin[0] * 7 + e->skin[1] * 131 + e->pet_points * 31) ^ 0x4D4B4558u; }
/* level skins belong to a save file: in the 1 player menu only the chosen file's progress counts
   (skin_scope = that file, -1 = no VMU / this session). Endless and pet skins are shared. */
static int skin_scope = -2;                       /* -2: any file (2 player race, pet) */
static int scope_beaten(int f);
static int skin_open(int c, int k)
{
    const Skin *sk = &skins[c][k];
    uint32_t bit = 1u << (c * 8 + k);
    if (sk->unlock == UNLOCK_FREE) return 1;
    if (sk->unlock == UNLOCK_ENDLESS && (skins_unlocked & bit)) return 1;         /* endless skins: every file */
    if (skin_scope >= 0) return file_used[skin_scope] && (files[skin_scope].skins & bit);   /* this file's own */
    if (skin_scope == -1) return sk->unlock == UNLOCK_LEVEL && scope_beaten(-1) >= sk->need;   /* no VMU: this session */
    uint32_t any = skins_unlocked;                                                  /* race: any file has it */
    for (int i = 0; i < NUM_FILES; i++) if (file_used[i]) any |= files[i].skins;
    return (any & bit) != 0;
}
static void read_extras(void)
{
    Extras e;
    if (plat_storage_present() && plat_load(SAVE_EXTRAS, &e, sizeof e) == (int)sizeof e && !memcmp(e.magic, "MKEX", 4) &&
        e.version == 1 && e.check == extras_check(&e)) {
        skins_unlocked |= e.unlocked;
        for (int c = 0; c < 2; c++) if (e.skin[c] >= 0 && e.skin[c] < NUM_SKINS) skin_sel[c] = e.skin[c];   /* checked per file when a menu opens */
    }
    skin_view[0] = skin_sel[0]; skin_view[1] = skin_sel[1];
}
static void write_extras(void)
{
    Extras e; memset(&e, 0, sizeof e); memcpy(e.magic, "MKEX", 4); e.version = 1;
    e.unlocked = skins_unlocked; e.skin[0] = skin_sel[0]; e.skin[1] = skin_sel[1]; e.check = extras_check(&e);
    plat_save(SAVE_EXTRAS, &e, sizeof e, "SKINS AND REWARDS");
}
static int saved_skin[2] = { -1, -1 };
static void save_skin_choice(void)       /* called when a game starts: only writes if the choice changed */
{
    if (skin_sel[0] != saved_skin[0] || skin_sel[1] != saved_skin[1]) {
        write_extras(); saved_skin[0] = skin_sel[0]; saved_skin[1] = skin_sel[1];
    }
}
static const char *skin_need_text(const Skin *sk, char *buf, int n)
{
    if (sk->unlock == UNLOCK_LEVEL) { if (sk->need >= 150) snprintf(buf, n, "BEAT ALL 150 LEVELS"); else snprintf(buf, n, "BEAT LEVEL %d", sk->need); }
    else if (sk->unlock == UNLOCK_ENDLESS) snprintf(buf, n, "GET %d POINTS IN ENDLESS", sk->need);
    else if (sk->unlock == UNLOCK_PET) snprintf(buf, n, "EARN IT FROM THE VMU PET");
    else buf[0] = 0;
    return buf;
}
static int add_score(int sc, int levels, int ch)      /* returns the rank (0..9) or -1 if it didn't make the board */
{
    int pos = scores.n;
    while (pos > 0 && scores.e[pos - 1].score < sc) pos--;
    if (pos >= NUM_SCORES) return -1;
    for (int i = (scores.n < NUM_SCORES ? scores.n : NUM_SCORES - 1); i > pos; i--) scores.e[i] = scores.e[i - 1];
    scores.e[pos].score = sc; scores.e[pos].levels = levels; scores.e[pos].ch = ch;
    if (scores.n < NUM_SCORES) scores.n++;
    scores.check = score_check(&scores);
    int r = plat_save(SAVE_SCORES, &scores, sizeof scores, "ENDLESS HIGH SCORES");
    if (r == SAVE_FULL) { save_msg = "VMU FULL - NOT SAVED"; save_msg_t = 2.5f; }
    else if (r == SAVE_ERROR) { save_msg = "SAVE FAILED"; save_msg_t = 2.5f; }
    return pos;
}

static uint32_t rng = 12345;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

/* endless: a random level that wasn't one of the last 9 (so a level never comes back within 10) */
static int endless_pick(void)
{
    for (;;) {
        int n = 1 + (int)(rnd() * NUM_LEVELS);
        if (n > NUM_LEVELS) n = NUM_LEVELS;
        int seen = 0;
        for (int i = 0; i < end_hist_n; i++) seen |= end_hist[i] == n;
        if (seen) continue;
        if (end_hist_n == 9) { memmove(end_hist, end_hist + 1, 8 * sizeof end_hist[0]); end_hist_n = 8; }
        end_hist[end_hist_n++] = n;
        return n;
    }
}

static void check_unlocks(int endless_score, int old_beaten, int new_beaten);
/* smiley pickup */
static int sm_state;        /* 0 none this attempt, 1 waiting to appear, 2 visible, 3 done */
static float sm_delay, sm_t, sm_x, sm_y, sm_z;
static float msg_t;
static char msg[32];

/* ---- optional checkpoints (1 player menu): some plain platforms of the normal level turn gold ---- */
static int opt_checkpoints;          /* setting from the 1 player menu */
static int cp_plat;                  /* checkpoint reached on this level (0 = the start) */

static int plat_is_plain(int i)      /* no spikes, no lava and no sad faces on it */
{
    const Plat *p = &lv.plat[i];
    for (int k = 0; k < lv.nhaz; k++) {
        const Hazard *h = &lv.haz[k];
        if (h->x1 > p->x0 && h->x0 < p->x1 && h->z1 > p->z0 && h->z0 < p->z1) return 0;
    }
    for (int k = 0; k < lv.nen; k++) {
        const Enemy *e = &lv.en[k];
        float x0 = e->axis == 0 ? e->a : e->x, x1 = e->axis == 0 ? e->b : e->x;
        float z0 = e->axis == 1 ? e->a : e->z, z1 = e->axis == 1 ? e->b : e->z;
        if (x1 > p->x0 - 0.5f && x0 < p->x1 + 0.5f && z1 > p->z0 - 0.5f && z0 < p->z1 + 0.5f && fabsf(e->y - ENEMY_R - p->y1) < 0.3f) return 0;
    }
    return 1;
}

static void mark_checkpoints(void)
{
    if (!opt_checkpoints) return;
    int since = 0;
    for (int i = 1; i < lv.nplat - 1; i++) {      /* about every 4 platforms, on the next plain one */
        if (++since >= 4 && plat_is_plain(i)) { lv.plat[i].checkpoint = 1; since = 0; }
    }
}

static void start_attempt(void)
{
    if (game_mode == 2) level_from_grid(&lv, ed_grid);     /* checkpoints are the ones placed in the editor */
    else { level_build(&lv, lv.num); mark_checkpoints(); }
    build_level_meshes();
    player_reset(&pl, &lv);
    if (cp_plat > 0) {                    /* back at the last checkpoint */
        Plat *c = &lv.plat[cp_plat];
        pl.x = (c->x0 + c->x1) * 0.5f; pl.z = (c->z0 + c->z1) * 0.5f; pl.y = c->y1;
        pl.ground_plat = cp_plat; pl.face = atan2f(c->fx, c->fz);
    }
    state = ST_PLAY; state_t = 0;
    sm_state = rnd() < 0.2f ? 1 : 0;            /* about 1 in 5 plays */
    sm_delay = 2.0f + rnd() * 5.0f;
    cam_yaw = 0; cam_manual_t = 0;
    if (cp_plat > 0) { cam_turn = cp_turn; memcpy(turn_used, cp_turn_used, sizeof turn_used); }
    else { cam_turn = 0; memset(turn_used, 0, sizeof turn_used); }
    if (cp_plat > 0) cam_yaw = atan2f(-lv.plat[cp_plat].fx, -lv.plat[cp_plat].fz);
    cam_yaw += cam_turn;
    while (cam_yaw > PI) cam_yaw -= 2 * PI;
    while (cam_yaw < -PI) cam_yaw += 2 * PI;
    cam_x = pl.x + sinf(cam_yaw) * 7.5f; cam_y = pl.y + 3.6f; cam_z = pl.z + cosf(cam_yaw) * 7.5f;
}

static void start_level(int n)
{
    lv.num = n;
    cp_plat = 0;
    start_attempt();
    if (game_mode != 1) { snprintf(msg, sizeof msg, "LEVEL %d", n); msg_t = 1.6f; }   /* endless doesn't say which level */
}

static float wrap_angle(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

static void update_smiley(float dt)
{
    if (sm_state == 1) {
        sm_delay -= dt;
        if (sm_delay <= 0) {
            int target = pl.ground_plat + 2;
            if (target > lv.nplat - 2) target = lv.nplat - 2;
            if (target < 1) target = 1;
            /* float it over safe ground on that platform (never over spikes or lava, where the only
               way to reach it was to die); if a platform has no safe spot, try the next ones */
            int found = 0;
            for (int t = 0; t < 4 && !found; t++) {
                int pi = target + (t == 0 ? 0 : t == 1 ? 1 : t == 2 ? -1 : 2);
                if (pi < 1 || pi > lv.nplat - 2) continue;
                Plat *p = &lv.plat[pi];
                float cx = (p->x0 + p->x1) * 0.5f, cz = (p->z0 + p->z1) * 0.5f, best = 1e9f;
                for (float x = p->x0 + 0.5f; x <= p->x1 - 0.5f + 0.001f; x += 0.25f)
                    for (float z = p->z0 + 0.5f; z <= p->z1 - 0.5f + 0.001f; z += 0.25f) {
                        int bad = 0;   /* keep a margin from any hazard */
                        for (int k = 0; k < 9 && !bad; k++)
                            bad = point_in_hazard(&lv, x + (k % 3 - 1) * 0.45f, z + (k / 3 - 1) * 0.45f, p->y1);
                        float d = (x - cx) * (x - cx) + (z - cz) * (z - cz);
                        if (!bad && d < best) { best = d; sm_x = x; sm_z = z; sm_y = p->y1 + 1.1f; found = 1; }
                    }
            }
            sm_state = found ? 2 : 3; sm_t = 5.0f;
        }
    } else if (sm_state == 2) {
        sm_t -= dt;
        float dx = sm_x - pl.x, dy = sm_y - (pl.y + 0.35f), dz = sm_z - pl.z;
        if (dx * dx + dy * dy + dz * dz < 0.95f * 0.95f) {
            score += 5; sm_state = 3;
            if (game_mode == 1) { lives += 2; snprintf(msg, sizeof msg, "SMILEY! +5 +2 LIVES"); check_unlocks(score, 0, 0); }
            else snprintf(msg, sizeof msg, "SMILEY! +5");
            msg_t = 1.4f;
        } else if (sm_t <= 0) sm_state = 3;
    }
}

static void turn_to_camera(float dt)
{
    float target = atan2f(cam_x - pl.x, cam_z - pl.z);
    pl.face += wrap_angle(target - pl.face) * (1.0f - expf(-6.0f * dt));
}

static void update_camera(float dt, const Input *in)
{
    /* triggers rotate; otherwise ease toward the course direction */
    float manual = in->rtrig - in->ltrig;
    if (fabsf(manual) > 0.1f) { cam_yaw += manual * 2.4f * dt; cam_manual_t = 2.5f; }
    else if (cam_manual_t > 0) cam_manual_t -= dt;
    else {
        int next = pl.ground_plat + 1; if (next >= lv.nplat) next = lv.nplat - 1;
        Plat *p = &lv.plat[next];
        float target = atan2f(-p->fx, -p->fz) + cam_turn;
        cam_yaw += wrap_angle(target - cam_yaw) * (1.0f - expf(-1.6f * dt));
    }
    cam_yaw = wrap_angle(cam_yaw);
    float dist = 7.5f, h = 3.6f;
    float tx = pl.x + sinf(cam_yaw) * dist, ty = pl.y + h, tz = pl.z + cosf(cam_yaw) * dist;
    float k = 1.0f - expf(-6.0f * dt);
    cam_x += (tx - cam_x) * k; cam_y += (ty - cam_y) * k; cam_z += (tz - cam_z) * k;
}

/* ======================================================================= */
/*  Drawing pieces                                                          */
/* ======================================================================= */
static void draw_sky(float spin)
{
    glPushMatrix();
    glTranslatef(cam_x, cam_y, cam_z);
    glRotatef(spin, 0, 1, 0);
    draw_mesh(&sky, tex[TEX_SKY]);
    glPopMatrix();
}

/* the screen quad again with its own uvs / colour / depth offset (animated skins) */
static void screen_quad(float (*q)[3], float dz, float du, float dv, const uint8_t c[4], GLuint t)
{
    float p[4][3];
    for (int i = 0; i < 4; i++) { p[i][0] = q[i][0]; p[i][1] = q[i][1]; p[i][2] = q[i][2] + dz; }
    m_reset(&scratch);
    m_quad(&scratch, p[0], p[1], p[2], p[3], du, dv, du + 1, dv + 1, c);
    draw_mesh(&scratch, t);
}

static void draw_mike(float x, float y, float z, float face, float scale, float tilt)
{
    const Skin *sk = &skins[0][skin_view[0]];
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(face * 180.0f / PI, 0, 1, 0);
    glRotatef(tilt, 1, 0, 0);
    glScalef(scale, scale, scale);
    glTranslatef(0, 0.5f, 0);
    draw_mesh(&skin_body[0][skin_view[0]], tex[TEX_WHITE]);
    draw_mesh(&skin_trim[0][skin_view[0]], tex[TEX_WHITE]);
    static const uint8_t full[4] = { 255, 255, 255, 255 };
    if (sk->fx == FX_STATIC) {             /* TV snow that jumps every frame, the face on top */
        screen_quad(mike_scr, 0, (float)(rand() % 64) / 64.0f, (float)(rand() % 64) / 64.0f, full, tex[TEX_SNOW]);
        glEnable(GL_BLEND);
        screen_quad(mike_scr, 0.002f, 0, 0, full, tex[TEX_FACE_OVERLAY]);
        glDisable(GL_BLEND);
    } else if (sk->fx == FX_GLITCH && fmodf(game_t * 1.3f, 1.7f) < 0.16f) {   /* every so often the picture slips */
        float j = (fmodf(game_t * 97.0f, 1.0f) - 0.5f) * 0.12f;
        screen_quad(mike_scr, 0, j, 0, full, tex[fmodf(game_t * 31.0f, 1.0f) < 0.5f ? TEX_FACE_GLITCH : TEX_FACE]);
    } else draw_mesh(&mike_screen, tex[sk->tex]);
    if (bday_char == 0)          /* his birthday: the hat goes on whatever skin he's wearing (grey on BLACK & WHITE) */
        draw_mesh(strcmp(sk->name, "BLACK & WHITE") ? &party_hat[0] : &party_hat_bw, tex[TEX_WHITE]);
    else if (sk->fx == FX_CAP) draw_mesh(&mike_cap, tex[TEX_WHITE]);
    glPopMatrix();
}

static void draw_alex(float x, float y, float z, float face, float scale, float tilt)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(face * 180.0f / PI, 0, 1, 0);
    glRotatef(tilt, 1, 0, 0);
    glScalef(scale, scale, scale);
    glTranslatef(0, 0.5f, 0);
    const Skin *sk = &skins[1][skin_view[1]];
    draw_mesh(&skin_body[1][skin_view[1]], tex[TEX_WHITE]);
    if (sk->fx == FX_CASE) glTranslatef(0, 0, 0.025f);      /* screen and button sit on the front of the case */
    draw_mesh(&skin_trim[1][skin_view[1]], tex[TEX_WHITE]);
    if (sk->fx == FX_RAINBOW) {
        float h = fmodf(game_t * 0.25f, 1.0f) * 6;
        int k = (int)h; float f = h - k;
        float r = k == 0 || k == 5 ? 1 : k == 1 ? 1 - f : k == 4 ? f : 0;
        float g = k == 0 ? f : k <= 2 ? 1 : k == 3 ? 1 - f : 0;
        float b = k <= 1 ? 0 : k == 2 ? f : k <= 4 ? 1 : 1 - f;
        uint8_t c[4] = { (uint8_t)(70 + 185 * r), (uint8_t)(70 + 185 * g), (uint8_t)(70 + 185 * b), 255 };
        screen_quad(alex_scr, 0, 0, 0, c, tex[TEX_ALEX_RAINBOW]);
    } else draw_mesh(&alex_screen, tex[sk->tex]);
    if (sk->fx == FX_PHONES) draw_mesh(&alex_phones, tex[TEX_WHITE]);
    if (bday_char == 1) draw_mesh(&party_hat[1], tex[TEX_WHITE]);
    glPopMatrix();
}

static void draw_hero(float x, float y, float z, float face, float scale, float tilt)
{
    if (player_char == 1) draw_alex(x, y, z, face, scale, tilt);
    else draw_mike(x, y, z, face, scale, tilt);
}

static void draw_ball(float x, float y, float z, float yaw_deg, int tex_id, float scale)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yaw_deg, 0, 1, 0);
    glScalef(scale, scale, scale);
    draw_mesh(&ball, tex[tex_id]);
    glPopMatrix();
}

int debug_lava_red;   /* test builds only: tint the lava floor so it can be counted */
static int lava_tiles = 14;          /* the race's two views use fewer */
static void draw_lava_floor(float y, float scroll)
{
    m_reset(&lava_mesh);
    const int N = lava_tiles; const float S = 12.0f;
    float ox = floorf(cam_x / S) * S - N * S / 2, oz = floorf(cam_z / S) * S - N * S / 2;
    uint8_t c[4] = { 255, (uint8_t)(debug_lava_red ? 0 : 255), (uint8_t)(debug_lava_red ? 0 : 255), 255 };
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            float x0 = ox + i * S, z0 = oz + j * S, x1 = x0 + S, z1 = z0 + S;
            float a[3] = { x0, y, z1 }, b[3] = { x1, y, z1 }, cc[3] = { x1, y, z0 }, d[3] = { x0, y, z0 };
            /* uv per vertex from world position so the tiles join up */
            V *v = m_push(&lava_mesh, 6);
            float k = 1.0f / 3.0f;
            setv(&v[0], a[0], y, a[2], a[0] * k + scroll, a[2] * k + scroll * 0.6f, c);
            setv(&v[1], b[0], y, b[2], b[0] * k + scroll, b[2] * k + scroll * 0.6f, c);
            setv(&v[2], cc[0], y, cc[2], cc[0] * k + scroll, cc[2] * k + scroll * 0.6f, c);
            setv(&v[3], a[0], y, a[2], a[0] * k + scroll, a[2] * k + scroll * 0.6f, c);
            setv(&v[4], cc[0], y, cc[2], cc[0] * k + scroll, cc[2] * k + scroll * 0.6f, c);
            setv(&v[5], d[0], y, d[2], d[0] * k + scroll, d[2] * k + scroll * 0.6f, c);
        }
    draw_mesh(&lava_mesh, tex[TEX_LAVA]);
}

static void draw_lava_pools(float scroll)
{
    m_reset(&lava_mesh);
    uint8_t c[4] = { 255, 255, 255, 255 };
    for (int i = 0; i < lv.nhaz; i++) {
        Hazard *h = &lv.haz[i];
        if (h->type != HZ_LAVA) continue;
        float y = h->y + 0.02f, k = 1.0f / 1.5f;
        V *v = m_push(&lava_mesh, 6);
        setv(&v[0], h->x0, y, h->z1, h->x0 * k + scroll, h->z1 * k, c);
        setv(&v[1], h->x1, y, h->z1, h->x1 * k + scroll, h->z1 * k, c);
        setv(&v[2], h->x1, y, h->z0, h->x1 * k + scroll, h->z0 * k, c);
        setv(&v[3], h->x0, y, h->z1, h->x0 * k + scroll, h->z1 * k, c);
        setv(&v[4], h->x1, y, h->z0, h->x1 * k + scroll, h->z0 * k, c);
        setv(&v[5], h->x0, y, h->z0, h->x0 * k + scroll, h->z0 * k, c);
    }
    draw_mesh(&lava_mesh, tex[TEX_LAVA]);
}

/* soft round shadow under M.I.K.E so jumps are easy to judge */
static void draw_blob_shadow_for(const Player *who)
{
    Player pl = *who;   /* local copy shadows the global */
    float best = -1e9f;
    for (int i = 0; i < lv.nplat; i++) {
        Plat *p = &lv.plat[i];
        if (pl.x > p->x0 && pl.x < p->x1 && pl.z > p->z0 && pl.z < p->z1 && p->y1 <= pl.y + 0.05f && p->y1 > best) best = p->y1;
    }
    if (best < -1e8f) return;
    float h = pl.y - best;
    float r = 0.55f * (1.0f - fminf(h, 6.0f) / 12.0f);
    uint8_t c[4] = { 20, 0, 40, (uint8_t)(150 * (1.0f - fminf(h, 6.0f) / 8.0f)) };
    m_reset(&scratch);
    for (int i = 0; i < 12; i++) {
        float a0 = i * 2 * PI / 12, a1 = (i + 1) * 2 * PI / 12;
        V *v = m_push(&scratch, 3);
        setv(&v[0], pl.x, best + 0.03f, pl.z, 0, 0, c);
        setv(&v[1], pl.x + cosf(a1) * r, best + 0.03f, pl.z + sinf(a1) * r, 0, 0, c);
        setv(&v[2], pl.x + cosf(a0) * r, best + 0.03f, pl.z + sinf(a0) * r, 0, 0, c);
    }
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    draw_mesh(&scratch, tex[TEX_WHITE]);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

static void draw_world(void)
{
    perspective(55.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, pl.x, pl.y + 0.9f, pl.z);
    draw_sky(game_t * 0.6f);
    draw_lava_floor(lv.lava_y, game_t * 0.05f);
    draw_mesh(&lv_top, tex[TEX_PLAT_TOP]);
    draw_mesh(&lv_side, tex[TEX_PLAT_SIDE]);
    draw_mesh(&lv_fin_top, tex[TEX_FINISH]);
    draw_mesh(&lv_spikes, tex[TEX_WHITE]);
    draw_mesh(&lv_turn, tex[TEX_WHITE]);
    draw_lava_pools(game_t * 0.08f);

    for (int i = 0; i < lv.nen; i++) {
        Enemy *e = &lv.en[i];
        float ddx = e->x - pl.x, ddz = e->z - pl.z;
        if (ddx * ddx + ddz * ddz > 55.0f * 55.0f) continue;   /* too far to matter: skip the draw */
        float bounce = fabsf(sinf(e->phase * 5.0f)) * 0.25f;
        float yaw = atan2f(pl.x - e->x, pl.z - e->z) * 180.0f / PI;   /* sad faces watch you */
        draw_ball(e->x, e->y + bounce, e->z, yaw, TEX_SAD, 1.0f);
    }

    if (sm_state == 2 && (sm_t > 1.5f || fmodf(sm_t, 0.25f) < 0.15f)) {
        float pop = fminf(1.0f, (5.0f - sm_t) / 0.3f);
        float yaw = atan2f(cam_x - sm_x, cam_z - sm_z) * 180.0f / PI;
        draw_ball(sm_x, sm_y + sinf(game_t * 2.4f) * 0.1f, sm_z, yaw, TEX_SMILEY, pop * 1.3f);
        glPushMatrix();
        glTranslatef(sm_x, sm_y, sm_z);
        float s = pop * (1.0f + sinf(game_t * 6) * 0.06f);
        glScalef(s, s, s);
        glRotatef(game_t * 40, 0, 1, 0);
        draw_mesh(&ring, tex[TEX_WHITE]);
        glPopMatrix();
    }

    int visible = !(state == ST_DEAD && fmodf(state_t, 0.2f) < 0.1f);
    if (visible) {
        float tilt = state == ST_DEAD ? fminf(state_t * 200, 80) : 0;
        draw_hero(pl.x, pl.y, pl.z, pl.face, 0.65f, tilt);
    }
    draw_blob_shadow_for(&pl);
}

/* pause menu panel, centred on row cy (called between begin_2d / end_2d) */
static void draw_pause_menu(float cy)
{
    uint8_t pc[4] = { 18, 0, 46, 245 };
    float a[3] = { 170, cy + 62, 0 }, b[3] = { 470, cy + 62, 0 }, c[3] = { 470, cy - 62, 0 }, d[3] = { 170, cy - 62, 0 };
    m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
    text_fx(0, cy - 50, 2.4f, 0xFFFFFFFF, 0x7F00FFFF, 1, "PAUSED");
    int blink = fmodf(game_t, 0.8f) < 0.55f;
    const char *rows[2] = { "RESUME", game_mode == 2 && state != ST_RACE ? (ed_testing ? "BACK TO EDITOR" : "QUIT TO MY LEVELS") : "QUIT TO TITLE" };
    for (int i = 0; i < 2; i++) {
        float y = cy + 2 + i * 28;
        text_fx(0, y, 1.4f, i == pause_sel ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF, 0x3A0070FF, 1, rows[i]);
        if (i == pause_sel) text_fx(SCR_W / 2 - text_width(rows[i], 1.4f) / 2 - 26, y, 1.4f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
    }
}

static void draw_hud(void)
{
    begin_2d();
    char buf[48];
    if (game_mode == 1) snprintf(buf, sizeof buf, "ENDLESS");
    else if (game_mode == 2) snprintf(buf, sizeof buf, ed_testing ? "TEST PLAY" : "MY LEVEL %d", ed_slot + 1);
    else snprintf(buf, sizeof buf, "LEVEL %d/%d", lv.num, NUM_LEVELS);
    text_fx(24, 20, 1.5f, 0xFFFFFFFF, 0x3A0070FF, 0, buf);
    if (game_mode == 1) {
        snprintf(buf, sizeof buf, "LIVES %d", lives);
        text_fx(SCR_W - 24 - text_width(buf, 1.5f), 48, 1.5f, 0xFFD400FF, 0x3A0070FF, 0, buf);
    } else if (game_mode == 0) {
        snprintf(buf, sizeof buf, "FALLS %d", falls);
        text_fx(SCR_W - 24 - text_width(buf, 1.1f), 50, 1.1f, 0xFF8A9AFF, 0x3A0070FF, 0, buf);
    }
    snprintf(buf, sizeof buf, "SCORE %d", score);
    text_fx(SCR_W - 24 - text_width(buf, 1.5f), 20, 1.5f, 0x00FF1AFF, 0x000000FF, 0, buf);
    if (sm_state == 2) {
        snprintf(buf, sizeof buf, "GRAB THE SMILEY! %d", (int)ceilf(sm_t));
        text_fx(0, 60, 1.5f, 0xFFD400FF, 0x3A0070FF, 1, buf);
    }
    /* the middle of the screen shows one thing at a time: GAME OVER / LEVEL CLEAR / OUCH beat the short
       messages (LEVEL n, CHECKPOINT!, SMILEY!...), and a long message shrinks to fit the screen */
    int centre_busy = state == ST_DEAD || state == ST_CLEAR || state == ST_OVER || paused;
    if (msg_t > 0 && !centre_busy) {
        float ms = 3.0f, mw = text_width(msg, ms);
        if (mw > SCR_W - 40) ms *= (SCR_W - 40) / mw;
        text_fx(0, 190 + (3.0f - ms) * 6, ms, 0xFFFFFFFF, 0x7F00FFFF, 1, msg);
    }
    if (unlock_msg_t > 0 && !paused && state != ST_OVER && fmodf(game_t, 0.5f) < 0.38f) text_fx(0, 420, 1.4f, 0xFFD400FF, 0x3A0070FF, 1, unlock_msg);
    if (state == ST_DEAD) text_fx(0, 190, 3.5f, 0xFF4D5EFF, 0x000000FF, 1, "OUCH!");
    if (state == ST_OVER) {
        uint8_t pc[4] = { 18, 0, 46, 200 };
        float a[3] = { 110, 360, 0 }, b[3] = { 530, 360, 0 }, c[3] = { 530, 130, 0 }, d[3] = { 110, 130, 0 };
        m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
        text_fx(0, 146, 3.0f, 0xFF4D5EFF, 0x000000FF, 1, "GAME OVER");
        snprintf(buf, sizeof buf, "SCORE %d", score);
        text_fx(0, 214, 2.0f, 0x00FF1AFF, 0x3A0070FF, 1, buf);
        snprintf(buf, sizeof buf, "LEVELS CLEARED %d", end_cleared);
        text_fx(0, 254, 1.3f, 0xFFFFFFFF, 0x3A0070FF, 1, buf);
        if (end_rank >= 0 && fmodf(game_t, 0.8f) < 0.55f) text_fx(0, 288, 1.5f, 0xFFD400FF, 0x3A0070FF, 1, end_rank == 0 ? "NEW HIGH SCORE!" : "YOU MADE THE BOARD!");
        if (state_t > 2.0f) text_fx(0, 326, 1.2f, 0xE0C8FFFF, 0x3A0070FF, 1, "PRESS START");
    }
    if (state == ST_CLEAR) {
        text_fx(0, 170, 3.0f, 0x00FF1AFF, 0x3A0070FF, 1, "LEVEL CLEAR!");
        text_fx(0, 230, 2.0f, 0xFFFFFFFF, 0x3A0070FF, 1, game_mode == 1 ? "+1  +1 LIFE" : "+1");
    }
    if (paused) draw_pause_menu(220);
    if (save_msg_t > 0) text_fx(SCR_W - 24 - text_width(save_msg, 1.0f), 444, 1.0f, 0xFFFFFFFF, 0x3A0070FF, 0, save_msg);
    end_2d();
}

/* ---------- title + win scenes ---------- */
static void draw_platform_at(float x, float y, float z, float w, float d)
{
    static Mesh side;
    m_reset(&scratch); m_reset(&side);
    m_box(&scratch, &side, x - w / 2, y - 0.6f, z - d / 2, x + w / 2, y, z + d / 2, WHITE, 1.0f);
    draw_mesh(&scratch, tex[TEX_PLAT_TOP]);
    draw_mesh(&side, tex[TEX_PLAT_SIDE]);
}


/* ---------- 1 player menu (like the mock-up), level select, endless high scores ---------- */
static void menu_panel(float x0, float y0, float x1, float y1, int alpha)
{
    uint8_t pc[4] = { 18, 0, 46, (uint8_t)alpha };
    float a[3] = { x0, y1, 0 }, b[3] = { x1, y1, 0 }, c[3] = { x1, y0, 0 }, d[3] = { x0, y0, 0 };
    m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
}
static void menu_frame(float x0, float y0, float x1, float y1, uint32_t rgba)   /* box outline */
{
    uint8_t c[4] = { rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255 };
    float t = 2.5f;
    float q[4][4] = { { x0, y0, x1, y0 + t }, { x0, y1 - t, x1, y1 }, { x0, y0, x0 + t, y1 }, { x1 - t, y0, x1, y1 } };
    m_reset(&scratch);
    for (int i = 0; i < 4; i++) {
        float a[3] = { q[i][0], q[i][3], 0 }, b[3] = { q[i][2], q[i][3], 0 }, cc[3] = { q[i][2], q[i][1], 0 }, d[3] = { q[i][0], q[i][1], 0 };
        m_quad(&scratch, a, b, cc, d, 0, 0, 1, 1, c);
    }
    draw_mesh(&scratch, tex[TEX_WHITE]);
}
/* a choice box: picked = this option is chosen; focus = the cursor is on this row */
static void menu_button(float cx, float y, float w, const char *label, int picked, int focus, int blink)
{
    float h = 38;
    if (picked) menu_panel(cx - w / 2, y, cx + w / 2, y + h, 235);
    uint32_t edge = picked ? (focus && blink ? 0xFFD400FF : 0x00FF1AFF) : 0x8A70C0FF;
    menu_frame(cx - w / 2, y, cx + w / 2, y + h, edge);
    float ts = 1.6f, tw = text_width(label, ts);
    if (tw > w - 20) { ts *= (w - 20) / tw; tw = w - 20; }      /* long labels shrink to fit */
    text_fx(cx - tw / 2, y + 10 + (1.6f - ts) * 6, ts, picked ? 0xFFFFFFFF : 0xB8A8E0FF, 0x3A0070FF, 0, label);
}

/* move the skin cursor; landing on an unlocked skin also picks it */
static void skin_step(int c, int d)
{
    skin_view[c] = (skin_view[c] + d + NUM_SKINS) % NUM_SKINS;
    if (skin_open(c, skin_view[c])) skin_sel[c] = skin_view[c];
}

/* "< SKIN NAME >" under a character box; a locked skin says what unlocks it */
static void draw_skin_row_at(float cx, float y, int c, int active, int blink, uint32_t col, float sc)
{
    const Skin *sk = &skins[c][skin_view[c]];
    char t[48], need[40];
    int open = skin_open(c, skin_view[c]);
    if (open) snprintf(t, sizeof t, "< %s >", sk->name);
    else snprintf(t, sizeof t, "< LOCKED >");
    uint32_t tc = !open ? 0xFF8A9AFF : active ? (blink ? col : 0xFFFFFFFF) : 0xB8A8E0FF;
    text_fx(cx - text_width(t, sc) / 2, y, sc, tc, 0x3A0070FF, 0, t);
    if (!open) { skin_need_text(sk, need, sizeof need); text_fx(cx - text_width(need, sc * 0.7f) / 2, y + 16, sc * 0.7f, 0xFF8A9AFF, 0x3A0070FF, 0, need); }
}
static void draw_skin_row(float y, int c, int active, int blink, uint32_t col)
{
    if (active) text_fx(SCR_W / 2 - 268, y, 1.1f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
    text_fx(SCR_W / 2 - 250, y, 1.1f, active ? 0xFFFFFFFF : 0xB8A8E0FF, 0x3A0070FF, 0, "SKIN");
    int n = 0; for (int k = 0; k < NUM_SKINS; k++) n += skin_open(c, k);
    char cnt[24]; snprintf(cnt, sizeof cnt, "%d/%d", n, NUM_SKINS);
    text_fx(SCR_W / 2 + 250 - text_width(cnt, 0.9f), y + 2, 0.9f, 0xB8A8E0FF, 0x3A0070FF, 0, cnt);
    draw_skin_row_at(SCR_W / 2, y, c, active, blink, col, 1.1f);
}

static void draw_1p_menus(void)
{
    int blink = fmodf(game_t, 0.8f) < 0.55f;
    if (title_mode == 5) {      /* 2 player race: characters and checkpoints, same look as the 1 player menu */
        menu_panel(30, 14, 610, 466, 205);
        menu_frame(30, 14, 610, 466, 0x7F00FFFF);
        text_fx(0, 34, 2.4f, 0x00FF1AFF, 0x3A0070FF, 1, "2 PLAYER RACE");
        text_fx(0, 92, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 1, "FIRST TO THE FINISH WINS!");
        const char *heads[3] = { "CHOOSE YOUR CHARACTER", NULL, "CHECKPOINTS ON OR OFF?" };
        float hy[3] = { 140, 0, 338 };
        for (int r = 0; r < 3; r++) {
            if (!heads[r]) continue;
            text_fx(0, hy[r], 1.5f, r == race_menu_row ? 0xFFFFFFFF : 0xB8A8E0FF, 0x3A0070FF, 1, heads[r]);
            if (r == race_menu_row) text_fx(SCR_W / 2 - text_width(heads[r], 1.5f) / 2 - 26, hy[r], 1.5f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
        }
        for (int i = 0; i < 2; i++) {
            float cx = i == 0 ? 200 : 440;
            int player = i ^ race_swap;                  /* 0 = player 1 */
            uint32_t pcol = player == 0 ? 0xFFD400FF : 0x57A0FFFF;
            menu_frame(cx - 100, 168, cx + 100, 300, race_menu_row == 0 && blink ? 0xFFFFFFFF : pcol);
            const char *tag = player == 0 ? "PLAYER 1" : "PLAYER 2";
            text_fx(cx - 94, 174, 1.0f, pcol, 0x3A0070FF, 0, tag);   /* corner, clear of the antennas */
            const char *nm = i == 0 ? "M.I.K.E" : "A.L.E.X";
            text_fx(cx - text_width(nm, 1.2f) / 2, 278, 1.2f, 0xFFFFFFFF, 0x3A0070FF, 0, nm);
        }
        for (int i = 0; i < 2; i++) {      /* each player picks the skin of the character they play */
            float cx = i == 0 ? 200 : 440;
            draw_skin_row_at(cx, 304, i, race_menu_row == 1, blink, (i ^ race_swap) == 0 ? 0xFFD400FF : 0x57A0FFFF, 0.85f);
        }
        menu_button(200, 370, 200, "ON", race_cp_on, race_menu_row == 2, blink);
        menu_button(440, 370, 200, "OFF", !race_cp_on, race_menu_row == 2, blink);
        if (blink) text_fx(0, 420, 1.3f, 0xFFD400FF, 0x3A0070FF, 1, "PRESS START WHEN YOU ARE READY");
        text_fx(0, 446, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_SWAP);
        return;
    }
    if (title_mode == 6) {
        menu_panel(30, 14, 610, 466, 205);
        menu_frame(30, 14, 610, 466, 0x7F00FFFF);
        const char *heads[4] = { "CHOOSE A MODE", "CHOOSE YOUR CHARACTER", NULL, "CHECKPOINTS ON OR OFF?" };
        float hy[4] = { 30, 140, 0, 338 };
        for (int r = 0; r < 4; r++) {
            if (!heads[r]) continue;
            uint32_t col = r == menu_row ? 0xFFFFFFFF : 0xB8A8E0FF;
            text_fx(0, hy[r], 1.5f, col, 0x3A0070FF, 1, heads[r]);
            if (r == menu_row) text_fx(SCR_W / 2 - text_width(heads[r], 1.5f) / 2 - 26, hy[r], 1.5f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
        }
        draw_skin_row(304, player_char, menu_row == 2, blink, 0xFFFFFFFF);
        menu_button(130, 66, 170, "ENDLESS", menu_mode_sel == 0, menu_row == 0, blink);
        menu_button(320, 66, 170, "LEVELS", menu_mode_sel == 1, menu_row == 0, blink);
        menu_button(510, 66, 170, "MY LEVELS", menu_mode_sel == 2, menu_row == 0, blink);
        /* the characters themselves are drawn in 3D over this panel; here just their boxes and names */
        for (int i = 0; i < 2; i++) {
            float cx = i == 0 ? 200 : 440;
            int picked = player_char == i;
            menu_frame(cx - 100, 168, cx + 100, 300, picked ? (menu_row == 1 && blink ? 0xFFD400FF : 0x00FF1AFF) : 0x8A70C0FF);
            const char *nm = i == 0 ? "M.I.K.E" : "A.L.E.X";
            text_fx(cx - text_width(nm, 1.2f) / 2, 278, 1.2f, picked ? 0xFFFFFFFF : 0xB8A8E0FF, 0x3A0070FF, 0, nm);
        }
        menu_button(200, 370, 200, "ON", menu_cp_sel == 0, menu_row == 3, blink);
        menu_button(440, 370, 200, "OFF", menu_cp_sel == 1, menu_row == 3, blink);
        if (cur_file >= 0) { char fl[16]; snprintf(fl, sizeof fl, "FILE %d", cur_file + 1); text_fx(46, 22, 0.9f, 0x00FF1AFF, 0x3A0070FF, 0, fl); }
        if (blink) text_fx(0, 420, 1.3f, 0xFFD400FF, 0x3A0070FF, 1, "PRESS START WHEN YOU ARE READY");
        text_fx(0, 446, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_MENU1P);
    } else if (title_mode == 7) {
        menu_panel(20, 14, 620, 466, 215);
        menu_frame(20, 14, 620, 466, 0x7F00FFFF);
        char h[40];
        if (cur_file >= 0) snprintf(h, sizeof h, "LEVEL SELECT - FILE %d", cur_file + 1);
        else snprintf(h, sizeof h, "LEVEL SELECT");
        text_fx(0, 26, 1.6f, 0xFFFFFFFF, 0x3A0070FF, 1, h);
        for (int n = 1; n <= NUM_LEVELS; n++) {
            int col = (n - 1) % 15, row = (n - 1) / 15;
            float x = 48 + col * 36.5f, y = 70 + row * 32;
            int open = n <= lvl_unlocked, sel = n == lvl_cursor;
            char t[8]; snprintf(t, sizeof t, "%d", n);
            if (sel) menu_panel(x - 2, y - 2, x + 34, y + 26, 255);
            menu_frame(x - 2, y - 2, x + 34, y + 26, sel ? (blink ? 0xFFD400FF : 0x00FF1AFF) : open ? 0x8A70C0FF : 0x3A2460FF);
            float ts = n >= 100 ? 0.72f : 0.9f;     /* 3-digit numbers a bit smaller so they fit their box */
            text_fx(x + 16 - text_width(t, ts) / 2, y + 7 + (0.9f - ts) * 5, ts, open ? (n < lvl_unlocked ? 0x00FF1AFF : 0xFFFFFFFF) : 0x4A3478FF, 0x000000FF, 0, t);
        }
        char info[48];
        if (lvl_cursor <= lvl_unlocked) snprintf(info, sizeof info, "LEVEL %d%s", lvl_cursor, lvl_cursor < lvl_unlocked ? " - CLEARED" : "");
        else snprintf(info, sizeof info, "LEVEL %d - LOCKED", lvl_cursor);
        text_fx(0, 400, 1.3f, lvl_cursor <= lvl_unlocked ? 0xFFD400FF : 0xFF8080FF, 0x3A0070FF, 1, info);
        text_fx(0, 440, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_LEVELSEL);
    } else if (title_mode >= 11) {
        draw_pet_menu();
    } else if (title_mode >= 9) {
        draw_mylevels_menu();
    } else {   /* 8: endless high scores */
        menu_panel(70, 14, 570, 466, 215);
        menu_frame(70, 14, 570, 466, 0x7F00FFFF);
        text_fx(0, 24, 1.8f, 0x00FF1AFF, 0x3A0070FF, 1, "ENDLESS");
        menu_button(SCR_W / 2, 62, 220, "START", 1, 1, blink);
        text_fx(0, 116, 1.1f, 0xFFD400FF, 0x3A0070FF, 1, "HIGH SCORES");
        text_fx(118, 142, 0.9f, 0xB8A8E0FF, 0, 0, "RANK");
        text_fx(210, 142, 0.9f, 0xB8A8E0FF, 0, 0, "SCORE");
        text_fx(316, 142, 0.9f, 0xB8A8E0FF, 0, 0, "LEVELS");
        text_fx(430, 142, 0.9f, 0xB8A8E0FF, 0, 0, "PLAYER");
        for (int i = 0; i < NUM_SCORES; i++) {
            float y = 164 + i * 26;
            char t[24];
            uint32_t col = i == end_rank && blink ? 0xFFD400FF : i < scores.n ? 0xFFFFFFFF : 0x6A5498FF;
            snprintf(t, sizeof t, "%d.", i + 1); text_fx(118, y, 1.2f, col, 0x3A0070FF, 0, t);
            if (i < scores.n) {
                snprintf(t, sizeof t, "%d", scores.e[i].score); text_fx(210, y, 1.2f, col, 0x3A0070FF, 0, t);
                snprintf(t, sizeof t, "%d", scores.e[i].levels); text_fx(316, y, 1.2f, col, 0x3A0070FF, 0, t);
                text_fx(430, y, 1.2f, scores.e[i].ch ? 0x57A0FFFF : 0x00FF1AFF, 0x3A0070FF, 0, scores.e[i].ch ? "A.L.E.X" : "M.I.K.E");
            } else text_fx(210, y, 1.2f, col, 0x3A0070FF, 0, "---");
        }
        text_fx(0, 444, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_SCORES);
    }
}

static void draw_1p_menu_characters(void)    /* 3D characters standing in their boxes on the 1 player menu */
{
    glClear(GL_DEPTH_BUFFER_BIT);
    perspective(45.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(0, 0.2f, 6.0f, 0, 0.1f, 0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
#ifdef _arch_dreamcast
    force_blend = 1;            /* on top of the menu panel, not under it */
#endif
    for (int i = 0; i < 2; i++) {
        int sel = title_mode == 5 || player_char == i;          /* race: both are playing, both bounce */
        float x = i == 0 ? -1.24f : 1.24f, y = -0.2f + (sel ? fabsf(sinf(game_t * 3 + i * 1.5f)) * 0.1f : 0);
        float face = sel ? sinf(game_t * 1.5f + i) * 0.5f : (i == 0 ? 0.3f : -0.3f);
        if (i == 0) draw_mike(x, y, 0, face, 0.55f, 0); else draw_alex(x, y, 0, face, 0.53f, 0);
    }
    force_blend = 0;
    glDisable(GL_BLEND);
}

static void draw_title(void)
{
    cam_x = 0; cam_y = 0.2f; cam_z = 6.0f;
    perspective(45.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, 0, 0.1f, 0);
    draw_sky(game_t * 1.2f);
    draw_platform_at(0, -1.25f, 0, 3, 2);
    draw_platform_at(-4, -1.6f, -5, 2, 2);
    draw_platform_at(4.2f, -0.6f, -6, 2, 2);
    draw_platform_at(-2.5f, 1.6f, -9, 3, 1);
    if (title_mode >= 5) {
        /* the menus draw their own characters on top of the panel (below) */
    } else if (title_mode == 4 || title_mode == 5) {
        /* character select: the picked one bounces and turns, the other stands still */
        for (int i = 0; i < 2; i++) {
            float x = i == 0 ? -1.4f : 1.4f, sel = title_mode == 5 || i == player_char;
            float y = -1.0f + (sel ? fabsf(sinf(game_t * 3)) * 0.25f : 0);
            float face = sel ? sinf(game_t * 1.5f) * 0.6f : (i == 0 ? 0.3f : -0.3f);
            if (i == 0) draw_mike(x, y, 0, face, 0.95f, 0); else draw_alex(x, y, 0, face, 0.95f, 0);
        }
    } else {
        draw_mike(0, -1.0f + sinf(game_t * 2) * 0.08f, 0, sinf(game_t * 0.8f) * 0.5f, 1.35f, 0);
        int beaten = 0;          /* a save file with all the levels beaten: M.I.K.E gets his trophy */
        for (int i = 0; i < NUM_FILES; i++) if (file_used[i] && files[i].level > NUM_LEVELS) beaten = 1;
        if (beaten) {
            glPushMatrix();
            glTranslatef(1.55f, -1.25f, 0.1f);
            glRotatef(-25 + sinf(game_t * 0.7f) * 20, 0, 1, 0);
            glScalef(0.55f, 0.55f, 0.55f);
            draw_mesh(&trophy_gold, tex[TEX_WHITE]);
            draw_mesh(&trophy_base, tex[TEX_WHITE]);
            draw_mesh(&plaque, tex[TEX_PLAQUE]);
            glPopMatrix();
        }
    }

    begin_2d();
    if (title_mode >= 5) { draw_1p_menus(); end_2d(); if (title_mode == 6 || title_mode == 5) draw_1p_menu_characters(); return; }
    {   /* layered logo shadow like the mock-up */
        float x = SCR_W / 2 - text_width("M.I.K.E", 6.0f) / 2;
        draw_text(x + 8, 44, 6.0f, 0x3A0070FF, "M.I.K.E");
        draw_text(x + 4, 40, 6.0f, 0xFF9A00FF, "M.I.K.E");
        draw_text(x, 36, 6.0f, 0x00FF1AFF, "M.I.K.E");
    }
    text_fx(0, 134, 1.0f, 0xFFFFFFFF, 0x3A0070FF, 1, "MULTIMEDIA INTERACTIVE KINETIC ELEMENT");
    int blink = fmodf(game_t, 0.8f) < 0.55f;
    if (title_mode == 0) {
        if (fmodf(game_t, 1.0f) < 0.6f) text_fx(0, 384, 2.0f, 0xFFFFFFFF, 0x7F00FFFF, 1, "PRESS START");
        if (!card_present) text_fx(0, 418, 0.9f, 0xFFB0B0FF, 0x3A0070FF, 1, "NO VMU FOUND - PROGRESS WON'T BE SAVED");
        text_fx(0, 434, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_CREDITS);
    } else if (title_mode == 3) {
        uint8_t pc[4] = { 18, 0, 46, 190 };
        float a[3] = { 140, 448, 0 }, b[3] = { 500, 448, 0 }, c[3] = { 500, 316, 0 }, d[3] = { 140, 316, 0 };
        m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
        const char *rows[3] = { "1 PLAYER", "2 PLAYER RACE", "M.I.K.E ON VMU" };
        for (int i = 0; i < 3; i++) {
            float y = 328 + i * 30;
            text_fx(0, y, 1.6f, i == main_sel ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF, 0x3A0070FF, 1, rows[i]);
            if (i == main_sel) text_fx(SCR_W / 2 - text_width(rows[i], 1.6f) / 2 - 28, y, 1.6f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
        }
        if (need_p2_t > 0) text_fx(0, 426, 0.9f, 0xFFB0B0FF, 0x3A0070FF, 1, "PLUG A CONTROLLER INTO PORT B");
        else text_fx(0, 426, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_CREDITS);
    } else if (title_mode == 4) {
        text_fx(0, 158, 1.6f, 0xFFFFFFFF, 0x3A0070FF, 1, "CHOOSE YOUR CHARACTER");
        uint8_t pc[4] = { 18, 0, 46, 190 };
        float a[3] = { 60, 448, 0 }, b[3] = { 580, 448, 0 }, c[3] = { 580, 372, 0 }, d[3] = { 60, 372, 0 };
        m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
        const char *names[2] = { "M.I.K.E", "A.L.E.X" };
        for (int i = 0; i < 2; i++) {
            float cx = i == 0 ? 185 : 455, w = text_width(names[i], 1.6f);
            int sel = i == player_char;
            text_fx(cx - w / 2, 384, 1.6f, sel ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF, 0x3A0070FF, 0, names[i]);
            if (sel) text_fx(cx - w / 2 - 28, 384, 1.6f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
        }
        text_fx(0, 420, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_CHOOSE);
    } else if (title_mode == 5) {
        /* race: who plays whom. Either player can swap */
        text_fx(0, 158, 1.6f, 0xFFFFFFFF, 0x3A0070FF, 1, "CHOOSE YOUR CHARACTER");
        uint8_t pc[4] = { 18, 0, 46, 190 };
        float a[3] = { 60, 448, 0 }, b[3] = { 580, 448, 0 }, c[3] = { 580, 344, 0 }, d[3] = { 60, 344, 0 };
        m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
        const char *names[2] = { "M.I.K.E", "A.L.E.X" };
        for (int i = 0; i < 2; i++) {
            float cx = i == 0 ? 185 : 455;
            int player = i ^ race_swap;          /* 0 = player 1 */
            const char *tag = player == 0 ? "PLAYER 1" : "PLAYER 2";
            text_fx(cx - text_width(tag, 1.2f) / 2, 356, 1.2f, player == 0 ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : (blink ? 0x57A0FFFF : 0xFFFFFFFF), 0x3A0070FF, 0, tag);
            text_fx(cx - text_width(names[i], 1.6f) / 2, 384, 1.6f, 0xFFFFFFFF, 0x3A0070FF, 0, names[i]);
        }
        text_fx(0, 420, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_SWAP);
    } else {
        /* file select (title_mode 1) or the continue / start over choice (title_mode 2) */
        char rows[3][64]; int n;
        if (title_mode == 1) {
            n = NUM_FILES;
            for (int i = 0; i < NUM_FILES; i++) {
                if (file_used[i] == 1) snprintf(rows[i], sizeof rows[i], "FILE %d  LEVEL %d  SCORE %d  FALLS %d", i + 1, files[i].level, files[i].score, files[i].falls);
                else if (file_used[i] == 2) snprintf(rows[i], sizeof rows[i], "FILE %d  ALL CLEARED  SCORE %d  FALLS %d", i + 1, files[i].score, files[i].falls);
                else snprintf(rows[i], sizeof rows[i], "FILE %d  EMPTY", i + 1);
            }
        } else {
            n = 2;
            snprintf(rows[0], sizeof rows[0], "CONTINUE - LEVEL %d", files[menu_sel].level);
            snprintf(rows[1], sizeof rows[1], "START OVER");
        }
        {   /* dark panel behind the menu so it reads over the scene */
            uint8_t pc[4] = { 18, 0, 46, 190 };
            float a[3] = { 60, 448, 0 }, b[3] = { 580, 448, 0 }, c[3] = { 580, 326, 0 }, d[3] = { 60, 326, 0 };
            m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, pc); draw_mesh(&scratch, tex[TEX_WHITE]);
        }
        int sel = title_mode == 1 ? menu_sel : sub_sel;
        float y0 = title_mode == 1 ? 346 : 362;
        for (int i = 0; i < n; i++) {
            float y = y0 + i * 28;
            float ts = 1.3f, tw = text_width(rows[i], ts);
            if (tw > 490) { ts *= 490 / tw; tw = 490; }           /* long rows shrink to fit the panel */
            text_fx(0, y + (1.3f - ts) * 6, ts, i == sel ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF, 0x3A0070FF, 1, rows[i]);
            if (i == sel) text_fx(SCR_W / 2 - tw / 2 - 26, y, 1.3f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
        }
        if (title_mode == 2) {
            char h[24]; snprintf(h, sizeof h, "FILE %d", menu_sel + 1);
            text_fx(0, 334, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 1, h);
        }
        text_fx(0, 432, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_SELECT);
    }
    text_fx(0, 452, 0.9f, 0xE0C8FFFF, 0, 1, "(C) 2026 PROSYSTEMS CORP");
    if (unlock_msg_t > 0 && fmodf(game_t, 0.5f) < 0.38f) text_fx(0, 8, 1.2f, 0xFFD400FF, 0x3A0070FF, 1, unlock_msg);
    if (bday_popup) {          /* birthday message over the title */
        const char *who = bday_char == 0 ? "M.I.K.E" : "A.L.E.X";
        char t1[40], t2[48];
        snprintf(t1, sizeof t1, "IT'S %s'S BIRTHDAY!", who);
        snprintf(t2, sizeof t2, "%s IS WEARING A PARTY HAT TODAY.", who);
        menu_panel(70, 150, 570, 330, 235);
        menu_frame(70, 150, 570, 330, 0xFFD400FF);
        text_fx(0, 176, 1.8f, 0xFFD400FF, 0x3A0070FF, 1, "HAPPY BIRTHDAY!");
        text_fx(0, 226, 1.25f, 0xFFFFFFFF, 0x3A0070FF, 1, t1);
        text_fx(0, 258, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 1, t2);
        if (state_t > 0.6f && fmodf(game_t, 1.0f) < 0.6f) text_fx(0, 296, 1.1f, 0x00FF1AFF, 0x3A0070FF, 1, "PRESS START");
    }
    end_2d();
}

typedef struct { float x, z, s, o, r; uint8_t c; } Confetti;
static Confetti conf[70];

static void draw_confetti(float t);
static void draw_win(void)
{
    cam_x = 0; cam_y = 0.5f; cam_z = 6.4f;
    perspective(45.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, 0, -0.05f, 0);
    draw_sky(game_t * 1.2f);
    draw_platform_at(0, -1.35f, 0, 5, 2.2f);
    float t = state_t;
    draw_hero(-1.65f, -1.35f + fabsf(sinf(t * 3)) * 0.3f, 0.2f, 0.35f, 1.0f, 0);
    draw_ball(1.65f, -1.35f + ENEMY_R * 1.3f + fabsf(sinf(t * 3 + 1.5f)) * 0.3f, 0.2f, -20 + sinf(t) * 17, TEX_SMILEY, 1.3f);

    glPushMatrix();
    glTranslatef(0, -1.35f, -0.1f);
    glRotatef(t * 40, 0, 1, 0);
    glScalef(1.05f, 1.05f, 1.05f);
    draw_mesh(&trophy_gold, tex[TEX_WHITE]);
    draw_mesh(&trophy_base, tex[TEX_WHITE]);
    draw_mesh(&plaque, tex[TEX_PLAQUE]);
    glPopMatrix();

    draw_confetti(t);

    begin_2d();
    text_fx(0, 30, 2.6f, 0xFFD400FF, 0xFF9A00FF, 1, "CONGRATULATIONS!");
    text_fx(0, 92, 1.5f, 0xFFFFFFFF, 0x3A0070FF, 1, "ALL 150 LEVELS CLEARED");
    {
        char fb[48];
        if (falls == 0) snprintf(fb, sizeof fb, "WITHOUT A SINGLE FALL!");
        else snprintf(fb, sizeof fb, "YOU FELL %d TIME%s ALONG THE WAY", falls, falls == 1 ? "" : "S");
        text_fx(0, 128, 1.1f, 0xFF8A9AFF, 0x3A0070FF, 1, fb);
    }
    char buf[32]; snprintf(buf, sizeof buf, "SCORE %d", score);
    text_fx(0, 400, 2.2f, 0x00FF1AFF, 0x000000FF, 1, buf);
    if (state_t > 3.0f && fmodf(game_t, 1.0f) < 0.6f) text_fx(0, 448, 1.0f, 0xFFFFFFFF, 0x3A0070FF, 1, "PRESS START");
    end_2d();
}

static void draw_confetti(float t)
{
    static const uint8_t cols[5][3] = { { 0, 255, 26 }, { 255, 154, 0 }, { 127, 0, 255 }, { 255, 212, 0 }, { 255, 255, 255 } };
    m_reset(&scratch);
    for (int i = 0; i < 70; i++) {
        Confetti *c = &conf[i];
        float y = 3.5f - fmodf(t * c->s + c->o, 6.0f);
        float x = c->x + sinf(t + c->o) * 0.2f, a = t * 3 + c->r;
        float dx = cosf(a) * 0.05f, dy = 0.07f * cosf(t * 2 + c->r);
        const uint8_t *cc = cols[c->c];
        uint8_t col[4] = { cc[0], cc[1], cc[2], 255 };
        float p0[3] = { x - dx, y - dy, c->z }, p1[3] = { x + dx, y - dy, c->z }, p2[3] = { x + dx, y + dy + 0.07f, c->z }, p3[3] = { x - dx, y + dy + 0.07f, c->z };
        m_quad(&scratch, p0, p1, p2, p3, 0, 0, 1, 1, col);
    }
    draw_mesh(&scratch, tex[TEX_WHITE]);
}

#include "race.inc"
#include "editor.inc"
#include "pet.inc"

/* ======================================================================= */
/*  Main loop                                                               */
/* ======================================================================= */
static float prev_sx, prev_sx2, prev_sy2, prev_sy_r;
static void char_select(int level, int sc, int back) { char_level = level; char_score = sc; char_back = back; title_mode = 4; prev_sx = 0; }
static void open_level_select(void)
{
    if (cur_file >= 0) lvl_unlocked = file_used[cur_file] ? files[cur_file].level : 1;
    else lvl_unlocked = session_level;
    if (lvl_unlocked > NUM_LEVELS) lvl_unlocked = NUM_LEVELS;        /* all cleared: everything is open */
    if (lvl_unlocked < 1) lvl_unlocked = 1;
    lvl_cursor = lvl_unlocked;
    title_mode = 7;
}

static void start_endless(void)
{
    game_mode = 1; score = 0; lives = 3; end_cleared = 0; end_hist_n = 0; end_rank = -1;
    rng ^= (uint32_t)(game_t * 1000.0f) * 2654435761u;      /* a different run every time */
    plat_music(MUSIC_GAME);
    start_level(endless_pick());
}

static int scope_beaten(int f)      /* levels beaten on file f (-1: this session) */
{
    if (f >= 0) return file_used[f] ? files[f].level - 1 : 0;
    return session_level - 1;
}
/* level skins: from the furthest level beaten (any file, for the race menu; the chosen file for 1 player).
   old_beaten / new_beaten: the file just played, before and after - a level skin reached in between is announced. */
static void check_unlocks(int endless_score, int old_beaten, int new_beaten)
{
    int beaten = session_level - 1;
    for (int i = 0; i < NUM_FILES; i++) if (file_used[i] && files[i].level - 1 > beaten) beaten = files[i].level - 1;
    int best_end = endless_score;          /* endless skins: points in one endless run */
    for (int i = 0; i < scores.n; i++) if (scores.e[i].score > best_end) best_end = scores.e[i].score;
    uint32_t before = skins_unlocked;
    const char *first = NULL; int firstc = 0;
    for (int c = 0; c < 2; c++)
        for (int k = 1; k < NUM_SKINS; k++) {
            const Skin *sk = &skins[c][k];
            int bit = (skins_unlocked >> (c * 8 + k)) & 1;
            int ok = (sk->unlock == UNLOCK_LEVEL && beaten >= sk->need) || (sk->unlock == UNLOCK_ENDLESS && best_end >= sk->need);
            if (ok && !bit) skins_unlocked |= 1u << (c * 8 + k);
            int news = sk->unlock == UNLOCK_LEVEL ? (old_beaten < sk->need && new_beaten >= sk->need) : (ok && !bit && sk->unlock == UNLOCK_ENDLESS);
            if (news && !first) { first = sk->name; firstc = c; }
        }
    if (first) {
        snprintf(unlock_msg, sizeof unlock_msg, "NEW SKIN: %s %s!", firstc ? "A.L.E.X" : "M.I.K.E", first);
        unlock_msg_t = 3.5f;
    }
    if (skins_unlocked != before) write_extras();
}

/* into the 1 player menu for save file f (-1: no VMU); a skin this file hasn't earned goes back to CLASSIC */
static void open_1p_menu(int f)
{
    cur_file = f; skin_scope = f;
    for (int c = 0; c < 2; c++) { if (!skin_open(c, skin_sel[c])) skin_sel[c] = 0; skin_view[c] = skin_sel[c]; }
    title_mode = 6; menu_row = 0;
}
static void leave_1p_menu(void) { skin_scope = -2; read_extras(); }   /* back out: the saved choice again */

static void go_title(void) { state = ST_TITLE; state_t = 0; paused = 0; title_mode = 0; read_saves(); plat_music(MUSIC_TITLE); }

static void go_win(void)
{
    state = ST_WIN; state_t = 0;
    for (int i = 0; i < 70; i++) {
        conf[i].x = (rnd() - 0.5f) * 7; conf[i].z = (rnd() - 0.5f) * 3 - 0.5f;
        conf[i].s = 0.4f + rnd() * 0.6f; conf[i].o = rnd() * 10; conf[i].r = rnd() * 6; conf[i].c = i % 5;
    }
    plat_music(MUSIC_TITLE);
}

int game_two_player_active(void);
/* one frame of game logic; also used by the PC test harness. ins[0] = port A, ins[1] = port B */
static int prev_y, prev_start2, prev_a2;
/* ---------- VMU screen during play: small character, level, and lives / falls / race place ---------- */
static const char *vmu_glyph(char ch)
{
    switch (ch) {
    case '0': return "####.##.##.####"; case '1': return ".#.##..#..#.###"; case '2': return "###..#####..###";
    case '3': return "###..#.##..####"; case '4': return "#.##.####..#..#"; case '5': return "####..###..####";
    case '6': return "####..####.####"; case '7': return "###..#..#.#..#."; case '8': return "####.#####.####";
    case '9': return "####.####..####"; case 'A': return ".#.#.####.##.##"; case 'C': return "####..#..#..###";
    case 'D': return "##.#.##.##.###."; case 'E': return "####..##.#..###"; case 'F': return "####..##.#..#..";
    case 'G': return "####..#.##.####";
    case 'H': return "#.##.####.##.##"; case 'I': return "###.#..#..#.###"; case 'L': return "#..#..#..#..###";
    case 'N': return "##.#.##.##.##.#"; case 'O': return ".#.#.##.##.#.#."; case 'P': return "##.#.###.#..#..";
    case 'R': return "##.#.###.#.##.#"; case 'S': return ".###...#...###."; case 'T': return "###.#..#..#..#.";
    case 'V': return "#.##.##.##.#.#."; case 'W': return "#.##.########.#"; case '/': return "..#..#.#.#..#..";
    case '!': return ".#..#..#.....#."; default: return "...............";
    }
}
static void vmu_text(char *img, int x, int y, int scale, const char *t)
{
    for (; *t; t++, x += 4 * scale) {
        const char *g = vmu_glyph(*t);
        for (int gy = 0; gy < 5; gy++) for (int gx = 0; gx < 3; gx++) if (g[gy * 3 + gx] == '#')
            for (int sy = 0; sy < scale; sy++) for (int sx = 0; sx < scale; sx++) {
                int px = x + gx * scale + sx, py = y + gy * scale + sy;
                if (px >= 0 && px < 48 && py >= 0 && py < 32) img[py * 48 + px] = '.';
            }
    }
}
static const char vmu_icon_mike[] =
    "......#.....#..........."
    ".......#...#............"
    "........#.#............."
    "########################"
    "#......................#"
    "#.##############.......#"
    "#.#............#..###..#"
    "#.#.....#......#.......#"
    "#.#....#...#...#..###..#"
    "#.#....#.......#.......#"
    "#.#....#...#...#..###..#"
    "#.#.....#......#.......#"
    "#.#............#.......#"
    "#.##############.......#"
    "#......................#"
    "########################"
    ;
static const char vmu_icon_alex[] =
    "........................"
    ".######################."
    "#......................#"
    "#.################.....#"
    "#.#..............#.....#"
    "#.#....######....#.....#"
    "#.#...#......#...#.....#"
    "#.#..#..#..#..#..#..##.#"
    "#.#..#........#..#.#..##"
    "#.#..#.#....#.#..#..##.#"
    "#.#...#.####.#...#.....#"
    "#.#....######....#.....#"
    "#.#..............#.....#"
    "#.################.....#"
    "#......................#"
    ".######################."
    ;
static void vmu_compose(char *img, int ch, const char *label, int big, const char *line1, const char *line2)
{
    memset(img, '+', 48 * 32); img[48 * 32] = 0;
    const char *icon = ch ? vmu_icon_alex : vmu_icon_mike;   /* small character picture, top left */
    for (int y = 0; y < 16; y++) for (int x = 0; x < 24; x++) if (icon[y * 24 + x] == '#') img[y * 48 + x] = '.';
    vmu_text(img, 27, 1, 1, label);
    char num[8]; snprintf(num, sizeof num, "%d", big);
    int w = (int)strlen(num) * 8 - 2;
    vmu_text(img, 47 - w, 7, 2, num);
    vmu_text(img, 0, 19, 1, line1);
    vmu_text(img, 0, 26, 1, line2);
}
static void vmu_update(void)
{
    static char img[2][48 * 32 + 1];
    char a[16], b[16];
    if (state == ST_PLAY || state == ST_DEAD || state == ST_CLEAR || state == ST_OVER) {
        if (game_mode == 1) {   /* endless: no level number - lives big, then the score */
            snprintf(a, sizeof a, "SCORE %d", score); snprintf(b, sizeof b, "ENDLESS");
            vmu_compose(img[0], player_char, "LIVES", lives, a, b);
        } else {
            snprintf(a, sizeof a, "SCORE %d", score); snprintf(b, sizeof b, "FALLS %d", falls);
            vmu_compose(img[0], player_char, game_mode == 2 ? "MY LV" : "LEVEL", game_mode == 2 ? ed_slot + 1 : lv.num, a, b);
        }
        plat_vmu_screen(0, img[0]);
        plat_vmu_screen(1, player_char ? lcd_alex : lcd_mike);
    } else if (state == ST_RACE) {
        for (int i = 0; i < 2; i++) {
            const Racer *r = &racer[i], *o = &racer[i ^ 1];
            int ncp = num_checkpoints();
            if (ncp) snprintf(a, sizeof a, "CP %d/%d", checkpoints_passed(r), ncp); else snprintf(a, sizeof a, "GO GO GO!");
            if (race_phase == RC_DONE) snprintf(b, sizeof b, race_winner == i ? "WINNER!" : "2ND PLACE");
            else snprintf(b, sizeof b, r->best >= o->best ? "1ST PLACE" : "2ND PLACE");
            vmu_compose(img[i], rc(i), "RACE", i + 1, a, b);
            plat_vmu_screen(i, img[i]);
        }
    } else {     /* title, podium, win, credits: the full-size pictures */
        int race = state == ST_PODIUM;
        int c1 = race ? rc(0) : state == ST_CREDITS ? 0 : player_char, c2 = race ? rc(1) : c1;
        if (state == ST_TITLE) plat_vmu_lcd(VMU_LCD_TITLE, VMU_LCD_TITLE);
        else plat_vmu_lcd(c1 ? VMU_LCD_ALEX : VMU_LCD_MIKE, c2 ? VMU_LCD_ALEX : VMU_LCD_MIKE);
    }
}

void game_update(const Input *ins, float dt)
{
    const Input *in = &ins[0];
    {   /* PC keyboard: keys move between players when the race starts or ends; a key still held
           from before the switch must not count as a new press for its new owner */
        static int last_2p = -1, lock;
        int now_2p = game_two_player_active();
        if (last_2p >= 0 && now_2p != last_2p) lock = 1;
        last_2p = now_2p;
        if (lock) {   /* until every button is released (a quick tap can span several steps and frames) */
            int held = ins[0].a || ins[0].start || ins[0].b || ins[0].y || ins[1].a || ins[1].start || ins[1].b || ins[1].y;
            if (held) prev_a = prev_start = prev_b = prev_y = prev_start2 = prev_a2 = 1;
            else lock = 0;
        }
    }
    int a_pressed = in->a && !prev_a, start_pressed = in->start && !prev_start, b_pressed = in->b && !prev_b;
    int y_pressed = in->y && !prev_y;
    int start2 = ins[1].start && !prev_start2, a2 = ins[1].a && !prev_a2;
    prev_a = in->a; prev_start = in->start; prev_b = in->b; prev_y = in->y;
    prev_start2 = ins[1].start; prev_a2 = ins[1].a;
    if (need_p2_t > 0) need_p2_t -= dt;
    if (unlock_msg_t > 0) unlock_msg_t -= dt;
    game_t += dt; state_t += dt;
    vmu_update();      /* VMU screens: play info, or that player's character */
    rng ^= (uint32_t)(game_t * 1000);

    if (paused) {
        /* pause menu: up/down picks, A chooses, Start or B resumes. In the race either player can use it */
        int race = state == ST_RACE, up = 0, down = 0;
        for (int i = 0; i < 2; i++) {
            float sy = ins[i].sy;
            if (i == 0 || race) { up |= sy < -0.5f && pause_prev_sy[i] >= -0.5f; down |= sy > 0.5f && pause_prev_sy[i] <= 0.5f; }
            pause_prev_sy[i] = sy;
        }
        if (up || down) pause_sel ^= 1;
        int choose = a_pressed || (race && a2);
        if (choose && pause_sel == 1) {
            if (state != ST_RACE && game_mode == 0) {      /* keep the falls since the last LEVEL CLEAR */
                session_falls = falls;
                if (cur_file >= 0 && falls != falls_saved)
                    write_save(file_used[cur_file] ? files[cur_file].level : 1, file_used[cur_file] ? files[cur_file].score : 0);
            }
            if (state != ST_RACE && game_mode == 2) { if (ed_testing) editor_back(); else { go_title(); mylevels_open(); } return; }
            go_title(); return;
        }
        if (choose || start_pressed || b_pressed || (race && start2)) {
            paused = 0;
            if (race) for (int i = 0; i < 2; i++) racer[i].prev_a = ins[i].a;   /* the A that resumed doesn't jump */
        }
        return;
    }

    switch (state) {
    case ST_TITLE: {
        int up = in->sy < -0.5f && prev_sy >= -0.5f, down = in->sy > 0.5f && prev_sy <= 0.5f;
        prev_sy = in->sy;
        if (bday_popup) {          /* the birthday message: any button closes it */
            if (state_t > 0.6f && (start_pressed || a_pressed || b_pressed)) { bday_popup = 0; state_t = 0; }
            return;
        }
        int ok = start_pressed || (a_pressed && state_t > 0.5f), back = b_pressed;
        if (y_pressed && (title_mode == 0 || title_mode == 3)) { go_credits(); return; }
        if (title_mode == 0) {
            if (fmodf(state_t, 3.0f) < dt && !card_present) read_saves();   /* notice a VMU plugged in later */
            if (ok) { title_mode = 3; main_sel = 0; }
        } else if (title_mode == 3) {
            if (up) main_sel = (main_sel + 2) % 3;
            if (down) main_sel = (main_sel + 1) % 3;
            if (back) title_mode = 0;
            else if (ok) {
                if (main_sel == 0) { read_saves(); if (card_present) { title_mode = 1; menu_sel = 0; } else open_1p_menu(-1); }
                else if (main_sel == 2) { skin_scope = -2; pet_menu_open(); }
                else if (!ins[1].present) need_p2_t = 2.5f;
                else { skin_scope = -2; for (int c = 0; c < 2; c++) { if (!skin_open(c, skin_sel[c])) skin_sel[c] = 0; skin_view[c] = skin_sel[c]; } title_mode = 5; race_menu_row = 0; prev_sx = in->sx; prev_sx2 = ins[1].sx; prev_sy_r = in->sy; prev_sy2 = ins[1].sy; }
            }
        } else if (title_mode == 1) {
            if (up) menu_sel = (menu_sel + NUM_FILES - 1) % NUM_FILES;
            if (down) menu_sel = (menu_sel + 1) % NUM_FILES;
            if (back) title_mode = 3;
            else if (ok) open_1p_menu(menu_sel);
        } else if (title_mode == 2) {
            if (up || down) sub_sel ^= 1;
            if (back) title_mode = 1;
            else if (ok) {
                if (sub_sel == 0) char_select(files[menu_sel].level, files[menu_sel].score, 2);
                else char_select(1, 0, 2);
            }
        } else if (title_mode == 6) {   /* 1 player menu: mode, character, checkpoints */
            int left = in->sx < -0.5f && prev_sx >= -0.5f, right = in->sx > 0.5f && prev_sx <= 0.5f;
            if (up && menu_row > 0) menu_row--;
            if (down && menu_row < 3) menu_row++;
            if (left || right) {
                if (menu_row == 0) menu_mode_sel = (menu_mode_sel + (right ? 1 : 2)) % 3;
                else if (menu_row == 1) player_char ^= 1;
                else if (menu_row == 2) skin_step(player_char, right ? 1 : -1);
                else menu_cp_sel ^= 1;
            }
            if (back) { leave_1p_menu(); title_mode = card_present ? 1 : 3; }
            else if (a_pressed && state_t > 0.5f && menu_row < 3) menu_row++;   /* A steps down the rows */
            else if (start_pressed || (a_pressed && state_t > 0.5f)) {           /* START: go with these choices */
                skin_view[0] = skin_sel[0]; skin_view[1] = skin_sel[1];
                save_skin_choice();
                opt_checkpoints = menu_cp_sel == 0;
                game_mode = menu_mode_sel == 0 ? 1 : 0;
                if (menu_mode_sel == 2) mylevels_open();
                else if (game_mode == 1) { read_scores(); end_rank = -1; title_mode = 8; }
                else open_level_select();          /* the file picked first (-1: no VMU, session progress only) */
            }
        } else if (title_mode == 7) {   /* level select */
            int left = in->sx < -0.5f && prev_sx >= -0.5f, right = in->sx > 0.5f && prev_sx <= 0.5f;
            if (left && lvl_cursor > 1) lvl_cursor--;
            if (right && lvl_cursor < NUM_LEVELS) lvl_cursor++;
            if (up && lvl_cursor > 15) lvl_cursor -= 15;
            if (down && lvl_cursor + 15 <= NUM_LEVELS) lvl_cursor += 15;
            if (back) title_mode = 6;
            else if (ok && lvl_cursor <= lvl_unlocked) {
                score = cur_file >= 0 ? (file_used[cur_file] ? files[cur_file].score : 0) : session_score;
                falls = falls_saved = cur_file >= 0 ? (file_used[cur_file] ? files[cur_file].falls : 0) : session_falls;
                game_mode = 0;
                plat_music(MUSIC_GAME); start_level(lvl_cursor);
            }
        } else if (title_mode == 11 || title_mode == 12) {   /* M.I.K.E ON VMU */
            int left = in->sx < -0.5f && prev_sx >= -0.5f, right = in->sx > 0.5f && prev_sx <= 0.5f;
            pet_menu_update(up, down, left, right, ok, back, dt);
        } else if (title_mode == 9 || title_mode == 10) {   /* my levels */
            int left = in->sx < -0.5f && prev_sx >= -0.5f, right = in->sx > 0.5f && prev_sx <= 0.5f;
            mylevels_update(up, down, left, right, ok, back, dt);
        } else if (title_mode == 8) {   /* endless high scores, START on top */
            if (back) title_mode = 6;
            else if (ok) start_endless();
        } else if (title_mode == 5) {   /* race character select: either controller swaps or starts */
            int sw = 0;
            for (int i = 0; i < 2; i++) {
                float sx = ins[i].sx, psx = i ? prev_sx2 : prev_sx;
                sw |= (sx < -0.5f && psx >= -0.5f) || (sx > 0.5f && psx <= 0.5f);
            }
            int mv = 0;                            /* up/down from either controller moves between the rows */
            for (int i = 0; i < 2; i++) {
                float sy = ins[i].sy, psy = i ? prev_sy2 : prev_sy_r;
                if (sy < -0.5f && psy >= -0.5f) mv = -1;
                if (sy > 0.5f && psy <= 0.5f) mv = 1;
            }
            prev_sy_r = ins[0].sy; prev_sy2 = ins[1].sy;
            race_menu_row += mv; if (race_menu_row < 0) race_menu_row = 0; if (race_menu_row > 2) race_menu_row = 2;
            if (race_menu_row == 1) {              /* skins: each controller changes its own player's character */
                for (int i = 0; i < 2; i++) {
                    float sx = ins[i].sx, psx = i ? prev_sx2 : prev_sx;
                    if (sx < -0.5f && psx >= -0.5f) skin_step(rc(i), -1);
                    if (sx > 0.5f && psx <= 0.5f) skin_step(rc(i), 1);
                }
            } else if (sw) { if (race_menu_row == 0) race_swap ^= 1; else race_cp_on ^= 1; }
            int go = start_pressed || start2 || (((a_pressed && state_t > 0.5f) || a2) && race_menu_row == 2);
            if (back) { title_mode = 3; skin_view[0] = skin_sel[0]; skin_view[1] = skin_sel[1]; }
            else if (!ins[1].present) { title_mode = 3; need_p2_t = 2.5f; }
            else if (go) { skin_view[0] = skin_sel[0]; skin_view[1] = skin_sel[1]; save_skin_choice(); race_start(); }
            else if ((a_pressed && state_t > 0.5f) || a2) race_menu_row++;
            prev_sx2 = ins[1].sx;
        } else {   /* title_mode 4: character select */
            int left = in->sx < -0.5f && prev_sx >= -0.5f, right = in->sx > 0.5f && prev_sx <= 0.5f;
            if (left || right || up || down) player_char ^= 1;
            if (back) title_mode = char_back;
            else if (ok) { score = char_score; plat_music(MUSIC_GAME); start_level(char_level); }
        }
        prev_sx = in->sx;
        return;
    }
    case ST_WIN:
        if (state_t > 3.0f && (start_pressed || a_pressed)) go_title();
        return;
    case ST_RACE:
        race_update(ins, start_pressed || start2, dt);
        return;
    case ST_PODIUM:
        if (state_t > 3.0f && (start_pressed || a_pressed || start2 || a2)) go_title();
        return;
    case ST_CREDITS:
        if (state_t > 1.0f && (b_pressed || start_pressed)) go_title();
        return;
    case ST_EDIT:
        editor_update(in, a_pressed, b_pressed, y_pressed, start_pressed, dt);
        return;
    case ST_PET:
        pet_here_update(in, start_pressed, dt);
        return;
    case ST_OVER:     /* endless game over: on to the high score board */
        if (state_t > 2.0f && (start_pressed || a_pressed)) { state = ST_TITLE; state_t = 0; title_mode = 8; plat_music(MUSIC_TITLE); }
        return;
    default: break;
    }

    if (start_pressed && (state == ST_PLAY)) { pause_open(ins); return; }
    if (paused) return;

    if (msg_t > 0) msg_t -= dt;
    if (save_msg_t > 0) save_msg_t -= dt;
    enemies_update(&lv, dt);

    if (state == ST_PLAY) {
        /* stick is camera-relative */
        float fx = -sinf(cam_yaw), fz = -cosf(cam_yaw);
        float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);
        float mx = rx * in->sx + fx * -in->sy, mz = rz * in->sx + fz * -in->sy;
        float ml = sqrtf(mx * mx + mz * mz); if (ml > 1) { mx /= ml; mz /= ml; }
        pl.jumped = 0;
        int r = player_step(&pl, &lv, mx, mz, a_pressed, in->a, dt);
        if (pl.jumped) plat_sfx(SFX_JUMP);
        /* standing still for a moment: M.I.K.E turns to show his face */
        if (pl.grounded && ml < 0.1f) idle_t += dt; else idle_t = 0;
        if (idle_t > 1.0f) turn_to_camera(dt);
        update_smiley(dt);
        if (pl.grounded && lv.plat[pl.ground_plat].turn && !turn_used[pl.ground_plat]) {     /* a turn piece */
            turn_used[pl.ground_plat] = 1;
            cam_turn = wrap_angle(cam_turn - lv.plat[pl.ground_plat].turn * PI * 0.5f);
            cam_manual_t = 0;
        }
        if (pl.grounded && (game_mode == 2 ? pl.ground_plat != cp_plat : pl.ground_plat > cp_plat) && lv.plat[pl.ground_plat].checkpoint) {
            cp_plat = pl.ground_plat; cp_turn = cam_turn; memcpy(cp_turn_used, turn_used, sizeof turn_used);
            snprintf(msg, sizeof msg, "CHECKPOINT!"); msg_t = 1.3f;
        }
        if (r == SIM_DEAD) { state = ST_DEAD; state_t = 0; msg_t = 0; pl.vx = pl.vz = 0; if (game_mode == 0) falls++; plat_sfx(SFX_DEATH); }
        else if (r == SIM_FINISH) { state = ST_CLEAR; state_t = 0; msg_t = 0; score += 1; plat_sfx(SFX_WIN); if (game_mode == 1) check_unlocks(score, 0, 0); }
        update_camera(dt, in);
    } else if (state == ST_DEAD) {
        if (state_t > 1.0f) {
            if (game_mode == 1 && --lives <= 0) {   /* endless: out of lives */
                lives = 0; state = ST_OVER; state_t = 0;
                end_rank = add_score(score, end_cleared, player_char);
                check_unlocks(score, 0, 0);
            } else start_attempt();
        }
    } else if (state == ST_CLEAR) {
        turn_to_camera(dt);
        if (game_mode == 1) {           /* endless: straight on to another random level, nothing saved */
            if (state_t > 1.6f) { end_cleared++; lives++; start_level(endless_pick()); }
        } else if (game_mode == 2) {    /* my levels: back to the editor, or the list */
            if (state_t > 1.8f) { if (ed_testing) editor_back(); else { go_title(); mylevels_open(); } }
        } else {
            /* autosave once the LEVEL CLEAR banner is on screen (never moves a file backwards when replaying) */
            if (state_t >= 0.15f && state_t - dt < 0.15f) {
                int reached = lv.num + 1, was = scope_beaten(cur_file);
                if (cur_file >= 0 && file_used[cur_file] && files[cur_file].level > reached) reached = files[cur_file].level;
                write_save(reached, score);
                if (cur_file >= 0) { files[cur_file].level = reached; files[cur_file].score = score; if (!file_used[cur_file]) file_used[cur_file] = 1; }
                if (reached > session_level) session_level = reached;
                session_score = score; session_falls = falls;
                check_unlocks(0, was, scope_beaten(cur_file));
            }
            if (state_t > 1.6f) {
                if (lv.num >= NUM_LEVELS) go_win();
                else start_level(lv.num + 1);
            }
        }
    }
}

void game_draw(void)
{
    glDepthMask(GL_TRUE);       /* (so the frame's depth clear works) */
    plat_begin_frame();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (state == ST_TITLE) draw_title();
    else if (state == ST_WIN) draw_win();
    else if (state == ST_RACE) race_draw();
    else if (state == ST_PODIUM) draw_podium();
    else if (state == ST_CREDITS) draw_credits();
    else if (state == ST_EDIT) draw_editor();
    else if (state == ST_PET) draw_pet_here();
    else { draw_world(); draw_hud(); }
    plat_end_frame();
}

void game_init(void)
{
    plat_upload_textures(tex);
    build_static_meshes();
    check_birthday();
    go_title();
    read_scores(); read_extras(); check_unlocks(0, 0, 0);
    saved_skin[0] = skin_sel[0]; saved_skin[1] = skin_sel[1];
}

/* box-art render: the title scene at any size, no text (text is added by tools/mkcover.py) */
int cover_variant;   /* 0 = box front, 1 = disc label */
void game_draw_cover(int w, int h, float t)
{
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    if (cover_variant == 1) {   /* disc: M.I.K.E sits below the centre hole */
        cam_x = 0; cam_y = 1.0f; cam_z = 8.6f;
        perspective(52.0f, (float)w / h, 0.3f, 160.0f);
        look_at(cam_x, cam_y, cam_z, 0, 2.35f, 0);
    } else if (cover_variant == 2) {   /* PC box: no spine, so M.I.K.E is centred */
        cam_x = 0; cam_y = 0.9f; cam_z = 6.6f;
        perspective(48.0f, (float)w / h, 0.3f, 160.0f);
        look_at(cam_x, cam_y, cam_z, 0, 0.35f, 0);
    } else {
        cam_x = -0.55f; cam_y = 0.9f; cam_z = 6.6f;
        perspective(48.0f, (float)w / h, 0.3f, 160.0f);
        look_at(cam_x, cam_y, cam_z, -0.55f, 0.35f, 0);
    }
    draw_sky(t * 1.2f);
    draw_platform_at(0, -1.25f, 0, 3, 2);
    draw_platform_at(-4.2f, -2.2f, -4, 2, 2);
    draw_platform_at(4.0f, -0.9f, -5, 2, 2);
    draw_platform_at(-2.8f, 1.9f, -9, 3, 1);
    draw_platform_at(3.2f, 2.6f, -12, 2, 2);
    draw_platform_at(0.8f, -3.4f, -7, 3, 2);
    draw_mike(0, -1.25f, 0, cover_variant == 1 ? 0.0f : cover_variant == 2 ? -0.2f : -0.35f, 1.45f, 0);
}

/* hooks for the PC test harness */
void game_debug_jump_to(int level, int st)
{
    if (st == ST_WIN) { score = 152; go_win(); return; }
    if (st == ST_TITLE) { go_title(); return; }
    plat_music(MUSIC_GAME);
    start_level(level);
}
void game_debug_force_smiley(void) { sm_state = 1; sm_delay = 0.01f; }
void game_debug_no_smiley(void) { if (sm_state == 1) sm_state = 0; }
void game_debug_set_score(int v) { score = v; }
void game_debug_set_char(int c) { player_char = c; }
void game_debug_checkpoints(int on) { opt_checkpoints = on; }
/* PC keyboard: the two players get separate keys only in the race (and its character select) */
int game_two_player_active(void) { return state == ST_RACE || state == ST_PODIUM || (state == ST_TITLE && title_mode == 5); }
void game_debug_get(float *px, float *py, float *pz, int *plat, int *st, int *lvl, int *sc)
{ *px = pl.x; *py = pl.y; *pz = pl.z; *plat = pl.ground_plat; *st = state; *lvl = lv.num; *sc = score; }
float game_debug_cam_yaw(void) { return cam_yaw; }
Level *game_debug_level(void) { return &lv; }
Player *game_debug_player(void) { return &pl; }
Player *game_debug_racer(int i, float *yaw, float *dead_t) { *yaw = racer[i].yaw; *dead_t = racer[i].dead_t; return &racer[i].p; }
int game_debug_state(void) { return state; }
