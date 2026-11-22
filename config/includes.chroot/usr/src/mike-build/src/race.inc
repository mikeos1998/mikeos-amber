/* 2 player split-screen race and the credits screen. Included by game.c (shares its statics). */

/* ======================================================================= */
/*  Race                                                                    */
/* ======================================================================= */
#define RACE_PLATS   40
#define RACE_CP      8
enum { RC_COUNT, RC_RUN, RC_DONE };

typedef struct {
    Player p;
    int cp;                 /* index of the last checkpoint pad reached (0 = start) */
    int best;               /* furthest pad stood on, for 1ST / 2ND */
    float dead_t, cp_msg_t;
    float yaw, manual_t, cx, cy, cz;
    int prev_a;
} Racer;

static Racer racer[2];
static int race_phase, race_winner;
static float race_t;
static const char *racer_name[2] = { "M.I.K.E", "A.L.E.X" };
static const uint32_t racer_col[2] = { 0x00FF1AFF, 0x57A0FFFF };   /* indexed by character: 0 = M.I.K.E, 1 = A.L.E.X */
static int rc(int player) { return player ^ race_swap; }             /* character that player 1 / 2 plays */
static void draw_char(int c, float x, float y, float z, float face, float scale, float tilt)
{
    if (c == 0) draw_mike(x, y, z, face, scale, tilt); else draw_alex(x, y, z, face, scale, tilt);
}

static int num_checkpoints(void) { int n = 0; for (int i = 0; i < lv.nplat; i++) n += lv.plat[i].checkpoint; return n; }
static int checkpoints_passed(const Racer *r) { int n = 0; for (int i = 1; i <= r->cp && i < lv.nplat; i++) n += lv.plat[i].checkpoint; return n; }

static void race_place(int i)
{
    Racer *r = &racer[i];
    Plat *c = &lv.plat[r->cp];
    memset(&r->p, 0, sizeof r->p);
    float cx = (c->x0 + c->x1) * 0.5f, cz = (c->z0 + c->z1) * 0.5f;
    float rx = -c->fz, rz = c->fx;                  /* across the course: side by side */
    float off = i == 0 ? -0.8f : 0.8f;
    r->p.x = cx + rx * off; r->p.z = cz + rz * off; r->p.y = c->y1;
    r->p.grounded = 1; r->p.ground_plat = r->cp;
    r->p.face = atan2f(c->fx, c->fz);
    r->dead_t = 0;
}

static void race_snap_camera(Racer *r)
{
    Plat *c = &lv.plat[r->cp];
    r->yaw = atan2f(-c->fx, -c->fz);
    r->cx = r->p.x + sinf(r->yaw) * 7.5f; r->cy = r->p.y + 3.6f; r->cz = r->p.z + cosf(r->yaw) * 7.5f;
    r->manual_t = 0;
}

static void race_start(void)
{
    /* wider sad face pads, slower faces; with checkpoints on: one every RACE_CP pads plus one after the first sad faces */
    level_race_options(2.4f, 0.55f, race_cp_on);
    level_build_ex(&lv, 60, 2026u, RACE_PLATS, 0.4f, race_cp_on ? RACE_CP : 0);
    level_race_options(0, 1, 0);
    build_level_meshes();
    for (int i = 0; i < 2; i++) {
        memset(&racer[i], 0, sizeof racer[i]);
        racer[i].prev_a = 1;          /* the A press that picked the menu item doesn't jump */
        race_place(i);
        race_snap_camera(&racer[i]);
    }
    race_phase = RC_COUNT; race_t = 0; race_winner = -1;
    paused = 0;
    state = ST_RACE; state_t = 0;
    plat_music(MUSIC_GAME);
}

static void race_camera(Racer *r, const Input *in, float dt)
{
    float manual = in->rtrig - in->ltrig;
    if (fabsf(manual) > 0.1f) { r->yaw += manual * 2.4f * dt; r->manual_t = 2.5f; }
    else if (r->manual_t > 0) r->manual_t -= dt;
    else {
        int next = r->p.ground_plat + 1; if (next >= lv.nplat) next = lv.nplat - 1;
        Plat *p = &lv.plat[next];
        float target = atan2f(-p->fx, -p->fz);
        r->yaw += wrap_angle(target - r->yaw) * (1.0f - expf(-1.6f * dt));
    }
    r->yaw = wrap_angle(r->yaw);
    float tx = r->p.x + sinf(r->yaw) * 7.5f, ty = r->p.y + 3.6f, tz = r->p.z + cosf(r->yaw) * 7.5f;
    float k = 1.0f - expf(-6.0f * dt);
    r->cx += (tx - r->cx) * k; r->cy += (ty - r->cy) * k; r->cz += (tz - r->cz) * k;
}

static float race_progress(const Racer *r)
{
    /* pads passed, plus how close to the next pad (so 1ST / 2ND is right mid-jump too) */
    int n = r->best + 1; if (n >= lv.nplat) return (float)r->best + 1;
    Plat *a = &lv.plat[r->best], *b = &lv.plat[n];
    float ax = (a->x0 + a->x1) * 0.5f, az = (a->z0 + a->z1) * 0.5f, bx = (b->x0 + b->x1) * 0.5f, bz = (b->z0 + b->z1) * 0.5f;
    float seg = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az)) + 0.001f;
    float d = sqrtf((bx - r->p.x) * (bx - r->p.x) + (bz - r->p.z) * (bz - r->p.z));
    float f = 1.0f - d / seg; if (f < 0) f = 0; if (f > 1) f = 1;
    return r->best + f;
}

static void go_podium(void)
{
    state = ST_PODIUM; state_t = 0;
    for (int i = 0; i < 70; i++) {
        conf[i].x = (rnd() - 0.5f) * 7; conf[i].z = (rnd() - 0.5f) * 3 - 0.5f;
        conf[i].s = 0.4f + rnd() * 0.6f; conf[i].o = rnd() * 10; conf[i].r = rnd() * 6; conf[i].c = i % 5;
    }
    plat_music(MUSIC_TITLE);
}

static void race_update(const Input *ins, int start_pressed, float dt)
{
    if (race_phase == RC_RUN && start_pressed) { pause_open(ins); return; }
    if (paused) return;
    race_t += dt;
    if (race_phase == RC_COUNT) {
        for (int i = 0; i < 2; i++) { racer[i].prev_a = ins[i].a; race_camera(&racer[i], &ins[i], dt); }
        if (race_t >= 3.0f) { race_phase = RC_RUN; race_t = 0; }
        return;
    }
    enemies_update(&lv, dt);
    for (int i = 0; i < 2; i++) {
        Racer *r = &racer[i];
        const Input *in = &ins[i];
        int a_pressed = in->a && !r->prev_a; r->prev_a = in->a;
        if (r->cp_msg_t > 0) r->cp_msg_t -= dt;
        if (race_phase == RC_DONE) {
            /* race over: everyone turns to face their camera */
            float target = atan2f(r->cx - r->p.x, r->cz - r->p.z);
            r->p.face += wrap_angle(target - r->p.face) * (1.0f - expf(-6.0f * dt));
        } else if (r->dead_t > 0) {
            r->dead_t -= dt;
            if (r->dead_t <= 0) { race_place(i); race_snap_camera(r); }
        } else {
            float fx = -sinf(r->yaw), fz = -cosf(r->yaw), rx = cosf(r->yaw), rz = -sinf(r->yaw);
            float mx = rx * in->sx + fx * -in->sy, mz = rz * in->sx + fz * -in->sy;
            float ml = sqrtf(mx * mx + mz * mz); if (ml > 1) { mx /= ml; mz /= ml; }
            r->p.jumped = 0;
            int res = player_step(&r->p, &lv, mx, mz, a_pressed, in->a, dt);
            if (r->p.jumped) plat_sfx(SFX_JUMP);
            if (r->p.grounded) {
                if (r->p.ground_plat > r->best) r->best = r->p.ground_plat;
                if (lv.plat[r->p.ground_plat].checkpoint && r->p.ground_plat > r->cp) { r->cp = r->p.ground_plat; r->cp_msg_t = 1.3f; }
            }
            if (res == SIM_DEAD) { r->dead_t = 1.0f; r->p.vx = r->p.vz = 0; plat_sfx(SFX_DEATH); }
            else if (res == SIM_FINISH) { race_winner = i; race_phase = RC_DONE; race_t = 0; r->best = lv.nplat - 1; plat_sfx(SFX_WIN); }
        }
        race_camera(r, in, dt);
    }
    if (race_phase == RC_DONE && race_t > 2.5f) go_podium();
}

/* ---------- drawing ---------- */
static void race_draw_view(int v)
{
    Racer *me = &racer[v];
    int gy = v == 0 ? 256 : 0;                  /* GL y of this half (top half = rows 0..223) */
    plat_viewport(0, gy, 640, 224);
    glEnable(GL_SCISSOR_TEST);                  /* 224 + 32 + 224: halves sit on the PVR's 32 px tiles */
    plat_scissor(0, gy, 640, 224);
    cam_x = me->cx; cam_y = me->cy; cam_z = me->cz;
    perspective(40.0f, 640.0f / 224.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, me->p.x, me->p.y + 0.9f, me->p.z);
    draw_sky(game_t * 0.6f);
    lava_tiles = 10;
    draw_lava_floor(lv.lava_y, game_t * 0.05f);
    lava_tiles = 14;
    draw_mesh(&lv_top, tex[TEX_PLAT_TOP]);
    draw_mesh(&lv_side, tex[TEX_PLAT_SIDE]);
    draw_mesh(&lv_fin_top, tex[TEX_FINISH]);
    draw_mesh(&lv_spikes, tex[TEX_WHITE]);
    draw_lava_pools(game_t * 0.08f);
    for (int i = 0; i < lv.nen; i++) {
        Enemy *e = &lv.en[i];
        float ddx = e->x - me->p.x, ddz = e->z - me->p.z;
        if (ddx * ddx + ddz * ddz > 40.0f * 40.0f) continue;
        float bounce = fabsf(sinf(e->phase * 5.0f)) * 0.25f;
        glPushMatrix();
        glTranslatef(e->x, e->y + bounce, e->z);
        glRotatef(atan2f(me->p.x - e->x, me->p.z - e->z) * 180.0f / PI, 0, 1, 0);
        draw_mesh(&ball_lo, tex[TEX_SAD]);
        glPopMatrix();
    }
    for (int i = 0; i < 2; i++) {
        Racer *r = &racer[i];
        if (r->dead_t > 0 && fmodf(r->dead_t, 0.2f) < 0.1f) continue;
        float tilt = r->dead_t > 0 ? fminf((1.0f - r->dead_t) * 200, 80) : 0;
        draw_char(rc(i), r->p.x, r->p.y, r->p.z, r->p.face, 0.65f, tilt);
    }
    for (int i = 0; i < 2; i++) draw_blob_shadow_for(&racer[i].p);
    glDisable(GL_SCISSOR_TEST);
    plat_viewport(0, 0, 640, 480);
}

static void race_draw(void)
{
    race_draw_view(0);
    race_draw_view(1);

    begin_2d();
    /* middle bar: progress track with both racers on it */
    uint8_t bar[4] = { 18, 0, 46, 255 };
    { float a[3] = { 0, 256, 0 }, b[3] = { 640, 256, 0 }, c[3] = { 640, 224, 0 }, d[3] = { 0, 224, 0 };
      m_reset(&scratch); m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, bar); }
    float x0 = 60, x1 = 580;
    uint8_t line[4] = { 120, 90, 180, 255 };
    { float a[3] = { x0, 241, 0 }, b[3] = { x1, 241, 0 }, c[3] = { x1, 239, 0 }, d[3] = { x0, 239, 0 }; m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, line); }
    for (int k = 1; k < lv.nplat; k++) if (lv.plat[k].checkpoint || k == lv.nplat - 1) {
        float x = x0 + (x1 - x0) * k / (float)(lv.nplat - 1);
        uint8_t tc[4] = { 255, 225, 70, 255 };
        float a[3] = { x - 1, 246, 0 }, b[3] = { x + 1, 246, 0 }, c[3] = { x + 1, 234, 0 }, d[3] = { x - 1, 234, 0 };
        m_quad(&scratch, a, b, c, d, 0, 0, 1, 1, tc);
    }
    draw_mesh(&scratch, tex[TEX_WHITE]);
    text_fx(8, 233, 0.9f, 0xFFFFFFFF, 0, 0, "START");
    text_fx(590, 233, 0.9f, 0xFFFFFFFF, 0, 0, "GOAL");
    for (int i = 0; i < 2; i++) {
        float f = race_progress(&racer[i]) / (float)(lv.nplat - 1); if (f > 1) f = 1;
        float x = x0 + (x1 - x0) * f;
        uint32_t c = racer_col[rc(i)];
        uint8_t mc[4] = { c >> 24, c >> 16, c >> 8, 255 };
        float y = i == 0 ? 230 : 242;
        float a[3] = { x - 5, y + 8, 0 }, b[3] = { x + 5, y + 8, 0 }, cc[3] = { x + 5, y, 0 }, d[3] = { x - 5, y, 0 };
        m_reset(&scratch); m_quad(&scratch, a, b, cc, d, 0, 0, 1, 1, mc); draw_mesh(&scratch, tex[TEX_WHITE]);
    }

    float lead0 = race_progress(&racer[0]), lead1 = race_progress(&racer[1]);
    for (int v = 0; v < 2; v++) {
        float top = v == 0 ? 0 : 256;
        Racer *r = &racer[v];
        char buf[40];
        text_fx(16, top + 10, 1.4f, racer_col[rc(v)], 0x000000FF, 0, racer_name[rc(v)]);
        if (num_checkpoints() > 0) {
            snprintf(buf, sizeof buf, "CHECKPOINT %d/%d", checkpoints_passed(r), num_checkpoints());
            text_fx(16, top + 36, 1.0f, 0xFFFFFFFF, 0x3A0070FF, 0, buf);
        }
        int first = race_phase == RC_DONE ? race_winner == v : (v == 0 ? lead0 >= lead1 : lead1 > lead0);
        const char *pos = first ? "1ST" : "2ND";
        text_fx(640 - 16 - text_width(pos, 2.0f), top + 10, 2.0f, first ? 0xFFD400FF : 0xC0C0D0FF, 0x3A0070FF, 0, pos);
        float mid = top + 112;
        if (race_phase == RC_COUNT) {
            const char *n = race_t < 1 ? "3" : race_t < 2 ? "2" : "1";
            text_fx(0, mid - 36, 5.0f, 0xFFFFFFFF, 0x3A0070FF, 1, n);
        } else if (race_phase == RC_RUN && race_t < 0.8f) {
            text_fx(0, mid - 36, 5.0f, 0x00FF1AFF, 0x3A0070FF, 1, "GO!");
        } else if (race_phase == RC_DONE) {
            text_fx(0, mid - 26, 3.5f, v == race_winner ? 0xFFD400FF : 0xFFFFFFFF, 0x3A0070FF, 1, v == race_winner ? "FINISH! 1ST" : "2ND");
        } else if (r->dead_t > 0) {
            text_fx(0, mid - 26, 3.5f, 0xFF4D5EFF, 0x000000FF, 1, "OUCH!");
        } else if (r->cp_msg_t > 0) {
            text_fx(0, mid - 20, 2.4f, 0xFFE146FF, 0x3A0070FF, 1, "CHECKPOINT!");
        }
    }
    if (paused) draw_pause_menu(240);
    end_2d();
}

/* a word in 3D, facing +Z (for the podium numbers) */
static void draw_text_3d(float cx, float y, float z, float h, uint32_t rgba, const char *s)
{
    uint8_t c[4] = { rgba >> 24, rgba >> 16, rgba >> 8, rgba };
    float sc = h / 16.0f, w = text_width(s, sc) * (1.0f / 1.0f), x = cx - w / 2;
    m_reset(&scratch);
    for (; *s; s++) {
        int ch = (unsigned char)*s; if (ch < 32 || ch > 127) ch = '?';
        int i = ch - 32;
        float u0 = ((i % 16) * 16 + 0.5f) / 256.0f, v0 = ((i / 16) * 16 + 0.5f) / 128.0f;
        float u1 = u0 + 15.0f / 256.0f, v1 = v0 + 15.0f / 128.0f;
        float p0[3] = { x, y, z }, p1[3] = { x + 16 * sc, y, z }, p2[3] = { x + 16 * sc, y + 16 * sc, z }, p3[3] = { x, y + 16 * sc, z };
        V *v = m_push(&scratch, 6);
        setv(&v[0], p0[0], p0[1], p0[2], u0, v1, c); setv(&v[1], p1[0], p1[1], p1[2], u1, v1, c); setv(&v[2], p2[0], p2[1], p2[2], u1, v0, c);
        setv(&v[3], p0[0], p0[1], p0[2], u0, v1, c); setv(&v[4], p2[0], p2[1], p2[2], u1, v0, c); setv(&v[5], p3[0], p3[1], p3[2], u0, v0, c);
        x += (font_widths[i] + 2) * sc;
    }
    glEnable(GL_BLEND);
    draw_mesh(&scratch, tex[TEX_FONT]);
    glDisable(GL_BLEND);
}

static void draw_podium(void)
{
    cam_x = 0; cam_y = 0.9f; cam_z = 7.0f;
    perspective(45.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, 0, 0.3f, 0);
    draw_sky(game_t * 1.2f);
    draw_platform_at(0, -1.35f, 0, 6, 2.6f);
    static const uint8_t gold[3] = { 255, 205, 40 }, silver[3] = { 190, 195, 215 };
    static Mesh pod;
    m_reset(&pod);
    float y0 = -1.35f;
    m_box(&pod, &pod, -1.85f, y0, -0.6f, -0.15f, y0 + 1.2f, 0.6f, gold, 0);     /* 1st */
    m_box(&pod, &pod, 0.15f, y0, -0.6f, 1.85f, y0 + 0.6f, 0.6f, silver, 0);     /* 2nd */
    draw_mesh(&pod, tex[TEX_WHITE]);
    draw_text_3d(-1.0f, y0 + 0.25f, 0.61f, 0.7f, 0x3A0070FF, "1");
    draw_text_3d(1.0f, y0 + 0.08f, 0.61f, 0.42f, 0x3A0070FF, "2");
    float t = state_t;
    int w = race_winner < 0 ? 0 : race_winner, l = 1 - w;
    float wy = y0 + 1.2f + fabsf(sinf(t * 3)) * 0.35f, ly = y0 + 0.6f;
    float face = atan2f(cam_x - 0, cam_z - 0) * 0;      /* face the camera */
    draw_char(rc(w), -1.0f, wy, 0, face, rc(w) ? 0.9f : 1.0f, 0);
    draw_char(rc(l), 1.0f, ly, 0, face - 0.2f, rc(l) ? 0.9f : 1.0f, 0);
    draw_confetti(t);

    begin_2d();
    char buf[32]; snprintf(buf, sizeof buf, "%s WINS!", racer_name[rc(w)]);
    text_fx(0, 30, 3.0f, racer_col[rc(w)], 0x3A0070FF, 1, buf);
    if (state_t > 3.0f && fmodf(game_t, 1.0f) < 0.6f) text_fx(0, 440, 1.4f, 0xFFFFFFFF, 0x3A0070FF, 1, "PRESS START");
    end_2d();
}

/* ======================================================================= */
/*  Credits (press Y on the title screen)                                   */
/* ======================================================================= */
static const struct { int head; const char *text; } credits[] = {
    { 1, "Produced by:" }, { 0, "Prosystems" }, { 0, "" },
    { 1, "Coded by:" }, { 0, "MikeOS1998" }, { 0, "" },
    { 1, "Character Designs by:" }, { 0, "MikeOS1998 and AlexOS2009" }, { 0, "" },
    { 1, "Music from:" }, { 0, "bitmidi.com, VGMusic.com" }, { 0, "" },
    { 1, "Soundfont by:" }, { 0, "Frank Wen" }, { 0, "" },
    { 1, "Beta Testers:" }, { 0, "ThatOneGamerDog" }, { 0, "Henry" }, { 0, "Bennett" }, { 0, "Chezhian" }, { 0, "AlexOS2009" }, { 0, "Siren fan 1003" }, { 0, "Nihaal" }, { 0, "" }, { 0, "" },
    { 2, "Thank you for playing!" },
};
#define NUM_CREDITS (int)(sizeof credits / sizeof credits[0])

static void go_credits(void) { state = ST_CREDITS; state_t = 0; plat_music(MUSIC_CREDITS); }

static void draw_credits(void)
{
    cam_x = 0; cam_y = 0.2f; cam_z = 6.0f;
    perspective(45.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, 0, 0.1f, 0);
    draw_sky(game_t * 1.2f);
    draw_mike(-3.4f, -2.3f + fabsf(sinf(game_t * 3)) * 0.15f, -1.5f, 0.5f, 0.75f, 0);
    draw_alex(3.4f, -2.3f + fabsf(sinf(game_t * 3 + 1.5f)) * 0.15f, -1.5f, -0.5f, 0.7f, 0);
    begin_2d();
    float y = 500 - state_t * 42.0f;
    for (int i = 0; i < NUM_CREDITS; i++) {
        float yy = y + i * 34;
        if (credits[i].head == 2) {                 /* last line stops in the middle */
            if (yy < 220) yy = 220;
            text_fx(0, yy, 2.0f, 0x00FF1AFF, 0x3A0070FF, 1, credits[i].text);
        } else if (yy > -40 && yy < 500) {
            text_fx(0, yy, credits[i].head ? 1.3f : 1.7f, credits[i].head ? 0xFFD400FF : 0xFFFFFFFF, 0x3A0070FF, 1, credits[i].text);
        }
    }
    if (state_t > 2.0f) text_fx(SCR_W - 16 - text_width(LBL_BACK, 0.9f), 12, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 0, LBL_BACK);
    end_2d();
}
