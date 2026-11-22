/* A small Dreamcast VMU emulator (Sanyo LC86K CPU), written for the M.I.K.E VMU pet.

   It runs a VMU mini-game image straight from its own flash, the way the VMU does. The BIOS isn't
   needed: the firmware calls a game makes (flash write / read / verify at $100-$120, the clock tick at
   $130 and quitting at $1F0) are done here instead. Used on the PC and in the browser to play the pet
   in its own window, in the "play on Dreamcast" mode, and to test the pet. */
#pragma once
#include <stdint.h>

#define VMU_W 48
#define VMU_H 32
#define VMU_FLASH_SIZE 0x20000

/* buttons, as bits in vmu_set_buttons (1 = pressed) */
enum { VMU_UP = 1, VMU_DOWN = 2, VMU_LEFT = 4, VMU_RIGHT = 8, VMU_A = 16, VMU_B = 32, VMU_MODE = 64, VMU_SLEEP = 128 };

typedef struct {
    uint8_t flash[VMU_FLASH_SIZE];
    uint8_t ram[2][256];
    uint8_t sfr[128];              /* $100-$17F */
    uint8_t xram[3][128];          /* $180-$1FF in each XRAM bank */
    uint8_t wram[512];
    uint8_t shown[2][128];         /* the screen as it was when the game last went to sleep (a finished frame) */
    uint16_t pc;
    int in_rom;                    /* EXT.0 has been flipped: the next jump goes into the (emulated) BIOS */
    int halted, in_isr, quit;
    int buttons;                   /* 1 = pressed */
    double cycles, next_tick;      /* emulated time in cycles; next 1/2 second base timer tick */
    int flash_writes;              /* how many times the game saved */
    int bad_op;                    /* first unknown opcode seen (for testing) */
} Vmu;

void vmu_load(Vmu *v, const uint8_t *image, int len);   /* put a game image at flash 0 and reset */
void vmu_reset(Vmu *v);
void vmu_set_buttons(Vmu *v, int buttons);
void vmu_run(Vmu *v, double seconds);                    /* run for this much VMU time */
int  vmu_pixel(const Vmu *v, int x, int y);             /* 1 = dark */
int  vmu_lcd_on(const Vmu *v);
