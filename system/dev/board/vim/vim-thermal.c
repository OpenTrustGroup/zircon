// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-common/aml-thermal.h>
#include "vim.h"

static const pbus_mmio_t mailbox_mmios[] = {
    // Mailbox
    {
        .base = S912_HIU_MAILBOX_BASE,
        .length = S912_HIU_MAILBOX_LENGTH,
    },
    // Mailbox Payload
    {
        .base = S912_MAILBOX_PAYLOAD_BASE,
        .length = S912_MAILBOX_PAYLOAD_LENGTH,
    },
};

// IRQ for Mailbox
static const pbus_irq_t mailbox_irqs[] = {
    {
        .irq = S912_MBOX_IRQ_RECEIV0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_RECEIV1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_RECEIV2,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_SEND3,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_SEND4,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
    {
        .irq = S912_MBOX_IRQ_SEND5,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    },
};

static const pbus_gpio_t fanctl_gpios[] = {
    {
        .gpio = S912_GPIODV(14),
    },
    {
        .gpio = S912_GPIODV(15),
    }
};

/* ACTIVE COOLING - For VIM2, we assume that all devices
 * are connected with a GPIO-controlled fan.
 * The GPIO controlled fan has 3 levels of speed (1-3)
 *
 * PASSIVE COOLING - For VIM2, we have DVFS support added
 * Below is the operating point information for Big cluster
 * Operating point 0 - Freq 0.1000 Ghz Voltage 0.9100 V
 * Operating point 1 - Freq 0.2500 Ghz Voltage 0.9100 V
 * Operating point 2 - Freq 0.5000 Ghz Voltage 0.9100 V
 * Operating point 3 - Freq 0.6670 Ghz Voltage 0.9500 V
 * Operating point 4 - Freq 1.0000 Ghz Voltage 0.9900 V
 * Operating point 5 - Freq 1.2000 Ghz Voltage 1.0700 V
 * Operating point 6 - Freq 1.2960 Ghz Voltage 1.1000 V
 *
 * Below is the operating point information for Little cluster
 * Operating point 0 - Freq 0.1000 Ghz Voltage 0.9100 V
 * Operating point 1 - Freq 0.2500 Ghz Voltage 0.9100 V
 * Operating point 2 - Freq 0.5000 Ghz Voltage 0.9100 V
 * Operating point 3 - Freq 0.6670 Ghz Voltage 0.9500 V
 * Operating point 4 - Freq 1.0000 Ghz Voltage 0.9900 V
 *
 * GPU_CLK_FREQUENCY_SOURCE - For VIM2, we support GPU
 * throttling. Currently we have pre-defined frequencies
 * we can set the GPU clock to, but we can always add more
 * One's we support now are below
 * Operating point  0 - 285.7 MHz
 * Operating point  1 - 400.0 MHz
 * Operating point  2 - 500.0 MHz
 * Operating point  3 - 666.0 MHz
 * Operating point -1 - INVALID/No throttling needed
 */

static thermal_device_info_t aml_vim2_config = {
    .active_cooling                 = true,
    .passive_cooling                = true,
    .gpu_throttling                 = true,
    .big_little                     = true,
    .num_trip_points                = 8,
    .critical_temp                  = 81,
    .trip_point_info = {
        {
            // This is the initial thermal setup of the device
            // Fan set to OFF
            // CPU freq set to a known stable MAX
            .fan_level                  = 0,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
            .gpu_clk_freq_source        = 3,
        },
        {
            .fan_level                  = 1,
            .up_temp                    = 65,
            .down_temp                  = 63,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
            .gpu_clk_freq_source        = 3,
        },
        {
            .fan_level                  = 2,
            .up_temp                    = 70,
            .down_temp                  = 68,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
            .gpu_clk_freq_source        = 3,
        },
        {
            .fan_level                  = 3,
            .up_temp                    = 75,
            .down_temp                  = 73,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
            .gpu_clk_freq_source        = 3,
        },
        {
            .fan_level                  = 3,
            .up_temp                    = 82,
            .down_temp                  = 79,
            .big_cluster_dvfs_opp       = 5,
            .little_cluster_dvfs_opp    = 4,
            .gpu_clk_freq_source        = 2,
        },
        {
            .fan_level                  = 3,
            .up_temp                    = 87,
            .down_temp                  = 84,
            .big_cluster_dvfs_opp       = 4,
            .little_cluster_dvfs_opp    = 4,
            .gpu_clk_freq_source        = 2,
        },
        {
            .fan_level                  = 3,
            .up_temp                    = 92,
            .down_temp                  = 89,
            .big_cluster_dvfs_opp       = 3,
            .little_cluster_dvfs_opp    = 3,
            .gpu_clk_freq_source        = 1,
        },
        {
            .fan_level                  = 3,
            .up_temp                    = 96,
            .down_temp                  = 93,
            .big_cluster_dvfs_opp       = 2,
            .little_cluster_dvfs_opp    = 2,
            .gpu_clk_freq_source        = 0,
        }
    }
};

static const pbus_metadata_t vim_thermal_metadata[] = {
    {
        .type       = DEVICE_METADATA_PRIVATE,
        .data       = &aml_vim2_config,
        .len        = sizeof(aml_vim2_config),
    }
};

static const pbus_dev_t scpi_children[] = {
    // VIM2 thermal driver
    {
        .gpios = fanctl_gpios,
        .gpio_count = countof(fanctl_gpios),
        .metadata = vim_thermal_metadata,
        .metadata_count = countof(vim_thermal_metadata),
    },
};

static const pbus_dev_t mailbox_children[] = {
    // Amlogic SCPI driver
    {
        .children = scpi_children,
        .child_count = countof(scpi_children),
    },
};

static const pbus_dev_t mailbox_dev = {
    .name = "mailbox",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_AMLOGIC_MAILBOX,
    .mmios = mailbox_mmios,
    .mmio_count = countof(mailbox_mmios),
    .irqs = mailbox_irqs,
    .irq_count = countof(mailbox_irqs),
    .children = mailbox_children,
    .child_count = countof(mailbox_children),
};

zx_status_t vim2_thermal_init(vim_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &mailbox_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_thermal_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}

