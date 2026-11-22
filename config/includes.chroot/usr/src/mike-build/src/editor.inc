/* Level editor and "MY LEVELS". Included by game.c (shares its statics).

   A level is a grid of ED_W x ED_H squares (level.h). Each square is empty or one piece at a height 0..4.
   Up to MY_SLOTS levels are kept in one save (SAVE_MYLEVELS); on the Dreamcast a level can be sent
   to another VMU plugged into the console. */

#define MY_SLOTS 5
typedef struct {
    char magic[4]; int version;
    uint8_t used[MY_SLOTS], pad[3];
    uint8_t grid[MY_SLOTS][ED_W * ED_H];
    uint32_t check;
} MyLevels;
static MyLevels mylv;
static int mylv_sel, mylv_opt, mylv_card = 1, mylv_confirm;
static int ed_cx = ED_W / 2, ed_cy, ed_tool = T_PAD, ed_h, ed_menu, ed_menu_sel, ed_dirty, ed_valid;
static float ed_rep_t, ed_msg_t, ed_cam_x, ed_cam_y, ed_cam_z;
static float ed_prev_sx, ed_prev_sy, ed_prev_lt, ed_prev_rt;
static char ed_msg[48];

static const char *tile_names[T_COUNT] = { "EMPTY", "PAD", "SPIKES", "LAVA", "SAD FACE", "CHECKPOINT", "START", "FINISH", "TURN LEFT", "TURN RIGHT" };
static const uint32_t tile_cols[T_COUNT] = { 0, 0x2EC93AFF, 0xD8D8E0FF, 0xFF6A00FF, 0x3B5BDBFF, 0xFFE146FF, 0x00FF1AFF, 0xFF9A00FF, 0x5AD8FFFF, 0x5AD8FFFF };

static uint32_t mylv_check(const MyLevels *m)
{
    uint32_t h = 0x4D4B4C56u;
    const uint8_t *p = (const uint8_t *)m->used;
    for (int i = 0; i < (int)(sizeof m->used + sizeof m->pad + sizeof m->grid); i++) h = h * 31u + p[i];
    return h;
}
static void mylv_blank(MyLevels *m) { memset(m, 0, sizeof *m); memcpy(m->magic, "MKLV", 4); m->version = 1; }
static int mylv_read(int card, MyLevels *m)
{
    if (plat_load_card(card, SAVE_MYLEVELS, m, sizeof *m) == (int)sizeof *m && !memcmp(m->magic, "MKLV", 4) &&
        m->version == 1 && m->check == mylv_check(m)) return 1;
    mylv_blank(m);
    return 0;
}
static int mylv_write(int card, MyLevels *m)
{
    m->check = mylv_check(m);
    return plat_save_card(card, SAVE_MYLEVELS, m, sizeof *m, "LEVELS MADE IN THE EDITOR");
}
static int grid_pieces(const uint8_t *g) { int n = 0; for (int i = 0; i < ED_W * ED_H; i++) n += TILE_TYPE(g[i]) != T_EMPTY; return n; }
static void grid_default(uint8_t *g)
{   /* a short straight course to start from: start, three pads, finish */
    memset(g, 0, ED_W * ED_H);
    int c = ED_W / 2;
    g[0 * ED_W + c] = TILE(T_START, 0);
    g[2 * ED_W + c] = TILE(T_PAD, 0);
    g[4 * ED_W + c] = TILE(T_PAD, 1);
    g[6 * ED_W + c] = TILE(T_PAD, 1);
    g[8 * ED_W + c] = TILE(T_FINISH, 0);
}

static void ed_say(const char *t) { snprintf(ed_msg, sizeof ed_msg, "%s", t); ed_msg_t = 2.2f; }
static void ed_rebuild(void) { ed_valid = level_from_grid(&lv, ed_grid); build_level_meshes(); }

static void editor_open(int slot)
{
    ed_slot = slot;
    if (mylv.used[slot]) memcpy(ed_grid, mylv.grid[slot], sizeof ed_grid); else grid_default(ed_grid);
    ed_cx = ED_W / 2; ed_cy = 0; ed_h = 0; ed_tool = T_PAD; ed_menu = 0; ed_dirty = !mylv.used[slot];
    state = ST_EDIT; state_t = 0; paused = 0; ed_testing = 0;
    ed_rebuild();
    float x = (ed_cx - (ED_W - 1) * 0.5f) * ED_CELL, z = -ed_cy * ED_CELL;
    ed_cam_x = x; ed_cam_y = 9; ed_cam_z = z + 8;
    plat_music(MUSIC_GAME);
}

static void mylevels_open(void)
{
    read_saves();
    mylv_read(0, &mylv);
    mylv_opt = 0; mylv_confirm = 0;
    title_mode = 9;
}

/* play the level in ed_grid; from the editor (test play) or from the MY LEVELS list */
static void custom_play(int testing)
{
    if (!level_from_grid(&lv, ed_grid)) { ed_say("NEEDS A START AND A FINISH"); return; }
    game_mode = 2; ed_testing = testing; score = 0; cp_plat = 0;
    if (!testing) plat_music(MUSIC_GAME);
    start_attempt();
    snprintf(msg, sizeof msg, testing ? "TEST PLAY" : "MY LEVEL %d", ed_slot + 1); msg_t = 1.6f;
}

static void editor_back(void)    /* from a test play back to editing */
{
    state = ST_EDIT; state_t = 0; paused = 0; ed_menu = 0;
    ed_rebuild();
}

static void editor_save(void)
{
    if (!plat_storage_present()) { ed_say(STORAGE_MISSING_MSG); return; }
    memcpy(mylv.grid[ed_slot], ed_grid, sizeof ed_grid); mylv.used[ed_slot] = 1;
    int r = mylv_write(0, &mylv);
    if (r == SAVE_OK) { ed_dirty = 0; ed_say("SAVED"); }
    else ed_say(r == SAVE_FULL ? "NOT ENOUGH ROOM TO SAVE" : "SAVE FAILED");
}

/* ---------- editor input ---------- */
static void editor_update(const Input *in, int a_pressed, int b_pressed, int y_pressed, int start_pressed, float dt)
{
    if (ed_msg_t > 0) ed_msg_t -= dt;
    enemies_update(&lv, dt);
    int up = in->sy < -0.5f && ed_prev_sy >= -0.5f, down = in->sy > 0.5f && ed_prev_sy <= 0.5f;
    int left = in->sx < -0.5f && ed_prev_sx >= -0.5f, right = in->sx > 0.5f && ed_prev_sx <= 0.5f;
    /* holding a direction repeats */
    if (fabsf(in->sx) > 0.5f || fabsf(in->sy) > 0.5f) {
        ed_rep_t += dt;
        if (ed_rep_t > 0.32f) {
            ed_rep_t -= 0.09f;
            up |= in->sy < -0.5f; down |= in->sy > 0.5f;
            left |= in->sx < -0.5f; right |= in->sx > 0.5f;
        }
    } else ed_rep_t = 0;
    int lt = in->ltrig > 0.5f && ed_prev_lt <= 0.5f, rt = in->rtrig > 0.5f && ed_prev_rt <= 0.5f;
    ed_prev_sx = in->sx; ed_prev_sy = in->sy; ed_prev_lt = in->ltrig; ed_prev_rt = in->rtrig;

    if (ed_menu) {
        if (up && ed_menu_sel > 0) ed_menu_sel--;
        if (down && ed_menu_sel < 4) ed_menu_sel++;
        if (b_pressed || start_pressed) { ed_menu = 0; return; }
        if (a_pressed) {
            switch (ed_menu_sel) {
            case 0: ed_menu = 0; break;
            case 1: ed_menu = 0; custom_play(1); break;
            case 2: editor_save(); ed_menu = 0; break;
            case 3: memset(ed_grid, 0, sizeof ed_grid); ed_grid[ED_W / 2] = TILE(T_START, 0);      /* keep just a start */
                    ed_dirty = 1; ed_rebuild(); ed_menu = 0; ed_say("CLEARED"); break;
            case 4: go_title(); mylevels_open(); break;
            }
        }
        return;
    }
    if (start_pressed) { ed_menu = 1; ed_menu_sel = 0; return; }
    if (up && ed_cy < ED_H - 1) ed_cy++;
    if (down && ed_cy > 0) ed_cy--;
    if (left && ed_cx > 0) ed_cx--;
    if (right && ed_cx < ED_W - 1) ed_cx++;
    uint8_t *cell = &ed_grid[ed_cy * ED_W + ed_cx];
    if (y_pressed) ed_tool = ed_tool % (T_COUNT - 1) + 1;           /* PAD .. TURN SCREEN RIGHT */
    if (lt || rt) {
        ed_h += rt - lt; if (ed_h < 0) ed_h = 0; if (ed_h > ED_MAXH) ed_h = ED_MAXH;
        if (TILE_TYPE(*cell) != T_EMPTY) { *cell = TILE(TILE_TYPE(*cell), ed_h); ed_dirty = 1; ed_rebuild(); }
    }
    if (a_pressed) {
        if (TILE_TYPE(*cell) == T_EMPTY && grid_pieces(ed_grid) >= MAX_PLATS) ed_say("NO ROOM FOR MORE PIECES");
        else {
            if (ed_tool == T_START)        /* only one start */
                for (int i = 0; i < ED_W * ED_H; i++) if (TILE_TYPE(ed_grid[i]) == T_START) ed_grid[i] = 0;
            *cell = TILE(ed_tool, ed_h); ed_dirty = 1; ed_rebuild();
        }
    }
    if (b_pressed && TILE_TYPE(*cell) != T_EMPTY) { *cell = 0; ed_dirty = 1; ed_rebuild(); }
    /* the camera eases after the cursor */
    float x = (ed_cx - (ED_W - 1) * 0.5f) * ED_CELL, z = -ed_cy * ED_CELL;
    float k = 1.0f - expf(-6.0f * dt);
    ed_cam_x += (x - ed_cam_x) * k; ed_cam_y += (ed_h * ED_STEP + 9 - ed_cam_y) * k; ed_cam_z += (z + 8 - ed_cam_z) * k;
}

/* ---------- editor drawing ---------- */
static void ed_line(Mesh *m, float x0, float y0, float z0, float x1, float y1, float z1, float w, const uint8_t c[4])
{   /* flat strip on the ground plane from (x0,z0) to (x1,z1) */
    float dx = x1 - x0, dz = z1 - z0, l = sqrtf(dx * dx + dz * dz); if (l < 1e-4f) return;
    float nx = -dz / l * w, nz = dx / l * w;
    float a[3] = { x0 - nx, y0, z0 - nz }, b[3] = { x1 - nx, y1, z1 - nz }, cc[3] = { x1 + nx, y1, z1 + nz }, d[3] = { x0 + nx, y0, z0 + nz };
    m_quad(m, a, b, cc, d, 0, 0, 1, 1, c);
    m_quad(m, d, cc, b, a, 0, 0, 1, 1, c);
}

static void draw_editor(void)
{
    float x = (ed_cx - (ED_W - 1) * 0.5f) * ED_CELL, z = -ed_cy * ED_CELL;
    uint8_t cur = ed_grid[ed_cy * ED_W + ed_cx];
    float top = (TILE_TYPE(cur) != T_EMPTY ? TILE_H(cur) : ed_h) * ED_STEP;
    perspective(55.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    cam_x = ed_cam_x; cam_y = ed_cam_y; cam_z = ed_cam_z;
    look_at(ed_cam_x, ed_cam_y, ed_cam_z, ed_cam_x, ed_cam_y - 9 + 0.5f, ed_cam_z - 8);
    draw_sky(game_t * 0.6f);
    draw_lava_floor(lv.lava_y, game_t * 0.05f);
    draw_mesh(&lv_top, tex[TEX_PLAT_TOP]);
    draw_mesh(&lv_side, tex[TEX_PLAT_SIDE]);
    draw_mesh(&lv_fin_top, tex[TEX_FINISH]);
    draw_mesh(&lv_spikes, tex[TEX_WHITE]);
    draw_mesh(&lv_turn, tex[TEX_WHITE]);
    draw_mesh(&lv_turn_arrows, tex[TEX_WHITE]);   /* only the editor shows which way they turn */
    draw_lava_pools(game_t * 0.08f);
    for (int i = 0; i < lv.nen; i++) {
        Enemy *e = &lv.en[i];
        draw_ball(e->x, e->y + fabsf(sinf(e->phase * 5.0f)) * 0.25f, e->z, 0, TEX_SAD, 1.0f);
    }
    /* M.I.K.E / A.L.E.X standing on the start, so you can see which way is forward */
    for (int i = 0; i < lv.nplat; i++) if (lv.plat[i].kind == PLAT_START) {
        const Plat *p = &lv.plat[i];
        draw_hero((p->x0 + p->x1) * 0.5f, p->y1, (p->z0 + p->z1) * 0.5f, PI, 0.5f, 0);
    }
    /* grid: faint lines under the pieces, and the cursor */
    m_reset(&scratch);
    uint8_t gc[4] = { 200, 170, 255, 255 };
    float x0 = -(ED_W * 0.5f) * ED_CELL, x1 = (ED_W * 0.5f) * ED_CELL, zn = ED_CELL * 0.5f, zf = zn - ED_H * ED_CELL, gy = -0.35f;
    for (int c = 0; c <= ED_W; c++) { float gx = x0 + c * ED_CELL; ed_line(&scratch, gx, gy, zn, gx, gy, zf, 0.03f, gc); }
    for (int r = 0; r <= ED_H; r++) { float gz = zn - r * ED_CELL; ed_line(&scratch, x0, gy, gz, x1, gy, gz, 0.03f, gc); }
    draw_mesh(&scratch, tex[TEX_WHITE]);
    m_reset(&scratch);
    int blink = fmodf(game_t, 0.6f) < 0.4f;
    uint8_t cc[4] = { 255, 212, 0, 255 }, cw[4] = { 255, 255, 255, 255 };
    const uint8_t *col = blink ? cc : cw;
    float h = ED_CELL * 0.5f, y = top + 0.03f;
    ed_line(&scratch, x - h, y, z - h, x + h, y, z - h, 0.07f, col);
    ed_line(&scratch, x + h, y, z - h, x + h, y, z + h, 0.07f, col);
    ed_line(&scratch, x + h, y, z + h, x - h, y, z + h, 0.07f, col);
    ed_line(&scratch, x - h, y, z + h, x - h, y, z - h, 0.07f, col);
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)      /* posts down to the grid */
        m_rod(&scratch, x + sx * h, gy, z + sz * h, x + sx * h, y, z + sz * h, 0.04f, col);
    glDisable(GL_DEPTH_TEST);
    draw_mesh(&scratch, tex[TEX_WHITE]);
    glEnable(GL_DEPTH_TEST);

    begin_2d();
    char t[64];
    menu_panel(0, 0, SCR_W, 52, 190);
    snprintf(t, sizeof t, "MY LEVEL %d%s", ed_slot + 1, ed_dirty ? " *" : "");
    text_fx(16, 10, 1.4f, 0x00FF1AFF, 0x3A0070FF, 0, t);
    snprintf(t, sizeof t, "PIECES %d/%d", grid_pieces(ed_grid), MAX_PLATS);
    text_fx(SCR_W - 16 - text_width(t, 1.0f), 8, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 0, t);
    snprintf(t, sizeof t, "ROW %d/%d", ed_cy + 1, ED_H);
    text_fx(SCR_W - 16 - text_width(t, 1.0f), 28, 1.0f, 0xB8A8E0FF, 0x3A0070FF, 0, t);
    menu_panel(0, 400, SCR_W, 480, 190);
    uint32_t tc = tile_cols[ed_tool];
    text_fx(16, 410, 1.0f, 0xB8A8E0FF, 0x3A0070FF, 0, "PIECE");
    text_fx(80, 408, 1.4f, tc, 0x000000FF, 0, tile_names[ed_tool]);
    snprintf(t, sizeof t, "HEIGHT %d", ed_h);
    text_fx(SCR_W - 16 - text_width(t, 1.4f), 408, 1.4f, 0xFFFFFFFF, 0x3A0070FF, 0, t);
    if (TILE_TYPE(cur) != T_EMPTY) {
        snprintf(t, sizeof t, "HERE: %s (HEIGHT %d)", tile_names[TILE_TYPE(cur)], TILE_H(cur));
        text_fx(0, 434, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, t);
    } else if (!ed_valid) text_fx(0, 434, 0.9f, 0xFF8A9AFF, 0x3A0070FF, 1, "A LEVEL NEEDS A START AND A FINISH");
    text_fx(0, 456, 0.8f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_EDIT);
    if (ed_msg_t > 0) text_fx(0, 200, 1.8f, 0xFFFFFFFF, 0x7F00FFFF, 1, ed_msg);
    if (ed_menu) {
        menu_panel(170, 120, 470, 330, 240);
        menu_frame(170, 120, 470, 330, 0x7F00FFFF);
        text_fx(0, 134, 1.8f, 0x00FF1AFF, 0x3A0070FF, 1, "EDITOR");
        const char *rows[5] = { "KEEP EDITING", "TEST PLAY", "SAVE", "CLEAR ALL", ed_dirty ? "EXIT (NOT SAVED)" : "EXIT" };
        for (int i = 0; i < 5; i++) {
            uint32_t c = i == ed_menu_sel ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF;
            text_fx(0, 176 + i * 28, 1.3f, c, 0x3A0070FF, 1, rows[i]);
            if (i == ed_menu_sel) text_fx(SCR_W / 2 - text_width(rows[i], 1.3f) / 2 - 26, 176 + i * 28, 1.3f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
        }
    }
    end_2d();
}

/* ---------- MY LEVELS list (title_mode 9) and what to do with one (title_mode 10) ---------- */
static int mylv_options(const char **rows)
{
    int n = 0, cards = plat_card_count();
    rows[n++] = "PLAY"; rows[n++] = "EDIT";
    if (cards > 1) rows[n++] = "SEND TO ANOTHER VMU";
    rows[n++] = mylv_confirm ? "DELETE - SURE? PRESS AGAIN" : "DELETE";
    return n;
}
static void draw_mylevels_menu(void)
{
    int blink = fmodf(game_t, 0.8f) < 0.55f;
    menu_panel(60, 14, 580, 466, 215);
    menu_frame(60, 14, 580, 466, 0x7F00FFFF);
    text_fx(0, 28, 2.0f, 0x00FF1AFF, 0x3A0070FF, 1, "MY LEVELS");
    text_fx(0, 74, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 1, plat_storage_present() ? "BUILD YOUR OWN COURSE" : STORAGE_MISSING_MSG);
    for (int i = 0; i < MY_SLOTS; i++) {
        char t[48];
        if (mylv.used[i]) {
            uint8_t *g = mylv.grid[i]; Level tmp; int ok = level_from_grid(&tmp, g);
            snprintf(t, sizeof t, "LEVEL %d - %d PIECES%s", i + 1, grid_pieces(g), ok ? "" : " (UNFINISHED)");
        } else snprintf(t, sizeof t, "LEVEL %d - NEW", i + 1);
        float y = 110 + i * 34;
        int sel = i == mylv_sel;
        text_fx(0, y, 1.3f, sel ? (title_mode == 9 && blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF, 0x3A0070FF, 1, t);
        if (sel) text_fx(SCR_W / 2 - text_width(t, 1.3f) / 2 - 26, y, 1.3f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
    }
    if (title_mode == 10) {
        const char *rows[4]; int n = mylv_options(rows);
        menu_panel(150, 286, 490, 300 + n * 28 + 14, 245);
        menu_frame(150, 286, 490, 300 + n * 28 + 14, 0x00FF1AFF);
        for (int i = 0; i < n; i++) {
            char t[48];
            if (!strcmp(rows[i], "SEND TO ANOTHER VMU")) snprintf(t, sizeof t, "SEND TO %s  < >", plat_card_name(mylv_card));
            else snprintf(t, sizeof t, "%s", rows[i]);
            uint32_t c = i == mylv_opt ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF;
            text_fx(0, 300 + i * 28, 1.2f, c, 0x3A0070FF, 1, t);
        }
    }
    if (ed_msg_t > 0) text_fx(0, 420, 1.2f, 0xFFD400FF, 0x3A0070FF, 1, ed_msg);
    text_fx(0, 446, 0.9f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_MYLV);
}

static void mylevels_update(int up, int down, int left, int right, int ok, int back, float dt)
{
    if (ed_msg_t > 0) ed_msg_t -= dt;
    if (title_mode == 9) {
        if (up) mylv_sel = (mylv_sel + MY_SLOTS - 1) % MY_SLOTS;
        if (down) mylv_sel = (mylv_sel + 1) % MY_SLOTS;
        if (back) title_mode = 6;
        else if (ok) {
            if (!mylv.used[mylv_sel]) editor_open(mylv_sel);
            else { title_mode = 10; mylv_opt = 0; mylv_confirm = 0; mylv_card = 1; }
        }
        return;
    }
    const char *rows[4]; int n = mylv_options(rows);
    if (up && mylv_opt > 0) { mylv_opt--; mylv_confirm = 0; }
    if (down && mylv_opt < n - 1) { mylv_opt++; mylv_confirm = 0; }
    int cards = plat_card_count();
    if ((left || right) && !strcmp(rows[mylv_opt], "SEND TO ANOTHER VMU") && cards > 2) {
        mylv_card += right ? 1 : -1;
        if (mylv_card < 1) mylv_card = cards - 1;
        if (mylv_card > cards - 1) mylv_card = 1;
    }
    if (back) { title_mode = 9; return; }
    if (!ok) return;
    const char *r = rows[mylv_opt];
    memcpy(ed_grid, mylv.grid[mylv_sel], sizeof ed_grid); ed_slot = mylv_sel;
    if (!strcmp(r, "PLAY")) {
        if (!level_from_grid(&lv, ed_grid)) ed_say("NEEDS A START AND A FINISH");
        else custom_play(0);
    } else if (!strcmp(r, "EDIT")) editor_open(mylv_sel);
    else if (!strcmp(r, "SEND TO ANOTHER VMU")) {
        MyLevels other;
        mylv_read(mylv_card, &other);
        int slot = -1;
        for (int i = 0; i < MY_SLOTS && slot < 0; i++)       /* the same level already there? overwrite it */
            if (other.used[i] && !memcmp(other.grid[i], ed_grid, sizeof ed_grid)) slot = i;
        for (int i = 0; i < MY_SLOTS && slot < 0; i++) if (!other.used[i]) slot = i;
        if (slot < 0) { ed_say("THAT VMU HAS NO FREE LEVEL SLOTS"); return; }
        memcpy(other.grid[slot], ed_grid, sizeof ed_grid); other.used[slot] = 1;
        int res = mylv_write(mylv_card, &other);
        char t[48];
        if (res == SAVE_OK) snprintf(t, sizeof t, "SENT TO %s AS LEVEL %d", plat_card_name(mylv_card), slot + 1);
        else snprintf(t, sizeof t, res == SAVE_FULL ? "THAT VMU IS FULL" : "COULDN'T SEND IT");
        ed_say(t);
    } else {     /* delete, after a second press */
        if (!mylv_confirm) { mylv_confirm = 1; return; }
        mylv.used[mylv_sel] = 0; memset(mylv.grid[mylv_sel], 0, sizeof mylv.grid[0]);
        mylv_write(0, &mylv); ed_say("DELETED"); title_mode = 9; mylv_confirm = 0;
    }
}
