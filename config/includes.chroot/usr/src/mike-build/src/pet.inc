/* "M.I.K.E ON VMU": the virtual pet. Included by game.c (shares its statics).

   The pet itself is a VMU mini-game (src/vmupet/pet.s, built into pet_vms.h). It keeps its state in
   the last 128 bytes of its own file (PET_SAVE_OFS), laid out like this:
     +0 "MIKEPET1"  +8 character  +9 food  +10 fun  +11 clean  +12 energy  +13/14 care points
     +15/16 minutes  +17 asleep  +18 presents  +19 present wait  +20 checksum of +8..+19
   Menu (title_mode 11): send the pet away (Dreamcast: to the VMU as its mini-game; PC: its own window;
   browser: MIKE_Pet.html), bring it back for rewards, or play it right here in 3D (ST_PET).
   Rewards (title_mode 12): points added to a save file, and the pet-only skin. */

#define PET_SKIN 6                 /* skins[c][6]: SLEEPY / HEADPHONES, only from the pet */
#define PET_SKIN_CARE 40           /* care points in one trip that earn it (about 20 minutes of good care) */
#define PET_CARE_PER_POINT 6       /* every 6 care points (3 minutes of good care) = 1 point */

typedef struct { int ok, ch, food, fun, clean, energy, care, mins, asleep, gifts; } PetInfo;
typedef struct { char magic[4]; int version; uint8_t page[2][128]; uint32_t check; } HomePet;

static uint8_t pet_img[PET_VMS_SIZE];
static PetInfo pet_away;           /* the pet that's on the VMU / in its window, as last read */
static int pet_row, pet_char, pet_confirm, pet_have_away;
static char pet_msg[64]; static float pet_msg_t;
static int reward_care, reward_points, reward_skin, reward_char, reward_sel, reward_clear;
static HomePet homepet;
static Vmu pet_vm;
static int pet_care_start, pet_writes_seen, pet_prev_buttons;
static float pet_t;

static uint32_t homepet_check(const HomePet *h)
{ uint32_t s = 0x484F4D45u; for (int i = 0; i < 256; i++) s = s * 31u + h->page[i / 128][i % 128]; return s; }

static PetInfo pet_parse(const uint8_t *p)
{
    PetInfo r; memset(&r, 0, sizeof r);
    if (memcmp(p, "MIKEPET1", 8)) return r;
    uint8_t sum = 0; for (int i = 8; i < 20; i++) sum += p[i];
    if (sum != p[20]) return r;
    r.ok = 1; r.ch = p[8] & 1; r.food = p[9]; r.fun = p[10]; r.clean = p[11]; r.energy = p[12];
    r.care = p[13] | p[14] << 8; r.mins = p[15] | p[16] << 8; r.asleep = p[17]; r.gifts = p[18];
    return r;
}
static void pet_new_page(uint8_t *p, int ch)
{
    memset(p, 0, 128); memcpy(p, "MIKEPET1", 8);
    p[8] = (uint8_t)ch; p[9] = p[10] = p[11] = p[12] = 4;
    uint8_t sum = 0; for (int i = 8; i < 20; i++) sum += p[i];
    p[20] = sum;
}
static void pet_say(const char *t) { snprintf(pet_msg, sizeof pet_msg, "%s", t); pet_msg_t = 3.0f; }

static void pet_check_away(void)
{
    int n = plat_pet_fetch(pet_img, sizeof pet_img);
    pet_have_away = n >= PET_SAVE_OFS + 128;
    if (pet_have_away) pet_away = pet_parse(pet_img + PET_SAVE_OFS); else memset(&pet_away, 0, sizeof pet_away);
    if (pet_have_away && !pet_away.ok) pet_have_away = 0;
}

static void pet_menu_open(void)
{
    title_mode = 11; pet_row = 0; pet_confirm = 0; pet_char = player_char;
    pet_check_away();
    if (pet_have_away) pet_char = pet_away.ch;
}

/* rewards for care points: points, and the pet-only skin */
static void pet_rewards(int care, int ch, int clear_after)
{
    reward_care = care; reward_char = ch; reward_clear = clear_after;
    reward_points = care / PET_CARE_PER_POINT;
    reward_skin = care >= PET_SKIN_CARE;          /* goes on the file the rewards are saved to */
    read_saves();
    reward_sel = 0;
    while (reward_sel < NUM_FILES && !file_used[reward_sel]) reward_sel++;
    state = ST_TITLE; state_t = 0; title_mode = 12;
    plat_music(MUSIC_TITLE);
}

static void pet_send(void)
{
    if (pet_have_away) { pet_say(pet_char ? "A.L.E.X IS ALREADY AWAY - BRING THEM BACK FIRST" : "M.I.K.E IS ALREADY AWAY - BRING THEM BACK FIRST"); return; }
    memcpy(pet_img, pet_vms, PET_VMS_SIZE);
    pet_new_page(pet_img + PET_SAVE_OFS, pet_char);
    int r = plat_pet_send(pet_img, PET_VMS_SIZE, pet_confirm);
    if (r == PET_OTHER_GAME) {
        char t[64]; snprintf(t, sizeof t, "THE VMU HAS %s - PRESS AGAIN TO REPLACE IT", plat_pet_other_game());
        pet_say(t); pet_confirm = 1; return;
    }
    pet_confirm = 0;
    if (r == PET_NO_CARD) pet_say(PET_NO_CARD_MSG);
    else if (r == PET_FULL) pet_say("NOT ENOUGH ROOM ON THE VMU (12 BLOCKS)");
    else if (r == PET_ERROR) pet_say("COULDN'T SEND IT");
    else { pet_say(PET_SENT_MSG); pet_check_away(); }
}

static void pet_bring_back(void)
{
    pet_check_away();
    if (!pet_have_away) { pet_say("NOBODY IS AWAY RIGHT NOW"); return; }
    pet_rewards(pet_away.care, pet_away.ch, 1);
}

/* ---------- playing it here, in 3D ---------- */
static void homepet_read(void)
{
    if (plat_load(SAVE_HOMEPET, &homepet, sizeof homepet) != (int)sizeof homepet || memcmp(homepet.magic, "MKHP", 4) ||
        homepet.check != homepet_check(&homepet)) {
        memset(&homepet, 0, sizeof homepet); memcpy(homepet.magic, "MKHP", 4); homepet.version = 1;
    }
    for (int c = 0; c < 2; c++) if (!pet_parse(homepet.page[c]).ok) pet_new_page(homepet.page[c], c);
}
static void homepet_write(void)
{
    homepet.check = homepet_check(&homepet);
    plat_save(SAVE_HOMEPET, &homepet, sizeof homepet, "PET AT HOME");
}

static void pet_play_here(void)
{
    homepet_read();
    memcpy(pet_img, pet_vms, PET_VMS_SIZE);
    memcpy(pet_img + PET_SAVE_OFS, homepet.page[pet_char], 128);
#if !defined(_arch_dreamcast) && !defined(__EMSCRIPTEN__)
    pet_hint_pc(pet_img, PET_VMS_SIZE);   /* B is the X key on PC */
#endif
    vmu_load(&pet_vm, pet_img, PET_VMS_SIZE);
    pet_care_start = pet_parse(homepet.page[pet_char]).care;
    pet_writes_seen = 0; pet_prev_buttons = 0; pet_t = 0;
    skin_view[pet_char] = skin_sel[pet_char];
    state = ST_PET; state_t = 0;
    plat_music(MUSIC_GAME);
}

static void pet_here_leave(void)
{
    memcpy(homepet.page[pet_char], pet_vm.flash + PET_SAVE_OFS, 128);
    PetInfo pi = pet_parse(homepet.page[pet_char]);
    homepet_write();
    int gained = pi.ok ? pi.care - pet_care_start : 0;
    if (gained > 0) pet_rewards(gained, pet_char, 0);
    else { state = ST_TITLE; state_t = 0; plat_music(MUSIC_TITLE); pet_menu_open(); }
}

static void pet_here_update(const Input *in, int start_pressed, float dt)
{
    pet_t += dt;
    int b = 0;
    if (in->sy < -0.5f) b |= VMU_UP;
    if (in->sy > 0.5f) b |= VMU_DOWN;
    if (in->sx < -0.5f) b |= VMU_LEFT;
    if (in->sx > 0.5f) b |= VMU_RIGHT;
    if (in->a) b |= VMU_A;
    if (in->b) b |= VMU_B;
    if (start_pressed) b |= VMU_MODE;       /* START: save and leave (MODE on a real VMU) */
    if (b != pet_prev_buttons) vmu_set_buttons(&pet_vm, b);
    pet_prev_buttons = b;
    vmu_run(&pet_vm, dt);
    if (pet_vm.flash_writes != pet_writes_seen) {          /* the pet saved: keep it */
        pet_writes_seen = pet_vm.flash_writes;
        memcpy(homepet.page[pet_char], pet_vm.flash + PET_SAVE_OFS, 128);
    }
    if (pet_vm.quit) pet_here_leave();
}

static void fill_rect(float x0, float y0, float x1, float y1, uint32_t rgba)
{
    uint8_t c[4] = { (uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16), (uint8_t)(rgba >> 8), (uint8_t)rgba };
    float a[3] = { x0, y1, 0 }, b[3] = { x1, y1, 0 }, cc[3] = { x1, y0, 0 }, d[3] = { x0, y0, 0 };
    m_reset(&scratch); m_quad(&scratch, a, b, cc, d, 0, 0, 1, 1, c); draw_mesh(&scratch, tex[TEX_WHITE]);
}
static void draw_hearts(float x, float y, int n, const char *label)
{
    text_fx(x, y, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 0, label);
    for (int i = 0; i < 4; i++) {
        float hx = x + 100 + i * 24;
        if (i < n) fill_rect(hx, y, hx + 16, y + 14, 0xFF4D7AFF);
        else menu_frame(hx, y, hx + 16, y + 14, 0x5A3C88FF);
    }
}

static void draw_pet_here(void)
{
    const uint8_t *page = pet_vm.flash + PET_SAVE_OFS;
    PetInfo pi = pet_parse(page);
    int asleep = pet_vm.ram[1][0x39], mode = pet_vm.ram[1][0x11];
    cam_x = 1.9f; cam_y = 0.6f; cam_z = 5.6f;
    perspective(45.0f, 4.0f / 3.0f, 0.3f, 160.0f);
    look_at(cam_x, cam_y, cam_z, 1.9f, 0.0f, 0);
    draw_sky(game_t * 0.5f);
    draw_platform_at(0, -1.25f, 0, 3, 2);
    /* the pet mirrors the VMU: bed, food, games, a bad mood */
    float bob = 0, face = sinf(game_t * 0.7f) * 0.4f, tilt = 0;
    int sad = pi.ok && (pi.food == 0 || pi.fun == 0 || pi.clean == 0 || pi.energy == 0);
    if (asleep) { tilt = -70; face = 0.3f; }
    else if (mode == 3) bob = fabsf(sinf(game_t * 9)) * 0.12f;
    else if (mode == 4) face = (pet_vm.ram[1][0x25] == 0 ? -1.0f : pet_vm.ram[1][0x25] == 1 ? 1.0f : 0);
    else if (sad) face = sinf(game_t * 0.4f) * 0.15f;
    else bob = fabsf(sinf(game_t * 3)) * 0.2f;
    if (pet_char) draw_alex(0, -1.25f + bob, 0, face, 1.3f, tilt); else draw_mike(0, -1.25f + bob, 0, face, 1.35f, tilt);

    begin_2d();
    if (asleep) menu_panel(0, 0, SCR_W, SCR_H, 120);          /* lights down */
    text_fx(0, 18, 2.0f, 0x00FF1AFF, 0x3A0070FF, 1, pet_char ? "A.L.E.X AT HOME" : "M.I.K.E AT HOME");
    /* the VMU's own screen, big */
    float sx = 300, sy = 84, px = 6;
    menu_panel(sx - 14, sy - 14, sx + 48 * px + 14, sy + 32 * px + 14, 255);
    menu_frame(sx - 14, sy - 14, sx + 48 * px + 14, sy + 32 * px + 14, 0x7F00FFFF);
    {
        uint8_t lcd[4] = { 150, 190, 170, 255 }, ink[4] = { 30, 40, 50, 255 };
        m_reset(&scratch);
        float a[3] = { sx, sy + 32 * px, 0 }, bq[3] = { sx + 48 * px, sy + 32 * px, 0 }, c[3] = { sx + 48 * px, sy, 0 }, d[3] = { sx, sy, 0 };
        m_quad(&scratch, a, bq, c, d, 0, 0, 1, 1, lcd);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 48; x++) {
                if (!vmu_pixel(&pet_vm, x, y)) continue;
                int x1 = x; while (x1 + 1 < 48 && vmu_pixel(&pet_vm, x1 + 1, y)) x1++;
                float p0[3] = { sx + x * px, sy + (y + 1) * px, 0 }, p1[3] = { sx + (x1 + 1) * px, sy + (y + 1) * px, 0 };
                float p2[3] = { sx + (x1 + 1) * px, sy + y * px, 0 }, p3[3] = { sx + x * px, sy + y * px, 0 };
                m_quad(&scratch, p0, p1, p2, p3, 0, 0, 1, 1, ink);
                x = x1;
            }
        draw_mesh(&scratch, tex[TEX_WHITE]);
    }
    if (pi.ok) {
        menu_panel(sx - 14, 300, sx + 48 * px + 14, 420, 200);
        draw_hearts(sx, 308, pi.food, "FOOD");
        draw_hearts(sx, 330, pi.fun, "FUN");
        draw_hearts(sx, 352, pi.clean, "CLEAN");
        draw_hearts(sx, 374, pi.energy, "ENERGY");
        char t[48]; snprintf(t, sizeof t, "CARE %d  (+%d)", pi.care, pi.care - pet_care_start > 0 ? pi.care - pet_care_start : 0);
        text_fx(sx, 398, 1.0f, 0xFFD400FF, 0x3A0070FF, 0, t);
    }
    text_fx(0, 444, 0.8f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_PET_HERE);
    end_2d();
}

/* ---------- menus ---------- */
static void draw_pet_menu(void)
{
    int blink = fmodf(game_t, 0.8f) < 0.55f;
    menu_panel(40, 14, 600, 466, 215);
    menu_frame(40, 14, 600, 466, 0x7F00FFFF);
    if (title_mode == 12) {    /* rewards */
        text_fx(0, 34, 2.0f, 0x00FF1AFF, 0x3A0070FF, 1, reward_char ? "A.L.E.X IS HOME!" : "M.I.K.E IS HOME!");
        char t[64];
        snprintf(t, sizeof t, "CARE POINTS: %d", reward_care);
        text_fx(0, 92, 1.4f, 0xFFFFFFFF, 0x3A0070FF, 1, t);
        snprintf(t, sizeof t, "+%d POINTS", reward_points);
        text_fx(0, 124, 1.8f, 0xFFD400FF, 0x3A0070FF, 1, t);
        uint32_t pbit = 1u << (reward_char * 8 + PET_SKIN);
        if (reward_skin) { snprintf(t, sizeof t, "+ SKIN: %s %s!", reward_char ? "A.L.E.X" : "M.I.K.E", skins[reward_char][PET_SKIN].name); text_fx(0, 164, 1.3f, 0xFF8AE0FF, 0x3A0070FF, 1, t); }
        else { snprintf(t, sizeof t, "%d CARE POINTS IN ONE GO EARNS A SKIN", PET_SKIN_CARE); text_fx(0, 164, 0.9f, 0xB8A8E0FF, 0x3A0070FF, 1, t); }
        if (reward_points > 0 || reward_skin) {
            text_fx(0, 210, 1.2f, 0xE0C8FFFF, 0x3A0070FF, 1, reward_skin ? "SAVE THE REWARDS TO WHICH FILE?" : "ADD THE POINTS TO WHICH FILE?");
            for (int i = 0; i <= NUM_FILES; i++) {
                if (i < NUM_FILES) {
                    if (file_used[i] && reward_skin && (files[i].skins & pbit)) snprintf(t, sizeof t, "FILE %d  (SCORE %d, HAS THE SKIN)", i + 1, files[i].score);
                    else if (file_used[i]) snprintf(t, sizeof t, "FILE %d  (SCORE %d)", i + 1, files[i].score);
                    else snprintf(t, sizeof t, "FILE %d  EMPTY", i + 1);
                } else snprintf(t, sizeof t, reward_skin ? "DON'T SAVE THEM" : "DON'T ADD THEM");
                float y = 242 + i * 30;
                int ok = i == NUM_FILES || file_used[i];
                text_fx(0, y, 1.3f, i == reward_sel ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : ok ? 0xB8A8E0FF : 0x5A4488FF, 0x3A0070FF, 1, t);
                if (i == reward_sel) text_fx(SCR_W / 2 - text_width(t, 1.3f) / 2 - 26, y, 1.3f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
            }
        } else text_fx(0, 250, 1.1f, 0xB8A8E0FF, 0x3A0070FF, 1, "KEEP THEM HAPPY LONGER FOR POINTS");
        if (blink) text_fx(0, 420, 1.2f, 0xFFD400FF, 0x3A0070FF, 1, LBL_READY);
        return;
    }
    text_fx(0, 28, 2.2f, 0x00FF1AFF, 0x3A0070FF, 1, "M.I.K.E ON VMU");
    text_fx(0, 76, 0.85f, 0xE0C8FFFF, 0x3A0070FF, 1, "TAKE GOOD CARE OF THEM TO EARN CARE POINTS");
    char t[64];
    snprintf(t, sizeof t, "CHARACTER   < %s >", pet_char ? "A.L.E.X" : "M.I.K.E");
    const char *rows[4] = { t, PET_SEND_LABEL, "BRING BACK HOME", PET_HERE_LABEL };
    for (int i = 0; i < 4; i++) {
        float y = 120 + i * 40;
        uint32_t c = i == pet_row ? (blink ? 0xFFD400FF : 0xFFFFFFFF) : 0xB8A8E0FF;
        if (i == 2 && !pet_have_away) c = i == pet_row ? c : 0x5A4488FF;
        text_fx(0, y, 1.4f, c, 0x3A0070FF, 1, rows[i]);
        if (i == pet_row) text_fx(SCR_W / 2 - text_width(rows[i], 1.4f) / 2 - 26, y, 1.4f, 0x00FF1AFF, 0x3A0070FF, 0, ">");
    }
    if (pet_have_away) {
        snprintf(t, sizeof t, "%s IS %s", pet_away.ch ? "A.L.E.X" : "M.I.K.E", PET_AWAY_WHERE);
        text_fx(0, 300, 1.1f, 0xFFD400FF, 0x3A0070FF, 1, t);
        snprintf(t, sizeof t, "CARE %d   MINUTES %d", pet_away.care, pet_away.mins);
        text_fx(0, 322, 1.0f, 0xE0C8FFFF, 0x3A0070FF, 1, t);
    } else text_fx(0, 300, 1.0f, 0xB8A8E0FF, 0x3A0070FF, 1, "NOBODY IS AWAY RIGHT NOW");
    if (pet_msg_t > 0) text_fx(0, 370, 1.0f, 0xFFFFFFFF, 0x7F00FFFF, 1, pet_msg);
    text_fx(0, 440, 0.8f, 0xE0C8FFFF, 0x3A0070FF, 1, LBL_PET_MENU);
}

static void pet_menu_update(int up, int down, int left, int right, int ok, int back, float dt)
{
    if (pet_msg_t > 0) pet_msg_t -= dt;
    if (title_mode == 12) {
        if (reward_points > 0 || reward_skin) {
            if (up) do reward_sel = (reward_sel + NUM_FILES) % (NUM_FILES + 1); while (reward_sel < NUM_FILES && !file_used[reward_sel]);
            if (down) do reward_sel = (reward_sel + 1) % (NUM_FILES + 1); while (reward_sel < NUM_FILES && !file_used[reward_sel]);
        }
        if (ok && state_t > 0.6f) {
            if ((reward_points > 0 || reward_skin) && reward_sel < NUM_FILES && file_used[reward_sel]) {
                cur_file = reward_sel;
                falls = files[cur_file].falls;
                files[cur_file].score += reward_points;
                if (reward_skin) files[cur_file].skins |= 1u << (reward_char * 8 + PET_SKIN);     /* the skin goes with the points */
                write_save(files[cur_file].level, files[cur_file].score);
            }
            if (reward_clear) plat_pet_clear();
            pet_menu_open();
            pet_say(reward_clear ? "WELCOME HOME!" : "SEE YOU NEXT TIME!");
        }
        return;
    }
    if (up && pet_row > 0) { pet_row--; pet_confirm = 0; }
    if (down && pet_row < 3) { pet_row++; pet_confirm = 0; }
    if ((left || right) && pet_row == 0) {
        if (pet_have_away) pet_say("BRING THEM BACK BEFORE SWITCHING");
        else pet_char ^= 1;
    }
    if (back) { title_mode = 3; return; }
    if (!ok) return;
    if (pet_row == 1) pet_send();
    else if (pet_row == 2) pet_bring_back();
    else if (pet_row == 3) pet_play_here();
}
