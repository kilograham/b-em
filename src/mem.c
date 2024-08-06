/*B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 *
 * ROM handling*/

#include <ctype.h>
#include "b-em.h"
#include "6502.h"
#include "config.h"
#include "mem.h"
#include "model.h"

#ifdef PICO_BUILD
#define INCLUDE_ROMS
#if PICO_ON_DEVICE
#include "hardware/structs/xip_ctrl.h"
#endif
#endif

#ifdef INCLUDE_ROMS
#include "roms/roms.h"
#endif

uint8_t ram_fe30, ram_fe34;

rom_slot_t rom_slots[ROM_NSLOT];

ALLEGRO_PATH *os_dir, *rom_dir;

static const char slotkeys[16][6] = {
    "rom00", "rom01", "rom02", "rom03",
    "rom04", "rom05", "rom06", "rom07",
    "rom08", "rom09", "rom10", "rom11",
    "rom12", "rom13", "rom14", "rom15"
};

uint8_t *ram;
#ifndef NO_USE_RAM_ROMS
uint8_t *os;
uint8_t *rom;
#else
const uint8_t *os;
#endif

void mem_init() {
    log_debug("mem: mem_init");
#if PICO_ON_DEVICE
#if PICO_RP2040
    hw_clear_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_ERR_BADWRITE_BITS);
#endif
    g_garbage_write = (uint8_t *)XIP_NOCACHE_NOALLOC_BASE;
    g_garbage_read = g_garbage_write; // todo OK?
#endif
    ram = (uint8_t *)malloc(RAM_SIZE);
#ifndef NO_USE_RAM_ROMS
    rom = (uint8_t *)malloc(ROM_NSLOT * ROM_SIZE);
    os  = (uint8_t *)malloc(ROM_SIZE);
#endif
    memset(ram, 0, RAM_SIZE);
    os_dir  = al_create_path_for_directory("roms/os");
    rom_dir = al_create_path_for_directory("roms/general");
}

static void rom_free(int slot) {
    if (rom_slots[slot].alloc) {
        if (rom_slots[slot].name)
            free(rom_slots[slot].name);
        if (rom_slots[slot].path)
            free(rom_slots[slot].path);
    }
#ifdef NO_USE_RAM_ROMS
    if (rom_slots[slot].swram) {
        free((void *) rom_slots[slot].data);
        rom_slots[slot].data = NULL;
    }
#endif
}

void mem_close() {
    int slot;

    for (slot = 0; slot < ROM_NSLOT; slot++)
        rom_free(slot);

#ifndef PICO_BUILD
    if (ram) free(ram);
    if (rom) free(rom);
    if (os)  free(os);
#endif
}

static void dump_mem(void *start, size_t size, const char *which, const char *file) {
    FILE *f;

    if ((f = fopen(file, "wb"))) {
        fwrite(start, size, 1, f);
        fclose(f);
    } else
        log_error("mem: unable to open %s dump file %s: %s", which, file, strerror(errno));
}

void mem_dump(void) {
    dump_mem(ram, 64*1024, "RAM", "ram.dmp");
#ifndef NO_USE_RAM_ROMS
    dump_mem(rom, ROM_NSLOT*ROM_SIZE, "ROM", "rom.dmp");
#endif
}

#ifdef INCLUDE_ROMS
const uint8_t *get_included_rom(const char *name) {
    for(uint i=0; i<embedded_rom_count; i++) {
        if (!strcmp(name, embedded_roms[i].name)) {
            return embedded_roms[i].data;
        }
    }
    return NULL;
}
#endif

static void load_os_rom(const char *sect) {
    const char *osname;
#ifndef INCLUDE_ROMS
    const char *cpath;
    FILE *f;
    ALLEGRO_PATH *path;
#endif

    osname = get_config_string(sect, "os", models[curmodel].os);
#ifndef INCLUDE_ROMS
    if ((path = find_dat_file(os_dir, osname, ".rom"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if ((f = fopen(cpath, "rb"))) {
            if (fread(os, ROM_SIZE, 1, f) == 1) {
                fclose(f);
                log_debug("mem: OS %s loaded from %s", osname, cpath);
                al_destroy_path(path);
                return;
            }
            else
                log_fatal("mem: unable to load OS %s, read error/truncated file on %s", osname, cpath);
        }
        else
            log_fatal("mem: unable to load OS %s, unable to open %s: %s", osname, cpath, strerror(errno));
        al_destroy_path(path);
    } else
        log_fatal("mem: unable to find OS %s", osname);
#else
    const uint8_t *irom = get_included_rom(osname);
    if (irom) {
#ifndef NO_USE_RAM_ROMS
        memcpy(os, irom ,ROM_SIZE);
#else
        os = irom;
#endif
        return;
    }
    log_fatal("mem: unable to find OS %s", osname);
#endif
    exit(1);
}

const uint8_t *mem_romdetail(int slot) {
    const uint8_t *base = rom_slot_ptr(slot);
    const uint8_t *copyr;
    uint8_t rtype;

    rtype = base[6];
    if (rtype & 0xc0) {
        copyr = base + base[7];
        if (copyr[0] == 0 && copyr[1] == '(' && copyr[2] == 'C' && copyr[3] == ')') {
            return base + 8;
        }
    }
    return NULL;
}

void mem_loadrom(int slot, const char *name, const char *path, uint8_t use_name) {
#ifndef INCLUDE_ROMS
    FILE *f;

    if ((f = fopen(path, "rb"))) {
        if (fread(rom_slot_ptr(slot), ROM_SIZE, 1, f) == 1 || feof(f)) {
            fclose(f);
            log_debug("mem: ROM slot %02d loaded with %s from %s", slot, name, path);
            rom_slots[slot].use_name = use_name;
            rom_slots[slot].alloc = 1;
            rom_slots[slot].name = strdup(name);
            rom_slots[slot].path = strdup(path);
        }
        else
            log_warn("mem: unable to load ROM slot %02d with %s: %s", slot, name, strerror(errno));
    }
    else
        log_warn("mem: unable to load ROM slot %02d with %s, uanble to open %s: %s", slot, name, path, strerror(errno));
#else
    const uint8_t *irom = get_included_rom(name);
    if (irom) {
#ifndef NO_USE_RAM_ROMS
        memcpy(rom_slot_ptr(slot), irom, ROM_SIZE);
#else
        rom_slots[slot].data = irom;
#endif
        log_debug("mem: ROM slot %02d loaded with %s", slot, name);
        rom_slots[slot].use_name = use_name;
        rom_slots[slot].alloc = 0;
        rom_slots[slot].name = strdup(name);
        rom_slots[slot].path = "";
        return;
    }
    log_warn("mem: unable to load ROM slot %02d with missing included rom %s", slot, name);
#endif
}

#ifndef INCLUDE_ROMS
static int is_relative_filename(const char *fn)
{
    int c0 = *fn;
    return !(c0 == '/' || c0 == '\\' || (isalpha(c0) && fn[1] == ':'));
}
#endif

static void cfg_load_rom(int slot, const char *sect) {
    const char *key, *name;
#ifndef INCLUDE_ROMS
    const char *file;
    ALLEGRO_PATH *path;
#endif

    key = slotkeys[slot];
    name = al_get_config_value(bem_cfg, sect, key);
    if (name != NULL && *name != '\0') {
#ifndef INCLUDE_ROMS
        if (is_relative_filename(name)) {
            if ((path = find_dat_file(rom_dir, name, ".rom"))) {
                file = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
                mem_loadrom(slot, name, file, 1);
                al_destroy_path(path);
            }
            else
                log_warn("mem: unable to load ROM slot %02d with %s, ROM file not found", slot, name);
        } else {
            if ((file = strrchr(name, '/')))
                file++;
            else
                file = name;
            mem_loadrom(slot, file, name, 0);
        }
#else
        mem_loadrom(slot, name, NULL, 1);
#endif
    }
}

void mem_romsetup_os01() {
    const char *sect = models[curmodel].cfgsect;
    char *name, *path;
    int c;

    load_os_rom(sect);
    cfg_load_rom(15, sect);
#ifndef NO_USE_RAM_ROMS
    memcpy(rom_slot_ptr(14), rom_slot_ptr(15), ROM_SIZE);
    memcpy(rom_slot_ptr(12), rom_slot_ptr(14), ROM_SIZE * 2);
    memcpy(rom_slot_ptr(8), rom_slot_ptr(12), ROM_SIZE * 4);
    memcpy(rom, rom_slot_ptr(8), ROM_SIZE * 8);
#endif
    name = rom_slots[15].name;
    path = rom_slots[15].path;
    for (c = 0; c < ROM_NSLOT; c++) {
        rom_slots[c].locked = 1;
        rom_slots[c].swram = 0;
        rom_slots[c].alloc = 0;
        rom_slots[c].name = name;
        rom_slots[c].path = path;
#ifdef NO_USE_RAM_ROMS
        rom_slots[c].data = rom_slots[15].data;
#endif
    }
}

void mem_romsetup_std(void) {
    const char *sect = models[curmodel].cfgsect;
    int slot;

    load_os_rom(sect);
    for (slot = 15; slot >= 0; slot--)
        cfg_load_rom(slot, sect);
}

static void fill_swram(void) {
    int slot;

    for (slot = 0; slot < ROM_NSLOT; slot++)
        if (!rom_slots[slot].name)
            rom_slots[slot].swram = 1;
}

void alloc_swram() {
#ifdef NO_USE_RAM_ROMS
    for (int c = 0; c < ROM_NSLOT; c++) {
        if (rom_slots[c].swram) {
            printf("Allocating RAM for slot %d\n", c);
            assert(!rom_slots[c].data);
            rom_slots[c].data = malloc(ROM_SIZE);
        }
    }
#endif
}

void mem_romsetup_swram(void) {
    mem_romsetup_std();
    fill_swram();
    alloc_swram();
}

void mem_romsetup_bp128(void) {
    const char *sect = models[curmodel].cfgsect;
    int slot;

    load_os_rom(sect);
    cfg_load_rom(15, sect);
    cfg_load_rom(14, sect);
    rom_slots[13].swram = 1;
    rom_slots[12].swram = 1;
    for (slot = 11; slot >= 0; slot--)
        cfg_load_rom(slot, sect);
    rom_slots[1].swram = 1;
    rom_slots[0].swram = 1;
    alloc_swram();
}

void mem_romsetup_master(void) {
    const char *sect = models[curmodel].cfgsect;
    const char *osname;
#ifndef INCLUDE_ROMS
    const char *cpath;
    FILE *f;
    ALLEGRO_PATH *path;
#endif
    int slot;

    osname = get_config_string(sect, "os", models[curmodel].os);
#ifndef INCLUDE_ROMS
    if ((path = find_dat_file(os_dir, osname, ".rom"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if ((f = fopen(cpath, "rb"))) {
            if (fread(os, ROM_SIZE, 1, f) == 1) {
#if defined(PICO_BUILD) && defined(__arm)
                assert(false);
#endif
                if (fread(rom_slot_ptr(9), 7 * ROM_SIZE, 1, f) == 1) {
                    fclose(f);
                    al_destroy_path(path);
                    for (slot = ROM_NSLOT-1; slot >= 9; slot--) {
                        rom_slots[slot].swram = 0;
                        rom_slots[slot].locked = 1;
                        rom_slots[slot].alloc = 0;
                        rom_slots[slot].name = (char *)models[curmodel].os;
                    }
// todo not correct #define, but this is what is generally loaded here
#ifndef NO_USE_VDFS
                    cfg_load_rom(8, sect);
#endif
                    rom_slots[7].swram = 1;
                    rom_slots[6].swram = 1;
                    rom_slots[5].swram = 1;
                    rom_slots[4].swram = 1;
                    for (slot = 7; slot >= 0; slot--)
                        cfg_load_rom(slot, sect);
                    return;
                }
            }
            log_fatal("mem: unable to read complete OS ROM %s: %s", osname, strerror(errno));
            fclose(f);
        } else
            log_fatal("mem: unable to load OS %s, unable to open %s: %s", osname, cpath, strerror(errno));
        al_destroy_path(path);
    } else
        log_fatal("mem: unable to find OS %s", osname);
    exit(1);
#else
    const uint8_t *irom = get_included_rom(osname);
    if (irom) {
#ifndef NO_USE_RAM_ROMS
        memcpy(os, irom, ROM_SIZE);
        memcpy(rom_slot_ptr(9), irom + ROM_SIZE, 7 * ROM_SIZE);
#else
        os = irom;
#endif
        for (slot = ROM_NSLOT-1; slot >= 9; slot--) {
            rom_slots[slot].swram = 0;
            rom_slots[slot].locked = 1;
            rom_slots[slot].alloc = 0;
            rom_slots[slot].name = (char *)models[curmodel].os;
#ifdef NO_USE_RAM_ROMS
            rom_slots[slot].data = irom + (slot - 8) * ROM_SIZE;
#endif
        }
        rom_slots[7].swram = 1;
        rom_slots[6].swram = 1;
        rom_slots[5].swram = 1;
        rom_slots[4].swram = 1;
        for (slot = 8; slot >= 0; slot--)
            cfg_load_rom(slot, sect);
        alloc_swram();
        return;
    }
    log_fatal("mem: unable to find OS %s", osname);
    exit(1);
#endif
}

int mem_findswram(int n) {
    int c;

    for (c = 0; c < ROM_NSLOT; c++)
        if (rom_slots[c].swram)
            if (n-- <= 0)
                return c;
    return -1;
}

static void rom_clearmeta(int slot) {
    rom_free(slot);
    rom_slots[slot].locked = 0;
    rom_slots[slot].use_name = 0;
    rom_slots[slot].alloc = 0;
    rom_slots[slot].name = NULL;
    rom_slots[slot].path = NULL;
#ifdef NO_USE_RAM_ROMS
    rom_slots[slot].data = NULL;
#endif
}

void mem_clearrom(int slot) {
#ifndef NO_USE_RAM_ROMS
    uint8_t *base = rom_slot_ptr(slot);

    memset(base, 0xff, ROM_SIZE);
#endif
    rom_clearmeta(slot);
}

void mem_clearroms(void) {
    int slot;

    for (slot = 0; slot < ROM_NSLOT; slot++) {
        mem_clearrom(slot);
        rom_clearmeta(slot);
        rom_slots[slot].swram = 0;
    }
}

#ifndef NO_USE_SAVE_STATE
void mem_savezlib(ZFILE *zfp)
{
    unsigned char latches[2];

    latches[0] = ram_fe30;
    latches[1] = ram_fe34;
    savestate_zwrite(zfp, latches, 2);
    savestate_zwrite(zfp, ram, RAM_SIZE);
    savestate_zwrite(zfp, rom, ROM_SIZE*ROM_NSLOT);
}

void mem_loadzlib(ZFILE *zfp)
{
    unsigned char latches[2];

    savestate_zread(zfp, latches, 2);
    writemem(0xFE30, latches[0]);
    writemem(0xFE34, latches[1]);
    savestate_zread(zfp, ram, RAM_SIZE);
    savestate_zread(zfp, rom, ROM_SIZE*ROM_NSLOT);
}

void mem_loadstate(FILE *f) {
    writemem(0xFE30, getc(f));
    writemem(0xFE34, getc(f));
    fread(ram, RAM_SIZE, 1, f);
    fread(rom, ROM_SIZE*ROM_NSLOT, 1, f);
}
#endif

void mem_save_romcfg(const char *sect) {
    int slot;
    rom_slot_t *slotp;
    const char *value;

    for (slot = ROM_NSLOT-1; slot >= 0; slot--) {
        slotp = rom_slots + slot;
        if (!slotp->locked) {
            value = slotp->use_name ? slotp->name : slotp->path;
            if (value)
                al_set_config_value(bem_cfg, sect, slotkeys[slot], value);
        }
    }
}
