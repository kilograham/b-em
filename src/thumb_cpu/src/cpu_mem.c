/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include <cpu/cpu.h>
#include <string.h>
#include "b-em.h"

#include "6502.h"
#include "adc.h"
#include "disc.h"
#include "i8271.h"
#include "ide.h"
#include "mem.h"
#include "model.h"
#include "mouse.h"
#include "music2000.h"
#include "music5000.h"
#include "serial.h"
#include "scsi.h"
#include "sid_b-em.h"
#include "sound.h"
#include "sysacia.h"
#include "tape.h"
#include "tube.h"
#include "via.h"
#include "sysvia.h"
#include "uservia.h"
#include "vdfs.h"
#include "video.h"
#include "wd1770.h"
#include "common.h"
#include "cpu_mem.h"
#ifdef PICO_BUILD
#include "hw_event_queue.h"
#include "hardware/gpio.h"

#ifdef INCLUDE_MMFS
#include "mmc/mmc.h"
#endif

CU_REGISTER_DEBUG_PINS(hw_event, hw_reading, hw_writing)
//CU_SELECT_DEBUG_PINS(hw_event)
//CU_SELECT_DEBUG_PINS(hw_reading)
#endif

int32_t global_time;

#ifdef USE_HW_EVENT
#if !defined(NO_USE_TUBE) || !defined(NO_USE_IDE) || !defined(NO_USE_ADC) || !defined(NO_USE_MOUSE) || !defined(NO_USE_MUSIC5000)
#error not implemented yet - need to add hw_events
#endif
#endif

static MemHandler memHandlers[ 2 * CPU_MEM_SIZE / CPU_MEM_BLOCKSIZE ];
static MemHandler *memHandlersOS = memHandlers + CPU_MEM_SIZE / CPU_MEM_BLOCKSIZE ;

static uint8_t FEslowdown[8] = { 1, 0, 1, 1, 0, 0, 1, 0 };

// for each 4K
MemHandler *cpu_memHandlers[16];

#if THUMB_CPU_USE_ASM
const uint8_t *fcff_ram_mapping;
#endif
// used
// ok so the memory mapping is confusing AF, so i'm writing it down to remind me
//
// the memory layout changes based on various ACCCON settings etc. however we
// just keep two memory layouts .. one "normal" and one "os" where "os"
// is selected when executing code from 0xc000-0xe000 (and sometimes 0xa000-0xb000)


#if PICO_NO_HARDWARE
#ifndef MAX
#define MAX(a,b) ((a)<(b)?(b):(a))
#endif
// on hardware this points into our XIP cache bit dumpster
static uint8_t garbage_read[ MAX(CPU_MEM_BLOCKSIZE, ROM_SIZE) ];
static uint8_t garbage_write[ MAX(CPU_MEM_BLOCKSIZE, ROM_SIZE) ];
uint8_t *g_garbage_read = garbage_read;
uint8_t *g_garbage_write = garbage_write;
#else
uint8_t *g_garbage_read;
uint8_t *g_garbage_write;
#endif

static uint8_t acccon;

#ifndef USE_HW_EVENT
static int otherstuffcount = 0;
#endif


#ifndef USE_HW_EVENT
static void otherstuff_poll(void) {
#ifndef USE_HW_EVENT
    otherstuffcount += 128;
#endif
#if !defined(NO_USE_ACIA) && !defined(USE_HW_EVENT)
    acia_poll(&sysacia);
#endif
#ifndef NO_USE_MUSIC5000
    if (sound_music5000)
        music2000_poll();
#endif
#ifndef USE_HW_EVENT
    sound_poll();
#endif
#ifndef NO_USE_TAPE
    if (!tapelcount) {
        tape_poll();
        tapelcount = tapellatch;
    }
    tapelcount--;
#endif
#ifndef USE_HW_EVENT
    if (motorspin) {
        motorspin--;
        if (!motorspin)
            fdc_spindown();
    }
#endif
#ifndef NO_USE_IDE
    if (ide_count) {
        ide_count -= 200;
        if (ide_count <= 0)
            ide_callback();
    }
#endif
#ifndef NO_USE_ADC
    if (adc_time) {
        adc_time--;
        if (!adc_time)
            adc_poll();
    }
#endif
#ifndef NO_USE_MOUSE
    mcount--;
    if (!mcount) {
        mcount = 6;
        mouse_poll();
    }
#endif
}
#endif

#ifdef USE_HW_EVENT
static cycle_timestamp_t hw_event_timestamp; // where we have run hw events up to
static cycle_timestamp_t cpu_clk_relative_to_timestamp; // cpu_time is this + g_cpu.clk
static cycle_timestamp_t cpu_limit;
static struct list_element *next_event;
static cycle_timestamp_t next_event_timestamp;

void __time_critical_func(set_cpu_limit)(cycle_timestamp_t limit) {
    assert((limit - get_cpu_timestamp()) > 0);
    cpu_limit = limit;
}

void __time_critical_func(set_simple_hw_event_counter)(struct hw_event *event, int cycles) {
    if (!cycles)
        remove_hw_event(event);
    else {
        event->target = get_hardware_timestamp() + cycles;
        upsert_hw_event(event);
    }
}

void __time_critical_func(upsert_hw_event)(struct hw_event *new_event) {
    DEBUG_PINS_SET(hw_event, 1);
    list_remove(&next_event, &new_event->e);
    struct list_element *prev = NULL;
    for (struct list_element *e = next_event; e; e = e->next) {
        struct hw_event *event = (struct hw_event *)e;
        if ((event->target - new_event->target) > 0) { // wrap safe
            break;
        }
        prev = e;
    }
    if (prev) {
        list_insert_after(prev, &new_event->e);
    } else {
        new_event->e.next = next_event;
        next_event = &new_event->e;
    }
    next_event_timestamp = ((struct hw_event *)next_event)->target;
//    if (get_hardware_timestamp() >= 520895) {
//        printf("Ving\n");
//    }
#ifdef PRINT_EVENTS
    printf("%04x %d %d [", g_cpu.pc, next_event_timestamp - get_hardware_timestamp(), get_hardware_timestamp());
    for (struct list_element *e = next_event; e; e=e->next) {
        printf(" %d (%d)", (int)(((struct hw_event *)e)->target - get_hardware_timestamp()), ((struct hw_event *)e)->target);
    }
    printf("]\n");
#endif
    DEBUG_PINS_CLR(hw_event, 1);
}

void __time_critical_func(remove_hw_event)(struct hw_event *event) {
    list_remove(&next_event, &event->e);
}

cycle_timestamp_t __time_critical_func(get_cpu_timestamp)() {
    return cpu_clk_relative_to_timestamp + g_cpu.clk;
}

cycle_timestamp_t __time_critical_func(get_hardware_timestamp)() {
    return hw_event_timestamp;
}

int32_t __time_critical_func(possible_cpu_irq_breakout)() {
    if ((g_cpu.interrupt && !FLG_ISSET(g_cpu, i)) ||
        (g_cpu.nmi/* && !oldnmi*/)) {
//        printf("positive possible\n");
        cpu_clk_relative_to_timestamp += g_cpu.clk;
        g_cpu.clk = 0;
    }
    return g_cpu.clk;
}

static inline void __time_critical_func(_set_next_cpu_clk)() {
    cycle_timestamp_t t = get_cpu_timestamp();
    int32_t delta1 = cpu_limit - t;
    int32_t delta2;
    if ((g_cpu.interrupt && !FLG_ISSET(g_cpu, i)) || g_cpu.nmi) {
        delta2 = 1; // run for one cycle (i.e. start one more instruction)
    } else {
        delta2 = next_event_timestamp - t;
    }
    int32_t delta = MIN(delta1, delta2);
    g_cpu.clk = -delta;
    cpu_clk_relative_to_timestamp = t - g_cpu.clk;
}

void __time_critical_func(set_next_cpu_clk)() {
    _set_next_cpu_clk();
}

// run any hardware events up to the given point
void __time_critical_func(advance_hardware)(cycle_timestamp_t target) {
    int32_t __unused delta = target - hw_event_timestamp;
    assert(delta >= 0);
    hw_event_timestamp = target;
    DEBUG_PINS_SET(hw_event, 2);
//    printf("PT GT %d %d\n", get_cpu_timestamp(), next_event_remaining);
    while ((next_event_timestamp - hw_event_timestamp) <= 0) {
        struct hw_event *firing = (struct hw_event *) list_remove_head(&next_event);
        struct hw_event *after = (struct hw_event *)next_event;
        if (after) {
            next_event_timestamp = after->target;
        } else {
            next_event_timestamp = target + 0x6fffffff; // large but nowhere close to overflow... our deltas are always <130000 or so anyway
        }
        if (firing) {
            DEBUG_PINS_SET(hw_event, 4);
            if (firing->invoke(firing)) {
                upsert_hw_event(firing);
            }
            DEBUG_PINS_CLR(hw_event, 4);
        }
    }
    DEBUG_PINS_CLR(hw_event, 2);
//    video_poll(c, 1);
//    if (motoron) {
//        if (fdc_time) {
//            fdc_time -= c;
//            if (fdc_time <= 0)
//                fdc_callback();
//        }
//        disc_time -= c;
//        if (disc_time <= 0) {
//            disc_time += 16;
//            disc_poll();
//        }
//    }
#ifndef NO_USE_TUBE
    assert(false);
    tubecycle += c;
#endif
}
#else

void polltime(int c)
{
#ifdef PICO_BUILD
    global_time += c;
#endif

    via_poll(&sysvia, c);
    via_poll(&uservia, c);
    video_poll(c, 1);
    otherstuffcount -= c;
    if (motoron) {
        if (fdc_time) {
            fdc_time -= c;
            if (fdc_time <= 0)
                fdc_callback();
        }
//        printf("disctime %d %d\n", disc_time, get_cpu_timestamp());
        disc_time -= c;
        if (disc_time <= 0) {
            disc_time += 16;
            disc_poll();
        }
    }
#ifndef NO_USE_TUBE
    tubecycle += c;
#endif
    if (otherstuffcount <= 0)
        otherstuff_poll();
}
#endif

void MemHandlerSetReadFunction( MemHandler* p, MemReadFunction w )
{
#if PI_ASM32
    // explicitly set a fake thumb bit
    p->read.function = (MemReadFunction)(((uintptr_t)w)|1u);
#else
    p->read.function = w;
#endif
#if !THUMB_CPU_USE_ASM
    p->read.isFunction = 1;
#endif
}

void MemHandlerSetReadPtr( MemHandler* p, const uint8_t* ptr )
{
    p->read.ptr = ptr;
#if !THUMB_CPU_USE_ASM
    p->read.isFunction = 0;
#endif
}

void MemHandlerSetWriteFunction( MemHandler* p, MemWriteFunction w )
{
#if PI_ASM32
    // explicitly set a fake thumb bit
    p->write.function = (MemWriteFunction)(((uintptr_t)w)|1u);
#else
    p->write.function = w;
#endif
#if !THUMB_CPU_USE_ASM
    p->write.isFunction = 1;
#endif
}

void MemHandlerSetWritePtr( MemHandler* p, uint8_t* ptr )
{
    p->write.ptr = ptr;
#if !THUMB_CPU_USE_ASM
    p->write.isFunction = 0;
#endif
}

void MemHandlerSetReadOnlyWritePtr( MemHandler* p, uint32_t block_address )
{
    p->write.ptr = g_garbage_write - block_address;
#if !THUMB_CPU_USE_ASM
    p->write.isFunction = 0;
#endif
}

// note in the case of read and write then these must be the same addres, so the write time is the time between
static char clock_offsets_rw[] = {
        0x00, 0x06, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x24, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x24, 0x00, 0x04, 0x00, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x24,

        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x24, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x24, 0x00, 0x04, 0x24, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x24, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x24,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x50, 0x00, 0x00, 0x40, 0x50, 0x40, 0x14, // 9e (undoc) is wrong for n6502
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x04, 0x04,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x24, 0x24,
        0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x23,  0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x25,
};

#define SHIELA_DEFAULT_READ 0xfe

static inline uint8_t sysacia_read(uint16_t addr) {
    return acia_read(&sysacia, addr);
}

static inline void sysacia_write(uint16_t addr, uint8_t val) {
    acia_write(&sysacia, addr, val);
}

#if !defined(NO_USE_MASTER) && !defined(NO_USE_ADC)
uint8_t master_only_adc_read(uint16_t addr) {
    return MASTER ? adc_read(addr) : SHIELA_DEFAULT_READ;
}
void master_only_adc_write(uint16_t addr, uint8_t val) {
    if (MASTER) adc_write(addr, val);
}
#endif

uint8_t __time_critical_func(master_only_wd1770_read)(uint16_t addr) {
    return MASTER ? wd1770_read(addr) : SHIELA_DEFAULT_READ;
}

void /*__time_critical*/ master_only_wd1770_write(uint16_t addr, uint8_t val) {
    if (MASTER) wd1770_write(addr, val);
}

void __time_critical_func(master_wd1770_or_videoula_write)(uint16_t addr, uint8_t val) {
    if (MASTER) wd1770_write(addr, val);
    else videoula_write(addr, val);
}

uint8_t __time_critical_func(master_only_acccon)(uint16_t addr) {
    return MASTER ? acccon : SHIELA_DEFAULT_READ;
}

uint8_t __time_critical_func(non_master_fdc_read)(uint16_t addr) {
    switch(fdc_type) {
        case FDC_NONE:
        case FDC_MASTER:
            break;
#ifndef NO_USE_I8271
            case FDC_I8271:
                    return i8271_read(addr);
#endif
        default:
            return wd1770_read(addr);
    }
    return SHIELA_DEFAULT_READ;
}

void __time_critical_func(non_master_fdc_write)(uint16_t addr, uint8_t val) {
    switch(fdc_type) {
        case FDC_NONE:
        case FDC_MASTER:
            break;
#ifndef NO_USE_I8271
            case FDC_I8271:
                    i8271_write(addr, val);
#endif
        default:
            wd1770_write(addr, val);
    }
}

//static uint8_t (*sheila_read_funcs[])(uint16_t addr) = {
//        [0x00/4] = crtc_read,
//        [0x04/4] = crtc_read,
//        [0x08/4] = sysacia_read,
//        [0x0c/4] = sysacia_read,
//        [0x10/4] = serial_read,
//        [0x14/4] = serial_read,
//#ifndef NO_USE_MASTER
//#ifndef NO_USE_ADC
//        [0x18/4] = master_only_adc_read,
//#endif
//        [0x24/4] = master_only_wd1770_read,
//        [0x28/4] = master_only_wd1770_read,
//        [0x34/4] = master_only_acccon,
//#endif
//        [0x40/4] = sysvia_read,
//        [0x44/4] = sysvia_read,
//        [0x48/4] = sysvia_read,
//        [0x4c/4] = sysvia_read,
//        [0x50/4] = sysvia_read,
//        [0x54/4] = sysvia_read,
//        [0x58/4] = sysvia_read,
//        [0x5c/4] = sysvia_read,
//        [0x60/4] = uservia_read,
//        [0x64/4] = uservia_read,
//        [0x68/4] = uservia_read,
//        [0x6c/4] = uservia_read,
//        [0x70/4] = uservia_read,
//        [0x74/4] = uservia_read,
//        [0x78/4] = uservia_read,
//        [0x7c/4] = uservia_read,
//        [0x80/4] = non_master_fdc_read,
//        [0x84/4] = non_master_fdc_read,
//        [0x88/4] = non_master_fdc_read,
//        [0x8c/4] = non_master_fdc_read,
//        [0x90/4] = non_master_fdc_read,
//        [0x94/4] = non_master_fdc_read,
//        [0x98/4] = non_master_fdc_read,
//        [0x9c/4] = non_master_fdc_read,
//#ifndef NO_USE_ADC
//        [0xc0/4] = master_only_adc_read,
//        [0xc4/4] = master_only_adc_read,
//        [0xc8/4] = master_only_adc_read,
//        [0xcc/4] = master_only_adc_read,
//        [0xd0/4] = master_only_adc_read,
//        [0xd4/4] = master_only_adc_read,
//        [0xd8/4] = master_only_adc_read,
//        [0xdc/4] = master_only_adc_read,
//#endif
//#ifndef NO_USE_TUBE
//        [0xe0/4] = tube_host_read,
//        [0xe4/4] = tube_host_read,
//        [0xe8/4] = tube_host_read,
//        [0xec/4] = tube_host_read,
//        [0xf0/4] = tube_host_read,
//        [0xf4/4] = tube_host_read,
//        [0xf8/4] = tube_host_read,
//        [0xfc/4] = tube_host_read,
//#endif
//};

uint8_t hw_read(uint16_t addr) {
    assert(addr >= 0xfc00 && addr <= 0xff00);
    if (addr >= 0xfe00) {
        uint8_t off = addr;
        switch (off >> 3) {
            case 0x00/8:
                return crtc_read(addr);
            case 0x08/8:
                return sysacia_read(addr);
            case 0x10/8:
                return serial_read(addr);
#ifndef NO_USE_MASTER
#ifndef NO_USE_ADC
            case 0x18/8:
                if (off < 0x1c) return master_only_adc_read(addr);
                breaj;
#endif
            case 0x20/8:
                if (off >= 0x24) return master_only_wd1770_read(addr);
                break;
            case 0x28/8:
                if (off < 0x2c) return master_only_wd1770_read(addr);
                break;
            case 0x30/8:
                if (off >= 0x34) return master_only_acccon(addr);
                break;
            case 0x40/8:
            case 0x48/8:
            case 0x50/8:
            case 0x58/8:
                return sysvia_read(addr);
            case 0x60/8:
            case 0x68/8:
            case 0x70/8:
            case 0x78/8:
                return uservia_read(addr);
            case 0x80/8:
            case 0x88/8:
            case 0x90/8:
            case 0x98/8:
                return non_master_fdc_read(addr);
#ifdef INCLUDE_MMFS
            case 0xdc/8:
                return mmc_read(addr);
#endif
#endif
#ifndef NO_USE_ADC
                [0xc0/4] = master_only_adc_read,
        [0xc4/4] = master_only_adc_read,
        [0xc8/4] = master_only_adc_read,
        [0xcc/4] = master_only_adc_read,
        [0xd0/4] = master_only_adc_read,
        [0xd4/4] = master_only_adc_read,
        [0xd8/4] = master_only_adc_read,
        [0xdc/4] = master_only_adc_read,
#endif
#ifndef NO_USE_TUBE
                [0xe0/4] = tube_host_read,
        [0xe4/4] = tube_host_read,
        [0xe8/4] = tube_host_read,
        [0xec/4] = tube_host_read,
        [0xf0/4] = tube_host_read,
        [0xf4/4] = tube_host_read,
        [0xf8/4] = tube_host_read,
        [0xfc/4] = tube_host_read,
#endif
        }
        return SHIELA_DEFAULT_READ;
    } else if (addr < 0xfd00) {
        switch (addr & ~3) {
#ifndef NO_USE_MUSIC5000
            case 0xFC08:
            case 0xFC0C:
                if (sound_music5000)
                    return music2000_read(addr);
                break;
#endif
#ifndef NO_USE_SID
            case 0xFC20:
            case 0xFC24:
            case 0xFC28:
            case 0xFC2C:
            case 0xFC30:
            case 0xFC34:
            case 0xFC38:
            case 0xFC3C:
                    if (sound_beebsid)
                            return sid_read(addr);
                    break;
#endif
#if !defined(NO_USE_SCSI) || !defined(NO_USE_IDE)
            case 0xFC40:
            case 0xFC44:
            case 0xFC48:
            case 0xFC4C:
            case 0xFC50:
            case 0xFC54:
            case 0xFC58:
#ifndef NO_USE_SCSI
                if (scsi_enabled)
                    return scsi_read(addr);
#endif
#ifndef NO_USE_IDE
            if (ide_enable)
                return ide_read(addr);
#endif
            break;
#endif
#ifndef NO_USE_VDFS
            case 0xFC5C:
                return vdfs_read(addr);
                break;
#endif
        }
    }
    return 0xff;
}

uint8_t __time_critical_func(MemReadFunctionFCFF)(uint16_t addr, uint32_t op) {
#ifndef NO_USE_MASTER
    if (MASTER && (acccon & 0x40) && addr >= 0xFC00) {
        return os[addr - 0xc000];
    }
#endif
    if (addr >= 0xff00) {
        return os[addr - 0xc000];
    }

    int clock_offset = clock_offsets_rw[op]&0xf;
#ifndef USE_HW_EVENT
    if (clock_offset) {
        g_cpu.clkAdjust += clock_offset;
        g_cpu.clk += clock_offset;
        polltime(clock_offset);
    }
#else
    advance_hardware(get_cpu_timestamp() + clock_offset);
#endif
    uint8_t rc = hw_read(addr);
#ifndef USE_HW_EVENT
    g_cpu.clk -= clock_offset;
#endif
    if (addr < 0xFE00 || FEslowdown[(addr >> 5) & 7]) {
        int c;
#ifdef USE_HW_EVENT
        c = get_cpu_timestamp();
#else
        c = g_cpu.clk;
#endif
        if (c & 1) {
            g_cpu.clk += 2;
        } else {
            g_cpu.clk += 1;
        }
    }
#ifdef USE_HW_EVENT
    set_next_cpu_clk();
#endif
    return rc;
}

static MemHandler *get_os_mem_handlers(bool which) {
    return which ? memHandlersOS : memHandlers;
}

void __time_critical_func(fe30_write)(uint16_t addr, uint8_t val) {
    ram_fe30 = val;
    for (int i = 32; i < 48; i++) {
        MemHandlerSetReadPtr(memHandlers + i, rom_slot_ptr(val & 15) - 0x8000);
        if (rom_slots[val & 15].swram) {
            MemHandlerSetWritePtr(memHandlers + i, (uint8_t *)rom_slot_ptr(val & 15) - 0x8000);
        } else {
            MemHandlerSetReadOnlyWritePtr(memHandlers + i, i * CPU_MEM_BLOCKSIZE);
        }
    }

//            romsel = (val & 15) << 14;
    bool ram4k = ((val & 0x80) && MASTER);
    bool ram12k = ((val & 0x80) && BPLUS);
#if THUMB_CPU_USE_ASM && BPLUS
    // this was a decision to limit scope of effect to executing from C-F only
#error right now bank A not handled by emit_pc_changed in gen_asm.rb
#endif
    cpu_memHandlers[0xA] = get_os_mem_handlers(ram12k);
    if (ram4k) {
        for (int i = 32; i < 36; i++) {
            MemHandlerSetReadPtr(memHandlers + i, ram);
            MemHandlerSetWritePtr(memHandlers + i, ram);
        }
    }
    if (ram12k) {
        for (int i = 32; i < 44; i++) {
            MemHandlerSetReadPtr(memHandlers + i, ram);
            MemHandlerSetWritePtr(memHandlers + i, ram);
        }
    }
//    printf("%d select rom %d\n", get_hardware_timestamp(), val & 15, ram4k, ram12k);

    // all the above only affects c000-e000
    memcpy(memHandlersOS + 32, memHandlers + 32, 16 * sizeof(MemHandler));
}

void __time_critical_func(fe34_write)(uint16_t addr, uint8_t val) {
    ram_fe34 = val;
    if (BPLUS) {
        acccon = val;
        bool shadow = (val & 80) != 0;
        select_vidbank(shadow);
        cpu_memHandlers[0xC] = cpu_memHandlers[0xd] = get_os_mem_handlers(shadow);
    }
    if (MASTER) {
        acccon = val;
        bool ram8k = !!(val & 8);
        bool ram20k = !!(val & 4);
        select_vidbank(!!(val & 1u));
        bool cpubank = !!(val & 2u);
        cpu_memHandlers[0xC] = cpu_memHandlers[0xd] = get_os_mem_handlers(cpubank);

        // User visibility of shadow ram
        for (int i = 12; i < 32; i++) {
            MemHandlerSetReadPtr(memHandlers + i, ram + (ram20k ? 0x8000 : 0));
            MemHandlerSetWritePtr(memHandlers + i, ram + (ram20k ? 0x8000 : 0));
        }

        if (ram8k) {
            for (int i = 48; i < 56; i++) {
                MemHandlerSetReadPtr(memHandlers + i, ram - 0x3000);
                MemHandlerSetWritePtr(memHandlers + i, ram - 0x3000);
            }
        } else {
            for (int i = 48; i < 56; i++) {
                MemHandlerSetReadPtr(memHandlers + i, os - 0xC000);
                MemHandlerSetReadOnlyWritePtr(memHandlers + i, i * CPU_MEM_BLOCKSIZE);
            }
        }
//        printf("%d select vidbank %d r8 %d r20 %d\n", get_hardware_timestamp(), !!(val & 2), ram8k, ram20k);
        memcpy(memHandlersOS + 48, memHandlers + 48, 8 * sizeof(MemHandler));
    }
};

static void (*sheila_write_funcs[])(uint16_t addr, uint8_t val) = {
        [0x00/4] = crtc_write,
        [0x04/4] = crtc_write,
#ifndef NO_USE_ACIA
        [0x08/4] = sysacia_write,
        [0x0c/4] = sysacia_write,
        [0x10/4] = serial_write,
        [0x14/4] = serial_write,
#endif
#ifndef NO_USE_MASTER
#ifndef NO_USE_ADC
        [0x18/4] = master_only_adc_write,
#endif
#endif
        [0x20/4] = videoula_write,
        [0x24/4] = master_wd1770_or_videoula_write,
#ifndef NO_USE_MASTER
        [0x28/4] = master_only_wd1770_write,
#endif
        [0x30/4] = fe30_write,
        [0x34/4] = fe34_write,
        [0x40/4] = sysvia_write,
        [0x44/4] = sysvia_write,
        [0x48/4] = sysvia_write,
        [0x4c/4] = sysvia_write,
        [0x50/4] = sysvia_write,
        [0x54/4] = sysvia_write,
        [0x58/4] = sysvia_write,
        [0x5c/4] = sysvia_write,
        [0x60/4] = uservia_write,
        [0x64/4] = uservia_write,
        [0x68/4] = uservia_write,
        [0x6c/4] = uservia_write,
        [0x70/4] = uservia_write,
        [0x74/4] = uservia_write,
        [0x78/4] = uservia_write,
        [0x7c/4] = uservia_write,
        [0x80/4] = non_master_fdc_write,
        [0x84/4] = non_master_fdc_write,
        [0x88/4] = non_master_fdc_write,
        [0x8c/4] = non_master_fdc_write,
        [0x90/4] = non_master_fdc_write,
        [0x94/4] = non_master_fdc_write,
        [0x98/4] = non_master_fdc_write,
        [0x9c/4] = non_master_fdc_write,
#ifndef NO_USE_ADC
        [0xc0/4] = master_only_adc_write,
        [0xc4/4] = master_only_adc_write,
        [0xc8/4] = master_only_adc_write,
        [0xcc/4] = master_only_adc_write,
        [0xd0/4] = master_only_adc_write,
        [0xd4/4] = master_only_adc_write,
        [0xd8/4] = master_only_adc_write,
        [0xdc/4] = master_only_adc_write,
#endif
#ifdef INCLUDE_MMFS
        [0xdc/4] = mmc_write,
#endif
#ifndef NO_USE_TUBE
        [0xe0/4] = tube_host_write,
        [0xe4/4] = tube_host_write,
        [0xe8/4] = tube_host_write,
        [0xec/4] = tube_host_write,
        [0xf0/4] = tube_host_write,
        [0xf4/4] = tube_host_write,
        [0xf8/4] = tube_host_write,
        [0xfc/4] = tube_host_write,
#endif
};

void hw_write(uint16_t addr, uint8_t val) {
    assert(addr >= 0xfc00 && addr <= 0xff00);
    if (addr >= 0xfe00) {
        uint a = (addr - 0xfe00) >> 2;
        if (a < count_of(sheila_write_funcs) && sheila_write_funcs[a])
            sheila_write_funcs[a](addr, val);
    } else if (addr < 0xfd00) {
        switch (addr & ~3) {
#ifndef NO_USE_MUSIC5000
            case 0xFC08:
            case 0xFC0C:
                if (sound_music5000)
                    music2000_write(addr, val);
                break;
#endif
#ifndef NO_USE_SID
            case 0xFC20:
            case 0xFC24:
            case 0xFC28:
            case 0xFC2C:
            case 0xFC30:
            case 0xFC34:
            case 0xFC38:
            case 0xFC3C:
                    if (sound_beebsid)
                            sid_write(addr, val);
                    break;
#endif
            case 0xFC40:
            case 0xFC44:
            case 0xFC48:
            case 0xFC4C:
            case 0xFC50:
            case 0xFC54:
            case 0xFC58:
#ifndef NO_USE_SCSI
                if (scsi_enabled) {
                    scsi_write(addr, val);
                    break;
                                 }
#endif
#ifndef NO_USE_IDE
            if (ide_enable) {
                ide_write(addr, val);
                break;
            }
#endif
            break;
#ifndef NO_USE_VDFS
            case 0xFC5C:
                vdfs_write(addr, val);
                break;
#endif
        }
    }
}

#ifndef __used
#define __used
#endif

void __time_critical_func(__used MemWriteFunctionFCFF)(uint16_t addr, uint8_t val, uint8_t op) {
#ifdef FIXME
    c = memstat[vis20k][addr >> 8];
    if (c == 1) {
        memlook[vis20k][addr >> 8][addr] = val;
        switch(addr) {
            case 0x022c:
                buf_remv = (buf_remv & 0xff00) | val;
                break;
            case 0x022d:
                buf_remv = (buf_remv & 0xff) | (val << 8);
                break;
            case 0x022e:
                buf_cnpv = (buf_cnpv & 0xff00) | val;
                break;
            case 0x022f:
                buf_cnpv = (buf_cnpv & 0xff) | (val << 8);
                break;
        }
        return;
    } else if (c == 2) {
        log_debug("6502: attempt to write to ROM %x:%04x=%02x\n", vis20k, addr, val);
        return;
    }
#endif
    if (addr >= 0xff00) {
        return;
    }

#ifndef NO_USE_MUSIC5000
#ifdef USE_HW_EVENT
#error this needs to be moved to cope with HW_EVENT
#endif
    if (sound_music5000) {
        if (addr >= 0xFCFF && addr <= 0xFDFF) {
            music5000_write(addr, val);
            return;
        }
    }
#endif

    int c;
#ifdef USE_HW_EVENT
    int t = get_cpu_timestamp();
    c = t;
#else
    c = g_cpu.clk;
#endif

    int clock_offset = clock_offsets_rw[op]>>4;
    c += clock_offset;
    // deal with read/write side effects even during say STA
    bool pre_read = op == 0x9d;
    if (pre_read) {
        if (addr < 0xFE00 || FEslowdown[(addr >> 5) & 7]) {
            // note this is backwards to usual because we assume we're a cycle earlier
            if (!(c & 1)) {
                g_cpu.clk += 2;
            } else {
                g_cpu.clk += 1;
            }
            c = 0; // now even
        }
    }
    if (addr < 0xFE00 || FEslowdown[(addr >> 5) & 7]) {
        if (c & 1) {
            c = 2;
        } else {
            c = 1;
        }
    } else {
        c = 0;
    }
    clock_offset += c;

#ifndef USE_HW_EVENT
    if (clock_offset) {
        g_cpu.clkAdjust += clock_offset;
        g_cpu.clk += clock_offset;
        polltime(clock_offset);
    }
#else
    advance_hardware(get_cpu_timestamp() + clock_offset);
#endif

    hw_write(addr, val);
#ifndef USE_HW_EVENT
    g_cpu.clk -= clock_offset;
#endif
    g_cpu.clk += c;
#ifdef USE_HW_EVENT
    set_next_cpu_clk();
#endif
}

#if defined(USE_HW_EVENT) && defined(USE_LEGACY_OTHERSTUFF_128)
static bool other_stuff_invoke(struct hw_event *event) {
//    printf("other stuff %d\n", get_cpu_timestamp());
    otherstuff_poll();
    event->target += 128;
    return true;
}
#endif

#if THUMB_CPU_USE_ASM
#define C6502_OFFSET_CLK            0
#define C6502_OFFSET_PC             4
#define C6502_OFFSET_STATUS         10

#define REG_PC                      r6
#define REG_PC_MEM_BASE             r8
#define REG_STATUS                  r9
#define REG_CLK                     r10

uint8_t __attribute__((naked)) __time_critical_func(ASMWrapMemReadFunctionFCFF)(uint16_t addr ) {
    asm ("mov r2, r12");
    asm ("push {r2, lr}");
    //asm ("strh r6, [r7, #4]");
    asm ("mov r2, r9");
    asm ("strb r2, [r7, #10]");
    asm ("mov r2, r10");
    asm ("str r2, [r7, #0]");
    // load op
    asm ("mov r2, r8");
    asm ("ldrb r1, [r2, r6]");
    asm ("bl MemReadFunctionFCFF");
    asm ("ldr r2, [r7, #0]");
    asm ("mov r10, r2");
    asm ("ldrb r2, [r7, #10]");
    asm ("mov r9, r2");
    asm ("pop {r2}");
    asm( "mov r12, r2");
    asm ("pop {pc}");
}

void __attribute__((naked)) __time_critical_func(ASMWrapMemWriteFunctionFCFF)(uint16_t addr , uint8_t val) {
    asm ("mov r2, r12");
    asm ("push {r2, lr}");
    //asm ("strh r6, [r7, #4]");
    asm ("mov r2, r9");
    asm ("strb r2, [r7, #10]");
    asm ("mov r2, r10");
    asm ("str r2, [r7, #0]");
    // load op
    asm ("mov r2, r8");
    asm ("ldrb r2, [r2, r6]");
    asm ("bl MemWriteFunctionFCFF");
    asm ("ldr r2, [r7, #0]");
    asm ("mov r10, r2");
    asm ("ldrb r2, [r7, #10]");
    asm ("mov r9, r2");
    asm ("pop {r2}");
    asm( "mov r12, r2");
    asm ("pop {pc}");
}
#endif
uint8_t CWrapMemReadFunctionFCFF(uint16_t addr ) {
    static bool recurse;
    if (!recurse) {
        recurse = true;
        int op = C6502_Read8(g_cpu.pc);
        uint8_t rc = MemReadFunctionFCFF(addr, op);
        recurse = false;
        return rc;
    } else {
        return MemReadFunctionFCFF(addr, 0);
    }
}

void CWrapMemWriteFunctionFCFF(uint16_t addr , uint8_t val) {
    // never seen a stack overflow, but don't currently handle recursion
    int op = C6502_Read8(g_cpu.pc);
    return MemWriteFunctionFCFF(addr, val, op);
}


MemHandler *MemMapInit() {
#if defined(USE_HW_EVENT) && defined(USE_LEGACY_OTHERSTUFF_128)
    // piggy back on existing initializer funk
    static struct hw_event other_stuff_event = {
        .invoke = other_stuff_invoke
    };
    other_stuff_event.target = get_cpu_timestamp() + 128;
    upsert_hw_event(&other_stuff_event);
#endif
    static_assert(CPU_MEM_BLOCKSIZE == 1024, ""); // smallest granularity we care about is 0xfc00->0xffff
    for(uint i=0;i<16;i++) {
        cpu_memHandlers[i] = memHandlers;
    }

    for(uint i=0;i<32;i++) {
        MemHandlerSetReadPtr(memHandlers + i, ram);
        MemHandlerSetWritePtr(memHandlers + i, ram);
    }
    if (MODELA) {
        for(int i=0;i<16;i++) {
            MemHandlerSetReadPtr(memHandlers + i, ram + 16384);
            MemHandlerSetWritePtr(memHandlers + i, ram + 16384);
        }
    }
    for(int i=32;i<48;i++) {
        MemHandlerSetReadPtr(memHandlers + i, rom_slot_ptr(0) - 0x8000);
        MemHandlerSetReadOnlyWritePtr( memHandlers + i, i * CPU_MEM_BLOCKSIZE);
    }

    for(int i=48;i<63;i++) {
        MemHandlerSetReadPtr(memHandlers + i, os - 0xC000);
        MemHandlerSetReadOnlyWritePtr( memHandlers + i, i * CPU_MEM_BLOCKSIZE);
    }

#if THUMB_CPU_USE_ASM
//    MemHandlerSetReadFunction(memHandlers + 63, ASMWrapMemReadFunctionFCFF);
    MemHandlerSetReadFunction(memHandlers + 63, MemReadFunctionFCFF);
    MemHandlerSetWriteFunction(memHandlers + 63, ASMWrapMemWriteFunctionFCFF);
#else
    MemHandlerSetReadFunction(memHandlers + 63, CWrapMemReadFunctionFCFF);
    MemHandlerSetWriteFunction(memHandlers + 63, CWrapMemWriteFunctionFCFF);
#endif

    // start off identical
    memcpy(memHandlersOS , memHandlers, 64 * sizeof(MemHandler));

    // Only different is in visible shadow RAM
    for (int i = 12; i < 32; i++) {
        MemHandlerSetReadPtr(memHandlersOS + i, ram + 0x8000);
        MemHandlerSetWritePtr(memHandlersOS + i, ram + 0x8000);
    }

#if THUMB_CPU_USE_ASM
    fcff_ram_mapping = os - 0xc000;
#endif
    return memHandlers;
}

void MemMapUpdatePC(uint32_t pc) {
    g_cpu.memHandlers = cpu_memHandlers[pc>>12];
}

#if THUMB_CPU_USE_ASM
#ifndef MODEL_MASTER
#define IS_C6512 false
#else
#define IS_C6512 true
#endif

// move here so we can inline clock stuff
bool __time_critical_func(CPUASMBreakout)() {
    static int oldnmi;
    //        printf("GT %d\n", get_cpu_timestamp());
    // we only need to check interrupts here because the above loop always terminates
    // when an hw_event is triggered
    // todo we need to break out when an interrupt is set in direct response to a write also!
    if (g_cpu.interrupt && !FLG_ISSET(g_cpu, i)) {
        // todo need to do the same as non ASM w.r.t. sei
//            printf("IRQ %d\n", get_cpu_timestamp());

        if (g_cpu.cli_breakout) {
            uint8_t op = C6502_Read8(g_cpu.pc);
            if (op == 0x78) {
                // do the SEI before then taking the interrupt
                FLG_SET(g_cpu, i);
                g_cpu.clk += 2;
                g_cpu.pc = (g_cpu.pc + 1);
            }
        }

//            static cycle_timestamp_t last_cpu_time;
//            static absolute_time_t last_time;
//            cycle_timestamp_t t_cpu = get_cpu_timestamp();
//            absolute_time_t  t = get_absolute_time();
//            last_time = t;
//            last_cpu_time = t_cpu;

//        g_cpu.interrupt &= ~128;
        CPUIRQ(IS_C6512);
    }
    g_cpu.interrupt &= ~128;
    advance_hardware(get_cpu_timestamp());
    if (g_cpu.nmi && !oldnmi) {
        CPUNMI(IS_C6512);
        g_cpu.nmi = 0;
        advance_hardware(get_cpu_timestamp());
    }
    _set_next_cpu_clk();
    oldnmi = g_cpu.nmi;
    return g_cpu.clk >= 0;
}
#endif
