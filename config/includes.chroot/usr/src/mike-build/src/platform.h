/* Thin layer between the game and the hardware (Dreamcast, Linux/Debian, or the PC test harness). */
#pragma once
#include <stdint.h>

typedef struct {
    float sx, sy;        /* stick / d-pad, -1..1 (sy: up = -1) */
    float ltrig, rtrig;  /* 0..1 */
    int a, b, y, start;  /* held */
    int present;         /* a controller is plugged into this port */
    int quit;            /* PC only */
} Input;

enum { MUSIC_NONE, MUSIC_TITLE, MUSIC_GAME, MUSIC_CREDITS };

void plat_init(void);
void plat_shutdown(void);
void plat_poll(Input *in);
void plat_begin_frame(void);
void plat_end_frame(void);       /* swap + wait for vblank */
void plat_music(int track);      /* switches the looping music track */
enum { SFX_JUMP, SFX_DEATH, SFX_WIN };   /* SFX_WIN: level beaten */
void plat_sfx(int id);           /* plays a sound effect over the music */
void plat_upload_textures(unsigned int *ids);
/* glViewport / glScissor in the game's 640x480 screen space (the PC build scales these to the window) */
void plat_viewport(int x, int y, int w, int h);
void plat_scissor(int x, int y, int w, int h);

/* save storage (VMU on Dreamcast). desc is shown in the Dreamcast file manager. */
enum { SAVE_OK, SAVE_NO_CARD, SAVE_FULL, SAVE_ERROR };
#define SAVE_SCORES 3      /* save "file" number used for the endless-mode high scores */
#define SAVE_EXTRAS 4      /* skins unlocked / chosen, VMU pet rewards */
#define SAVE_MYLEVELS 5    /* levels made in the level editor */
#define SAVE_HOMEPET 6     /* the pet played at home ("play on Dreamcast") */
int  plat_storage_present(void);
int  plat_save(int file, const void *data, int len, const char *desc);   /* file 0..2, or SAVE_SCORES */
int  plat_load(int file, void *data, int len);   /* bytes loaded (up to len), 0 = none */
void plat_vmu_screen(int port, const char *img);
/* the VMU pet ("M.I.K.E ON VMU"). Dreamcast: the pet is written to the VMU as its mini-game.
   PC: it opens in its own window. Browser: MIKE_Pet.html opens in a new tab. */
enum { PET_OK, PET_NO_CARD, PET_OTHER_GAME, PET_FULL, PET_ERROR };
int  plat_pet_send(const unsigned char *img, int len, int replace);  /* replace: OK to remove another VMU game */
int  plat_pet_fetch(unsigned char *img, int max);    /* the pet's file as it is now (bytes), 0 = not there */
void plat_pet_clear(void);                           /* the pet came home: remove it */
const char *plat_pet_other_game(void);               /* the other game's name, after PET_OTHER_GAME */

/* memory cards: card 0 is where the game saves; others are for sending levels to a friend's VMU */
int  plat_card_count(void);
const char *plat_card_name(int card);
int  plat_save_card(int card, int file, const void *data, int len, const char *desc);
int  plat_load_card(int card, int file, void *data, int len);   /* custom 48x32 VMU picture ('.' = dark pixel), drawn when it changes */

/* button hints on screen: Dreamcast controller buttons, or the PC keyboard keys */
#ifdef _arch_dreamcast
#define LBL_CREDITS "Y: CREDITS"
#define LBL_CHOOSE  "LEFT/RIGHT: CHOOSE   A: START   B: BACK"
#define LBL_SWAP    "UP/DOWN: MOVE   LEFT/RIGHT: CHOOSE   B: BACK"
#define LBL_SELECT  "A: SELECT   B: BACK"
#define LBL_BACK    "B: BACK"
#define LBL_MENU1P  "UP/DOWN: MOVE   LEFT/RIGHT: CHOOSE   B: BACK"
#define LBL_LEVELSEL "A: PLAY   B: BACK"
#define LBL_EDIT    "A: PLACE  B: ERASE  Y: PIECE  L/R: HEIGHT  START: MENU"
#define LBL_MYLV    "A: CHOOSE   B: BACK"
#define STORAGE_MISSING_MSG "NO VMU - LEVELS WON'T BE SAVED"
#define LBL_SCORES  "A: PLAY   B: BACK"
#define PET_SEND_LABEL  "SEND TO THE VMU"
#define PET_HERE_LABEL  "PLAY ON DREAMCAST"
#define PET_AWAY_WHERE  "ON THE VMU"
#define PET_SENT_MSG    "SENT! UNPLUG THE VMU AND PICK GAME ON ITS MENU"
#define PET_NO_CARD_MSG "PLUG A VMU INTO CONTROLLER A"
#define LBL_PET_MENU    "UP/DOWN: MOVE   LEFT/RIGHT: CHOOSE   A: OK   B: BACK"
#define LBL_READY       "PRESS A WHEN YOU ARE READY"
#define LBL_PET_HERE    "B: MENU   UP/DOWN: SCROLL   A: DO IT   START: SAVE AND LEAVE"
#else
#define LBL_CREDITS "C: CREDITS"
#define LBL_CHOOSE  "LEFT/RIGHT: CHOOSE   SPACE: START   X: BACK"
#define LBL_SWAP    "UP/DOWN: MOVE   LEFT/RIGHT: CHOOSE   X: BACK"
#define LBL_SELECT  "SPACE: SELECT   X: BACK"
#define LBL_BACK    "X: BACK"
#define LBL_MENU1P  "UP/DOWN: MOVE   LEFT/RIGHT: CHOOSE   X: BACK"
#define LBL_LEVELSEL "SPACE: PLAY   X: BACK"
#define LBL_EDIT    "SPACE: PLACE  X: ERASE  C: PIECE  Q/E: HEIGHT  ENTER: MENU"
#define LBL_MYLV    "SPACE: CHOOSE   X: BACK"
#define STORAGE_MISSING_MSG "CAN'T SAVE ON THIS COMPUTER"
#define LBL_SCORES  "SPACE: PLAY   X: BACK"
#ifdef __EMSCRIPTEN__
#define PET_SEND_LABEL  "OPEN THE PET PAGE"
#define PET_AWAY_WHERE  "ON THE PET PAGE"
#define PET_SENT_MSG    "OPENED MIKE_PET.HTML IN A NEW TAB"
#define PET_NO_CARD_MSG "THIS BROWSER CAN'T SAVE"
#else
#define PET_SEND_LABEL  "OPEN IN ITS OWN WINDOW"
#define PET_AWAY_WHERE  "IN ITS OWN WINDOW"
#define PET_SENT_MSG    "THE PET IS OPEN IN ITS OWN WINDOW"
#define PET_NO_CARD_MSG "CAN'T SAVE ON THIS COMPUTER"
#endif
#define PET_HERE_LABEL  "PLAY IT HERE"
#define LBL_PET_MENU    "UP/DOWN: MOVE   LEFT/RIGHT: CHOOSE   SPACE: OK   X: BACK"
#define LBL_PET_HERE    "X: MENU   UP/DOWN: SCROLL   SPACE: DO IT   ENTER: SAVE"
#define LBL_READY       "PRESS SPACE WHEN YOU ARE READY"
#endif

/* picture on the VMU's little screen */
enum { VMU_LCD_TITLE, VMU_LCD_MIKE, VMU_LCD_ALEX };
void plat_vmu_lcd(int port_a, int port_b);   /* picture for the VMUs in controller A and B */
