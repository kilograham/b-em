/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include "font.h"

#if PICO_NO_HARDWARE
#include "keycodes.h"
#else
#include "allegro5/keyboard.h"
#endif
#ifndef X_GUI
#include "pico/scanvideo/composable_scanline.h"
#include "pico/scanvideo.h"
#else

#include "x_gui.h"

#endif

#include "hardware/gpio.h"
#include "menu.h"
#include <sn76489.h>
#include "main.h"
#include "display.h"
#include "disc.h"
#include "discs/discs.h"

CU_REGISTER_DEBUG_PINS(menu)
//CU_SELECT_DEBUG_PINS(menu)

#if DISPLAY_MENU
static uint16_t menu_text_color;
static uint16_t menu_highlight_bg_color;
static uint16_t menu_highlight_fg_color;
static uint16_t menu_highlight_fg_color2;
static uint16_t menu_disabled_fg_color;

struct menu_state menu_state;
#ifndef NO_USE_CMD_LINE
#include <stdio.h>
embedded_disc_t cmd_line_disc;
#endif
static void update_menu_visuals();

static const char *vols[] = {"Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};


static void sound_volume_handler(struct option *option, enum option_cmd cmd, int param);
static void disc_volume_handler(struct option *option, enum option_cmd cmd, int param);
static void force_tv_comma1_handler(struct option *option, enum option_cmd cmd, int param);
//static void screen_adjust_handler(struct option *option, enum option_cmd cmd, int param);
static void vpos_adjust_handler(struct option *option, enum option_cmd cmd, int param);
static void reset_handler(struct option *option, enum option_cmd cmd, int param);
static void quit_handler(struct option *option, enum option_cmd cmd, int param);
#if X_GUI
static void aspect_ratio_handler(struct option *option, enum option_cmd cmd, int param);
bool xgui_paused;
#endif
#if X_GUI || PIXELATED_PAUSE
static void pause_handler(struct option *option, enum option_cmd cmd, int param);
#endif
#ifdef VGABOARD_BUTTON_A_PIN
//#define SMPS_MENU_OPTION 1
#endif
#ifdef SMPS_MENU_OPTION
static void smps_mode_handler(struct option *option, enum option_cmd cmd, int param);
#endif
#if ENABLE_FRAME_SKIP
static void frame_skip_handler(struct option *option, enum option_cmd cmd, int param);
#endif
#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
static void widescreen_handler(struct option *option, enum option_cmd cmd, int param);
#endif

struct option options[] = {
        {
                .handler = sound_volume_handler,
                .txt = "Sound Volume",
                .value = 5,
        },
        {
                .handler = disc_volume_handler,
                .txt = "Disc Volume",
                .value = 5,
        },
#ifdef USE_SECTOR_READ
        {
                .handler = disc0_handler,
                .txt = "Disc 0",
                .value = 0,
        },
#endif
        {
                .handler = force_tv_comma1_handler,
                .txt = "Force *TV -, 1",
                .value = 1,
                .is_action = false,
                .is_enter_option = true,
        },
        {
                .handler = vpos_adjust_handler,
                .txt = "VPos Adjust",
                .value = 0,
                .value_string = "0",
        },
#ifdef MODE_1280
#if 0
        {
            .handler = screen_adjust_handler,
            .txt = "Show Border",
            .is_action = true,
        },
#endif
#endif
#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
        {
                .handler = widescreen_handler,
                .txt = "Widescreen",
                .value = 1,
        },
#endif
#if ENABLE_FRAME_SKIP
        {
                .handler = frame_skip_handler,
                .txt = "Skip Frames",
                .value = -1,
        },
#endif
#if SMPS_MENU_OPTION
        {
                .handler = smps_mode_handler,
                .txt = "SMPS mode",
                .value = 1,
                .is_action = false,
                .is_enter_option = true
        },
#endif
#if X_GUI
        {
                .handler = aspect_ratio_handler,
                .txt = "Force Aspect Ratio",
                .value = 1,
        },
#endif
#if X_GUI || PIXELATED_PAUSE
        {
                .handler = pause_handler,
                .txt = "Pause",
                .is_action = false,
                .is_enter_option = true,
        },
#endif
        {
                .handler = reset_handler,
                .txt = "Hard Reset",
                .is_action = true,
        },
        {
                .handler = quit_handler,
                .txt = "Quit",
                .is_action = true,
        },
};

extern bool force_tv_comma_1;
#if X_GUI || PIXELATED_PAUSE
int pause_number;
#endif

static const char *onoff_str(bool v) {
    return v ? "On" : "Off";
}

bool do_reset;

#ifdef USE_SECTOR_READ
static int8_t disc0_number;

static inline const embedded_disc_t *get_embedded_disc(int option_value) {
    const embedded_disc_t *ed;
#ifdef NO_USE_CMD_LINE
    ed = embedded_discs + option_value;
#else
    ed = option_value < 0 ? &cmd_line_disc : (embedded_discs + option_value);
#endif
    return ed;
}

void disc0_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_UPDATE) {
        disc0_number = option - options;
        if (!option->change_pending) {
            option->old_value = option->value;
        }
        int min;
#ifdef NO_USE_CMD_LINE
        min = 0;
#else
        min = cmd_line_disc.data_size ? -1 : 0;
#endif
        option->value += param;
        if (option->value < min) option->value = embedded_disc_count - 1;
        if (option->value >= embedded_disc_count) option->value = min;
        option->change_pending = option->value != option->old_value;
    } else if (cmd == OPTION_INIT_COMPLETE || cmd == OPTION_EXECUTE) {
        if (cmd == OPTION_INIT_COMPLETE) {
            option->value = embedded_disc_default;
        }
        option->change_pending = false;
        disc_close(0);
        const embedded_disc_t *ed = get_embedded_disc(option->value);
        sdf_load_image_memory(0, ed->geometry, ed->data, ed->data_size);
    } else if (cmd == OPTION_CANCEL) {
        option->change_pending = false;
        option->value = option->old_value;
    }
    option->value_string = get_embedded_disc(option->value)->name;
}

#ifndef NO_USE_CMD_LINE
void select_cmd_line_disc() {
    if (disc0_number) {
        struct option *o = options + disc0_number;
        o->handler(o, OPTION_UPDATE, -1 - o->value);
        o->handler(o, OPTION_EXECUTE, 0);
    }
}
#endif
#endif

static void reset_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_EXECUTE) {
#if X_GUI
        if (xgui_paused && pause_number) {
            struct option *o = options + pause_number;
            o->handler(o, OPTION_UPDATE, 1);
        }
#endif
#if PIXELATED_PAUSE
        set_pixelated_pause(false);
        if (pause_number) {
            struct option *o = options + pause_number;
            if (o->value) {
                o->handler(o, OPTION_UPDATE, 1);
            }
        }
#endif
        do_reset = true;
    }
}

static void quit_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_EXECUTE) {
        exit(0);
    }
}

//static void screen_adjust_handler(struct option *option, enum option_cmd cmd, int param) {
//    extern uint8_t border_counter;
//    if (cmd == OPTION_EXECUTE) {
//        border_counter = 200;
//    }
//}

static void force_tv_comma1_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_UPDATE || cmd == OPTION_EXECUTE) {
        if (param || cmd == OPTION_EXECUTE) {
            option->value ^= 1;
        }
        force_tv_comma_1 = option->value;
        option->value_string = onoff_str(option->value);
    }
}
#if X_GUI

static void aspect_ratio_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_UPDATE) {
        if (param) {
            option->value ^= 1;
        }
        x_gui_set_force_aspect_ratio(option->value);
        option->value_string = onoff_str(option->value);
    }
}

#endif

#if X_GUI || PIXELATED_PAUSE
static void pause_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_UPDATE || cmd == OPTION_EXECUTE) {
        pause_number = option - options;
        if (param || cmd == OPTION_EXECUTE) {
            option->value ^= 1;
        }
#ifdef X_GUI
        xgui_paused = option->value;
#else
        set_pixelated_pause(option->value);
#endif
        option->value_string = onoff_str(option->value);
    }
}

#endif

#if defined(MODE_1080p) && defined(WIDESCREEN_OPTION)
static uint8_t widescreen_number;
static void widescreen_handler(struct option *option, enum option_cmd cmd, int param) {
    widescreen_number = option - options;
    if (cmd == OPTION_UPDATE || cmd == OPTION_INIT_COMPLETE) {
        if (param) {
            option->value ^= 1;
        }
        option->value_string = onoff_str(option->value);
        set_widescreen(option->value);
    }
}

void reflect_widescreen(bool widescreen) {
    if (widescreen_number > 0) {
        struct option *o = options + widescreen_number;
        if (widescreen != o->value) {
            o->value = widescreen;
            menu_state.do_fill_menu = true;
        }
    }
}
#endif

#if ENABLE_FRAME_SKIP
static uint8_t frame_skip_number;
static void frame_skip_handler(struct option *option, enum option_cmd cmd, int param) {
    frame_skip_number = option - options;
    if (cmd == OPTION_UPDATE) {
        set_frame_skip_count(option->value + param);
    }
}

void reflect_frame_skip_count(int8_t value) {
    if (frame_skip_number > 0) {
        struct option *o = options + frame_skip_number;
        if (o->value != value) {
            static char buf[8];
            if (value == 0) {
                strcpy(buf, onoff_str(false));
            } else {
                sprintf(buf, "%d", value);
            }
            o->value_string = buf;
            menu_state.do_fill_menu = true;
            o->value = value;
        }
    }
}
#endif
static uint8_t vpos_number = -1;

void reflect_vpos_offset(int8_t value) {
    if (vpos_number >= 0) {
        struct option *o = options + vpos_number;
        if (o->value != value) {
            static char buf[8];
            sprintf(buf, "%d", value);
            o->value_string = buf;
            menu_state.do_fill_menu = true;
            o->value = value;
        }
    }
}

static void vpos_adjust_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_INIT_COMPLETE) {
        vpos_number = option - options;
        param = -option->value;
        cmd = OPTION_UPDATE;
    }
    if (cmd == OPTION_UPDATE) {
        set_vpos_offset(option->value + param);
    }
}

#ifdef SMPS_MENU_OPTION

static void smps_mode_handler(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_UPDATE || cmd == OPTION_EXECUTE) {
        if (param || cmd == OPTION_EXECUTE) {
            option->value ^= 1;
        }
        option->value_string = onoff_str(option->value);
        gpio_put(23, option->value);
    }
}
#endif

static void volume_handler_common(struct option *option, enum option_cmd cmd, int param) {
    if (cmd == OPTION_UPDATE) {
        option->value += param;
        if (option->value < 0) option->value += count_of(vols);
        else if (option->value >= count_of(vols)) option->value = 0;
        option->value_string = vols[option->value];
    }
}

static void sound_volume_handler(struct option *option, enum option_cmd cmd, int param) {
    volume_handler_common(option, cmd, param);
    sn_setvolume(option->value * 25);
}

static void disc_volume_handler(struct option *option, enum option_cmd cmd, int param) {
    volume_handler_common(option, cmd, param);
    set_disc_volume(option->value);
}


static bool option_next, option_complete;
static struct option *option_needed;

//#define __maybe_in_ram __not_in_flash("menu")
#define __maybe_in_ram

#define ELLIPSIS_WIDTH 9

void __maybe_in_ram measure_text(struct text_element *element, int max) {
    if (!element->width) {
        element->width = 0;
        element->ellipsis_pos = 0;
        uint elipsis_width = 0;
        if (element->text) {
            const char *p = element->text;
            uint8_t c;
            while ((c = *p++)) {
                c -= MENU_GLYPH_MIN;
                if (c < MENU_GLYPH_MAX) {
                    element->width += menu_glyph_widths[c] + MENU_GLYPH_ADVANCE;
                }
                if (element->width <= max - ELLIPSIS_WIDTH) {
                    element->ellipsis_pos = p - element->text - 1;
                    elipsis_width = element->width;
                }
            }
            if (element->width > max)
                element->width = elipsis_width;
            else
                element->ellipsis_pos = 0;
        }
    }
}

void hide_menu() {
    menu_state.appearing = false;
    if (menu_state.opacity) {
        menu_state.disappearing = true;
    }
}

// doesn't need to be this big really, given the big gap in the middle, but this is simplest (for one color menu anyway)
// note we don't store the empty lines between glyphs
static uint32_t menu_bitmap[MENU_WIDTH_IN_WORDS * MENU_GLYPH_HEIGHT * MENU_MAX_LINES];

int render_font_line(const struct text_element *element, uint32_t *out, const uint8_t *bitmaps, int pos, uint bits);

uint32_t mix_frac16(uint32_t a, uint32_t b, int level) {
    int rr = (PICO_SCANVIDEO_R5_FROM_PIXEL(a) * (16 - level) + PICO_SCANVIDEO_R5_FROM_PIXEL(b) * level) / 16;
    int gg = (PICO_SCANVIDEO_G5_FROM_PIXEL(a) * (16 - level) + PICO_SCANVIDEO_G5_FROM_PIXEL(b) * level) / 16;
    int bb = (PICO_SCANVIDEO_B5_FROM_PIXEL(a) * (16 - level) + PICO_SCANVIDEO_B5_FROM_PIXEL(b) * level) / 16;
    return PICO_SCANVIDEO_ALPHA_MASK | PICO_SCANVIDEO_PIXEL_FROM_RGB5(rr, gg, bb);
}

void __maybe_in_ram fill_main_menu() {
    menu_state.num_lines = 0;
    for (uint i = 0; i < count_of(options); i++) {
        struct option *o = options + i;
        menu_state.lines[menu_state.num_lines].left_text.text = o->txt;
        menu_state.lines[menu_state.num_lines].right_text.text = o->value_string;
        menu_state.lines[menu_state.num_lines].left_text.width = 0;
        menu_state.lines[menu_state.num_lines].right_text.width = 0;
        menu_state.disabled[menu_state.num_lines] = o->disabled;
        menu_state.num_lines++;
        if (menu_state.num_lines == MENU_MAX_LINES) break;
    }
    for (int i = menu_state.num_lines; i < MENU_MAX_LINES; i++) {
        menu_state.lines[i].left_text.text = NULL;
        menu_state.lines[i].right_text.text = NULL;
        menu_state.lines[i].left_text.width = 0;
        menu_state.lines[i].right_text.width = 0;
    }
}

static uint16_t midy;

void menu_init() {
    mutex_init(&menu_state.mutex);
#if !X_GUI
    midy = scanvideo_get_mode().height / 2;
#else
    midy = 256;
#endif
}

void options_init() {
    for (int i = 0; i < count_of(options); i++) {
        options[i].handler(options + i, OPTION_UPDATE, 0);
        options[i].handler(options + i, OPTION_INIT_COMPLETE, 0);
    }
    menu_state.do_fill_menu = true;
}

// called once per frame
void update_menu_visuals() {
    bool hide;
    bool fill;
    static uint count_since_dirty;
    mutex_enter_blocking(&menu_state.mutex);
    hide = menu_state.do_hide_menu;
    menu_state.do_hide_menu = false;
    fill = menu_state.do_fill_menu;
    menu_state.do_fill_menu = false;
    mutex_exit(&menu_state.mutex);
    if (hide) hide_menu();
    if (fill) {
        fill_main_menu();
        count_since_dirty = 0;
    }
    DEBUG_PINS_SET(menu, 1);
    if (menu_state.opacity > MENU_OPACITY_THRESHOLD) {
        menu_text_color = PICO_SCANVIDEO_ALPHA_MASK | PICO_SCANVIDEO_PIXEL_FROM_RGB5(26, 26, 30);
        menu_highlight_fg_color = PICO_SCANVIDEO_ALPHA_MASK | PICO_SCANVIDEO_PIXEL_FROM_RGB5(31, 31, 30);

        if (menu_state.error_level) {
            menu_highlight_fg_color = mix_frac16(menu_highlight_fg_color, PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0),
                                                 menu_state.error_level);
        }
        menu_highlight_fg_color2 = menu_highlight_fg_color;
        if (menu_state.flashing) {
            int level;
            if (menu_state.flash_pos < 8)
                level = menu_state.flash_pos * 2;
            else if ((MENU_FLASH_LENGTH - menu_state.flash_pos) < 8)
                level = (MENU_FLASH_LENGTH - menu_state.flash_pos) * 2;
            else
                level = 16;
            menu_highlight_fg_color2 = mix_frac16(PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x10, 0x10, 0x10),
                                                  menu_highlight_fg_color2, level);
        }
        menu_disabled_fg_color = PICO_SCANVIDEO_ALPHA_MASK + PICO_SCANVIDEO_PIXEL_FROM_RGB5(17, 17, 20);
#if PICO_SCANVIDEO_COLOR_PIN_COUNT > 3
        menu_highlight_bg_color = PICO_SCANVIDEO_ALPHA_MASK + PICO_SCANVIDEO_PIXEL_FROM_RGB8(48, 138, 208);
#else
        menu_highlight_bg_color = PICO_SCANVIDEO_ALPHA_MASK + PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, 0, 255);
#endif
    } else {
        menu_text_color = menu_highlight_bg_color = menu_highlight_fg_color = menu_disabled_fg_color = 0;
    }

    if (menu_state.opacity && count_since_dirty++ < MENU_MAX_LINES) {
        static int refresh_line = 0;
        measure_text(&menu_state.lines[refresh_line].left_text, MENU_LEFT_WIDTH_IN_WORDS * 32);
        measure_text(&menu_state.lines[refresh_line].right_text, MENU_RIGHT_WIDTH_IN_WORDS * 32);

        uint32_t *out = menu_bitmap + refresh_line * MENU_GLYPH_HEIGHT * MENU_WIDTH_IN_WORDS;
        const uint8_t *bitmaps = menu_glypth_bitmap;
        for (int j = 0; j < MENU_GLYPH_HEIGHT; j++) {
            int pos = 0;
            int bits = 0;
            if (menu_state.lines[refresh_line].left_text.text) {
                pos = render_font_line(&menu_state.lines[refresh_line].left_text, out, bitmaps, pos, bits);
            }
            while (pos < MENU_LEFT_WIDTH_IN_WORDS)
                out[pos++] = 0;
            pos = MENU_LEFT_WIDTH_IN_WORDS; // truncate
            if (menu_state.lines[refresh_line].right_text.text) {
                int xoff = MENU_RIGHT_WIDTH_IN_WORDS * 32 - menu_state.lines[refresh_line].right_text.width;
                int post = pos + (xoff >> 5);
                while (pos < post)
                    out[pos++] = 0;
                bits = xoff & 31;
                pos = render_font_line(&menu_state.lines[refresh_line].right_text, out, bitmaps, pos, bits);
            }
            while (pos < MENU_WIDTH_IN_WORDS)
                out[pos++] = 0;
            out += pos;
            bitmaps++; // next line of input
        }
        refresh_line++;
        if (refresh_line == MENU_MAX_LINES) refresh_line = 0;
    }
    int expected_top_pixel = menu_state.selected_line * MENU_LINE_HEIGHT;
    int delta = expected_top_pixel - menu_state.selection_top_pixel;
    if (delta > 1) {
        menu_state.selection_top_pixel += 1 + (((delta + MENU_LINE_HEIGHT / 2) * ((1 << 16) / MENU_LINE_HEIGHT)) >> 16);
    } else if (delta < -1) {
        menu_state.selection_top_pixel -= 1 + (((MENU_LINE_HEIGHT / 2 - delta) * ((1 << 16) / MENU_LINE_HEIGHT)) >> 16);
    } else {
        menu_state.selection_top_pixel = expected_top_pixel;
    }
    DEBUG_PINS_CLR(menu, 1);
}

uint bit_reverse(uint foo) {
    return ((foo & 1) << 7) |
           ((foo & 2) << 5) |
           ((foo & 4) << 3) |
           ((foo & 8) << 1) |
           ((foo & 16) >> 1) |
           ((foo & 32) >> 3) |
           ((foo & 64) >> 5) |
           ((foo & 128) >> 7);
}

int render_font_line(const struct text_element *element, uint32_t *out, const uint8_t *bitmaps, int pos, uint bits) {
    const char *p;
    if (!element->ellipsis_pos) {
        p = element->text;
    } else {
        char *buf = alloca(element->ellipsis_pos + 4);
        strncpy(buf, element->text, element->ellipsis_pos);
        strcpy(buf + element->ellipsis_pos, "...");
        p = buf;
    }

    uint32_t acc = 0;
    uint8_t c;
    assert(bits < 32);
    while ((c = *p++)) {
        c -= MENU_GLYPH_MIN;
        if (c < MENU_GLYPH_MAX) {
            int cbits = menu_glyph_widths[c];
            uint b = bit_reverse(bitmaps[c * MENU_GLYPH_HEIGHT]);
            acc |= b << bits;
            bits += cbits;
            if (bits >= 32) {
                out[pos++] = acc;
                bits &= 31;
                if (bits) {
                    acc = b >> (cbits - bits);
                } else {
                    acc = 0;
                }
            }
            bits++;
            if (bits >= 32) {
                out[pos++] = acc;
                acc = bits = 0;
            }
        }
    }
    if (bits) {
        out[pos++] = acc;
    }
    return pos;
}

static void obscure_pixels(uint16_t *pixels, uint count8) {
    uint16_t c = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 8);
    for (uint i = 0; i < count8; i++) {
        pixels[0] = c;
        pixels[2] = c;
        pixels[4] = c;
        pixels[6] = c;
        pixels += 8;
    }
}

static void highlight_pixels(uint16_t *pixels, uint count8) {
    if (((uintptr_t) pixels) & 3) pixels++;
    uint32_t c = menu_highlight_bg_color | (menu_highlight_bg_color << 16);
    uint32_t *p = (uint32_t *) pixels;
    for (uint i = 0; i < count8; i++) {
        p[0] = c;
        p[1] = c;
        p[2] = c;
        p[3] = c;
        p += 4;
    }
}

int draw_menu_background(uint16_t *pixels, uint l, int line_length) {
    int rc = line_length;
    // play with dither, better as overlay
    if (menu_state.opacity) {
        l -= midy;
        l += menu_state.current_height / 2;
        l -= MENU_AREA_OFFSET_Y;
        if (l >= 0 && l < menu_state.current_height) {
            l -= MENU_AREA_BORDER_HEIGHT;
            if (l >= menu_state.selection_top_pixel && l < MIN(menu_state.selection_top_pixel + MENU_LINE_HEIGHT + 1,
                                                               menu_state.current_height - MENU_AREA_BORDER_HEIGHT)) {
                static_assert(!(MENU_LINE_BORDER_WIDTH & 7), "");
                static_assert(!(MENU_AREA_WIDTH & 7), "");
                if (line_length < MENU_AREA_OFFSET_X + MENU_LINE_BORDER_WIDTH) {
                    memset(pixels + line_length, 0, (MENU_AREA_OFFSET_X + MENU_LINE_BORDER_WIDTH - line_length) * 2);
                }
                if (line_length < MENU_AREA_OFFSET_X + MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH) {
                    memset(pixels + MENU_AREA_OFFSET_X + MENU_AREA_WIDTH, 0, MENU_LINE_BORDER_WIDTH * 2);
                }
                obscure_pixels(pixels + MENU_AREA_OFFSET_X + ((l >> 1) & 1), MENU_LINE_BORDER_WIDTH / 8);
                highlight_pixels(pixels + MENU_AREA_OFFSET_X + MENU_LINE_BORDER_WIDTH,
                                 (MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH * 2) / 8);
                obscure_pixels(pixels + MENU_AREA_OFFSET_X + MENU_AREA_WIDTH - MENU_LINE_BORDER_WIDTH + ((l >> 1) & 1),
                               MENU_LINE_BORDER_WIDTH / 8);
                rc = MAX(rc, MENU_AREA_OFFSET_X + MENU_AREA_WIDTH);
            } else {
                obscure_pixels(pixels + MENU_AREA_OFFSET_X + ((l >> 1) & 1), MENU_AREA_WIDTH / 8);
            }
        }
    }
    return rc;
}

void __time_critical_func(draw_menu_foreground)(struct scanvideo_scanline_buffer *scanline_buffer) {
#if !X_GUI
    uint32_t *p = scanline_buffer->data2;
    if (menu_state.opacity) {
        int l = scanvideo_scanline_number(scanline_buffer->scanline_id);
        l -= midy;
        l += menu_state.current_height / 2 - MENU_AREA_BORDER_HEIGHT;
        l -= MENU_AREA_OFFSET_Y;
        if (l >= 0 && l < MENU_LINE_HEIGHT * MENU_MAX_LINES && l < menu_state.current_height - MENU_AREA_BORDER_HEIGHT * 2) {
            int menu_line = (l * ((1 << 16) / MENU_LINE_HEIGHT)) >> 16;
            int menu_line_offset = l - menu_line * MENU_LINE_HEIGHT;
            if (menu_line_offset >= MENU_GLYPH_Y_OFFSET && menu_line_offset < MENU_GLYPH_Y_OFFSET + MENU_GLYPH_HEIGHT) {
                uint32_t fg_color;
                if (l >= menu_state.selection_top_pixel && l < menu_state.selection_top_pixel + MENU_LINE_HEIGHT + 1) {
                    fg_color = menu_highlight_fg_color;
                } else {
                    fg_color = menu_text_color;
                }
                uint32_t *pixels = (uint32_t *) &menu_bitmap[MENU_WIDTH_IN_WORDS *
                                                             (menu_line * MENU_GLYPH_HEIGHT + menu_line_offset -
                                                              MENU_GLYPH_Y_OFFSET)];
                assert(!(3u & (uintptr_t) pixels));

                *p++ = (0 << 16) | COMPOSABLE_COLOR_RUN;
                *p++ = (masked_run_aligned_cmd << 16) | (MENU_AREA_OFFSET_X + MENU_LINE_BORDER_WIDTH + MENU_LINE_TEXT_INDENT - 3);
                *p++ = ((MENU_LEFT_WIDTH_IN_WORDS * 32 - 1) << 16) | fg_color;
                for (uint i = 0; i < MENU_LEFT_WIDTH_IN_WORDS; i++) {
                    *p++ = (uintptr_t) *pixels++;
                }
                if (menu_state.flashing && menu_line == menu_state.selected_line) {
                    fg_color = menu_highlight_fg_color2;
                } else if (menu_state.disabled[menu_line]) {
                    fg_color = menu_disabled_fg_color;
                }
                // need something to align us so stick in two black pixels
                *p++ = COMPOSABLE_RAW_2P;
                *p++ = (masked_run_aligned_cmd << 16) | 0;
                *p++ = ((MENU_RIGHT_WIDTH_IN_WORDS * 32 - 1) << 16) | fg_color;
                for (uint i = 0; i < MENU_RIGHT_WIDTH_IN_WORDS; i++) {
                    *p++ = (uintptr_t) *pixels++;
                }
                *p++ = COMPOSABLE_RAW_1P;
                *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
                scanline_buffer->data2_used = p - scanline_buffer->data2;
                assert(scanline_buffer->data2_used <= scanline_buffer->data2_max);
                return;
            }
        }
    }
    p[0] = COMPOSABLE_RAW_1P;
    p[1] = COMPOSABLE_EOL_SKIP_ALIGN;
    scanline_buffer->data2_used = 2;
#endif
}

void __maybe_in_ram defer_option(struct option *option, bool next, bool complete) {
    mutex_enter_blocking(&menu_state.mutex);
    option_needed = option;
    option_next = next;
    option_complete = complete;
    mutex_exit(&menu_state.mutex);
}

static inline void nav_error() {
    menu_state.error_level = MENU_ERROR_LEVEL_MAX;
}

void menu_cpu_blanking() {
    int i = menu_state.selected_line;
    struct option *o = options + i;
    // bit of a hack.. just a safe place to do this once per frame
    menu_state.flashing = o->change_pending;
    if (!menu_state.disappearing && do_reset) {
        do_reset = false;
        main_reset();
        menu_state.do_fill_menu = true;
    }
    update_menu_visuals();
}

// called to update the menu
bool __maybe_in_ram menu_selection_change(int scancode) {
    int i = menu_state.selected_line;
    struct option *o = options + i;
    switch (scancode) {
        case SDL_SCANCODE_UP:
            if (!o->change_pending || o->disabled) {
                menu_state.selected_line = (menu_state.selected_line + menu_state.num_lines - 1) % menu_state.num_lines;
            } else nav_error();
            return true;
        case SDL_SCANCODE_DOWN:
            if (!o->change_pending) {
                menu_state.selected_line = (menu_state.selected_line + 1) % menu_state.num_lines;
            } else nav_error();
            return true;
        case SDL_SCANCODE_LEFT:
            if (!o->is_action) {
                if (!o->disabled) {
                    o->handler(o, OPTION_UPDATE, -1);
                    menu_state.do_fill_menu = true;
                }
            } else nav_error();
            return true;
        case SDL_SCANCODE_RIGHT:
            if (!o->is_action) {
                if (!o->disabled) {
                    o->handler(o, OPTION_UPDATE, 1);
                    menu_state.do_fill_menu = true;
                }
            } else nav_error();
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_RETURN2:
            if (o->is_action || o->is_enter_option || o->change_pending) {
                if (!o->disabled) {
                    o->handler(o, OPTION_EXECUTE, 0);
                    if (!o->is_enter_option) {
                        menu_state.do_hide_menu = true;
                    } else {
                        menu_state.do_fill_menu = true;
                    }
                    menu_state.flashing = false;
                }
//                defer_option(o, true, o->change_pending);
            }
            return true;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_LGUI:
        case SDL_SCANCODE_RGUI:
        case SDL_SCANCODE_F11:
        case SDL_SCANCODE_F15:
            if (o->change_pending) {
                o->handler(o, OPTION_CANCEL, 0);
                menu_state.do_fill_menu = true;
                menu_state.flashing = false;
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

void __maybe_in_ram menu_display_blanking() {
    if (menu_state.error_level > 0) {
        menu_state.error_level--;
    }
    if (menu_state.flashing) {
        menu_state.flash_pos = (menu_state.flash_pos + 1);
        if (menu_state.flash_pos == MENU_FLASH_LENGTH) menu_state.flash_pos = 0;
    }
    int next_opacity = menu_state.opacity;
    if (menu_state.appearing) {
        if (next_opacity < MENU_OPACITY_MAX) {
            next_opacity = menu_state.opacity + 2;
        } else {
            menu_state.appearing = false;
        }
    } else if (menu_state.disappearing) {
        if (next_opacity > 0) {
            next_opacity = menu_state.opacity - 2;
        } else {
            menu_state.disappearing = false;
        }
    }
    if (next_opacity != menu_state.opacity) {
        menu_state.opacity = next_opacity;
        menu_state.current_height = next_opacity * MENU_AREA_HEIGHT / MENU_OPACITY_MAX;
    }
}

bool handle_menu_key(int scancode, bool down) {
    if (scancode == SDL_SCANCODE_F11 || scancode == SDL_SCANCODE_F15 || scancode == SDL_SCANCODE_LGUI ||
        scancode == SDL_SCANCODE_RGUI ||
        (menu_state.opacity && !menu_state.disappearing && scancode == SDL_SCANCODE_ESCAPE)) {
        if (down) {
            // todo need defer to at least come outside of this
            mutex_enter_blocking(&menu_state.mutex);
            if (menu_state.appearing || menu_state.opacity) {
                if (!menu_selection_change(SDL_SCANCODE_LGUI)) {
                    hide_menu();
                }
            } else if (menu_state.disappearing || !menu_state.opacity) {
                menu_state.disappearing = false;
                menu_state.appearing = true;
                fill_main_menu();
            }
            mutex_exit(&menu_state.mutex);
        }
        return true;
    } else {
        if (menu_state.opacity && !menu_state.disappearing) {
            switch (scancode) {
                case SDL_SCANCODE_UP:
                case SDL_SCANCODE_DOWN:
                case SDL_SCANCODE_LEFT:
                case SDL_SCANCODE_RIGHT:
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_RETURN2:
                    if (down) {
                        mutex_enter_blocking(&menu_state.mutex);
                        menu_selection_change(scancode);
                        mutex_exit(&menu_state.mutex);
                    }
                    return true;
                default:
                    break;
            }
            // disable all keys todo make this a setting
            return true;
        }
    }
    return false;
}

#else
bool handle_menu_key(int scancode, bool down) {
    return false;
}

#endif
