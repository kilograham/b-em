/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#include "display.h"
#ifdef X_GUI
#include "x_gui.h"
#endif

#include "xip_stream.h"

#if PICO_NO_HARDWARE

#ifndef X_GUI
#include "SDL_scancode.h"
#endif
#include "keycode_to_allegro.h"

#else
#include <hardware/structs/pll.h>
#include <hardware/structs/clocks.h>
#include <hardware/clocks.h>
#endif

#include "pico/video/menu.h"

#ifdef USE_USB_KEYBOARD
#include "usb_host_hid.h"
#endif

#include "main.h"

#ifdef NO_USE_KEYCODE_MAP
static const uint8_t sdl_to_allegro_keycodes[232] = {
    0, 0, 0, 0, 1, 2, 3, 4,
    5, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 27,
    67, 59, 63, 64, 75, 61, 62, 65,
    66, 70, 71, 68, 69, 60, 72, 73,
    74, 226, 47, 48, 49, 50, 51, 52,
    53, 54, 55, 56, 57, 58, 92, 224,
    93, 76, 78, 80, 77, 79, 81, 83,
    82, 85, 84, 225, 86, 87, 88, 89,
    91, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 0, 60, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    217, 215, 219, 221, 218, 216, 220, 222,
};
#endif

extern "C" int _al_mangled_main(int argc, char **argv);
#if PICO_ON_DEVICE
#include "hardware/vreg.h"
#endif

void setup_clock() {
#if PICO_ON_DEVICE && !PICO_ON_FPGA
    vreg_set_voltage(VREG_VOLTAGE_1_25);
//    set_sys_clock(1176 * MHZ, 6, 1);
#if RUN_AT_360
    // needs an even faster clock atm (room for improvement in teletext rendering tho)
    set_sys_clock_khz(360000, true);
#else
    set_sys_clock_khz(270000, true);
#endif
#endif
}

#if PICO_ON_DEVICE
#include "keycode_to_allegro.h"
#endif

extern "C" int main(int argc, char **argv) {
#if X_GUI
    x_gui_init();
#endif
    setup_clock();
//    gpio_debug_pins_init();
#if 0 && !PICO_ON_DEVICE
    static uint8_t mappo[232];
    for(auto &p : sdl_to_allegro_keycodes) {
        assert(p.first < count_of(mappo));
        assert(p.second < 256);
        mappo[p.first] = p.second;
    }
    printf("static const uint8_t sdl_to_allegro_keycodes[%d] = {\n", (int)count_of(mappo));
    for(uint i = 0; i<count_of(mappo);i+=8) {
        for(int j = i; j<MIN(i+8, count_of(mappo)); j++) {
            printf("%d, ", mappo[j]);
        }
        printf("\n");
    }
    printf("};\n");
#endif

    setup_default_uart();
    xip_stream_init();
#ifdef USE_USB_KEYBOARD
#ifndef USB_SETUP_ON_CORE1
    usb_host_hid_init();
#endif
#endif
#ifdef PICO_SMPS_MODE_PIN
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
#endif
    return _al_mangled_main(argc, argv);
}

#include <allegro5/allegro.h>

#if PICO_FS_PATH

#include <filesystem>

#endif

#if PICO_NO_HARDWARE

#include <vector>

#endif

static struct ALLEGRO_CONFIG {

} _config;

struct ALLEGRO_PATH {
#if PICO_FS_PATH
    std::filesystem::path path;
#endif
};

static struct ALLEGRO_TIMER {

} _timer;

static ALLEGRO_EVENT_SOURCE _display_events;
static ALLEGRO_EVENT_SOURCE _joystick_events;
static ALLEGRO_EVENT_SOURCE _keyboard_events;
static ALLEGRO_EVENT_SOURCE _mouse_events;
static ALLEGRO_EVENT_SOURCE _timer_events;

static struct ALLEGRO_EVENT_QUEUE {

} _queue;

bool al_install_system(int version, int (*atexit_ptr)(void (*)(void))) {
#if PICO_NO_HARDWARE
    sdl_to_allegro_init();
#endif
    return true;
}

ALLEGRO_EVENT_QUEUE *al_create_event_queue(void) {
    return &_queue;
}

ALLEGRO_EVENT_SOURCE *al_get_display_event_source(ALLEGRO_DISPLAY *display) {
    return &_display_events;
}

ALLEGRO_PATH *al_get_standard_path(int id) {
    return NULL;
}

ALLEGRO_PATH *al_create_path_for_directory(const char *str) {
    ALLEGRO_PATH *path = new ALLEGRO_PATH();
#if PICO_FS_PATH
    path->path = str;
    path->path += "/";
#endif
    return path;
}

ALLEGRO_PATH *al_create_path(const char *str) {
    ALLEGRO_PATH *path = new ALLEGRO_PATH();
#if PICO_FS_PATH
    path->path = str;
#endif
    return path;
}

void al_append_path_component(ALLEGRO_PATH *path, const char *s) {
#if PICO_FS_PATH
    if (path)
        path->path /= s;
#endif
}

void al_set_path_filename(ALLEGRO_PATH *path, const char *filename) {
#if PICO_FS_PATH
    path->path.replace_filename(filename);
#endif
}

bool al_set_path_extension(ALLEGRO_PATH *path, char const *extension) {
#if PICO_FS_PATH
    path->path.replace_extension(extension);
#endif
    return true;
}

bool al_join_paths(ALLEGRO_PATH *path, const ALLEGRO_PATH *tail) {
#if PICO_FS_PATH
    if (tail->path.is_absolute()) {
        return false;
    }
    path->path.append(tail->path.c_str());
#endif
    return true;
}

const char *al_get_path_filename(const ALLEGRO_PATH *path) {
#if PICO_FS_PATH
    return path->path.filename().c_str();
#else
    return NULL;
#endif
}

const char *al_path_cstr(const ALLEGRO_PATH *path, char delim) {
#if PICO_FS_PATH
    return path->path.c_str();
#else
    return NULL;
#endif
}

const char *al_get_path_extension(const ALLEGRO_PATH *path) {
#if PICO_FS_PATH
    return path->path.extension().c_str();
#else
    return NULL;
#endif
}

void al_destroy_path(ALLEGRO_PATH *path) {
    delete path;
}

#ifndef GAME_ROOT
#ifdef __APPLE__
#define GAME_ROOT "/Users/graham/dev/b-em/games"
#else
#define GAME_ROOT "/home/graham/dev/b-em/games"
#endif
#endif

struct {
    const char *key;
    const char *value;
} config_values[] = {
#ifdef MODEL_MASTER
        {"name", "BBC Master 128"},
        {"fdc", "master"},
        {"65c02", "true"},
        {"b+", "false"},
        {"master", "true"},
        {"modela", "false"},
        {"os01", "false"},
        {"compact", "false"},
        {"os", "mos320"},
        {"cmos", "cmos"},
        {"tube", "none"},
        {"romsetup", "master"},
#ifdef INCLUDE_MMFS
        { "rom08", "mmfs" },
#endif
//        { "rom08", "vdfs" },
//        {"disc0", GAME_ROOT "/bs-badappl.dsd"},
//        {"disc0", GAME_ROOT "/bs-wave-runner-v1-1.ssd"},
//        { "disc0", GAME_ROOT "/bs-twisted.ssd"},
//        { "disc0", GAME_ROOT "/crtc-somenastyeffects.ssd"},
//        {"disc0", GAME_ROOT "/bs-bbcnula1.ssd"},
//        { "disc0", GAME_ROOT "/bs-beeb-niccc.dsd"}, // todo hmm weirdness in loader... when did that regress?
//        { "disc0", GAME_ROOT "/stnicc-A.dsd"},
//        { "disc0", GAME_ROOT "/bs-patarty.ssd"},
//        { "disc0", GAME_ROOT "/beebstep.ssd"},
//        { "disc0", GAME_ROOT "/bs-scr-beeb.ssd" },
//        { "disc0", GAME_ROOT "/interlace.ssd"},
//        { "disc0", GAME_ROOT "/Disc999-PrinceOfPersia.ssd"},
//        { "disc0", GAME_ROOT "/TheMaster-Demo128.ssd" },
//        { "disc0", GAME_ROOT "/mode8l-video.dsd" },
#else
        {"name","BBC B w/1770 FDC"},
        {"fdc","acorn"},
        { "os", "os12"},
        { "tube", "none"},
        { "romsetup", "swram"},
        { "rom15", "basic2" },
        { "rom14", "dfs226" },
#endif
#if PICO_NO_HARDWARE
//        { "disc0", GAME_ROOT "/eng_test.ssd"},
//        { "disc0", GAME_ROOT "/Disc040-ExileR.ssd"},
//        { "disc0", GAME_ROOT "/Disc007-JetPac.ssd"},
//        { "disc0", GAME_ROOT "/Disc036-Arkanoid.ssd" },
//        { "disc0", GAME_ROOT "/Disc011-SabreWulf.ssd"},
//        { "disc0", GAME_ROOT "/160x128.ssd" },
//        { "disc0", GAME_ROOT "/Chuckie Egg (1983)(A&F)[a2][CHUCKIE start].ssd" },
//        { "disc0", GAME_ROOT "/Disc006-Frak.ssd" },
//        { "disc0", GAME_ROOT "/Disc014-CastleQuest.ssd" },
//        {"disc0", GAME_ROOT "/Disc007-JetPac.ssd" },
//        {"disc0", GAME_ROOT "/Disc005-Fortress.ssd" },
//        {"disc0", GAME_ROOT "/Disc011-SabreWulf.ssd" },
//       {"disc0", GAME_ROOT "/Disc012-KnightLore.ssd" },

//       {"disc0", GAME_ROOT "/Disc006-PolePosition.ssd" },
//        { "disc0", GAME_ROOT "/Disc015-Revs.ssd" },
//        { "disc0", GAME_ROOT "/Disc017-Citadel.ssd" },
//        { "disc0", GAME_ROOT "/Disc108-FroggerRSCB.ssd" },
//        { "disc0", GAME_ROOT "/Disc024-Thrust.ssd" },
//        { "disc0", GAME_ROOT "/Disc036-EmpireStrikesBack.ssd" },
//        { "disc0", GAME_ROOT "/Disc025-JetSetWilly.ssd" },
//        { "disc0", GAME_ROOT "/Disc015-ReptonP.ssd" },
//        { "disc0", GAME_ROOT "/Disc034-SentinelP.ssd" },
//        { "disc0", GAME_ROOT "/Disc001-KillerGorilla.ssd"},
//        { "disc0", GAME_ROOT "/Disc016-Boffin.ssd"},
//        { "disc0", GAME_ROOT "/Disc031-UridiumCB.ssd"},
//        { "disc0", GAME_ROOT "/Disc024-Psycastria.ssd"},
//        { "disc0", GAME_ROOT "/Disc999-Colourspace.ssd"},
//        { "disc0", GAME_ROOT "/Disc999-WhiteLight10DFS.ssd" },
//        { "disc0", GAME_ROOT "/Disc110-2048.ssd" },
//        { "disc0", GAME_ROOT "/Elite (1984)(Acornsoft)[ELITE start].ssd" },
//        { "disc0", GAME_ROOT "/Disc042-SphereOfDestiny2FabFour.ssd"},
//        { "disc0", GAME_ROOT "/Fire Track (1987)(Aardvark)[h8].ssd" }, // todo regressed on menu screen - this seems to have broken as a result of making unused slots unwritable!
//        { "disc0", GAME_ROOT "/Knight Lore (19xx)(Ultimate)[h TSTH][bootfile].ssd" },
//        { "disc0", GAME_ROOT "/Pole Position (1984)(Atari)[cr Piratesoft][a][POLE start].ssd" },
//        {"disc0", GAME_ROOT "/Rocket Raid (1982)(Acornsoft)[bootfile].ssd"},
//        { "disc0", GAME_ROOT "/Zalaga (1983)(Aardvark)[h TSTH][t +2].ssd" },
//        { "disc0", GAME_ROOT "/circle.ssd" },
//        { "disc0", GAME_ROOT "/decimal.ssd" },
//        { "disc0", GAME_ROOT "/interlaced_animation.ssd" },
//        { "disc0", GAME_ROOT "/mode7-smooth.ssd" }, // todo this has regressed with HC_PRIORITY; weird this works when not debugging; go figure!
//        { "disc0", GAME_ROOT "/mode7-smooth-blind.ssd" },
//        { "disc0", GAME_ROOT "/palline.ssd" }, // todo timing off by a couple of cycles
//        { "disc0", GAME_ROOT "/palline2.ssd" },
//        { "disc0", GAME_ROOT "/palline3.ssd" }, // todo why is sound stuck on?
//        { "disc0", GAME_ROOT "/raster-fx.ssd" },  // todo why is sound stuck on?
//        {"disc0", GAME_ROOT "/ScrambleRC5.ssd"},
#endif
};

ALLEGRO_CONFIG *al_create_config(void) {
    return &_config; // todo for now;
}

ALLEGRO_CONFIG *al_load_config_file(const char *filename) {
    return &_config;
}

bool al_save_config_file(const char *filename, const ALLEGRO_CONFIG *config) {
    return false;
}

char const *al_get_first_config_section(ALLEGRO_CONFIG const *config, ALLEGRO_CONFIG_SECTION **iterator) {
    return "model_00";
}


char const *al_get_next_config_section(ALLEGRO_CONFIG_SECTION **iterator) {
    return NULL;
}

const char *al_get_config_value(const ALLEGRO_CONFIG *config, const char *section, const char *key) {
//    printf("al_get_config_value %s %s\n", section, key);

    if (!section) {
        if (!strcmp(key, "model")) return "0";
    }
    for (uint i = 0; i < count_of(config_values); i++) {
        if (!strcmp(config_values[i].key, key))
            return config_values[i].value;
    }
    return NULL;
}

void al_set_config_value(ALLEGRO_CONFIG *config, const char *section, const char *key, const char *value) {
//    printf("al_set_config_value %s %s %s\n", section, key, value);
};

bool al_remove_config_key(ALLEGRO_CONFIG *config, char const *section, char const *key) {
//    printf("al_remove_config_value %s %s\n", section, key);
    return false;
}

void al_destroy_config(ALLEGRO_CONFIG *config) {

}

void al_register_event_source(ALLEGRO_EVENT_QUEUE *, ALLEGRO_EVENT_SOURCE *) {

}

ALLEGRO_TIMER *al_create_timer(double speed_secs) {
    return &_timer;
}

ALLEGRO_EVENT_SOURCE *al_get_timer_event_source(ALLEGRO_TIMER *timer) {
    return &_timer_events;
}

void al_init_user_event_source(ALLEGRO_EVENT_SOURCE *) {

}

bool al_install_keyboard(void) {
    return true;
}

ALLEGRO_EVENT_SOURCE *al_get_keyboard_event_source(void) {
    return &_keyboard_events;
}

bool al_install_joystick() {
    return false;
}

int al_get_num_joysticks(void) {
    return 0;
}

ALLEGRO_JOYSTICK *al_get_joystick(int joyn) {
    return NULL;
}

const char *al_get_joystick_name(ALLEGRO_JOYSTICK *joystick) {
    return "Norma";
}

int al_get_joystick_num_sticks(ALLEGRO_JOYSTICK *joystick) {
    return 0;
}

int al_get_joystick_num_buttons(ALLEGRO_JOYSTICK *joystick) {
    return 0;
}

int al_get_joystick_num_axes(ALLEGRO_JOYSTICK *joystick, int stick) {
    return 2;
}

ALLEGRO_EVENT_SOURCE *al_get_joystick_event_source(void) {
    return &_joystick_events;
}

bool al_install_mouse(void) {
    return true;
}

ALLEGRO_EVENT_SOURCE *al_get_mouse_event_source(void) {
    return &_mouse_events;
}

void al_start_timer(ALLEGRO_TIMER *timer) {

}

void al_set_timer_speed(ALLEGRO_TIMER *timer, double speed_secs) {

}

void al_stop_timer(ALLEGRO_TIMER *timer) {

}

void al_get_keyboard_state(ALLEGRO_KEYBOARD_STATE *ret_state) {

}

bool al_key_down(const ALLEGRO_KEYBOARD_STATE *, int keycode) {
    return false;
}

static uint32_t ticks;

double al_get_time(void) {
    //return ticks/50.0;
    return 0.0;
}

absolute_time_t tick_next;

#ifdef PICO_NO_HARDWARE
bool slowdown_flag;
int slowdown_delay = 30;
#endif

#ifdef HACK_KEY_PASTE
static int xcount;
static int spos = -1;
static int sequence[] = {
        SDL_SCANCODE_LSHIFT,
        SDL_SCANCODE_APOSTROPHE,
        -SDL_SCANCODE_APOSTROPHE,
        -SDL_SCANCODE_LSHIFT,
        SDL_SCANCODE_E,
        -SDL_SCANCODE_E,
        SDL_SCANCODE_D,
        -SDL_SCANCODE_D,
        SDL_SCANCODE_I,
        -SDL_SCANCODE_I,
        SDL_SCANCODE_T,
        -SDL_SCANCODE_T,
        SDL_SCANCODE_RETURN,
        -SDL_SCANCODE_RETURN,
};
#endif

extern "C" void main_key_down(ALLEGRO_EVENT *event);
extern "C" void main_key_up(ALLEGRO_EVENT *event);

#if PIXELATED_PAUSE
enum pixelated_pause_state cpu_pps;
static inline bool pause_events() {
    return cpu_pps != 0;
}
#else
static inline bool pause_events() {
    return false;
}
#endif
void handle_scancode(bool down, uint scancode) {
#ifdef NO_USE_KEYCODE_MAP
    if (scancode >= count_of(sdl_to_allegro_keycodes)) return;
#endif
    ALLEGRO_EVENT event;
    if (down) {
#if DISPLAY_MENU
        if (!handle_menu_key(scancode, true) && !pause_events())
#endif
        {
            event.keyboard.keycode = sdl_to_allegro_keycodes[scancode];
            event.any.timestamp = al_get_time();
            event.any.type = ALLEGRO_EVENT_KEY_DOWN;
            main_key_down(&event);
        }
    } else {
#if DISPLAY_MENU
        if (!handle_menu_key(scancode, false) && !pause_events())
#endif
        {
            event.keyboard.keycode = sdl_to_allegro_keycodes[scancode];
            event.any.timestamp = al_get_time();
            event.any.type = ALLEGRO_EVENT_KEY_UP;
            main_key_up(&event);
        }
    }
}

void al_wait_for_event(ALLEGRO_EVENT_QUEUE *,
                       ALLEGRO_EVENT *ret_event) {
#if DISPLAY_MENU
    menu_cpu_blanking();
#endif
#ifdef HACK_KEY_PASTE
    xcount++;
    if (xcount > 20) {
        int pos = (xcount - 50) / 3;
        if (pos > spos && pos < count_of(sequence)) {
            spos = pos;
            int scancode = sequence[spos];
            if (scancode < 0) {
                scancode = -scancode;
                ret_event->any.type = ALLEGRO_EVENT_KEY_UP;
            } else {
                ret_event->any.type = ALLEGRO_EVENT_KEY_DOWN;
            }
            ret_event->keyboard.keycode = sdl_to_allegro_keycodes[scancode];
            ret_event->any.timestamp = al_get_time();
            return;
        }
    }
#endif
#if PICO_NO_HARDWARE
    if (get_gui_key_event(ret_event)) {
        bool handled = false;
        if (ret_event->keyboard.type == ALLEGRO_EVENT_KEY_DOWN) {
            handled = true;
            switch (ret_event->keyboard.keycode) {
                case ALLEGRO_KEY_PAD_ENTER:
                    slowdown_flag ^= 1;
                    break;
                case ALLEGRO_KEY_PAD_PLUS:
                    if (slowdown_delay > 2)
                        slowdown_delay--;
                    break;
                case ALLEGRO_KEY_PAD_MINUS:
                    slowdown_delay++;
                    break;
                default:
                    handled = false;
                    break;
            }
        }
        if (!handled) return;
    }





#else
#ifdef USE_USB_KEYBOARD
    usb_host_hid_poll();
    struct kb_event kbe;
    while (get_kb_event(&kbe)) {
        handle_scancode(kbe.down, kbe.scancode);
    }
#endif
#define KEY_TIMEOUT 50
    if (uart_is_readable(uart_default)) {
        char c = uart_getc(uart_default);
        if (c == 26 && uart_is_readable_within_us(uart_default, KEY_TIMEOUT)) {
            c = uart_getc(uart_default);
            switch (c) {
                case 0:
                    if (uart_is_readable_within_us(uart_default, KEY_TIMEOUT)) {
                        uint scancode = (uint8_t)uart_getc(uart_default);
                        handle_scancode(true, scancode);
                    }
                    return;
                case 1:
                    if (uart_is_readable_within_us(uart_default, KEY_TIMEOUT)) {
                        uint scancode = (uint8_t)uart_getc(uart_default);
                        handle_scancode(false, scancode);
                    }
                    return;
                case 2:
                case 3:
                case 5:
                    if (uart_is_readable_within_us(uart_default, KEY_TIMEOUT)) {
                        uint __unused scancode = (uint8_t)uart_getc(uart_default);
                    }
                    return;
                case 4:
                    if (uart_is_readable_within_us(uart_default, KEY_TIMEOUT)) {
                        uint __unused scancode = (uint8_t)uart_getc(uart_default);
                    }
                    if (uart_is_readable_within_us(uart_default, KEY_TIMEOUT)) {
                        uint __unused scancode = (uint8_t)uart_getc(uart_default);
                    }
                    return;
            }
        }
    }
#endif

    if (!pause_events()) {
#if !PICO_ON_DEVICE
    //    sleep_until(tick_next); // we just sleep a bit in case, but the time should have passed
        absolute_time_t atime = get_absolute_time();
        if (is_nil_time(tick_next)) {
            tick_next = atime;
        }

        int64_t delta = absolute_time_diff_us(tick_next, atime);

        // if we're passed the time
        if (delta < -20000) {
            tick_next = atime;
        }
        //printf("%ld %ld\n", to_us_since_boot(get_absolute_time()), to_us_since_boot(tick_next));
        tick_next = delayed_by_us(tick_next, 20000);// * (1 + slowdown_flag * slowdown_delay));
#if X_GUI
        if (xgui_paused) {
            ret_event->any.type = 0;
            x_gui_refresh_menu_display(); // needed because we aren't sending video scanlines
            sleep_until(tick_next);
            return;
        } else if (x_gui_audio_init_failed) {
            // not paced by audio, so have to sleep
            sleep_until(tick_next);
        }
#endif
#ifdef NO_USE_SOUND
        sleep_until(tick_next);
#endif
#endif
        ticks++;
    }

    ret_event->any.source = &_timer_events;
    ret_event->any.type = ALLEGRO_EVENT_TIMER;
    ret_event->any.timestamp = al_get_time();
}

bool al_emit_user_event(ALLEGRO_EVENT_SOURCE *, ALLEGRO_EVENT *,
                        void (*dtor)(ALLEGRO_USER_EVENT *)) {
    return true;
}

size_t al_ustr_append_chr(ALLEGRO_USTR *us, int32_t c) {
    assert(false);
    return 0;
}

void setejecttext(int drive, const char *fn) {}

ALLEGRO_PATH *find_dat_file(ALLEGRO_PATH *dir, const char *name, const char *ext)
{
//    ALLEGRO_PATH *path;
//    const char *var;
//    char *cpy, *ptr, *sep;
//
//    if ((path = al_get_standard_path(ALLEGRO_RESOURCES_PATH))) {
//        al_join_paths(path, dir);
//        if (try_file(path, name, ext))
//            return path;
//        al_destroy_path(path);
//    }
//    if ((var = getenv("XDG_DATA_HOME"))) {
//        if ((path = al_create_path_for_directory(var))) {
//            if (try_dat_file(path, dir, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
//    if ((var = getenv("HOME"))) {
//        if ((path = al_create_path_for_directory(var))) {
//            al_append_path_component(path, ".local");
//            al_append_path_component(path, "share");
//            if (try_dat_file(path, dir, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
//    if ((var = getenv("XDG_DATA_DIRS")) == NULL)
//        var = "/usr/local/share:/usr/share";
//    if ((cpy = strdup(var))) {
//        for (ptr = cpy; (sep = strchr(ptr, ':')); ptr = sep) {
//            *sep++ = '\0';
//            if ((path = al_create_path_for_directory(ptr))) {
//                if (try_dat_file(path, dir, name, ext)) {
//                    free(cpy);
//                    return path;
//                }
//                al_destroy_path(path);
//            }
//        }
//        path = al_create_path_for_directory(ptr);
//        free(cpy);
//        if (path) {
//            if (try_dat_file(path, dir, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
    assert(false);
    return NULL;
}

//static bool try_cfg_file(ALLEGRO_PATH *path, const char *name, const char *ext)
//{
//    al_append_path_component(path, "b-em");
//    return try_file(path, name, ext);
//}

ALLEGRO_PATH *find_cfg_file(const char *name, const char *ext) {
    return NULL;
//    ALLEGRO_PATH *path;
//    const char *var;
//    char *cpy, *ptr, *sep;
//
//    if ((var = getenv("XDG_CONFIG_HOME"))) {
//        if ((path = al_create_path_for_directory(var))) {
//            if (try_cfg_file(path, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
//    if ((var = getenv("HOME"))) {
//        if ((path = al_create_path_for_directory(var))) {
//            al_append_path_component(path, ".config");
//            if (try_cfg_file(path, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
//    if ((var = getenv("XDG_CONFIG_DIRS"))) {
//        if ((cpy = strdup(var))) {
//            for (ptr = cpy; (sep = strchr(ptr, ':')); ptr = sep) {
//                *sep++ = '\0';
//                if ((path = al_create_path_for_directory(ptr))) {
//                    if (try_cfg_file(path, name, ext)) {
//                        free(cpy);
//                        return path;
//                    }
//                    al_destroy_path(path);
//                }
//            }
//            path = al_create_path_for_directory(ptr);
//            free(cpy);
//            if (path) {
//                if (try_cfg_file(path, name, ext))
//                    return path;
//                al_destroy_path(path);
//            }
//        }
//    }
//    if ((path = al_get_standard_path(ALLEGRO_RESOURCES_PATH))) {
//        if (try_file(path, name, ext))
//            return path;
//        al_destroy_path(path);
//    }
//    if ((var = getenv("XDG_DATA_HOME"))) {
//        al_append_path_component(path, ".config");
//        if ((path = al_create_path_for_directory(var))) {
//            if (try_cfg_file(path, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
//    if ((var = getenv("XDG_DATA_DIRS")) == NULL)
//        var = "/usr/local/share:/usr/share";
//    if ((cpy = strdup(var))) {
//        for (ptr = cpy; (sep = strchr(ptr, ':')); ptr = sep) {
//            *sep++ = '\0';
//            if ((path = al_create_path_for_directory(ptr))) {
//                if (try_cfg_file(path, name, ext)) {
//                    free(cpy);
//                    return path;
//                }
//                al_destroy_path(path);
//            }
//        }
//        path = al_create_path_for_directory(ptr);
//        free(cpy);
//        if (path) {
//            if (try_cfg_file(path, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
}

//static bool try_cfg_dest(ALLEGRO_PATH *path, const char *name, const char *ext)
//{
//    const char *cpath;
//    struct stat stb;
//
//    al_append_path_component(path, "b-em");
//    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
//    log_debug("linux: trying cfg dest dir %s", cpath);
//    if (stat(cpath, &stb) == 0) {
//        if ((stb.st_mode & S_IFMT) == S_IFDIR) {
//            al_set_path_filename(path, name);
//            al_set_path_extension(path, ext);
//            return true;
//        }
//    }
//    else if (errno == ENOENT) {
//        if (mkdir(cpath, 0777) == 0) {
//            al_set_path_filename(path, name);
//            al_set_path_extension(path, ext);
//            return true;
//        }
//    }
//    return false;
//}

ALLEGRO_PATH *find_cfg_dest(const char *name, const char *ext)
{
//    ALLEGRO_PATH *path;
//    const char *var;
//
//    if ((var = getenv("XDG_CONFIG_HOME"))) {
//        if ((path = al_create_path_for_directory(var))) {
//            if (try_cfg_dest(path, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
//    if ((var = getenv("HOME"))) {
//        if ((path = al_create_path_for_directory(var))) {
//            al_append_path_component(path, ".config");
//            if (try_cfg_dest(path, name, ext))
//                return path;
//            al_destroy_path(path);
//        }
//    }
    //assert(false);
    return NULL;
}
