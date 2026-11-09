// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A 16550A UART whose registers are 32 bits apart (a register shift of 2),
// as Nebula's venus QEMU machine gives the hypervisor its console and GRT's
// zircon drives it. Transmit is polled; receive is interrupt driven.

#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <arch/arm64/periphmap.h>
#include <lib/cbuf.h>
#include <kernel/thread.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <platform/debug.h>
#include <pdev/driver.h>
#include <pdev/uart.h>
#include <zircon/boot/driver-config.h>

#define UART_RBR 0 // receive buffer (read)
#define UART_THR 0 // transmit holding (write)
#define UART_IER 1 // interrupt enable
#define UART_FCR 2 // FIFO control (write)
#define UART_LCR 3 // line control
#define UART_MCR 4 // modem control
#define UART_LSR 5 // line status

#define UART_IER_ERBFI (1u << 0) // received data available
#define UART_FCR_FIFO_ENABLE (1u << 0)
#define UART_LCR_8N1 0x03
#define UART_MCR_OUT2 (1u << 3) // routes the interrupt out on PC-style parts
#define UART_LSR_DR (1u << 0)   // data ready
#define UART_LSR_THRE (1u << 5) // transmit holding register empty

#define UART_REG_SHIFT 2
#define UARTREG(reg) (*REG32(uart_base + ((reg) << UART_REG_SHIFT)))

#define RXBUF_SIZE 16

// values read from bootdata
static vaddr_t uart_base = 0;
static uint32_t uart_irq = 0;

static cbuf_t uart_rx_buf;
static bool uart_rx_irq_enabled = false;

static spin_lock_t uart_spinlock = SPIN_LOCK_INITIAL_VALUE;

static void ns16550a_uart_irq(void* arg) {
    // while the receive buffer has data, read it out
    while (UARTREG(UART_LSR) & UART_LSR_DR) {
        // if we're out of rx buffer, mask the irq instead of handling it
        if (cbuf_space_avail(&uart_rx_buf) == 0) {
            UARTREG(UART_IER) = 0;
            break;
        }
        char c = static_cast<char>(UARTREG(UART_RBR));
        cbuf_write_char(&uart_rx_buf, c);
    }
}

static void ns16550a_uart_init(const void* driver_data, uint32_t length) {
    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    zx_status_t status = register_int_handler(uart_irq, &ns16550a_uart_irq, NULL);
    DEBUG_ASSERT(status == ZX_OK);

    // enable the rx interrupt
    UARTREG(UART_IER) = UART_IER_ERBFI;
    unmask_interrupt(uart_irq);
    uart_rx_irq_enabled = true;
}

static int ns16550a_uart_getc(bool wait) {
    if (!uart_rx_irq_enabled) {
        return ZX_ERR_INTERNAL;
    }
    char c;
    if (cbuf_read_char(&uart_rx_buf, &c, wait) == 1) {
        UARTREG(UART_IER) = UART_IER_ERBFI;
        return c;
    }
    return ZX_ERR_INTERNAL;
}

// panic-time getc/putc
static int ns16550a_uart_pputc(char c) {
    // spin while the transmit holding register is full
    while (!(UARTREG(UART_LSR) & UART_LSR_THRE))
        ;
    UARTREG(UART_THR) = c;
    return 1;
}

static int ns16550a_uart_pgetc(void) {
    if (UARTREG(UART_LSR) & UART_LSR_DR) {
        return UARTREG(UART_RBR);
    }
    return -1;
}

static void ns16550a_dputs(const char* str, size_t len, bool block, bool map_NL) {
    spin_lock_saved_state_t state;
    bool copied_CR = false;

    spin_lock_irqsave(&uart_spinlock, state);
    while (len > 0) {
        while (!(UARTREG(UART_LSR) & UART_LSR_THRE)) {
            spin_unlock_irqrestore(&uart_spinlock, state);
            arch_spinloop_pause();
            spin_lock_irqsave(&uart_spinlock, state);
        }
        if (!copied_CR && map_NL && *str == '\n') {
            copied_CR = true;
            UARTREG(UART_THR) = '\r';
        } else {
            copied_CR = false;
            UARTREG(UART_THR) = *str++;
            len--;
        }
    }
    spin_unlock_irqrestore(&uart_spinlock, state);
}

static void ns16550a_start_panic(void) {}

static const struct pdev_uart_ops uart_ops = {
    .getc = ns16550a_uart_getc,
    .pputc = ns16550a_uart_pputc,
    .pgetc = ns16550a_uart_pgetc,
    .start_panic = ns16550a_start_panic,
    .dputs = ns16550a_dputs,
};

static void ns16550a_uart_init_early(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_simple_t));
    const dcfg_simple_t* driver = static_cast<const dcfg_simple_t*>(driver_data);
    ASSERT(driver->mmio_phys && driver->irq);

    uart_base = periph_paddr_to_vaddr(driver->mmio_phys);
    ASSERT(uart_base);
    uart_irq = driver->irq;

    UARTREG(UART_LCR) = UART_LCR_8N1;
    UARTREG(UART_FCR) = UART_FCR_FIFO_ENABLE;
    UARTREG(UART_MCR) = UART_MCR_OUT2;
    UARTREG(UART_IER) = 0;

    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(ns16550a_uart_init_early, KDRV_NS16550A_UART, ns16550a_uart_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(ns16550a_uart_init, KDRV_NS16550A_UART, ns16550a_uart_init, LK_INIT_LEVEL_PLATFORM);
