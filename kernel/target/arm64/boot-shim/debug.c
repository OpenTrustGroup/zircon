// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include "debug.h"

#if DEBUG_UART
void uart_puts(const char* str) {
    char ch;
    while ((ch = *str++)) {
        uart_pputc(ch);
    }
}

void uart_print_hex(uint64_t value) {
    const char digits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = 60; i >= 0; i -= 4) {
        uart_pputc(digits[(value >> i) & 0xf]);
    }
}

#else

void uart_puts(const char* str) {
}

void uart_print_hex(uint64_t value) {
}

#endif // DEBUG_UART
