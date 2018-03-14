// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <reg.h>
#include <bits.h>
#include <stdio.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <vm/physmap.h>
#include <lk/init.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <dev/interrupt.h>
#include <kernel/cmdline.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <lib/cbuf.h>
#include <lk/init.h>
#include <platform.h>
#include <platform/console.h>
#include <platform/debug.h>
#include <platform/pc.h>
#include <platform/pc/bootloader.h>
#include <reg.h>
#include <stdarg.h>
#include <stdio.h>
#include <trace.h>
#include <vm/physmap.h>
#include <zircon/types.h>

#include "platform_p.h"

static const int uart_baud_rate = 115200;
static int uart_io_port = 0x3f8;
static uint64_t uart_mem_addr = 0;
static uint32_t uart_irq = ISA_IRQ_SERIAL1;

cbuf_t console_input_buf;
static bool output_enabled = false;
uint32_t uart_fifo_depth;

// tx driven irq
static bool uart_tx_irq_enabled = false;
static event_t uart_dputc_event = EVENT_INITIAL_VALUE(uart_dputc_event, true, 0);
static spin_lock_t uart_spinlock = SPIN_LOCK_INITIAL_VALUE;

static uint8_t uart_read(uint8_t reg) {
    if (uart_mem_addr) {
        return (uint8_t)readl(uart_mem_addr + 4 * reg);
    } else {
        return (uint8_t)inp((uint16_t)(uart_io_port + reg));
    }
}

static void uart_write(uint8_t reg, uint8_t val) {
    if (uart_mem_addr) {
        writel(val, uart_mem_addr + 4 * reg);
    } else {
        outp((uint16_t)(uart_io_port + reg), val);
    }
}

static void uart_irq_handler(void *arg) {
    spin_lock(&uart_spinlock);

    // see why we have gotten an irq
    for (;;) {
        uint8_t iir = uart_read(2);
        if (BIT(iir, 0))
            break; // no valid interrupt

        // 3 bit identification field
        uint ident = BITS(iir, 3, 0);
        switch (ident) {
        case 0b0100:
        case 0b1100: {
            // rx fifo is non empty, drain it
            unsigned char c = uart_read(0);
            cbuf_write_char(&console_input_buf, c);
            break;
        }
        case 0b0010:
            // transmitter is empty, signal any waiting senders
            event_signal(&uart_dputc_event, true);
            // disable the tx irq
            uart_write(1, (1<<0)); // just rx interrupt enable
            break;
        case 0b0110: // receiver line status
            uart_read(5); // read the LSR
            break;
        default:
            spin_unlock(&uart_spinlock);
            panic("UART: unhandled ident %#x\n", ident);
        }
    }

    spin_unlock(&uart_spinlock);
}

static void platform_drain_debug_uart_rx(void) {
    while (uart_read(5) & (1<<0)) {
        unsigned char c = uart_read(0);
        cbuf_write_char(&console_input_buf, c);
    }
}

// for devices where the uart rx interrupt doesn't seem to work
static void uart_rx_poll(timer_t* t, zx_time_t now, void* arg) {
    timer_set(t, now + ZX_MSEC(10), TIMER_SLACK_CENTER, ZX_MSEC(1), uart_rx_poll, NULL);
    platform_drain_debug_uart_rx();
}

void platform_debug_start_uart_timer();

void platform_debug_start_uart_timer(void) {
    static timer_t uart_rx_poll_timer;
    static bool started = false;

    if (!started) {
        started = true;
        timer_init(&uart_rx_poll_timer);
        timer_set(&uart_rx_poll_timer, current_time() + ZX_MSEC(10),
                  TIMER_SLACK_CENTER, ZX_MSEC(1), uart_rx_poll, NULL);
    }
}

static void init_uart() {
    /* configure the uart */
    int divisor = 115200 / uart_baud_rate;

    /* get basic config done so that tx functions */
    uart_write(1, 0);                                  // mask all irqs
    uart_write(3, 0x80);                               // set up to load divisor latch
    uart_write(0, static_cast<uint8_t>(divisor));      // lsb
    uart_write(1, static_cast<uint8_t>(divisor >> 8)); // msb
    uart_write(3, 3);                                  // 8N1
    // enable FIFO, rx FIFO reset, tx FIFO reset, 16750 64 byte fifo enable,
    // Rx FIFO irq trigger level at 14-bytes
    uart_write(2, 0xe7);

    /* figure out the fifo depth */
    uint8_t fcr = uart_read(2);
    if (BITS_SHIFT(fcr, 7, 6) == 3 && BIT(fcr, 5)) {
        // this is a 16750
        uart_fifo_depth = 64;
    } else if (BITS_SHIFT(fcr, 7, 6) == 3) {
        // this is a 16550A
        uart_fifo_depth = 16;
    } else {
        uart_fifo_depth = 1;
    }
}

void pc_init_debug_early() {
    switch (bootloader.uart.type) {
    case BOOTDATA_UART_PC_PORT:
        uart_io_port = static_cast<uint32_t>(bootloader.uart.base);
        uart_irq = bootloader.uart.irq;
        break;
    case BOOTDATA_UART_PC_MMIO:
        uart_mem_addr = (uint64_t)paddr_to_physmap(bootloader.uart.base);
        uart_irq = bootloader.uart.irq;
        break;
    }

    init_uart();

    output_enabled = true;

    dprintf(INFO, "UART: FIFO depth %u\n", uart_fifo_depth);
}

void pc_init_debug(void) {
    bool tx_irq_driven = false;
    /* finish uart init to get rx going */
    cbuf_initialize(&console_input_buf, 1024);

    if ((uart_irq == 0) || cmdline_get_bool("kernel.debug_uart_poll", false)) {
        printf("debug-uart: polling enabled\n");
        platform_debug_start_uart_timer();
    } else {
        uart_irq = apic_io_isa_to_global(static_cast<uint8_t>(uart_irq));
        zx_status_t status = register_int_handler(uart_irq, uart_irq_handler, NULL);
        DEBUG_ASSERT(status == ZX_OK);
        unmask_interrupt(uart_irq);

        uart_write(1, (1<<0)); // enable receive data available interrupt

        // modem control register: Axiliary Output 2 is another IRQ enable bit
        const uint8_t mcr = uart_read(4);
        uart_write(4, mcr | 0x8);
        printf("UART: started IRQ driven RX\n");
#if !ENABLE_KERNEL_LL_DEBUG
        tx_irq_driven = true;
#endif
    }
    if (tx_irq_driven) {
        // start up tx driven output
        printf("UART: started IRQ driven TX\n");
        uart_tx_irq_enabled = true;
    }
}

void pc_suspend_debug(void) {
    output_enabled = false;
}

void pc_resume_debug(void) {
    init_uart();
    output_enabled = true;
}

/*
 * This is called when the FIFO is detected to be empty. So we can write an
 * entire FIFO's worth of bytes. Much more efficient than writing 1 byte
 * at a time (and then checking for FIFO to drain).
 */
static char *debug_platform_tx_FIFO_bytes(const char *str, size_t *len,
                                          bool *copied_CR,
                                          size_t *wrote_bytes,
                                          bool map_NL)
{
    size_t i, copy_bytes;;
    char *s = (char *)str;

    copy_bytes = MIN(uart_fifo_depth, *len);
    for (i = 0 ; i < copy_bytes ; i++) {
        if (*s == '\n' && map_NL && !*copied_CR) {
            uart_write(0, '\r');
            *copied_CR = true;
            if (++i == copy_bytes)
                break;
            uart_write(0, '\n');
        } else {
            uart_write(0, *s);
            *copied_CR = false;
        }
        s++;
        (*len)--;
    }
    if (wrote_bytes != NULL)
        *wrote_bytes = i;
    return s;
}

/*
 * dputs() Tx is either polling driven (if the caller is non-preemptible
 * or earlyboot or panic) or blocking (and irq driven).
 * TODO - buffered Tx support may be a win, (lopri but worth investigating)
 * When we do that dputs() can be completely asynchronous, and return when
 * the write has been (atomically) deposited into the buffer, except when
 * we run out of room in the Tx buffer (rare) - we'd either need to spin
 * (if non-blocking) or block waiting for space in the Tx buffer (adding
 * support to optionally block in cbuf_write_char() would be easiest way
 * to achieve that).
 *
 * block : Blocking vs Non-Blocking
 * map_NL : If true, map a '\n' to '\r'+'\n'
 */
static void platform_dputs(const char* str, size_t len,
                           bool block, bool map_NL) {
    spin_lock_saved_state_t state;
    bool copied_CR = false;
    size_t wrote;

    // drop strings if we haven't initialized the uart yet
    if (unlikely(!output_enabled))
        return;

    if (!uart_tx_irq_enabled)
        block = false;

    spin_lock_irqsave(&uart_spinlock, state);
    while (len > 0) {
        // Is FIFO empty ?
        while (!(uart_read(5) & (1<<5))) {
            spin_unlock_irqrestore(&uart_spinlock, state);
            if (block)
                event_wait(&uart_dputc_event);
            else
                arch_spinloop_pause();
            spin_lock_irqsave(&uart_spinlock, state);
        }
        // Fifo is completely empty now, we can shove an entire
        // fifo's worth of Tx...
        str = debug_platform_tx_FIFO_bytes(str, &len, &copied_CR,
                                           &wrote, map_NL);
        if (block && wrote > 0) {
            // If blocking/irq driven wakeps, enable rx/tx intrs
            uart_write(1, (1<<0)|(1<<1)); // rx and tx interrupt enable
        }
    }
    spin_unlock_irqrestore(&uart_spinlock, state);
}

void platform_dputs_thread(const char* str, size_t len) {
    platform_dputs(str, len, true, true);
}

void platform_dputs_irq(const char* str, size_t len) {
    platform_dputs(str, len, false, true);
}

// polling versions of debug uart read/write
static int debug_uart_getc_poll(char *c) {
    // if there is a character available, read it
    if (uart_read(5) & (1<<0)) {
        *c = uart_read(0);
        return 0;
    }

    return -1;
}

static void debug_uart_putc_poll(char c) {
    // while the fifo is non empty, spin
    while (!(uart_read(5) & (1<<6))) {
        arch_spinloop_pause();
    }
    uart_write(0, c);
}


int platform_dgetc(char* c, bool wait) {
    return static_cast<int>(cbuf_read_char(&console_input_buf, c, wait));
}

// panic time polling IO for the panic shell
void platform_pputc(char c) {
    if (c == '\n')
        debug_uart_putc_poll('\r');
    debug_uart_putc_poll(c);
}

int platform_pgetc(char *c, bool wait) {
    return debug_uart_getc_poll(c);
}

/*
 * Called on start of a panic.
 * When we do Tx buffering, drain the Tx buffer here in polling mode.
 * Turn off Tx interrupts, so force Tx be polling from this point
 */
void platform_debug_panic_start(void) {
    uart_tx_irq_enabled = false;
}
