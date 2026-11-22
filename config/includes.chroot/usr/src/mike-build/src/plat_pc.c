/* PC platform layer for M.I.K.E (Windows and Linux): SDL2 + OpenGL.
 *
 * - The game is drawn at the window's size, letterboxed to the Dreamcast's 4:3 screen.
 * - Music: TITLE.WAV / JUMP.WAV from the data folder, looped.
 * - Saves: 3 save files (stand in for the VMU) in %APPDATA%\Prosystems\MIKE on Windows,
 *   ~/.local/share/Prosystems/MIKE on Linux.
 * - Controls: keyboard for both players, plus up to two game controllers.
 *
 *   1 player (and menus): arrows or W A S D move, Space = A (jump), X / Backspace = B (back),
 *                         C = Y (credits), Enter = Start, Q / E turn the camera
 *   2 player race:        player 1: W A S D, Z = A, X = B, Tab = Start, Q / E camera
 *                         player 2: arrows, Space = A, Backspace = B, Enter = Start, , and . camera
 *   Controllers:          1st controller = player 1, 2nd = player 2 (A, B, Y, Start, sticks,
 *                         d-pad, triggers)
 *   F11 or Alt+Enter:  fullscreen on/off. Esc leaves fullscreen.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>        /* must come before GL/gl.h on Windows */
#endif
#include <SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "platform.h"
#include "assets.h"
#include "vmu_emu.h"
#include "pet_vms.h"          /* for pet_hint_pc: the pet says "X: MENU" on PC */
#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef GL_MULTISAMPLE      /* Windows' gl.h stops at OpenGL 1.1 */
#define GL_MULTISAMPLE 0x809D
#endif

#ifndef DATADIR
#define DATADIR "/usr/local/share/mike"
#endif

void game_init(void);
void game_update(const Input *ins, float dt);
void game_draw(void);
int game_two_player_active(void);

static SDL_Window *win;
static int fullscreen;

/* ---------- video: 640x480 game space, scaled and letterboxed to the window ---------- */
static float view_sx = 1, view_sy = 1;
static int view_ox, view_oy;

void plat_viewport(int x, int y, int w, int h)
{
    glViewport(view_ox + (int)(x * view_sx), view_oy + (int)(y * view_sy), (int)(w * view_sx + 0.5f), (int)(h * view_sy + 0.5f));
}

void plat_scissor(int x, int y, int w, int h)
{
    glScissor(view_ox + (int)(x * view_sx), view_oy + (int)(y * view_sy), (int)(w * view_sx + 0.5f), (int)(h * view_sy + 0.5f));
}

void plat_begin_frame(void)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    int vw = w, vh = w * 3 / 4;                      /* 4:3 box as large as fits */
    if (vh > h) { vh = h; vw = h * 4 / 3; }
    view_ox = (w - vw) / 2; view_oy = (h - vh) / 2;
    view_sx = vw / 640.0f; view_sy = vh / 480.0f;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    plat_viewport(0, 0, 640, 480);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void plat_end_frame(void) { SDL_GL_SwapWindow(win); }

void plat_upload_textures(unsigned int *ids)
{
    glGenTextures(TEX_COUNT, ids);
    for (int i = 0; i < TEX_COUNT; i++) {
        glBindTexture(GL_TEXTURE_2D, ids[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_info[i].w, tex_info[i].h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_info[i].data);
        GLint f = tex_info[i].nearest ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

static void set_fullscreen(int on)
{
    fullscreen = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

/* ---------- data folder (music) ---------- */
static char data_dir[1024];

static int file_exists(const char *path) { FILE *f = fopen(path, "rb"); if (f) fclose(f); return f != NULL; }

static void find_data_dir(void)
{
    const char *env = getenv("MIKE_DATA");
    char *base = SDL_GetBasePath();                  /* folder the program is in */
    const char *tries[4] = { env, NULL, "data/", DATADIR "/" };
    char here[1024] = "";
    if (base) { snprintf(here, sizeof here, "%sdata/", base); SDL_free(base); tries[1] = here; }
    for (int i = 0; i < 4; i++) {
        if (!tries[i]) continue;
        char p[1100];
        snprintf(data_dir, sizeof data_dir, "%s%s", tries[i], tries[i][strlen(tries[i]) - 1] == '/' ? "" : "/");
        snprintf(p, sizeof p, "%sTITLE.WAV", data_dir);
        if (file_exists(p)) return;
    }
    snprintf(data_dir, sizeof data_dir, "data/");
    fprintf(stderr, "mike: music not found (looked for data/TITLE.WAV next to the program and in %s); playing without music\n", DATADIR);
}

/* ---------- music: both songs loaded into memory, converted to the output format ---------- */
typedef struct { Uint8 *buf; Uint32 len; } Song;
static Song songs[4];                                /* indexed by MUSIC_* */
static SDL_AudioDeviceID audio_dev;
static SDL_AudioSpec audio_have;
static volatile int cur_song, songs_ready;
static Uint32 song_pos;

static void mix_sfx(Uint8 *out, int len);
static void audio_cb(void *ud, Uint8 *out, int len)
{
    (void)ud;
    Song *s = &songs[cur_song];
    Uint8 *start = out; int total = len;
    if (!songs_ready || !s->buf || s->len == 0) { memset(out, audio_have.silence, len); mix_sfx(start, total); return; }
    while (len > 0) {
        if (song_pos >= s->len) song_pos = 0;        /* loop */
        Uint32 n = s->len - song_pos;
        if (n > (Uint32)len) n = len;
        memcpy(out, s->buf + song_pos, n);
        out += n; len -= n; song_pos += n;
    }
    mix_sfx(start, total);
}

/* sound effects (src/sfx_data.h, 22050 Hz mono), mixed over the music in audio_cb */
#include "sfx_data.h"
static const int16_t *sfx_pcm[3] = { sfx_jump_pcm, sfx_death_pcm, sfx_win_pcm };
static const int sfx_len[3] = { sizeof sfx_jump_pcm / 2, sizeof sfx_death_pcm / 2, sizeof sfx_win_pcm / 2 };
#define VOICES 4
static struct { int id; double pos; } voice[VOICES];
static int voices_on;
static void mix_sfx(Uint8 *out, int len)
{
    if (!voices_on || audio_have.format != AUDIO_S16SYS) return;
    int16_t *o = (int16_t *)out; int ch = audio_have.channels, frames = len / (2 * ch);
    double step = (double)SFX_RATE / audio_have.freq;
    for (int v = 0; v < VOICES; v++) {
        if (voice[v].id < 0) continue;
        const int16_t *p = sfx_pcm[voice[v].id]; int n = sfx_len[voice[v].id];
        for (int f = 0; f < frames; f++) {
            int k = (int)voice[v].pos;
            if (k >= n) { voice[v].id = -1; break; }
            int s = voice[v].id == SFX_WIN ? p[k] : p[k] * 7 / 8;
            for (int c = 0; c < ch; c++) { int m = o[f * ch + c] + s; o[f * ch + c] = m > 32767 ? 32767 : m < -32768 ? -32768 : m; }
            voice[v].pos += step;
        }
    }
}
void plat_sfx(int id)
{
    if (!audio_dev || id < 0 || id > 2) return;
    SDL_LockAudioDevice(audio_dev);
    if (!voices_on) { for (int v = 0; v < VOICES; v++) voice[v].id = -1; voices_on = 1; }
    int best = 0;
    for (int v = 0; v < VOICES; v++) if (voice[v].id < 0) { best = v; break; }
    voice[best].id = id; voice[best].pos = 0;
    SDL_UnlockAudioDevice(audio_dev);
}

static void load_song(int idx, const char *file)
{
    char path[1200];
    snprintf(path, sizeof path, "%s%s", data_dir, file);
    SDL_AudioSpec spec; Uint8 *buf; Uint32 len;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) { fprintf(stderr, "mike: can't load %s: %s\n", path, SDL_GetError()); return; }
    SDL_AudioCVT cvt;
    int r = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, audio_have.format, audio_have.channels, audio_have.freq);
    if (r < 0) { SDL_FreeWAV(buf); return; }
    if (r == 0) {                                     /* already the right format */
        songs[idx].buf = malloc(len); memcpy(songs[idx].buf, buf, len); songs[idx].len = len;
        SDL_FreeWAV(buf);
        return;
    }
    cvt.len = len;
    cvt.buf = malloc((size_t)len * cvt.len_mult);
    memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);
    SDL_ConvertAudio(&cvt);
    songs[idx].buf = cvt.buf;
    songs[idx].len = cvt.len_cvt - cvt.len_cvt % (audio_have.channels * SDL_AUDIO_BITSIZE(audio_have.format) / 8);
}

static int load_songs(void *ud)
{
    (void)ud;
    load_song(MUSIC_TITLE, "TITLE.WAV");
    load_song(MUSIC_GAME, "JUMP.WAV");
    load_song(MUSIC_CREDITS, "CREDITS.WAV");
    songs_ready = 1;
    return 0;
}

static void audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return;
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
    want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &audio_have, 0);
    if (!audio_dev) { fprintf(stderr, "mike: no audio: %s\n", SDL_GetError()); return; }
    SDL_DetachThread(SDL_CreateThread(load_songs, "music", NULL));   /* the game starts while the music loads */
    SDL_PauseAudioDevice(audio_dev, 0);
}

void plat_music(int track)
{
    if (track == cur_song) return;
    if (audio_dev) SDL_LockAudioDevice(audio_dev);
    cur_song = track; song_pos = 0;
    if (audio_dev) SDL_UnlockAudioDevice(audio_dev);
}

/* ---------- saves (the VMU on Dreamcast) ---------- */
static char save_dir[1024];

int plat_storage_present(void) { return save_dir[0] != 0; }

static void save_path(int file, char *out, size_t n)
{
    if (file == SAVE_SCORES) snprintf(out, n, "%smike_scores.sav", save_dir);   /* endless high scores */
    else if (file == SAVE_EXTRAS) snprintf(out, n, "%smike_extras.sav", save_dir);
    else if (file == SAVE_MYLEVELS) snprintf(out, n, "%smike_levels.sav", save_dir);
    else if (file == SAVE_HOMEPET) snprintf(out, n, "%smike_homepet.sav", save_dir);
    else snprintf(out, n, "%smike_save%d.sav", save_dir, file + 1);
}

int plat_save(int file, const void *data, int len, const char *desc)
{
    (void)desc;
    if (!plat_storage_present()) return SAVE_NO_CARD;
    char path[1100], tmp[1110];
    save_path(file, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);       /* write then rename: a crash can't leave half a save */
    FILE *f = fopen(tmp, "wb");
    if (!f) return SAVE_ERROR;
    int ok = fwrite(data, 1, len, f) == (size_t)len;
    ok &= fclose(f) == 0;
#ifdef _WIN32
    if (ok) ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;   /* rename() won't replace on Windows */
#else
    if (ok) ok = rename(tmp, path) == 0;
#endif
    if (!ok) { remove(tmp); return SAVE_ERROR; }
    return SAVE_OK;
}

int plat_load(int file, void *data, int len)
{
    if (!plat_storage_present()) return 0;
    char path[1100];
    save_path(file, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = (int)fread(data, 1, len, f);
    fclose(f);
    return n > 0 ? n : 0;
}

void plat_vmu_lcd(int port_a, int port_b) { (void)port_a; (void)port_b; }   /* no VMU screen on a PC */
void plat_vmu_screen(int port, const char *img) { (void)port; (void)img; }
/* ---------- the VMU pet: on a PC it runs in its own little window ("mike --pet") ---------- */
static char exe_path[1024];
static void pet_path(char *out, size_t n) { snprintf(out, n, "%smike_pet.vms", save_dir); }
const char *plat_pet_other_game(void) { return ""; }

static int write_file(const char *path, const void *data, int len)
{
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return 0;
    int ok = fwrite(data, 1, len, f) == (size_t)len; ok &= fclose(f) == 0;
#ifdef _WIN32
    if (ok) ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    if (ok) ok = rename(tmp, path) == 0;
#endif
    return ok;
}

static void open_pet_window(void)
{
#ifdef _WIN32
    char cmd[1200]; snprintf(cmd, sizeof cmd, "\"%s\" --pet", exe_path);
    STARTUPINFOA si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
#else
    if (fork() == 0) { execl(exe_path, exe_path, "--pet", (char *)NULL); _exit(1); }
#endif
}

int plat_pet_send(const unsigned char *img, int len, int replace)
{
    (void)replace;
    if (!plat_storage_present()) return PET_NO_CARD;
    char path[1100]; pet_path(path, sizeof path);
    if (!write_file(path, img, len)) return PET_ERROR;
    open_pet_window();
    return PET_OK;
}

int plat_pet_fetch(unsigned char *img, int max)
{
    if (!plat_storage_present()) return 0;
    char path[1100]; pet_path(path, sizeof path);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    int n = (int)fread(img, 1, max, f); fclose(f);
    return n;
}

void plat_pet_clear(void) { char path[1100]; pet_path(path, sizeof path); remove(path); }

/* the pet window: a VMU drawn on screen, running the pet in the emulator */
/* ---------- the pet window's look: a VMU with the keyboard keys that work it, each one labelled ---------- */
#include "saveedit_font.h"            /* Silkscreen 8 px, ASCII 32..95 (also used by the Dreamcast save editor) */
static int pw_text_w(const char *s, int sc)
{
    int w = 0;
    for (; *s; s++) { int c = (unsigned char)*s; if (c >= 'a' && c <= 'z') c -= 32; if (c < 32 || c > 95) c = '?'; w += efont[c - 32][0]; }
    return w * sc;
}
static void pw_text(SDL_Renderer *r, int x, int y, int sc, const char *s, Uint8 R, Uint8 G, Uint8 B)   /* x < 0: centred on -x */
{
    if (x < 0) x = -x - pw_text_w(s, sc) / 2;
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    for (; *s; s++) {
        int c = (unsigned char)*s; if (c >= 'a' && c <= 'z') c -= 32; if (c < 32 || c > 95) c = '?';
        const unsigned char *g = efont[c - 32];
        for (int yy = 0; yy < 8; yy++) for (int xx = 0; xx < 8; xx++)
            if (g[1 + yy] & (0x80 >> xx)) { SDL_Rect p = { x + xx * sc, y + yy * sc, sc, sc }; SDL_RenderFillRect(r, &p); }
        x += g[0] * sc;
    }
}
static void pw_round(SDL_Renderer *r, SDL_Rect k, int rad, Uint8 R, Uint8 G, Uint8 B)     /* filled rounded box */
{
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    for (int y = 0; y < k.h; y++) {
        int in = 0;
        if (y < rad) { float d = rad - y - 0.5f; in = rad - (int)sqrtf((float)rad * rad - d * d); }
        else if (y >= k.h - rad) { float d = y - (k.h - rad) + 0.5f; in = rad - (int)sqrtf((float)rad * rad - d * d); }
        SDL_RenderDrawLine(r, k.x + in, k.y + y, k.x + k.w - 1 - in, k.y + y);
    }
}
static void pw_arrow(SDL_Renderer *r, SDL_Rect k, int dir, Uint8 c)     /* dir 0 up, 1 down, 2 left, 3 right */
{
    SDL_SetRenderDrawColor(r, c, c, c, 255);
    int cx = k.x + k.w / 2, cy = k.y + k.h / 2, s = k.w / 4;
    for (int i = 0; i <= s; i++) {
        int half = i * 9 / 10;
        if (dir == 0) SDL_RenderDrawLine(r, cx - half, cy - s / 2 + i, cx + half, cy - s / 2 + i);
        if (dir == 1) SDL_RenderDrawLine(r, cx - half, cy + s / 2 - i, cx + half, cy + s / 2 - i);
        if (dir == 2) SDL_RenderDrawLine(r, cx - s / 2 + i, cy - half, cx - s / 2 + i, cy + half);
        if (dir == 3) SDL_RenderDrawLine(r, cx + s / 2 - i, cy - half, cx + s / 2 - i, cy + half);
    }
}
/* one keyboard key: lit purple while held, its key name on it */
static void pw_key(SDL_Renderer *r, SDL_Rect k, const char *name, int held)
{
    SDL_Rect sh = { k.x, k.y + 4, k.w, k.h };
    pw_round(r, sh, 7, 28, 14, 52);                                    /* shadow under the key */
    SDL_Rect cap = { k.x, k.y + (held ? 3 : 0), k.w, k.h };
    pw_round(r, cap, 7, held ? 127 : 74, held ? 0 : 66, held ? 255 : 96);
    if (name) { int sc = strlen(name) == 1 ? 3 : 2; pw_text(r, -(cap.x + cap.w / 2), cap.y + cap.h / 2 - 4 * sc, sc, name, 255, 255, 255); }
}
static int pw_hit(SDL_Rect k, int x, int y) { return x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h + 4; }

static int pet_window_main(void)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    char *pref = SDL_GetPrefPath("Prosystems", "MIKE");
    if (pref) { snprintf(save_dir, sizeof save_dir, "%s", pref); SDL_free(pref); }
    char path[1100]; pet_path(path, sizeof path);
    static Vmu vm; static uint8_t img[VMU_FLASH_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "M.I.K.E ON VMU", "Nobody is here! Send M.I.K.E or A.L.E.X from the game first.", NULL); return 0; }
    int n = (int)fread(img, 1, sizeof img, f); fclose(f);
    pet_hint_pc(img, n);
    vmu_load(&vm, img, n);
    const int S = 7, W = 48 * S + 104, H = 32 * S + 330;
    const int LX = (W - 48 * S) / 2, LY = 74;
    SDL_Window *w = SDL_CreateWindow("M.I.K.E ON VMU", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    /* the keys: arrows (move / choose), X (menu), SPACE (do it), ESC (save and quit) */
    int ky = LY + 32 * S + 52, ks = 46;
    SDL_Rect k_up = { 42 + ks, ky, ks, ks }, k_left = { 42, ky + ks + 6, ks, ks }, k_down = { 42 + ks, ky + ks + 6, ks, ks }, k_right = { 42 + 2 * ks, ky + ks + 6, ks, ks };
    SDL_Rect k_b = { W - 42 - 150, ky, 64, ks }, k_a = { W - 42 - 150, ky + ks + 6, 150, ks };
    SDL_Rect k_mode = { (W - 110) / 2, H - 92, 110, 40 };
    struct { SDL_Rect *k; int bit; } hits[] = { { &k_up, VMU_UP }, { &k_down, VMU_DOWN }, { &k_left, VMU_LEFT }, { &k_right, VMU_RIGHT },
                                                 { &k_b, VMU_B }, { &k_a, VMU_A }, { &k_mode, VMU_MODE } };
    Uint32 last = SDL_GetTicks(); int writes = 0, quitting = 0, keys = 0, mouse = 0;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quitting = 1;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int bit = 0;
                switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_w: bit = VMU_UP; break;
                case SDLK_DOWN: case SDLK_s: bit = VMU_DOWN; break;
                case SDLK_LEFT: case SDLK_a: bit = VMU_LEFT; break;
                case SDLK_RIGHT: case SDLK_d: bit = VMU_RIGHT; break;
                case SDLK_SPACE: case SDLK_z: case SDLK_RETURN: bit = VMU_A; break;
                case SDLK_x: case SDLK_BACKSPACE: bit = VMU_B; break;
                case SDLK_ESCAPE: case SDLK_m: bit = VMU_MODE; break;
                }
                if (e.type == SDL_KEYDOWN) keys |= bit; else keys &= ~bit;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)   /* the keys can be clicked too */
                for (unsigned i = 0; i < sizeof hits / sizeof hits[0]; i++) if (pw_hit(*hits[i].k, e.button.x, e.button.y)) mouse = hits[i].bit;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) mouse = 0;
        }
        int held = keys | mouse;
        vmu_set_buttons(&vm, quitting ? VMU_MODE : held);     /* closing the window = MODE: save and quit */
        Uint32 now = SDL_GetTicks();
        vmu_run(&vm, (now - last) / 1000.0); last = now;
        if (vm.flash_writes != writes || vm.quit) {
            writes = vm.flash_writes;
            FILE *chk = fopen(path, "rb");
            if (!chk) break;                    /* brought back home in the game: this window's done */
            fclose(chk);
            write_file(path, vm.flash, n);
        }
        if (vm.quit) break;
        /* draw */
        SDL_SetRenderDrawColor(r, 30, 8, 62, 255); SDL_RenderClear(r);
        pw_text(r, -(W / 2) + 2, 18 + 2, 3, "M.I.K.E ON VMU", 58, 0, 112);
        pw_text(r, -(W / 2), 18, 3, "M.I.K.E ON VMU", 0, 255, 26);
        SDL_Rect body = { 18, LY - 26, W - 36, 32 * S + 52 };
        pw_round(r, body, 18, 236, 236, 242);                           /* the VMU around the screen */
        SDL_Rect bez = { LX - 10, LY - 10, 48 * S + 20, 32 * S + 20 };
        pw_round(r, bez, 8, 60, 60, 72);
        SDL_Rect lcd = { LX, LY, 48 * S, 32 * S };
        SDL_SetRenderDrawColor(r, 150, 190, 170, 255); SDL_RenderFillRect(r, &lcd);
        SDL_SetRenderDrawColor(r, 30, 40, 50, 255);
        if (vmu_lcd_on(&vm))
            for (int y = 0; y < 32; y++) for (int x = 0; x < 48; x++)
                if (vmu_pixel(&vm, x, y)) { SDL_Rect p = { LX + x * S, LY + y * S, S, S }; SDL_RenderFillRect(r, &p); }
        /* the keys, each with what it does underneath */
        pw_key(r, k_up, NULL, held & VMU_UP); pw_key(r, k_down, NULL, held & VMU_DOWN);
        pw_key(r, k_left, NULL, held & VMU_LEFT); pw_key(r, k_right, NULL, held & VMU_RIGHT);
        {
            SDL_Rect a0 = k_up, a1 = k_down, a2 = k_left, a3 = k_right;
            if (held & VMU_UP) a0.y += 3;
            if (held & VMU_DOWN) a1.y += 3;
            if (held & VMU_LEFT) a2.y += 3;
            if (held & VMU_RIGHT) a3.y += 3;
            pw_arrow(r, a0, 0, 255); pw_arrow(r, a1, 1, 255); pw_arrow(r, a2, 2, 255); pw_arrow(r, a3, 3, 255);
        }
        pw_text(r, -(k_down.x + ks / 2), k_down.y + ks + 12, 2, "MOVE", 200, 180, 240);
        pw_key(r, k_b, "X", held & VMU_B);
        pw_text(r, k_b.x + k_b.w + 12, k_b.y + ks / 2 - 8, 2, "MENU", 255, 212, 0);
        pw_key(r, k_a, "SPACE", held & VMU_A);
        pw_text(r, -(k_a.x + k_a.w / 2), k_a.y + ks + 12, 2, "DO IT", 200, 180, 240);
        pw_key(r, k_mode, "ESC", held & VMU_MODE);
        pw_text(r, -(W / 2), k_mode.y + 38 + 10, 2, "SAVE + CLOSE", 200, 180, 240);
        SDL_RenderPresent(r);
        SDL_Delay(15);
    }
    SDL_Quit();
    return 0;
}

/* only one "memory card" here: the save folder / browser storage */
int plat_card_count(void) { return plat_storage_present() ? 1 : 0; }
const char *plat_card_name(int card) { (void)card; return "SAVE"; }
int plat_save_card(int card, int file, const void *data, int len, const char *desc) { return card == 0 ? plat_save(file, data, len, desc) : SAVE_NO_CARD; }
int plat_load_card(int card, int file, void *data, int len) { return card == 0 ? plat_load(file, data, len) : 0; }

/* ---------- input ---------- */
static SDL_GameController *pads[2];

static void open_pads(void)
{
    for (int i = 0; i < 2; i++) if (pads[i] && !SDL_GameControllerGetAttached(pads[i])) { SDL_GameControllerClose(pads[i]); pads[i] = NULL; }
    for (int j = 0; j < SDL_NumJoysticks(); j++) {
        if (!SDL_IsGameController(j)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
        int used = 0;
        for (int i = 0; i < 2; i++) if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) used = 1;
        if (used) continue;
        for (int i = 0; i < 2; i++) if (!pads[i]) { pads[i] = SDL_GameControllerOpen(j); break; }
    }
}

static float stick(Sint16 v)
{
    float f = v / 32767.0f;
    if (f > 1) f = 1;
    if (f < -1) f = -1;
    if (f > -0.2f && f < 0.2f) return 0;             /* dead zone, same as the Dreamcast build */
    return f > 0 ? (f - 0.2f) / 0.8f : (f + 0.2f) / 0.8f;
}

static void read_pad(SDL_GameController *c, Input *in)
{
    if (!c) return;
    float sx = stick(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    float sy = stick(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) sx = -1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) sx = 1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP)) sy = -1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) sy = 1;
    if (sx != 0) in->sx = sx;
    if (sy != 0) in->sy = sy;
    in->a |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A);
    in->b |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B);
    in->y |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y);
    in->start |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START);
    float lt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    float rt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) lt = 1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) rt = 1;
    if (lt > in->ltrig) in->ltrig = lt;
    if (rt > in->rtrig) in->rtrig = rt;
}

static int quit_requested;

/* in[0] = player 1 (controller A), in[1] = player 2 (controller B) */
void plat_poll(Input *in)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit_requested = 1;
        else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) open_pads();
        else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) set_fullscreen(!fullscreen);
            else if (k == SDLK_ESCAPE && fullscreen) set_fullscreen(0);
        }
    }
    memset(in, 0, 2 * sizeof *in);
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    int alt = (SDL_GetModState() & KMOD_ALT) != 0;

    int enter = (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) && !alt;
    if (!game_two_player_active()) {
        /* 1 player and the menus: arrows or WASD, Space jumps, X/Backspace back, C credits */
        in[0].sx = (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D] ? 1.0f : 0) - (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A] ? 1.0f : 0);
        in[0].sy = (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S] ? 1.0f : 0) - (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] ? 1.0f : 0);
        in[0].a = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z];
        in[0].b = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_BACKSPACE];
        in[0].y = k[SDL_SCANCODE_C];
        in[0].start = enter;
        in[0].ltrig = k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_COMMA]; in[0].rtrig = k[SDL_SCANCODE_E] || k[SDL_SCANCODE_PERIOD];
    } else {
        /* 2 player race: player 1 on WASD + Z, player 2 on the arrows + Space */
        in[0].sx = (k[SDL_SCANCODE_D] ? 1.0f : 0) - (k[SDL_SCANCODE_A] ? 1.0f : 0);
        in[0].sy = (k[SDL_SCANCODE_S] ? 1.0f : 0) - (k[SDL_SCANCODE_W] ? 1.0f : 0);
        in[0].a = k[SDL_SCANCODE_Z]; in[0].b = k[SDL_SCANCODE_X]; in[0].y = k[SDL_SCANCODE_C];
        in[0].start = k[SDL_SCANCODE_TAB];
        in[0].ltrig = k[SDL_SCANCODE_Q]; in[0].rtrig = k[SDL_SCANCODE_E];
        in[1].sx = (k[SDL_SCANCODE_RIGHT] ? 1.0f : 0) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0);
        in[1].sy = (k[SDL_SCANCODE_DOWN] ? 1.0f : 0) - (k[SDL_SCANCODE_UP] ? 1.0f : 0);
        in[1].a = k[SDL_SCANCODE_SPACE]; in[1].b = k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT];
        in[1].start = enter;
        in[1].ltrig = k[SDL_SCANCODE_COMMA]; in[1].rtrig = k[SDL_SCANCODE_PERIOD];
    }
    read_pad(pads[0], &in[0]);
    read_pad(pads[1], &in[1]);

    in[0].present = in[1].present = 1;               /* the keyboard is always there for both players */
    in[0].quit = quit_requested;
}

/* ---------- start-up and main loop ---------- */
void plat_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "mike: SDL_Init failed: %s\n", SDL_GetError()); exit(1); }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);   /* smooth edges if the GPU can */
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int ww = 640, wh = 480;                           /* biggest 640x480 multiple that fits the screen */
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        for (int k = 2; k <= 4; k++) if (640 * k <= usable.w * 9 / 10 && 480 * k <= usable.h * 9 / 10) { ww = 640 * k; wh = 480 * k; }
    win = SDL_CreateWindow("M.I.K.E", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!win) {                                       /* no multisampling available: try without */
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        win = SDL_CreateWindow("M.I.K.E", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    }
    if (!win || !SDL_GL_CreateContext(win)) { fprintf(stderr, "mike: can't open an OpenGL window: %s\n", SDL_GetError()); exit(1); }
    SDL_SetWindowMinimumSize(win, 320, 240);
    if (SDL_GL_SetSwapInterval(1) < 0) SDL_GL_SetSwapInterval(0);
    glEnable(GL_MULTISAMPLE);

    char *pref = SDL_GetPrefPath("Prosystems", "MIKE");
    if (pref) { snprintf(save_dir, sizeof save_dir, "%s", pref); SDL_free(pref); }
    else fprintf(stderr, "mike: no save folder: progress won't be saved\n");

    find_data_dir();
    audio_init();
    open_pads();
}

void plat_shutdown(void)
{
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    for (int i = 0; i < 2; i++) if (pads[i]) SDL_GameControllerClose(pads[i]);
    SDL_Quit();
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    GetModuleFileNameA(NULL, exe_path, sizeof exe_path);
#else
    { ssize_t k = readlink("/proc/self/exe", exe_path, sizeof exe_path - 1); if (k > 0) exe_path[k] = 0; else snprintf(exe_path, sizeof exe_path, "%s", argv[0]); }
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "-f")) fullscreen = 1;
        if (!strcmp(argv[i], "--pet")) return pet_window_main();
    }
    plat_init();
    if (fullscreen) set_fullscreen(1);
    game_init();

    /* same timing as the Dreamcast build: game logic in fixed 1/60 s steps, as many as real time calls for */
    const float step = 1.0f / 60.0f;
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    float acc = step;
    Input in[2];
    for (;;) {
        plat_poll(in);
        if (in[0].quit) break;
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (float)(now - last) / freq;
        last = now;
        if (acc > 0.1f) acc = 0.1f;
        int steps = 0;
        while (acc >= step * 0.9f && steps < 4) {
            game_update(in, step);
            acc -= step;
            steps++;
        }
        if (acc < -step) acc = -step;
        game_draw();
        if (steps == 0) SDL_Delay(1);                 /* no vsync: don't spin the CPU */
    }
    plat_shutdown();
    return 0;
}
