/* Copyright 2025 ~ 2026 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "keychron.h"
#include "factory_test.h"
#include "qmk_midi.h"
#include "midi.h"
#include "rgb_matrix.h"
#include <string.h>

extern MidiDevice midi_device;

/* ==========================================================================
 * 1. MODE DEFINITIONS & STATE VARIABLES
 * ========================================================================== */
typedef enum {
    MODE_LP_PROGRAMMER = 0, // Main Mode: MIDI Out + MIDI In Velocity LEDs
    MODE_MIDI_KC_RGB   = 1, // MIDI Out + Keychron Launcher RGB Effects
    MODE_STANDARD_KBD  = 2, // Standard Keyboard/Macro + Keychron Launcher RGB
    MODE_COUNT
} pad_mode_t;

static pad_mode_t current_mode = MODE_LP_PROGRAMMER;

// Stores the RGB state of all 100 LEDs for Launchpad Programmer Mode
static RGB midi_led_state[RGB_MATRIX_LED_COUNT] = {0};

// Mode switching combo state: Hold (0,0) + (0,4) + (0,5)
static uint8_t  combo_mask        = 0;
static uint32_t combo_hold_timer  = 0;
static bool     combo_triggered   = false;
static uint32_t mode_flash_timer  = 0;

// Keychron factory reset variables
static uint32_t factory_reset_timer = 0;
static uint8_t  factory_reset_state = 0;
static uint8_t  keys_released       = 0;

/* ==========================================================================
 * 2. NOVATION LAUNCHPAD 128-COLOR VELOCITY LOOKUP TABLE (RGB888)
 * ========================================================================== */
static const RGB PROGMEM launchpad_palette[128] = {
    {0x00, 0x00, 0x00}, {0x1C, 0x1C, 0x1C}, {0x7F, 0x7F, 0x7F}, {0xFF, 0xFF, 0xFF}, // 0-3: Greys/White
    {0xFF, 0x4C, 0x4C}, {0xFF, 0x00, 0x00}, {0x59, 0x00, 0x00}, {0x19, 0x00, 0x00}, // 4-7: Reds
    {0xFF, 0xBD, 0x6C}, {0xFF, 0x54, 0x00}, {0x59, 0x1D, 0x00}, {0x27, 0x1B, 0x00}, // 8-11: Oranges
    {0xFF, 0xFF, 0x4C}, {0xFF, 0xFF, 0x00}, {0x59, 0x59, 0x00}, {0x19, 0x19, 0x00}, // 12-15: Yellows
    {0x88, 0xFF, 0x4C}, {0x54, 0xFF, 0x00}, {0x1D, 0x59, 0x00}, {0x14, 0x2B, 0x00}, // 16-19: Lime Greens
    {0x4C, 0xFF, 0x4C}, {0x00, 0xFF, 0x00}, {0x00, 0x59, 0x00}, {0x00, 0x19, 0x00}, // 20-23: Pure Greens
    {0x4C, 0xFF, 0x5E}, {0x00, 0xFF, 0x19}, {0x00, 0x59, 0x0D}, {0x00, 0x19, 0x02}, // 24-27: Spring Greens
    {0x4C, 0xFF, 0x88}, {0x00, 0xFF, 0x55}, {0x00, 0x59, 0x1D}, {0x00, 0x1F, 0x12}, // 28-31: Mint
    {0x4C, 0xFF, 0xB7}, {0x00, 0xFF, 0x99}, {0x00, 0x59, 0x35}, {0x00, 0x19, 0x12}, // 32-35: Turquoise
    {0x4C, 0xC3, 0xFF}, {0x00, 0xA9, 0xFF}, {0x00, 0x41, 0x52}, {0x00, 0x10, 0x19}, // 36-39: Sky Blues
    {0x4C, 0x88, 0xFF}, {0x00, 0x55, 0xFF}, {0x00, 0x1D, 0x59}, {0x00, 0x08, 0x19}, // 40-43: Ocean Blues
    {0x4C, 0x4C, 0xFF}, {0x00, 0x00, 0xFF}, {0x00, 0x00, 0x59}, {0x00, 0x00, 0x19}, // 44-47: Deep Blues
    {0x87, 0x4C, 0xFF}, {0x54, 0x00, 0xFF}, {0x19, 0x00, 0x64}, {0x0F, 0x00, 0x30}, // 48-51: Purples
    {0xFF, 0x4C, 0xFF}, {0xFF, 0x00, 0xFF}, {0x59, 0x00, 0x59}, {0x19, 0x00, 0x19}, // 52-55: Magentas
    {0xFF, 0x4C, 0x87}, {0xFF, 0x00, 0x54}, {0x59, 0x00, 0x1D}, {0x22, 0x00, 0x13}, // 56-59: Pinks
    {0xFF, 0x15, 0x00}, {0x99, 0x35, 0x00}, {0x79, 0x51, 0x00}, {0x43, 0x64, 0x00}, // 60-63: Warm Accents
    {0x03, 0x39, 0x00}, {0x00, 0x57, 0x35}, {0x00, 0x54, 0x7F}, {0x00, 0x00, 0xFF}, // 64-67
    {0x00, 0x45, 0x4F}, {0x25, 0x00, 0xCC}, {0x7F, 0x7F, 0x7F}, {0x20, 0x20, 0x20}, // 68-71
    {0xFF, 0x00, 0x00}, {0xBD, 0xFF, 0x2D}, {0xAF, 0xED, 0x06}, {0x64, 0xFF, 0x09}, // 72-75
    {0x10, 0x8B, 0x00}, {0x00, 0xFF, 0x87}, {0x00, 0xA9, 0xFF}, {0x00, 0x2A, 0xFF}, // 76-79
    {0x3F, 0x00, 0xFF}, {0x7A, 0x00, 0xFF}, {0xB2, 0x1A, 0x7D}, {0x40, 0x21, 0x00}, // 80-83
    {0xFF, 0x4A, 0x00}, {0x88, 0xE1, 0x06}, {0x72, 0xFF, 0x15}, {0x00, 0xFF, 0x00}, // 84-87
    {0x3B, 0xFF, 0x26}, {0x59, 0xFF, 0x71}, {0x38, 0xFF, 0xCC}, {0x5B, 0x8A, 0xFF}, // 88-91
    {0x31, 0x51, 0xC6}, {0x87, 0x7F, 0xE9}, {0xD3, 0x1D, 0xFF}, {0xFF, 0x00, 0x5D}, // 92-95
    {0xFF, 0x7F, 0x00}, {0xB9, 0xB0, 0x00}, {0x90, 0xFF, 0x00}, {0x83, 0x5D, 0x07}, // 96-99
    {0x39, 0x2B, 0x00}, {0x14, 0x4C, 0x10}, {0x0D, 0x50, 0x38}, {0x15, 0x15, 0x2A}, // 100-103
    {0x16, 0x20, 0x5A}, {0x69, 0x3C, 0x1C}, {0xA8, 0x00, 0x0A}, {0xDE, 0x51, 0x3D}, // 104-107
    {0xD8, 0x6A, 0x1C}, {0xFF, 0xE1, 0x26}, {0x9E, 0xE1, 0x2F}, {0x67, 0xB5, 0x0F}, // 108-111
    {0x1E, 0x1E, 0x30}, {0xDC, 0xFF, 0x6B}, {0x80, 0xFF, 0xBD}, {0x9A, 0x99, 0xFF}, // 112-115
    {0x8E, 0x66, 0xFF}, {0x40, 0x40, 0x40}, {0x75, 0x75, 0x75}, {0xE0, 0xFF, 0xFF}, // 116-119
    {0xA0, 0x00, 0x00}, {0x35, 0x00, 0x00}, {0x1A, 0xD0, 0x00}, {0x07, 0x42, 0x00}, // 120-123
    {0xB9, 0xB0, 0x00}, {0x3F, 0x31, 0x00}, {0xB3, 0x5F, 0x00}, {0x4B, 0x15, 0x02}  // 124-127
};

/* ==========================================================================
 * 3. LAUNCHPAD GRID <-> MIDI NOTE MAPPING HELPERS
 * ========================================================================== */
static inline uint8_t matrix_to_launchpad_note(uint8_t row, uint8_t col) {
    return (9 - row) * 10 + col;
}

static inline int8_t launchpad_note_to_led_index(uint8_t note) {
    if (note >= 100) return -1;
    uint8_t lp_row     = note / 10;
    uint8_t lp_col     = note % 10;
    uint8_t matrix_row = 9 - lp_row;
    return (matrix_row * 10) + lp_col;
}

static void set_mode(pad_mode_t new_mode) {
    // 1. Send Note Off / CC clear for all 100 possible notes to prevent stuck MIDI triggers
    for (uint8_t note = 0; note < 100; note++) {
        midi_send_noteoff(&midi_device, 0, note, 0);
    }

    // 2. Clear all physical keyboard state so the OS doesn't think a key is stuck down
    clear_keyboard();

    current_mode     = new_mode % MODE_COUNT;
    mode_flash_timer = timer_read32();

    if (current_mode != MODE_LP_PROGRAMMER) {
        // Restore saved Keychron Launcher RGB effect when entering Mode 1 or 2
        rgb_matrix_reload_from_eeprom();
    }
}

/* ==========================================================================
 * 4. NATIVE QMK MIDI DEVICE CALLBACKS (Receives MIDI In from FL Studio)
 * ========================================================================== */
static void update_led_by_velocity(uint8_t note, uint8_t velocity) {
    int8_t led_idx = launchpad_note_to_led_index(note);
    if (led_idx >= 0 && led_idx < RGB_MATRIX_LED_COUNT) {
        uint8_t v = velocity & 0x7F;
        RGB color;
        memcpy_P(&color, &launchpad_palette[v], sizeof(RGB));
        midi_led_state[led_idx] = color;
    }
}

static void midi_in_noteon_cb(MidiDevice *device, uint8_t chan, uint8_t note, uint8_t vel) {
    (void)device;
    (void)chan;
    update_led_by_velocity(note, vel);
}

static void midi_in_noteoff_cb(MidiDevice *device, uint8_t chan, uint8_t note, uint8_t vel) {
    (void)device;
    (void)chan;
    (void)vel;
    update_led_by_velocity(note, 0);
}

static void midi_in_cc_cb(MidiDevice *device, uint8_t chan, uint8_t num, uint8_t val) {
    (void)device;
    (void)chan;
    if (num == 120 || num == 123) {
        // All Sound Off / All Notes Off -> Clear Grid
        memset(midi_led_state, 0, sizeof(midi_led_state));
    } else {
        update_led_by_velocity(num, val);
    }
}

/* ==========================================================================
 * 5. RGB MATRIX RENDERING (Overrides Keychron RGB in Mode 0)
 * ========================================================================== */
static void render_launchpad_leds(uint8_t led_min, uint8_t led_max) {
    uint8_t brightness = rgb_matrix_get_val();
    if (brightness < 32) brightness = 200; // Fallback if Launcher brightness is turned down to 0

    // 1. In Launchpad Programmer Mode, paint all 100 LEDs from midi_led_state
    if (current_mode == MODE_LP_PROGRAMMER) {
        for (uint8_t i = led_min; i < led_max; i++) {
            uint8_t r = ((uint16_t)midi_led_state[i].r * brightness) >> 8;
            uint8_t g = ((uint16_t)midi_led_state[i].g * brightness) >> 8;
            uint8_t b = ((uint16_t)midi_led_state[i].b * brightness) >> 8;
            rgb_matrix_set_color(i, r, g, b);
        }
    }

    // 2. Interactive 3-Key Combo Menu on Top Row: Hold (0,0) + (0,4) + (0,5)
    if (combo_mask == 0x07) {
        if (1 >= led_min && 1 < led_max) rgb_matrix_set_color(1, 255, 0, 0);   // (0,1) Red = Mode 0
        if (2 >= led_min && 2 < led_max) rgb_matrix_set_color(2, 0, 255, 0);   // (0,2) Green = Mode 1
        if (3 >= led_min && 3 < led_max) rgb_matrix_set_color(3, 0, 0, 255);   // (0,3) Blue = Mode 2
    }

    // 3. 600ms Top-Row Mode Switch Flash
    if (mode_flash_timer && timer_elapsed32(mode_flash_timer) < 600) {
        RGB flash_color = {0, 0, 0};
        if (current_mode == MODE_LP_PROGRAMMER) flash_color = (RGB){255, 0, 50};
        if (current_mode == MODE_MIDI_KC_RGB)   flash_color = (RGB){0, 255, 80};
        if (current_mode == MODE_STANDARD_KBD)  flash_color = (RGB){0, 120, 255};

        for (uint8_t i = 0; i < 10; i++) {
            if (i >= led_min && i < led_max) {
                rgb_matrix_set_color(i, flash_color.r, flash_color.g, flash_color.b);
            }
        }
    } else {
        mode_flash_timer = 0;
    }
}

bool rgb_matrix_indicators_user(void) {
    render_launchpad_leds(0, RGB_MATRIX_LED_COUNT);
    return false;
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    render_launchpad_leds(led_min, led_max);
    return false;
}

/* ==========================================================================
 * 6. KEYCHRON INIT, TASK & KEY PROCESSING
 * ========================================================================== */
void keyboard_post_init_kb(void) {
    keychron_common_init();
    memset(midi_led_state, 0, sizeof(midi_led_state));

    // Register incoming USB MIDI callbacks with QMK's midi_device
    midi_register_noteon_callback(&midi_device, midi_in_noteon_cb);
    midi_register_noteoff_callback(&midi_device, midi_in_noteoff_cb);
    midi_register_cc_callback(&midi_device, midi_in_cc_cb);

    // Ensure RGB Matrix driver is active so indicators_user runs every frame
    if (!rgb_matrix_is_enabled()) {
        rgb_matrix_enable_noeeprom();
    }

    keyboard_post_init_user();
}

void c100_8k_factory_timer_start(void) {
    factory_reset_timer = timer_read32();
}

static inline void c100_8k_factory_timer_check(void) {
    if (timer_elapsed32(factory_reset_timer) > 4000) {
        factory_reset_timer = 0;
        if (factory_reset_state == 0x03) {
            keys_released = 0x01 | 0x02;
            factory_reset();
        }
        factory_reset_state = 0;
    }
}

void keychron_task_kb(void) {
    if (factory_reset_timer) c100_8k_factory_timer_check();

    // Ensure QMK's MIDI input task processes incoming USB packets every cycle
    midi_device_process(&midi_device);

    if (combo_mask == 0x07 && !combo_triggered && timer_elapsed32(combo_hold_timer) > 1500) {
        combo_triggered = true;
        set_mode((current_mode + 1) % MODE_COUNT);
    }
}

bool process_record_keychron_kb(uint16_t keycode, keyrecord_t *record) {
    uint8_t row = record->event.key.row;
    uint8_t col = record->event.key.col;

    /* --- A. Track 3-Key Mode Switch Combo: (0,0) + (0,4) + (0,5) --- */
    uint8_t c_bit = 0;
    if (row == 0 && col == 0) c_bit = 0x01;
    if (row == 0 && col == 4) c_bit = 0x02;
    if (row == 0 && col == 5) c_bit = 0x04;

    if (c_bit) {
        if (record->event.pressed) {
            combo_mask |= c_bit;
            if (combo_mask == 0x07) {
                combo_hold_timer = timer_read32();
                combo_triggered  = false;
                return false;
            }
        } else {
            combo_mask &= ~c_bit;
            combo_hold_timer = 0;
        }
    }

    if (combo_mask == 0x07 && record->event.pressed) {
        if (row == 0 && col == 1) { set_mode(MODE_LP_PROGRAMMER); combo_triggered = true; return false; }
        if (row == 0 && col == 2) { set_mode(MODE_MIDI_KC_RGB);   combo_triggered = true; return false; }
        if (row == 0 && col == 3) { set_mode(MODE_STANDARD_KBD);  combo_triggered = true; return false; }
    }

    /* --- B. Keychron Factory Reset Check (4-second Corner Hold) --- */
    bool is_prev_corner = (row == 0) && (col == 0 || col == MATRIX_COLS - 1);
    bool is_next_corner = (row == MATRIX_ROWS - 1) && (col == 0 || col == MATRIX_COLS - 1);

    if (is_prev_corner || is_next_corner) {
        uint8_t bit = is_prev_corner ? 0x02 : 0x01;
        if (record->event.pressed) {
            factory_reset_state |= bit;
            if (factory_reset_state == 0x03) {
                c100_8k_factory_timer_start();
            }
        } else {
            factory_reset_state &= ~bit;
            factory_reset_timer = 0;
            if (keys_released & bit) {
                keys_released &= ~bit;
                if (keycode >= QK_BACKLIGHT_ON && keycode <= RGB_MODE_TWINKLE) return false;
            }
        }
    }

    /* --- C. Optional Custom Keycodes for Mode Switching --- */
    if (record->event.pressed) {
        switch (keycode) {
            case QK_KB_0 + 12: set_mode((current_mode + 1) % MODE_COUNT); return false;
            case QK_KB_0 + 13: set_mode(MODE_LP_PROGRAMMER);              return false;
            case QK_KB_0 + 14: set_mode(MODE_MIDI_KC_RGB);                return false;
            case QK_KB_0 + 15: set_mode(MODE_STANDARD_KBD);               return false;
        }
    }

    /* --- D. MIDI Mode Interception (Modes 0 and 1) --- */
    if (current_mode == MODE_LP_PROGRAMMER || current_mode == MODE_MIDI_KC_RGB) {
        uint8_t note = matrix_to_launchpad_note(row, col);

        if (record->event.pressed) {
            midi_send_noteon(&midi_device, 0, note, 127);
            if (row == 0) {
                midi_send_cc(&midi_device, 0, note, 127);
            }
        } else {
            midi_send_noteoff(&midi_device, 0, note, 0);
            if (row == 0) {
                midi_send_cc(&midi_device, 0, note, 0);
            }
        }
        return false;
    }

    return true;
}