/* Darius Gaiden recomp - Taito F3 main board: memory map, I/O, interrupts, EEPROM, frame scheduling,
 * ROM loading and save-state container. */
#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "state.h"
#ifdef _WIN32
#include <windows.h>
/* paths are UTF-8 everywhere; Windows needs the wide-char API for non-ASCII folders */
static FILE *u8fopen(const char *path, const char *mode) {
    wchar_t wp[4096], wm[16];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 4096) || !MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 16)) return NULL;
    return _wfopen(wp, wm);
}
#define fopen u8fopen
#endif

uint8_t g_rom[0x200000];
uint8_t g_ram[0x20000];
uint8_t g_palram[0x8000];
uint8_t g_spriteram[0x10000];
uint8_t g_pfram[0xc000];
uint8_t g_textram[0x2000];
uint8_t g_charram[0x2000];
uint8_t g_lineram[0x10000];
uint8_t g_pivotram[0x10000];
uint16_t g_ctrl0[8], g_ctrl1[8];
uint8_t g_dpram[0x800];
uint16_t g_input[2];
uint8_t *g_spr_gfx;  uint32_t g_spr_count;
uint8_t *g_tile_gfx; uint32_t g_tile_count;
static uint8_t *g_snd_prog, *g_snd_samples;

int64_t  g_cycles;
int64_t  g_next_event;
int      g_frame_done;
uint64_t g_frame_count;

static int irq3_done;
/* Watchdog: the board resets itself when the program stops kicking 0x4a0000. Darius Gaiden relies on
 * this after it writes factory defaults to a blank or foreign EEPROM (it masks interrupts and waits). */
#define WD_FRAMES 180
static int wd_frames;
int g_wd_max_gap, g_wd_resets;
static int64_t ev_snd;
static uint16_t coin_word[2];
static uint32_t timer_ctrl;
#define SND_SLICE 4096
static uint32_t fb[SCREEN_W * SCREEN_H];

void cpu_take_pending_irq(void);  /* cpu.c */

/* ------------------------------------------------------------------ scheduling */
void core_irq_changed(void) { g_next_event = 0; }

static void compute_next_event(void) {
    int64_t n = ev_snd;
    if (!irq3_done && IRQ3_DELAY < n) n = IRQ3_DELAY;
    if (CYCLES_PER_FRAME < n) n = CYCLES_PER_FRAME;
    g_next_event = n;
}

void core_service(void) {
    if (g_cycles >= ev_snd) { snd_run_until(g_cycles); while (ev_snd <= g_cycles) ev_snd += SND_SLICE; }
    if (!irq3_done && g_cycles >= IRQ3_DELAY) { irq3_done = 1; cpu_set_irq_line(3); }
    if (g_cycles >= CYCLES_PER_FRAME) g_frame_done = 1;
    compute_next_event();
    if (!g_frame_done) cpu_take_pending_irq();
}

/* ------------------------------------------------------------------ I/O (TC0640FIO) */
static uint32_t control_r(int offset) {
    uint16_t p0 = g_input[0], p1 = g_input[1];
    switch (offset) {
    case 0: {
        uint32_t v = 0xffff;
        if (p0 & IN_B1) v &= ~0x0001; if (p0 & IN_B2) v &= ~0x0002;
        if (p0 & IN_B3) v &= ~0x0004; if (p0 & IN_B4) v &= ~0x0008;
        if (p1 & IN_B1) v &= ~0x0010; if (p1 & IN_B2) v &= ~0x0020;
        if (p1 & IN_B3) v &= ~0x0040; if (p1 & IN_B4) v &= ~0x0080;
        if ((p0 | p1) & IN_TILT) v &= ~0x0100;
        if ((p0 | p1) & IN_SERVICE) v &= ~0x0200;
        if (p0 & IN_START) v &= ~0x1000; if (p1 & IN_START) v &= ~0x2000;
        uint32_t e = 0xfc | (eeprom_do() ? 1 : 0);   /* EEPROMIN: DO, test (active low), coins (active low) */
        if ((p0 | p1) & IN_TEST) e &= ~0x02; else e |= 0x02;
        if (p0 & IN_COIN) e &= ~0x10;
        if (p1 & IN_COIN) e &= ~0x20;
        return v | (e << 16) | (e << 24);
    }
    case 1: {
        uint32_t v = 0xffff;
        if (p0 & IN_UP) v &= ~0x01; if (p0 & IN_DOWN) v &= ~0x02; if (p0 & IN_LEFT) v &= ~0x04; if (p0 & IN_RIGHT) v &= ~0x08;
        if (p1 & IN_UP) v &= ~0x10; if (p1 & IN_DOWN) v &= ~0x20; if (p1 & IN_LEFT) v &= ~0x40; if (p1 & IN_RIGHT) v &= ~0x80;
        return v | ((uint32_t)coin_word[0] << 16);
    }
    case 2: case 3: return 0xffff0000u;          /* analog dials (unused): MSW active-low unknown */
    case 4: return 0xffffffffu;
    case 5: return 0x0000ffffu | ((uint32_t)coin_word[1] << 16);
    }
    return 0xffffffffu;
}
static void control_w(int offset, uint32_t d, uint32_t mask) {
    switch (offset) {
    case 0: if (g_wd_resets && wd_frames > g_wd_max_gap) g_wd_max_gap = wd_frames; wd_frames = 0; return;   /* watchdog kick */
    case 1: if (mask & 0xff000000u) coin_word[0] = (uint16_t)(d >> 16); return;
    case 4: if (mask & 0xff) eeprom_write_lines((d >> 2) & 1, (d >> 4) & 1, (d >> 3) & 1); return;
    case 5: if (mask & 0xff000000u) coin_word[1] = (uint16_t)(d >> 16); return;
    }
}

/* ------------------------------------------------------------------ bus */
static inline uint32_t arr_r32(const uint8_t *base, uint32_t off) { return be32(base + off); }
static inline void arr_w32(uint8_t *base, uint32_t off, uint32_t d, uint32_t mask) {
    for (int i = 0; i < 4; i++) {
        int sh = 24 - i * 8;
        if ((mask >> sh) & 0xff) base[off + i] = (uint8_t)(d >> sh);
    }
}
static int64_t snd_sync_point(void) { return g_cycles; }

/* aligned 32-bit device access */
static uint32_t dev_r32(uint32_t a) {
    a &= 0xfffffc;
    if (a < 0x200000) return arr_r32(g_rom, a);
    if ((a & 0xfc0000) == 0x400000) return arr_r32(g_ram, a & 0x1ffff);
    if (a >= 0x440000 && a < 0x448000) return arr_r32(g_palram, a - 0x440000);
    if (a >= 0x4a0000 && a < 0x4a0020) return control_r((a - 0x4a0000) >> 2);
    if (a >= 0x600000 && a < 0x610000) return arr_r32(g_spriteram, a - 0x600000);
    if (a >= 0x610000 && a < 0x61c000) return arr_r32(g_pfram, a - 0x610000);
    if (a >= 0x61c000 && a < 0x61e000) return arr_r32(g_textram, a - 0x61c000);
    if (a >= 0x61e000 && a < 0x620000) return arr_r32(g_charram, a - 0x61e000);
    if (a >= 0x620000 && a < 0x630000) return arr_r32(g_lineram, a - 0x620000);
    if (a >= 0x630000 && a < 0x640000) return arr_r32(g_pivotram, a - 0x630000);
    if (a >= 0xc00000 && a < 0xc00800) { snd_run_until(snd_sync_point()); return arr_r32(g_dpram, a - 0xc00000); }
    return 0xffffffffu;
}
static void dev_w32(uint32_t a, uint32_t d, uint32_t mask) {
    a &= 0xfffffc;
    if ((a & 0xfc0000) == 0x400000) { arr_w32(g_ram, a & 0x1ffff, d, mask); return; }
    if (a >= 0x440000 && a < 0x448000) { arr_w32(g_palram, a - 0x440000, d, mask); return; }
    if (a >= 0x4a0000 && a < 0x4a0020) { control_w((a - 0x4a0000) >> 2, d, mask); return; }
    if (a >= 0x4c0000 && a < 0x4c0004) { timer_ctrl = (timer_ctrl & ~mask) | (d & mask); return; }
    if (a >= 0x600000 && a < 0x610000) { arr_w32(g_spriteram, a - 0x600000, d, mask); return; }
    if (a >= 0x610000 && a < 0x61c000) { arr_w32(g_pfram, a - 0x610000, d, mask); return; }
    if (a >= 0x61c000 && a < 0x61e000) { arr_w32(g_textram, a - 0x61c000, d, mask); return; }
    if (a >= 0x61e000 && a < 0x620000) { arr_w32(g_charram, a - 0x61e000, d, mask); return; }
    if (a >= 0x620000 && a < 0x630000) { arr_w32(g_lineram, a - 0x620000, d, mask); return; }
    if (a >= 0x630000 && a < 0x640000) { arr_w32(g_pivotram, a - 0x630000, d, mask); return; }
    if (a >= 0x660000 && a < 0x660020) {
        uint16_t *r = a < 0x660010 ? &g_ctrl0[(a & 0xf) >> 1] : &g_ctrl1[(a & 0xf) >> 1];
        uint16_t hm = (uint16_t)(mask >> 16), lm = (uint16_t)mask;
        r[0] = (r[0] & ~hm) | ((uint16_t)(d >> 16) & hm);
        r[1] = (r[1] & ~lm) | ((uint16_t)d & lm);
        return;
    }
    if (a >= 0xc00000 && a < 0xc00800) { snd_run_until(snd_sync_point()); arr_w32(g_dpram, a - 0xc00000, d, mask); return; }
    if (a == 0xc80000) { snd_run_until(snd_sync_point()); snd_set_reset_line(0); return; }
    if (a == 0xc80100) { snd_run_until(snd_sync_point()); snd_set_reset_line(1); return; }
    /* 0x300000 sound bankswitch: only used by Kirameki; ignored */
}

uint32_t mem_read8(uint32_t a) {
    a &= 0xffffff;
    return (dev_r32(a) >> (24 - (a & 3) * 8)) & 0xff;
}
uint32_t mem_read16(uint32_t a) {
    a &= 0xffffff;
    if (a & 1) return (mem_read8(a) << 8) | mem_read8(a + 1);
    return (dev_r32(a) >> (16 - (a & 2) * 8)) & 0xffff;
}
uint32_t mem_read32(uint32_t a) {
    a &= 0xffffff;
    if (a & 3) return (mem_read16(a) << 16) | mem_read16(a + 2);
    return dev_r32(a);
}
void mem_write8(uint32_t a, uint32_t v) {
    a &= 0xffffff;
    int sh = 24 - (a & 3) * 8;
    dev_w32(a, (v & 0xff) << sh, 0xffu << sh);
}
void mem_write16(uint32_t a, uint32_t v) {
    a &= 0xffffff;
    if (a & 1) { mem_write8(a, v >> 8); mem_write8(a + 1, v); return; }
    int sh = 16 - (a & 2) * 8;
    dev_w32(a, (v & 0xffff) << sh, 0xffffu << sh);
}
void mem_write32(uint32_t a, uint32_t v) {
    a &= 0xffffff;
    if (a & 3) { mem_write16(a, v >> 16); mem_write16(a + 2, v & 0xffff); return; }
    dev_w32(a, v, 0xffffffffu);
}

/* ------------------------------------------------------------------ EEPROM 93C46 (16-bit) */
uint16_t g_eeprom[64];
enum { EE_RESET, EE_START, EE_CMD, EE_READ, EE_DATA, EE_DONE };
static int ee_state, ee_cs, ee_clk, ee_di, ee_bits, ee_accum, ee_locked = 1, ee_cmd, ee_addr;
static uint32_t ee_shift;

void eeprom_reset(void) { ee_state = EE_RESET; ee_cs = ee_clk = ee_di = 0; ee_locked = 1; }
int eeprom_do(void) {
    if (ee_state == EE_READ) return (ee_shift >> 31) & 1;
    return 1;
}
static void ee_event(int ev) { /* 0=cs rise 1=cs fall 2=clk rise */
    if (ev == 1) { ee_state = EE_RESET; return; }
    switch (ee_state) {
    case EE_RESET: if (ev == 0) ee_state = EE_START; break;
    case EE_START: if (ev == 2 && ee_di) { ee_accum = ee_bits = 0; ee_state = EE_CMD; } break;
    case EE_CMD:
        if (ev == 2) {
            ee_accum = (ee_accum << 1) | ee_di;
            if (++ee_bits == 8) {
                ee_bits = 0; ee_addr = ee_accum & 0x3f;
                switch (ee_accum >> 6) {
                case 0:
                    switch (ee_addr >> 4) {
                    case 0: ee_locked = 1; ee_state = EE_RESET; break;
                    case 1: ee_cmd = 2; ee_shift = 0; ee_state = EE_DATA; break; /* WRAL */
                    case 2: if (!ee_locked) for (int i = 0; i < 64; i++) g_eeprom[i] = 0xffff;
                            ee_state = ee_locked ? EE_RESET : EE_DONE; break;
                    case 3: ee_locked = 0; ee_state = EE_RESET; break;
                    }
                    break;
                case 1: ee_cmd = 1; ee_shift = 0; ee_state = EE_DATA; break;
                case 2: ee_shift = 0; ee_state = EE_READ; break;
                case 3: if (!ee_locked) g_eeprom[ee_addr] = 0xffff; ee_state = ee_locked ? EE_RESET : EE_DONE; break;
                }
            }
        }
        break;
    case EE_READ:
        if (ev == 2) {
            int bi = ee_bits++;
            if (bi == 0) ee_shift = (uint32_t)g_eeprom[ee_addr & 63] << 16;
            else ee_shift = (ee_shift << 1) | 1;
        }
        break;
    case EE_DATA:
        if (ev == 2) {
            ee_shift = (ee_shift << 1) | ee_di;
            if (++ee_bits == 16) {
                if (ee_locked) { ee_state = EE_RESET; break; }
                if (ee_cmd == 1) g_eeprom[ee_addr] = (uint16_t)ee_shift;
                else for (int i = 0; i < 64; i++) g_eeprom[i] &= (uint16_t)ee_shift;
                ee_state = EE_DONE;
            }
        }
        break;
    }
}
void eeprom_write_lines(int di, int cs, int clk) {
    ee_di = di;
    if (cs != ee_cs) { ee_cs = cs; ee_event(cs ? 0 : 1); if (cs) { ee_clk = clk; return; } }
    if (clk != ee_clk) { ee_clk = clk; if (clk) ee_event(2); }
}
void core_eeprom_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t b[128];
    if (fread(b, 1, 128, f) == 128) for (int i = 0; i < 64; i++) g_eeprom[i] = (b[i * 2] << 8) | b[i * 2 + 1];
    fclose(f);
}
void core_eeprom_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint8_t b[128];
    for (int i = 0; i < 64; i++) { b[i * 2] = g_eeprom[i] >> 8; b[i * 2 + 1] = (uint8_t)g_eeprom[i]; }
    fwrite(b, 1, 128, f); fclose(f);
}
/* a blank 93C46 reads all ones; the game initialises it on first boot (as on a new board) */
void core_eeprom_factory(void) { for (int i = 0; i < 64; i++) g_eeprom[i] = 0xffff; }

/* ------------------------------------------------------------------ ROM loading */
/* CRCs and load layout from MAME's taito/taito_f3.cpp (dariusg / dariusgx), cross-checked against the
 * user's archives: every CRC below matched the decompressed file contents (not only the zip directory). */
typedef struct { const char *name; uint32_t size; uint32_t crc; } romdef_t;
enum { R_P0, R_P1, R_P2, R_P3, R_SPR_A, R_SPR_B, R_SPR_HI, R_TILE_A, R_TILE_B, R_TILE_HI, R_SND_E, R_SND_O, R_SMP_0, R_SMP_1, NROMS };
/* 68EC020 program, ROM_LOAD32_BYTE byte lanes 0..3 */
static const romdef_t prog_roms[2][4] = {
    {{"d87-12.bin", 0x80000, 0xde78f328}, {"d87-11.bin", 0x80000, 0xf7bed18e}, {"d87-10.bin", 0x80000, 0x4149f66f}, {"d87-16.bin", 0x80000, 0x8f7e5901}},
    {{"dge_mpr3.bin", 0x80000, 0x1c1e24a7}, {"dge_mpr2.bin", 0x80000, 0x7be23e23}, {"dge_mpr1.bin", 0x80000, 0xbc030f6f}, {"dge_mpr0.bin", 0x80000, 0xc5bd135c}},
};
/* shared by both editions (identical files in both sets) */
static const romdef_t shared_roms[10] = {
    {"d87-03.bin", 0x200000, 0x4be1666e}, {"d87-04.bin", 0x200000, 0x2616002c}, {"d87-05.bin", 0x200000, 0x4e5891a9},   /* sprites */
    {"d87-06.bin", 0x200000, 0x3b97a07c}, {"d87-17.bin", 0x200000, 0xe601d63e}, {"d87-08.bin", 0x200000, 0x76d23602},   /* playfields */
    {"d87-13.bin", 0x40000, 0x15b1fff4},  {"d87-14.bin", 0x40000, 0xeecda29a},                                         /* sound 68000 */
    {"d87-01.bin", 0x200000, 0x3848a110}, {"d87-02.bin", 0x200000, 0x9250abae},                                        /* ES5505 samples */
};
static const char *const set_names[2] = {"dariusg", "dariusgx"};
static const char *const set_versions[2] = {"Ver 2.5O 1994/09/19", "Extra Version, Ver 2.7J 1995/03/06"};
static const char *const ed_titles[2] = {"Darius Gaiden - Original", "Darius Gaiden - Extra Edition"};

static char last_error[1024];
static uint32_t rom_fp;
static edition_t cur_edition = ED_NONE;

const char *core_last_error(void) { return last_error; }
const char *core_romset_name(void) { return cur_edition == ED_NONE ? "" : set_names[cur_edition]; }
const char *core_edition_setname(edition_t ed) { return ed == ED_ORIGINAL || ed == ED_EXTRA ? set_names[ed] : ""; }
const char *core_edition_version(edition_t ed) { return ed == ED_ORIGINAL || ed == ED_EXTRA ? set_versions[ed] : ""; }
edition_t core_edition(void) { return cur_edition; }
uint32_t core_rom_fingerprint(void) { return rom_fp; }
uint32_t core_stats_native(void) { const rc_program_t *p = rc_program_for(cur_edition); return p ? p->native : 0; }
uint32_t core_stats_fallback(void) { const rc_program_t *p = rc_program_for(cur_edition); return p ? p->fallback : 0; }

/* ---- a romset source: a zip archive or a folder (the files may also sit in a dariusg/ or dariusgx/ subfolder) */
typedef struct { int is_zip; mz_zip_archive z; FILE *zf; char path[2048]; int open; } romsrc_t;
enum { F_OK = 0, F_MISSING, F_SIZE, F_CRC };

static int ends_with_zip(const char *p) {
    size_t l = strlen(p);
    return l > 4 && p[l - 4] == '.' && (p[l - 3] | 32) == 'z' && (p[l - 2] | 32) == 'i' && (p[l - 1] | 32) == 'p';
}
static int src_open(romsrc_t *s, const char *path) {
    memset(s, 0, sizeof *s);
    snprintf(s->path, sizeof s->path, "%s", path);
    s->is_zip = ends_with_zip(path);
    if (s->is_zip) {
        s->zf = fopen(path, "rb");          /* UTF-8 aware on Windows (see u8fopen) */
        if (!s->zf) return 0;
        if (!mz_zip_reader_init_cfile(&s->z, s->zf, 0, 0)) { fclose(s->zf); return 0; }
    } else {
        /* a folder: must exist and hold at least one file we can open */
        char probe[2300]; FILE *f = NULL;
        const char *subs[] = {"", "dariusg/", "dariusgx/"};
        for (int i = 0; i < 3 && !f; i++) { snprintf(probe, sizeof probe, "%s/%sd87-03.bin", path, subs[i]); f = fopen(probe, "rb"); }
        for (int i = 0; i < 3 && !f; i++) { snprintf(probe, sizeof probe, "%s/%sdge_mpr0.bin", path, subs[i]); f = fopen(probe, "rb"); }
        for (int i = 0; i < 3 && !f; i++) { snprintf(probe, sizeof probe, "%s/%sd87-12.bin", path, subs[i]); f = fopen(probe, "rb"); }
        if (!f) return 0;
        fclose(f);
    }
    s->open = 1;
    return 1;
}
static void src_close(romsrc_t *s) {
    if (s->open && s->is_zip) { mz_zip_reader_end(&s->z); fclose(s->zf); }
    s->open = 0;
}
static uint8_t *read_whole(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (64 << 20)) { fclose(f); return NULL; }
    uint8_t *b = malloc(sz ? (size_t)sz : 1);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f); *n = (size_t)sz; return b;
}
/* does the source hold this ROM (CRC + size from the zip directory, or the named file in a folder) */
static int src_has(romsrc_t *s, const romdef_t *d) {
    if (!s->open) return 0;
    if (!s->is_zip) {
        const char *subs[] = {"", "dariusg/", "dariusgx/"};
        for (int i = 0; i < 3; i++) {
            char fp[2300]; snprintf(fp, sizeof fp, "%s/%s%s", s->path, subs[i], d->name);
            FILE *f = fopen(fp, "rb"); if (f) { fclose(f); return 1; }
        }
        return 0;
    }
    int n = (int)mz_zip_reader_get_num_files(&s->z);
    for (int k = 0; k < n; k++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&s->z, k, &st) && !st.m_is_directory && st.m_crc32 == d->crc && st.m_uncomp_size == d->size) return 1;
    }
    return 0;
}
/* fetch one ROM: by CRC first (renamed files are fine), then by name in any subfolder; always CRC-checked */
static int src_get(romsrc_t *s, const romdef_t *d, uint8_t **out) {
    *out = NULL;
    uint8_t *p = NULL; size_t sz = 0;
    if (s->is_zip) {
        int n = (int)mz_zip_reader_get_num_files(&s->z), idx = -1;
        for (int k = 0; k < n && idx < 0; k++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&s->z, k, &st) && !st.m_is_directory && st.m_crc32 == d->crc && st.m_uncomp_size == d->size) idx = k;
        }
        if (idx < 0) idx = mz_zip_reader_locate_file(&s->z, d->name, NULL, MZ_ZIP_FLAG_IGNORE_PATH);
        if (idx < 0) return F_MISSING;
        p = mz_zip_reader_extract_to_heap(&s->z, idx, &sz, 0);
        if (!p) return F_CRC;            /* miniz verifies the stored CRC while inflating */
    } else {
        const char *subs[] = {"", "dariusg/", "dariusgx/"};
        for (int i = 0; i < 3 && !p; i++) {
            char fp[2300]; snprintf(fp, sizeof fp, "%s/%s%s", s->path, subs[i], d->name);
            p = read_whole(fp, &sz);
        }
        if (!p) return F_MISSING;
    }
    if (sz != d->size) { free(p); return F_SIZE; }
    if ((uint32_t)mz_crc32(MZ_CRC32_INIT, p, sz) != d->crc) { free(p); return F_CRC; }
    *out = p;
    return F_OK;
}

/* 16x16 tiles, low 4bpp "packed lsb" (8 bytes per row, low nibble first) + hi 2 planes */
static uint8_t *decode_16x16(const uint8_t *lo, const uint8_t *hi, uint32_t count, int sprite) {
    uint8_t *dst = malloc((size_t)count * 256), *d = dst;
    if (!dst) return NULL;
    for (uint32_t t = 0; t < count; t++) {
        const uint8_t *L = lo + (size_t)t * 128, *H = hi + (size_t)t * 64;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            uint8_t b = L[y * 8 + (x >> 1)];
            int v = (x & 1) ? (b >> 4) : (b & 15);
            int h;
            if (sprite) { /* layout_6bpp_sprite_hi */
                h = (H[y * 4 + (x >> 2)] >> (2 * (x & 3))) & 3;
            } else {      /* layout_6bpp_tile_hi: plane offsets {8,0} */
                int xb = x & 7, base = y * 4 + (x >> 3) * 2;
                h = ((H[base] >> xb) & 1) | (((H[base + 1] >> xb) & 1) << 1);
            }
            *d++ = (uint8_t)(v | (h << 4));
        }
    }
    return dst;
}

static const char *base_name(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}
static void parent_dir(const char *p, char *out, size_t n) {
    snprintf(out, n, "%s", p);
    size_t l = strlen(out);
    while (l > 1 && (out[l - 1] == '/' || out[l - 1] == '\\')) out[--l] = 0;   /* folder given with a trailing slash */
    char *b = (char *)base_name(out);
    if (b > out) b[-1] = 0; else snprintf(out, n, ".");
}

static int load_romset(edition_t ed, const char *path, int commit) {
    last_error[0] = 0;
    if (ed != ED_ORIGINAL && ed != ED_EXTRA) { snprintf(last_error, sizeof last_error, "Unknown edition."); return 0; }
    const edition_t other = ed == ED_ORIGINAL ? ED_EXTRA : ED_ORIGINAL;
    uint8_t *data[NROMS] = {0};
    romsrc_t src, par; int have_par = 0;
    int ok = 1;
    char pdesc[64] = "";
    if (!src_open(&src, path)) {
        if (ends_with_zip(path)) snprintf(last_error, sizeof last_error, "Cannot open \"%s\" as a zip archive.", base_name(path));
        else snprintf(last_error, sizeof last_error, "\"%s\" does not contain Darius Gaiden ROM files.", base_name(path));
        return 0;
    }
    /* 1. program ROMs (edition specific) */
    int other_found = 0;
    for (int i = 0; i < 4; i++) if (src_has(&src, &prog_roms[other][i])) other_found++;
    for (int i = 0; i < 4 && ok; i++) {
        int r = src_get(&src, &prog_roms[ed][i], &data[R_P0 + i]);
        if (r == F_OK) continue;
        if (r == F_MISSING && other_found)
            snprintf(last_error, sizeof last_error,
                "This is the %s romset ('%s'), not the %s set ('%s'). Choose it under the %s entry in ROM Configuration.",
                ed_titles[other], set_names[other], ed_titles[ed], set_names[ed], other == ED_ORIGINAL ? "Original" : "Extra Edition");
        else if (r == F_MISSING)
            snprintf(last_error, sizeof last_error,
                "Missing program ROM %s. This edition needs the MAME '%s' set (%s). Other revisions (for example dariusgj or dariusgu) have different program ROMs and are not supported.",
                prog_roms[ed][i].name, set_names[ed], set_versions[ed]);
        else
            snprintf(last_error, sizeof last_error, "%s %s. It is damaged or from a different revision; this edition needs the MAME '%s' set (%s).",
                prog_roms[ed][i].name, r == F_SIZE ? "has the wrong size" : "has a bad checksum", set_names[ed], set_versions[ed]);
        ok = 0;
    }
    /* 2. shared ROMs: from the same source or, for a split clone set, from the parent 'dariusg' set beside it */
    int shared_in_src = 0;
    for (int i = 0; i < 10; i++) if (src_has(&src, &shared_roms[i])) shared_in_src++;
    for (int i = 0; i < 10 && ok; i++) {
        int r = src_get(&src, &shared_roms[i], &data[R_SPR_A + i]);
        if (r == F_MISSING) {
            if (!have_par) {
                char dir[2048], cand[2300];
                parent_dir(path, dir, sizeof dir);
                const char *tries[] = {"dariusg.zip", "dariusg"};
                have_par = -1;
                for (int k = 0; k < 2 && have_par != 1; k++) {
                    snprintf(cand, sizeof cand, "%s/%s", dir, tries[k]);
                    if (strcmp(cand, path) && src_open(&par, cand)) { have_par = 1; snprintf(pdesc, sizeof pdesc, "%s", tries[k]); }
                }
            }
            if (have_par == 1) r = src_get(&par, &shared_roms[i], &data[R_SPR_A + i]);
            if (r == F_MISSING) {
                if (ed == ED_EXTRA && have_par != 1 && shared_in_src == 0)
                    snprintf(last_error, sizeof last_error,
                        "Missing %s. This looks like a split 'dariusgx' clone set, which keeps the shared graphics and sound files in its parent set. Put dariusg.zip in the same folder, or use a merged or non-merged dariusgx set.",
                        shared_roms[i].name);
                else
                    snprintf(last_error, sizeof last_error, "Missing %s (a shared graphics/sound ROM). The set is incomplete%s%s.", shared_roms[i].name,
                        have_par == 1 ? "; it is also missing from the parent set " : "", have_par == 1 ? pdesc : "");
                ok = 0; break;
            }
        }
        if (r == F_SIZE || r == F_CRC) {
            snprintf(last_error, sizeof last_error, "%s %s (corrupt, or not from the MAME '%s' set).", shared_roms[i].name,
                r == F_SIZE ? "has the wrong size" : "has a bad checksum", set_names[ed]);
            ok = 0;
        }
    }
    src_close(&src);
    if (have_par == 1) src_close(&par);
    if (!ok) { for (int i = 0; i < NROMS; i++) free(data[i]); return 0; }
    if (!commit) { for (int i = 0; i < NROMS; i++) free(data[i]); return 1; }   /* verification only */

    /* ---- everything verified: build the new machine. Only from here on is the running state touched. */
    uint8_t *lo = malloc(0x400000), *spr = NULL, *tile = NULL, *sprog = malloc(0x80000), *samples = calloc(1, 0x800000);
    if (lo) {
        /* sprites: d87-03/04 ROM_LOAD16_BYTE (4MB), d87-05 hi planes */
        for (int i = 0; i < 0x200000; i++) { lo[i * 2] = data[R_SPR_A][i]; lo[i * 2 + 1] = data[R_SPR_B][i]; }
        spr = decode_16x16(lo, data[R_SPR_HI], 0x400000 / 128, 1);
        /* playfield tiles: d87-06/17 ROM_LOAD32_WORD (4MB), d87-08 hi planes */
        for (int i = 0; i < 0x200000; i += 2) {
            lo[i * 2 + 0] = data[R_TILE_A][i]; lo[i * 2 + 1] = data[R_TILE_A][i + 1];
            lo[i * 2 + 2] = data[R_TILE_B][i]; lo[i * 2 + 3] = data[R_TILE_B][i + 1];
        }
        tile = decode_16x16(lo, data[R_TILE_HI], 0x400000 / 128, 0);
    }
    if (!lo || !spr || !tile || !sprog || !samples) {
        free(lo); free(spr); free(tile); free(sprog); free(samples);
        for (int i = 0; i < NROMS; i++) free(data[i]);
        snprintf(last_error, sizeof last_error, "Out of memory while decoding the graphics.");
        return 0;
    }
    free(lo);
    memset(g_rom, 0, sizeof g_rom);
    for (int i = 0; i < 0x80000; i++) for (int k = 0; k < 4; k++) g_rom[i * 4 + k] = data[R_P0 + k][i];
    free(g_spr_gfx); free(g_tile_gfx); free(g_snd_prog); free(g_snd_samples);
    g_spr_gfx = spr; g_spr_count = 0x400000 / 128;
    g_tile_gfx = tile; g_tile_count = 0x400000 / 128;
    /* sound program: d87-13 even, d87-14 odd (512KB at 0xc00000 on the sound CPU) */
    g_snd_prog = sprog;
    for (int i = 0; i < 0x40000; i++) { g_snd_prog[i * 2] = data[R_SND_E][i]; g_snd_prog[i * 2 + 1] = data[R_SND_O][i]; }
    /* ES5505 samples: 16-bit words with the ROM byte in the high half; d87-01 at 0, d87-02 at 0x400000 (bytes) */
    g_snd_samples = samples;
    for (int i = 0; i < 0x200000; i++) { g_snd_samples[i * 2] = data[R_SMP_0][i]; g_snd_samples[0x400000 + i * 2] = data[R_SMP_1][i]; }
    for (int i = 0; i < NROMS; i++) free(data[i]);

    rom_fp = 2166136261u;
    rom_fp = (rom_fp ^ (uint32_t)(ed + 1)) * 16777619u;
    for (int i = 0; i < 4; i++) rom_fp = (rom_fp ^ prog_roms[ed][i].crc) * 16777619u;
    for (int i = 0; i < 10; i++) rom_fp = (rom_fp ^ shared_roms[i].crc) * 16777619u;
    cur_edition = ed;
    const rc_program_t *pr = rc_program_for(ed);
    rc_select_program(pr ? pr->table : NULL);
    core_eeprom_factory();
    snd_init(g_snd_prog, 0x80000, g_snd_samples, 0x800000);
    video_init();
    cpu_init();
    return 1;
}
int core_load_romset(edition_t ed, const char *path) { return load_romset(ed, path, 1); }
int core_verify_romset(edition_t ed, const char *path) { return load_romset(ed, path, 0); }

void core_reset(void) {
    memset(g_ram, 0, sizeof g_ram);
    memset(g_palram, 0, sizeof g_palram);
    memset(g_spriteram, 0, sizeof g_spriteram);
    memset(g_pfram, 0, sizeof g_pfram);
    memset(g_textram, 0, sizeof g_textram);
    memset(g_charram, 0, sizeof g_charram);
    memset(g_lineram, 0, sizeof g_lineram);
    memset(g_pivotram, 0, sizeof g_pivotram);
    memset(g_ctrl0, 0, sizeof g_ctrl0); memset(g_ctrl1, 0, sizeof g_ctrl1);
    memset(g_dpram, 0, sizeof g_dpram);
    coin_word[0] = coin_word[1] = 0; timer_ctrl = 0; wd_frames = 0;
    g_cycles = 0; g_frame_count = 0;
    eeprom_reset();
    video_reset();
    cpu_reset();
    snd_reset();       /* sound CPU starts held in reset (MAME machine_reset) */
}

void core_run_frame(cpu_mode_t mode) {
    g_frame_done = 0; irq3_done = 0;
    ev_snd = (g_cycles / SND_SLICE + 1) * SND_SLICE;
    cpu_set_irq_line(2);                  /* vblank */
    compute_next_event();
    core_service();
    if (!g_frame_done) { if (mode == CPU_MODE_RECOMP) rc_run(); else cpu_interp_run(); }
    snd_run_until(CYCLES_PER_FRAME);
    snd_frame_end();
    g_cycles -= CYCLES_PER_FRAME;
    video_frame(fb);
    g_frame_count++;
    ++wd_frames;
    if (wd_frames > WD_FRAMES) {
        /* watchdog reset: CPUs and I/O restart; RAM contents and the EEPROM survive, as on the board */
        wd_frames = 0; g_wd_resets++;
        coin_word[0] = coin_word[1] = 0; timer_ctrl = 0;
        g_cycles = 0;
        eeprom_reset();
        cpu_reset();
        snd_reset();
    }
}

const uint32_t *core_framebuffer(void) { return fb; }

uint32_t cpu_state_hash_regs(uint32_t h); /* cpu.c */
static uint32_t hash_buf(uint32_t h, const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }
uint32_t core_state_hash(void) {
    uint32_t h = 2166136261u;
    h = hash_buf(h, g_ram, sizeof g_ram);
    h = hash_buf(h, g_spriteram, sizeof g_spriteram);
    h = hash_buf(h, g_palram, sizeof g_palram);
    h = hash_buf(h, g_pfram, sizeof g_pfram);
    h = hash_buf(h, g_lineram, sizeof g_lineram);
    h = hash_buf(h, g_textram, sizeof g_textram);
    h = hash_buf(h, g_dpram, sizeof g_dpram);
    h = (h ^ (uint32_t)g_cycles) * 16777619u;
    h = snd_state_hash(h);
    return cpu_state_hash_regs(h);
}

/* ------------------------------------------------------------------ save states */
void hw_state(sstream_t *s) {
    ss_io(s, g_ram, sizeof g_ram); ss_io(s, g_palram, sizeof g_palram);
    ss_io(s, g_spriteram, sizeof g_spriteram); ss_io(s, g_pfram, sizeof g_pfram);
    ss_io(s, g_textram, sizeof g_textram); ss_io(s, g_charram, sizeof g_charram);
    ss_io(s, g_lineram, sizeof g_lineram); ss_io(s, g_pivotram, sizeof g_pivotram);
    ss_io(s, g_ctrl0, sizeof g_ctrl0); ss_io(s, g_ctrl1, sizeof g_ctrl1);
    ss_io(s, g_dpram, sizeof g_dpram);
    ss_io(s, g_eeprom, sizeof g_eeprom);
    SS(s, g_cycles); SS(s, g_next_event); SS(s, g_frame_count);
    SS(s, irq3_done); SS(s, wd_frames); SS(s, ev_snd); SS(s, coin_word); SS(s, timer_ctrl);
    SS(s, ee_state); SS(s, ee_cs); SS(s, ee_clk); SS(s, ee_di); SS(s, ee_bits); SS(s, ee_accum);
    SS(s, ee_locked); SS(s, ee_cmd); SS(s, ee_addr); SS(s, ee_shift);
}

/* state blob: magic, format version, ROM fingerprint, then the devices */
#define STATE_MAGIC   0x44474131u /* "DGA1" */
#define STATE_VERSION 1u
static void all_state(sstream_t *s) { hw_state(s); snd_state(s); video_state(s); cpu_state(s); }
size_t core_state_save(uint8_t **out) {
    sstream_t s = {0};
    uint32_t magic = STATE_MAGIC, ver = STATE_VERSION, fp = rom_fp;
    SS(&s, magic); SS(&s, ver); SS(&s, fp);
    all_state(&s);
    uint32_t end = 0x454e4421u; SS(&s, end);
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, s.p, s.pos); SS(&s, crc);   /* integrity check */
    if (s.error) { free(s.p); *out = NULL; return 0; }
    *out = s.p; return s.pos;
}
int core_state_load(const uint8_t *data, size_t n) {
    sstream_t s = {(uint8_t *)data, 0, n, 1, 0};
    uint32_t magic = 0, ver = 0, fp = 0;
    SS(&s, magic); SS(&s, ver); SS(&s, fp);
    if (s.error || magic != STATE_MAGIC || ver != STATE_VERSION || fp != rom_fp) return 0;
    if (n < 16) return 0;
    uint32_t crc; memcpy(&crc, data + n - 4, 4);
    if ((uint32_t)mz_crc32(MZ_CRC32_INIT, data, n - 4) != crc) return 0;          /* damaged */
    s.cap = n - 4;
    /* full dry run on a backup so that a damaged blob never half-applies */
    uint8_t *backup = NULL; size_t bn = core_state_save(&backup);
    if (!bn) return 0;
    all_state(&s);
    uint32_t end = 0; SS(&s, end);
    if (s.error || s.pos != n - 4 || end != 0x454e4421u) {
        sstream_t b = {backup, 12, bn, 1, 0}; all_state(&b);
        free(backup); return 0;
    }
    free(backup);
    return 1;
}
