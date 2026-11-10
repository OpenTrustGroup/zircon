// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

__BEGIN_CDECLS;

// Vendor, Product and Device IDs for generic platform drivers
#define PDEV_VID_GENERIC            0
#define PDEV_PID_GENERIC            0
#define PDEV_DID_USB_DWC3           1   // DWC3 USB Controller
#define PDEV_DID_USB_XHCI           2   // XHCI USB Controller
#define PDEV_DID_KPCI               3   // Syscall based PCI driver
#define PDEV_DID_ARM_MALI           4   // ARM MALI GPU
#define PDEV_DID_USB_DWC2           5   // DWC2 USB Controller
#define PDEV_DID_RTC_PL031          6   // ARM Primecell PL031 RTC
#define PDEV_DID_DSI                7   // DSI
#define PDEV_DID_GPIO_TEST          8   // Simple GPIO test driver
#define PDEV_DID_DW_I2C             9   // Designware I2C
#define PDEV_DID_DW_PCIE            10  // Designware PCIe

// QEMU emulator
#define PDEV_VID_QEMU               1
#define PDEV_PID_QEMU               1

// 96Boards
#define PDEV_VID_96BOARDS           2
#define PDEV_PID_HIKEY960           1

#define PDEV_DID_HIKEY960_I2C_TEST  1

// Google
#define PDEV_VID_GOOGLE             3
#define PDEV_PID_GAUSS              1
#define PDEV_PID_MACHINA            2

#define PDEV_DID_GAUSS_AUDIO_IN     1
#define PDEV_DID_GAUSS_AUDIO_OUT    2
#define PDEV_DID_GAUSS_I2C_TEST     3
#define PDEV_DID_GAUSS_LED          4

// Khadas
#define PDEV_VID_KHADAS             4
#define PDEV_PID_VIM                1
#define PDEV_PID_VIM2               2

#define PDEV_DID_VIM_DISPLAY        1

// Amlogic
#define PDEV_VID_AMLOGIC            5
#define PDEV_PID_AMLOGIC_A113       1
#define PDEV_PID_AMLOGIC_S905X      2
#define PDEV_PID_AMLOGIC_S912       3
#define PDEV_PID_AMLOGIC_S905       4
#define PDEV_PID_AMLOGIC_S905D2     5

#define PDEV_DID_AMLOGIC_GPIO       1
#define PDEV_DID_AMLOGIC_I2C        2
#define PDEV_DID_AMLOGIC_UART       3
#define PDEV_DID_AMLOGIC_AXG_CLK    4
#define PDEV_DID_AMLOGIC_GXL_CLK    5

// Broadcom
#define PDEV_VID_BROADCOM           6
#define PDEV_PID_BCM4356            1

//Hardkernel
#define PDEV_VID_HARDKERNEL         7
#define PDEV_PID_ODROID_C2          1

// Intel
#define PDEV_VID_INTEL              8
#define PDEV_DID_INTEL_CPU_TRACE    1 // Intel CPU tracing driver

// NXP
#define PDEV_VID_NXP                9
#define PDEV_PID_IMX8MEVK           1

#define PDEV_DID_IMX_GPIO           1

// MediaTek. Boards' bootdata carries these IDs (build/bootfs/BUILD.gn), and
// garnet's programs name some platform devices by them (guest_manager opens
// /dev/sys/platform/00:00:f), so they keep the values garnet was written
// against.
#define PDEV_VID_MTK                100
#define PDEV_PID_MT8675             11
#define PDEV_PID_MT8678             20
#define PDEV_PID_MT8676             21
#define PDEV_PID_MT8668             22

#define PDEV_DID_GRT_UFS            1   // UFS host controller
#define PDEV_DID_GRT_WDT            2   // Watchdog
#define PDEV_DID_GRT_RAMFB          3   // RAM framebuffer
#define PDEV_DID_GRT_FRAMEBUFFER    4   // Framebuffer
#define PDEV_DID_GRT_SPI            5   // SPI controller
#define PDEV_DID_GRT_GPIO           6   // GPIO controller
#define PDEV_DID_GRT_MONITOR        7   // Monitor
#define PDEV_DID_GRT_EINT           8   // External interrupt controller
#define PDEV_DID_GRT_RTC            9   // Real-time clock
#define PDEV_DID_GRT_GUEST_MEMORY_ALLOCATOR 15  // Guest memory allocator

#define PDEV_DID_GRT_DISP           3   // Display controller

// GoldenRiver's MediaTek ARMv8 boards.
#define PDEV_VID_GRT_MTKV8          100

// Qualcomm
#define PDEV_VID_QCOM               101

// Nebula's own platform devices.
#define PDEV_VID_GRT                0x545247 // 'GRT'
#define PDEV_PID_NBL                0x4c424e // 'NBL'
#define PDEV_DID_NBL_BOARD          4   // Board; 3 is the kernel PCI device
#define PDEV_DID_NBL_DISPLAY        5   // Display controller
#define PDEV_DID_NBL_TOUCH          6   // Touch controller
#define PDEV_DID_NBL_DRVTEST        7   // Driver API test device

#define PDEV_FSL_IMX1_I2C           0x1000
#define PDEV_FSL_IMX21_I2C          0x1001
#define PDEV_QEMU_ECHO_I2C          0xF000

__END_CDECLS;
