// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <soc/aml-s905x/s905x-gpio.h>

static aml_gpio_block_t s905x_gpio_blocks[] = {
    // GPIOX Block
    {
        .pin_count = S905X_GPIOX_PINS,
        .oen_offset = S905X_GPIOX_0EN,
        .input_offset = S905X_GPIOX_IN,
        .output_offset = S905X_GPIOX_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIODV Block
    {
        .pin_count = S905X_GPIODV_PINS,
        .oen_offset = S905X_GPIODV_0EN,
        .input_offset = S905X_GPIODV_IN,
        .output_offset = S905X_GPIODV_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIOH Block
    {
        .pin_count = S905X_GPIOH_PINS,
        .oen_offset = S905X_GPIOH_0EN,
        .input_offset = S905X_GPIOH_IN,
        .output_offset = S905X_GPIOH_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIOBOOT Block
    {
        .pin_count = S905X_GPIOBOOT_PINS,
        .oen_offset = S905X_GPIOBOOT_0EN,
        .input_offset = S905X_GPIOBOOT_IN,
        .output_offset = S905X_GPIOBOOT_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIOCARD Block
    {
        .pin_count = S905X_GPIOCARD_PINS,
        .oen_offset = S905X_GPIOCARD_0EN,
        .input_offset = S905X_GPIOCARD_IN,
        .output_offset = S905X_GPIOCARD_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIOCLK Block
    {
        .pin_count = S905X_GPIOCLK_PINS,
        .oen_offset = S905X_GPIOCLK_0EN,
        .input_offset = S905X_GPIOCLK_IN,
        .output_offset = S905X_GPIOCLK_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIOZ Block
    {
        .pin_count = S905X_GPIOZ_PINS,
        .oen_offset = S905X_GPIOZ_0EN,
        .input_offset = S905X_GPIOZ_IN,
        .output_offset = S905X_GPIOZ_OUT,
        .output_shift = 0,
        .mmio_index = 0,
        .lock = MTX_INIT,
    },
    // GPIOAO Block
    {
        .pin_count = S905X_GPIOAO_PINS,
        .oen_offset = S905X_AO_GPIO_OEN_OUT,
        .input_offset = S905X_AO_GPIO_IN,
        .output_offset = S905X_AO_GPIO_OEN_OUT,
        .output_shift = 16, // output is shared with OEN
        .mmio_index = 1,
        .lock = MTX_INIT,
    },
};

#define REG_0 S905X_PERIPHS_PIN_MUX_0
#define REG_1 S905X_PERIPHS_PIN_MUX_1
#define REG_2 S905X_PERIPHS_PIN_MUX_2
#define REG_3 S905X_PERIPHS_PIN_MUX_3
#define REG_4 S905X_PERIPHS_PIN_MUX_4
#define REG_5 S905X_PERIPHS_PIN_MUX_5
#define REG_6 S905X_PERIPHS_PIN_MUX_6
#define REG_7 S905X_PERIPHS_PIN_MUX_7
#define REG_8 S905X_PERIPHS_PIN_MUX_8
#define REG_9 S905X_PERIPHS_PIN_MUX_9
#define AO_REG S905X_AO_RTI_PIN_MUX_REG
#define AO_REG_2 S905X_AO_RTI_PIN_MUX_REG2

static const aml_pinmux_block_t s905x_pinmux_blocks[] = {
    // GPIOX Block
    {
        .mux = {
            { .regs = { REG_5 }, .bits = { 31 }, },
            { .regs = { REG_5 }, .bits = { 30 }, },
            { .regs = { REG_5 }, .bits = { 29 }, },
            { .regs = { REG_5 }, .bits = { 28 }, },
            { .regs = { REG_5 }, .bits = { 27 }, },
            { .regs = { REG_5 }, .bits = { 26 }, },
            { .regs = { REG_5 }, .bits = { 25 }, },
            { .regs = { REG_5, REG_5 }, .bits = { 24, 14 }, },
            { .regs = { REG_5, REG_5, 0, REG_5 }, .bits = { 23, 13, 0, 3 }, },
            { .regs = { REG_5, REG_5, 0, REG_5 }, .bits = { 22, 12, 0, 2 }, },
            { .regs = { REG_5, REG_5, REG_5, REG_5 }, .bits = { 21, 11, 5, 1 }, },
            { .regs = { REG_5, REG_5, REG_5, REG_5 }, .bits = { 20, 10, 4, 0 }, },
            { .regs = { REG_5 }, .bits = { 19 }, },
            { .regs = { REG_5 }, .bits = { 18 }, },
            { .regs = { REG_5 }, .bits = { 17 }, },
            { .regs = { REG_5 }, .bits = { 16 }, },
            { .regs = { REG_5 }, .bits = { 15 }, },
            // pinmux not specified for GPIOX_17 and GPIOX_18.
        },
    },
    // GPIODV Block
    {
        .mux = {
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            { .regs = { REG_2, REG_2, REG_1 }, .bits = { 16, 7, 15 }, },
            { .regs = { REG_2, REG_2, REG_1 }, .bits = { 15, 6, 14 }, },
            { .regs = { REG_2, 0, REG_1 }, .bits = { 14, 0, 13 }, },
            { .regs = { REG_2, 0, REG_1 }, .bits = { 13, 0, 12 }, },
            { .regs = { REG_2, REG_1, REG_1 }, .bits = { 12, 9, 11 }, },
            { .regs = { REG_2, REG_2, REG_1 }, .bits = { 11, 5, 10 }, },
        },
    },
    // GPIOH Block
    {
        .mux = {
            { .regs = { REG_6 }, .bits = { 31 }, },
            { .regs = { REG_6 }, .bits = { 30 }, },
            { .regs = { REG_6 }, .bits = { 29 }, },
            {},
            { .regs = { REG_6, REG_6 }, .bits = { 28, 27 }, },
            {},
            { .regs = { 0, 0, REG_6 }, .bits = { 0, 0, 26 }, },
            { .regs = { 0, 0, REG_6, REG_6 }, .bits = { 0, 0, 25, 22 }, },
            { .regs = { 0, 0, REG_6, REG_6 }, .bits = { 0, 0, 24, 21 }, },
            { .regs = { 0, 0, REG_6 }, .bits = { 0, 0, 23 }, },
        },
    },
    // GPIOBOOT Block
    {
        .mux = {
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7 }, .bits = { 31 }, },
            { .regs = { REG_7, REG_7 }, .bits = { 30, 7 }, },
            { .regs = { 0, REG_7 }, .bits = { 0, 6 }, },
            { .regs = { REG_7, REG_7 }, .bits = { 29, 5 }, },
            { .regs = { 0, REG_7, REG_7 }, .bits = { 0, 4, 13 }, },
            { .regs = { 0, REG_7, REG_7 }, .bits = { 0, 3, 12 }, },
            { .regs = { 0, REG_7, REG_7 }, .bits = { 0, 2, 11 }, },
            { .regs = { 0, REG_7 }, .bits = { 0, 1 }, },
        },
    },
    // GPIOCARD Block
    {
        .mux = {
            { .regs = { REG_6 }, .bits = { 5 }, },
            { .regs = { REG_6 }, .bits = { 4 }, },
            { .regs = { REG_6 }, .bits = { 3 }, },
            { .regs = { REG_6 }, .bits = { 2 }, },
            { .regs = { REG_6, REG_6, REG_6 }, .bits = { 1, 9, 11 }, },
            { .regs = { REG_6, REG_6, REG_6 }, .bits = { 0, 8, 10 }, },
        },
    },
    // GPIOCLK Block
    {
        .mux = {
           {},
           {},
        },
    },
    // GPIOZ Block
    {
        .mux = {
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            { .regs = { REG_4, REG_3 }, .bits = { 25, 21 }, },
            { .regs = { REG_4, 0, REG_3 }, .bits = { 24, 0, 20 }, },
        },
    },
    // GPIOAO Block
    {
        .mux = {
            { .regs = { AO_REG, AO_REG }, .bits = { 12, 26 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 11, 25 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 10, 8 }, },
            { .regs = { AO_REG, AO_REG, 0, AO_REG }, .bits = { 9, 7, 0, 22 }, },
            { .regs = { AO_REG, AO_REG, AO_REG }, .bits = { 24, 6, 2 }, },
            { .regs = { AO_REG, AO_REG, AO_REG }, .bits = { 23, 5, 1 }, },
            { .regs = { 0, 0, AO_REG, AO_REG }, .bits = { 0, 0, 16, 18 }, },
            { .regs = { AO_REG, AO_REG }, .bits = { 0, 21 }, },
            { .regs = { AO_REG, AO_REG, AO_REG_2, AO_REG }, .bits = { 15, 14, 0, 17 }, },
            { .regs = { AO_REG, AO_REG, AO_REG_2, AO_REG }, .bits = { 31, 4, 1, 3 }, },
        },
    },
};

static_assert(countof(s905x_gpio_blocks) == countof(s905x_pinmux_blocks), "");

#undef REG_0
#undef REG_1
#undef REG_2
#undef REG_3
#undef REG_4
#undef REG_5
#undef REG_6
#undef REG_7
#undef REG_8
#undef REG_9
#undef AO_REG
#undef AO_REG_2
