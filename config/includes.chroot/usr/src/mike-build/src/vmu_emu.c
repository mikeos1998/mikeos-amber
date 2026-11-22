/* Dreamcast VMU emulator for the M.I.K.E VMU pet. See vmu_emu.h. */
#include "vmu_emu.h"
#include <string.h>

/* special function registers ($100 + n) */
enum {
    R_ACC = 0x00, R_PSW = 0x01, R_B = 0x02, R_C = 0x03, R_TRL = 0x04, R_TRH = 0x05, R_SP = 0x06, R_PCON = 0x07,
    R_IE = 0x08, R_EXT = 0x0D, R_OCR = 0x0E, R_XBNK = 0x25, R_P3 = 0x4C, R_P3INT = 0x4E, R_FPR = 0x54,
    R_P7 = 0x5C, R_VSEL = 0x63, R_VRMAD1 = 0x64, R_VRMAD2 = 0x65, R_VTRBF = 0x66, R_BTCR = 0x7F
};
#define PSW_CY 0x80
#define PSW_AC 0x40
#define PSW_OV 0x04
#define ACC (v->sfr[R_ACC])
#define PSW (v->sfr[R_PSW])

static int game_last_block;
static uint8_t ext_start;
static int p3_pending;

static double rate(const Vmu *v)       /* CPU cycles per second for the current clock setting */
{
    return (v->sfr[R_OCR] & 0x20) ? 32768.0 / 6.0 : 879236.0 / 6.0;   /* quartz or RC oscillator */
}

void vmu_reset(Vmu *v)
{
    memset(v->ram, 0, sizeof v->ram); memset(v->sfr, 0, sizeof v->sfr); memset(v->xram, 0, sizeof v->xram); memset(v->shown, 0, sizeof v->shown);
    v->sfr[R_SP] = 0x7F;               /* what the BIOS hands a game: stack at $7F, RAM bank 1, quartz clock */
    PSW = 0x02;
    v->sfr[R_OCR] = 0xA3;
    v->sfr[R_P7] = 0x02;               /* not docked, battery fine */
    v->sfr[R_BTCR] = 0x40;
    v->sfr[R_IE] = 0x80;
    v->sfr[R_XBNK - 0] = 0;
    ext_start = v->sfr[R_EXT] & 1;
    v->pc = 0; v->in_rom = 0; v->halted = 0; v->in_isr = 0; v->quit = 0;
    v->cycles = 0; v->next_tick = 0.5; v->bad_op = -1;
    p3_pending = 0;
}

void vmu_load(Vmu *v, const uint8_t *image, int len)
{
    memset(v->flash, 0xFF, sizeof v->flash);
    if (len > VMU_FLASH_SIZE) len = VMU_FLASH_SIZE;
    memcpy(v->flash, image, len);
    game_last_block = (len + 511) / 512 - 1;
    v->flash_writes = 0;
    vmu_reset(v);
}

void vmu_set_buttons(Vmu *v, int b)
{
    if (b != v->buttons && (v->sfr[R_P3INT] & 1)) p3_pending = 1;
    v->buttons = b;
}

/* ---------- memory ---------- */
static uint8_t rd(Vmu *v, int a)
{
    a &= 0x1FF;
    if (a < 0x100) return v->ram[(PSW >> 1) & 1][a];
    if (a >= 0x180) { int b = v->sfr[R_XBNK]; return b < 3 ? v->xram[b][a - 0x180] : 0; }
    int r = a - 0x100;
    switch (r) {
    case R_P3: return (uint8_t)~v->buttons;
    case R_PSW: { uint8_t p = ACC; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1; return (uint8_t)((PSW & 0xFE) | (p & 1)); }
    case R_VTRBF: {
        int addr = ((v->sfr[R_VRMAD2] & 1) << 8) | v->sfr[R_VRMAD1];
        uint8_t val = v->wram[addr];
        if (v->sfr[R_VSEL] & 0x10) { addr = (addr + 1) & 0x1FF; v->sfr[R_VRMAD1] = addr & 0xFF; v->sfr[R_VRMAD2] = addr >> 8; }
        return val;
    }
    }
    return v->sfr[r];
}

static void wr(Vmu *v, int a, uint8_t val)
{
    a &= 0x1FF;
    if (a < 0x100) { v->ram[(PSW >> 1) & 1][a] = val; return; }
    if (a >= 0x180) { int b = v->sfr[R_XBNK]; if (b < 3) v->xram[b][a - 0x180] = val; return; }
    int r = a - 0x100;
    if (r == R_VTRBF) {
        int addr = ((v->sfr[R_VRMAD2] & 1) << 8) | v->sfr[R_VRMAD1];
        v->wram[addr] = val;
        if (v->sfr[R_VSEL] & 0x10) { addr = (addr + 1) & 0x1FF; v->sfr[R_VRMAD1] = addr & 0xFF; v->sfr[R_VRMAD2] = addr >> 8; }
        return;
    }
    if (r == R_P3) return;             /* buttons are inputs */
    v->sfr[r] = val;
}

static int ind_addr(Vmu *v, int i)    /* @R0..@R3: R0/R1 point into RAM, R2/R3 into the SFRs / XRAM */
{
    int bank = (PSW >> 1) & 1, irbk = (PSW >> 3) & 3;
    int val = v->ram[bank][irbk * 4 + i];
    return (i < 2 ? 0 : 0x100) | val;
}

static void push(Vmu *v, uint8_t val) { v->sfr[R_SP]++; v->ram[0][v->sfr[R_SP]] = val; }
static uint8_t pop(Vmu *v) { uint8_t val = v->ram[0][v->sfr[R_SP]]; v->sfr[R_SP]--; return val; }
static uint8_t fetch(Vmu *v) { return v->flash[v->pc++]; }

static void add(Vmu *v, int x, int carry)
{
    int a = ACC, r = a + x + carry;
    PSW &= ~(PSW_CY | PSW_AC | PSW_OV);
    if (r > 0xFF) PSW |= PSW_CY;
    if ((a & 0xF) + (x & 0xF) + carry > 0xF) PSW |= PSW_AC;
    if (~(a ^ x) & (a ^ r) & 0x80) PSW |= PSW_OV;
    ACC = (uint8_t)r;
}
static void sub(Vmu *v, int x, int carry)
{
    int a = ACC, r = a - x - carry;
    PSW &= ~(PSW_CY | PSW_AC | PSW_OV);
    if (r < 0) PSW |= PSW_CY;
    if ((a & 0xF) - (x & 0xF) - carry < 0) PSW |= PSW_AC;
    if ((a ^ x) & (a ^ r) & 0x80) PSW |= PSW_OV;
    ACC = (uint8_t)r;
}
static void cmp_flag(Vmu *v, int a, int b) { if (a < b) PSW |= PSW_CY; else PSW &= ~PSW_CY; }

/* ---------- the firmware calls a game makes ---------- */
static void firmware(Vmu *v, int target)
{
    uint8_t *bank1 = v->ram[1];
    int fmbank = bank1[0x7D], hi = bank1[0x7E], lo = bank1[0x7F] & 0x80;
    int addr = (fmbank & 1) << 16 | hi << 8 | lo;
    int ret = -1;
    switch (target) {
    case 0x100: case 0x108:            /* FM_WRT_EX: a game may only write inside its own file */
        if (fmbank != 0 || hi > 2 * game_last_block + 1) ACC = 0xFF;
        else { memcpy(v->flash + addr, bank1 + 0x80, 128); ACC = 0; v->flash_writes++; }
        ret = target + (target == 0x100 ? 5 : 3);
        break;
    case 0x110: ACC = memcmp(v->flash + addr, bank1 + 0x80, 128) ? 0xFF : 0; ret = 0x115; break;
    case 0x120: memcpy(bank1 + 0x80, v->flash + addr, 128); ret = 0x125; break;
    case 0x130: v->sfr[R_BTCR] &= ~0x0A; ret = 0x139; break;      /* clock tick: just acknowledge */
    case 0x140: ret = 0x145; break;
    case 0x1F0: v->quit = 1; ret = 0x1F0; break;                   /* MODE: back to the VMU menu */
    default: v->quit = 1; ret = target; break;
    }
    v->sfr[R_EXT] ^= 1;                /* the BIOS flips back to running from flash */
    v->in_rom = 0;
    v->pc = (uint16_t)ret;
}

static void jump(Vmu *v, int target)
{
    if (v->in_rom) firmware(v, target);
    else v->pc = (uint16_t)target;
}

/* ---------- one instruction; returns its cycles ---------- */
static int step(Vmu *v)
{
    int op = fetch(v);
    int d8 = op & 1, d, x, r, b;
    switch (op) {
    case 0x00: return 1;                                                         /* nop */
    case 0x01: r = (int8_t)fetch(v); jump(v, v->pc + r); return 2;               /* br */
    case 0x02: case 0x03: d = (d8 << 8) | fetch(v); ACC = rd(v, d); return 1;    /* ld d9 */
    case 0x04: case 0x05: case 0x06: case 0x07: ACC = rd(v, ind_addr(v, op & 3)); return 1;
    case 0x10: case 0x11: {                                                      /* callr / brf */
        int lo = fetch(v), hi = fetch(v), t = (v->pc + (lo | hi << 8) - 1) & 0xFFFF;
        if (op == 0x10) { push(v, v->pc & 0xFF); push(v, v->pc >> 8); }
        jump(v, t); return 4;
    }
    case 0x12: case 0x13: d = (d8 << 8) | fetch(v); wr(v, d, ACC); return 1;     /* st d9 */
    case 0x14: case 0x15: case 0x16: case 0x17: wr(v, ind_addr(v, op & 3), ACC); return 1;
    case 0x20: case 0x21: {                                                      /* callf / jmpf */
        int hi = fetch(v), lo = fetch(v), t = hi << 8 | lo;
        if (op == 0x20) { push(v, v->pc & 0xFF); push(v, v->pc >> 8); }
        jump(v, t); return 2;
    }
    case 0x22: case 0x23: d = (d8 << 8) | fetch(v); x = fetch(v); wr(v, d, (uint8_t)x); return 2;   /* mov #,d9 */
    case 0x24: case 0x25: case 0x26: case 0x27: x = fetch(v); wr(v, ind_addr(v, op & 3), (uint8_t)x); return 1;
    case 0x30: {                                                                 /* mul */
        unsigned m = ((unsigned)ACC << 8 | v->sfr[R_C]) * v->sfr[R_B];
        v->sfr[R_C] = m & 0xFF; ACC = (m >> 8) & 0xFF; v->sfr[R_B] = (m >> 16) & 0xFF;
        PSW &= ~(PSW_CY | PSW_OV); if (m > 0xFFFF) PSW |= PSW_OV; return 7;
    }
    case 0x31: x = fetch(v); r = (int8_t)fetch(v); cmp_flag(v, ACC, x); if (ACC == x) jump(v, v->pc + r); return 2;   /* be #i8 */
    case 0x32: case 0x33: d = (d8 << 8) | fetch(v); r = (int8_t)fetch(v); x = rd(v, d); cmp_flag(v, ACC, x); if (ACC == x) jump(v, v->pc + r); return 2;
    case 0x34: case 0x35: case 0x36: case 0x37: { int m = rd(v, ind_addr(v, op & 3)); x = fetch(v); r = (int8_t)fetch(v); cmp_flag(v, m, x); if (m == x) jump(v, v->pc + r); return 2; }
    case 0x40: {                                                                 /* div */
        unsigned n = (unsigned)ACC << 8 | v->sfr[R_C], dv = v->sfr[R_B];
        if (dv == 0) { ACC = 0xFF; v->sfr[R_C] = 0xFF; PSW |= PSW_OV; return 7; }
        unsigned q = n / dv, rm = n % dv;
        ACC = (q >> 8) & 0xFF; v->sfr[R_C] = q & 0xFF; v->sfr[R_B] = (uint8_t)rm;
        PSW &= ~(PSW_CY | PSW_OV); if (rm == 0) PSW |= PSW_OV; return 7;
    }
    case 0x41: x = fetch(v); r = (int8_t)fetch(v); cmp_flag(v, ACC, x); if (ACC != x) jump(v, v->pc + r); return 2;   /* bne */
    case 0x42: case 0x43: d = (d8 << 8) | fetch(v); r = (int8_t)fetch(v); x = rd(v, d); cmp_flag(v, ACC, x); if (ACC != x) jump(v, v->pc + r); return 2;
    case 0x44: case 0x45: case 0x46: case 0x47: { int m = rd(v, ind_addr(v, op & 3)); x = fetch(v); r = (int8_t)fetch(v); cmp_flag(v, m, x); if (m != x) jump(v, v->pc + r); return 2; }
    case 0x50: ACC = v->flash[((v->sfr[R_FPR] & 1) << 16) | v->sfr[R_TRH] << 8 | v->sfr[R_TRL]]; return 2;   /* ldf */
    case 0x51: return 2;                                                         /* stf: not used */
    case 0x52: case 0x53: d = (d8 << 8) | fetch(v); r = (int8_t)fetch(v); x = (uint8_t)(rd(v, d) - 1); wr(v, d, (uint8_t)x); if (x) jump(v, v->pc + r); return 2;
    case 0x54: case 0x55: case 0x56: case 0x57: { int a = ind_addr(v, op & 3); r = (int8_t)fetch(v); x = (uint8_t)(rd(v, a) - 1); wr(v, a, (uint8_t)x); if (x) jump(v, v->pc + r); return 2; }
    case 0x60: case 0x61: d = (d8 << 8) | fetch(v); push(v, rd(v, d)); return 2;  /* push */
    case 0x62: case 0x63: d = (d8 << 8) | fetch(v); wr(v, d, rd(v, d) + 1); return 1;   /* inc */
    case 0x64: case 0x65: case 0x66: case 0x67: { int a = ind_addr(v, op & 3); wr(v, a, rd(v, a) + 1); return 1; }
    case 0x70: case 0x71: d = (d8 << 8) | fetch(v); wr(v, d, pop(v)); return 2;   /* pop */
    case 0x72: case 0x73: d = (d8 << 8) | fetch(v); wr(v, d, rd(v, d) - 1); return 1;   /* dec */
    case 0x74: case 0x75: case 0x76: case 0x77: { int a = ind_addr(v, op & 3); wr(v, a, rd(v, a) - 1); return 1; }
    case 0x80: r = (int8_t)fetch(v); if (ACC == 0) jump(v, v->pc + r); return 2;    /* bz */
    case 0x90: r = (int8_t)fetch(v); if (ACC != 0) jump(v, v->pc + r); return 2;    /* bnz */
    case 0x81: add(v, fetch(v), 0); return 1;
    case 0x82: case 0x83: d = (d8 << 8) | fetch(v); add(v, rd(v, d), 0); return 1;
    case 0x84: case 0x85: case 0x86: case 0x87: add(v, rd(v, ind_addr(v, op & 3)), 0); return 1;
    case 0x91: add(v, fetch(v), (PSW & PSW_CY) ? 1 : 0); return 1;
    case 0x92: case 0x93: d = (d8 << 8) | fetch(v); add(v, rd(v, d), (PSW & PSW_CY) ? 1 : 0); return 1;
    case 0x94: case 0x95: case 0x96: case 0x97: add(v, rd(v, ind_addr(v, op & 3)), (PSW & PSW_CY) ? 1 : 0); return 1;
    case 0xA0: case 0xB0: {                                                      /* ret / reti */
        int hi = pop(v), lo = pop(v); v->pc = (uint16_t)(hi << 8 | lo);
        if (op == 0xB0) v->in_isr = 0;
        return 2;
    }
    case 0xA1: sub(v, fetch(v), 0); return 1;
    case 0xA2: case 0xA3: d = (d8 << 8) | fetch(v); sub(v, rd(v, d), 0); return 1;
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: sub(v, rd(v, ind_addr(v, op & 3)), 0); return 1;
    case 0xB1: sub(v, fetch(v), (PSW & PSW_CY) ? 1 : 0); return 1;
    case 0xB2: case 0xB3: d = (d8 << 8) | fetch(v); sub(v, rd(v, d), (PSW & PSW_CY) ? 1 : 0); return 1;
    case 0xB4: case 0xB5: case 0xB6: case 0xB7: sub(v, rd(v, ind_addr(v, op & 3)), (PSW & PSW_CY) ? 1 : 0); return 1;
    case 0xC0: ACC = (uint8_t)(ACC >> 1 | ACC << 7); return 1;                  /* ror */
    case 0xC1: ACC = v->flash[(uint16_t)((v->sfr[R_TRH] << 8 | v->sfr[R_TRL]) + ACC)]; return 2;   /* ldc */
    case 0xC2: case 0xC3: d = (d8 << 8) | fetch(v); x = rd(v, d); wr(v, d, ACC); ACC = (uint8_t)x; return 1;   /* xch */
    case 0xC4: case 0xC5: case 0xC6: case 0xC7: { int a = ind_addr(v, op & 3); x = rd(v, a); wr(v, a, ACC); ACC = (uint8_t)x; return 1; }
    case 0xD0: { int c = (PSW & PSW_CY) ? 0x80 : 0; if (ACC & 1) PSW |= PSW_CY; else PSW &= ~PSW_CY; ACC = (uint8_t)(ACC >> 1 | c); return 1; }   /* rorc */
    case 0xD1: ACC |= fetch(v); return 1;
    case 0xD2: case 0xD3: d = (d8 << 8) | fetch(v); ACC |= rd(v, d); return 1;
    case 0xD4: case 0xD5: case 0xD6: case 0xD7: ACC |= rd(v, ind_addr(v, op & 3)); return 1;
    case 0xE0: ACC = (uint8_t)(ACC << 1 | ACC >> 7); return 1;                  /* rol */
    case 0xE1: ACC &= fetch(v); return 1;
    case 0xE2: case 0xE3: d = (d8 << 8) | fetch(v); ACC &= rd(v, d); return 1;
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: ACC &= rd(v, ind_addr(v, op & 3)); return 1;
    case 0xF0: { int c = (PSW & PSW_CY) ? 1 : 0; if (ACC & 0x80) PSW |= PSW_CY; else PSW &= ~PSW_CY; ACC = (uint8_t)(ACC << 1 | c); return 1; }   /* rolc */
    case 0xF1: ACC ^= fetch(v); return 1;
    case 0xF2: case 0xF3: d = (d8 << 8) | fetch(v); ACC ^= rd(v, d); return 1;
    case 0xF4: case 0xF5: case 0xF6: case 0xF7: ACC ^= rd(v, ind_addr(v, op & 3)); return 1;
    }
    /* families spread over the high nibble */
    b = op & 7; d8 = (op >> 4) & 1;
    switch (op & 0xE8) {
    case 0x08: case 0x28: {                                                      /* call / jmp a12 */
        int lo = fetch(v), t = (v->pc & 0xF000) | (d8 << 11) | ((op & 7) << 8) | lo;
        if ((op & 0xE8) == 0x08) { push(v, v->pc & 0xFF); push(v, v->pc >> 8); }
        jump(v, t); return 2;
    }
    case 0x48: case 0x68: case 0x88: {                                           /* bpc / bp / bn */
        d = (d8 << 8) | fetch(v); r = (int8_t)fetch(v);
        x = rd(v, d); int set = (x >> b) & 1;
        if ((op & 0xE8) == 0x48) { if (set) { wr(v, d, (uint8_t)(x & ~(1 << b))); jump(v, v->pc + r); } }
        else if ((op & 0xE8) == 0x68) { if (set) jump(v, v->pc + r); }
        else if (!set) jump(v, v->pc + r);
        return 2;
    }
    case 0xC8: case 0xE8: case 0xA8: {                                           /* clr1 / set1 / not1 */
        d = (d8 << 8) | fetch(v);
        x = rd(v, d);
        if ((op & 0xE8) == 0xC8) x &= ~(1 << b); else if ((op & 0xE8) == 0xE8) x |= 1 << b; else x ^= 1 << b;
        if (d == 0x100 + R_PSW) x = (x & 0xFE) | (PSW & 1);
        wr(v, d, (uint8_t)x);
        if (d == 0x100 + R_EXT && b == 0) v->in_rom = (v->sfr[R_EXT] & 1) != ext_start;
        if (d == 0x100 + R_PCON && (x & 1)) { v->halted = 1; memcpy(v->shown, v->xram, sizeof v->shown); }
        return 1;
    }
    }
    if (v->bad_op < 0) v->bad_op = op;
    return 1;
}

static void interrupt(Vmu *v, int vec)
{
    push(v, v->pc & 0xFF); push(v, v->pc >> 8);
    v->pc = (uint16_t)vec; v->in_isr = 1;
}

void vmu_run(Vmu *v, double seconds)
{
    double end = v->cycles + seconds;   /* v->cycles counts seconds of VMU time */
    while (v->cycles < end && !v->quit) {
        int irq = 0;
        if (v->cycles >= v->next_tick) {                       /* base timer: twice a second */
            v->next_tick += 0.5;
            v->sfr[R_BTCR] |= 0x02;
            irq = 0x1B;
        }
        if (p3_pending) { p3_pending = 0; v->sfr[R_P3INT] |= 0x02; irq = 0x4B; }
        if (irq) {
            if (v->halted) { v->halted = 0; v->sfr[R_PCON] &= ~1; }
            if ((v->sfr[R_IE] & 0x80) && !v->in_isr) interrupt(v, irq);
        }
        if (v->halted) {                                       /* sleep until the next tick */
            double t = v->next_tick < end ? v->next_tick : end;
            if (t > v->cycles) v->cycles = t;
            continue;
        }
        int c = step(v);
        v->cycles += c / rate(v);
    }
}

int vmu_pixel(const Vmu *v, int x, int y)
{
    int bank = y >> 4, r = y & 15;
    int a = (r >> 1) * 16 + (r & 1) * 6 + (x >> 3);
    return (v->shown[bank][a] >> (7 - (x & 7))) & 1;
}

int vmu_lcd_on(const Vmu *v) { return (v->sfr[0x27] & 0x80) != 0; }
