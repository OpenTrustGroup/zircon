// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro.h"
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zircon/device/thermal.h>

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = S905D2_TEMP_SENSOR_BASE,
        .length = S905D2_TEMP_SENSOR_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        .base = S905D2_AO_PWM_CD_BASE,
        .length = S905D2_AO_PWM_LENGTH,
    }};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = S905D2_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t thermal_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_THERMAL,
    },
};

static const pbus_clk_t thermal_clk_gates[] = {
    {
        .clk = CLK_SYS_PLL_DIV16,
    },
    {
        .clk = CLK_SYS_CPU_CLK_DIV16,
    },
};

/*
 * PASSIVE COOLING - For Astro, we have DVFS support added
 * Below is the operating point information for Big cluster
 * Operating point 0  - Freq 0.1000 Ghz Voltage 0.7310 V
 * Operating point 1  - Freq 0.2500 Ghz Voltage 0.7310 V
 * Operating point 2  - Freq 0.5000 Ghz Voltage 0.7310 V
 * Operating point 3  - Freq 0.6670 Ghz Voltage 0.7310 V
 * Operating point 4  - Freq 1.0000 Ghz Voltage 0.7310 V
 * Operating point 5  - Freq 1.2000 Ghz Voltage 0.7310 V
 * Operating point 6  - Freq 1.3980 Ghz Voltage 0.7610 V
 * Operating point 7  - Freq 1.5120 Ghz Voltage 0.7910 V
 * Operating point 8  - Freq 1.6080 Ghz Voltage 0.8310 V
 * Operating point 9  - Freq 1.7040 Ghz Voltage 0.8610 V
 * Operating point 10 - Freq 1.8960 Ghz Voltage 0.9810 V
 *
 * GPU_CLK_FREQUENCY_SOURCE -
 * 0 - 285.7 MHz
 * 1 - 400 MHz
 * 2 - 500 MHz
 * 3 - 666 MHz
 * 4 - 800 MHz
 * 5 - 846 MHz
 */

// clang-format off

// NOTE: This is a very trivial policy, no data backing it up
// As we do more testing this policy can evolve.
static thermal_device_info_t aml_astro_config = {
    .active_cooling                     = false,
    .passive_cooling                    = true,
    .gpu_throttling                     = true,
    .num_trip_points                    = 7,
    .critical_temp                      = 102,
    .big_little                         = false,
    .trip_point_info                    = {
        // Below trip point info is dummy for now.
        {
            // This is the initial thermal setup of the device.
            // CPU freq set to a known stable MAX.
            .big_cluster_dvfs_opp       = 10,
            .gpu_clk_freq_source        = 5,
        },
        {
            .up_temp                    = 75,
            .down_temp                  = 73,
            .big_cluster_dvfs_opp       = 9,
            .gpu_clk_freq_source        = 4,
        },
        {
            .up_temp                    = 80,
            .down_temp                  = 77,
            .big_cluster_dvfs_opp       = 8,
            .gpu_clk_freq_source        = 3,
        },
        {
            .up_temp                    = 85,
            .down_temp                  = 83,
            .big_cluster_dvfs_opp       = 7,
            .gpu_clk_freq_source        = 3,
        },
        {
            .up_temp                    = 90,
            .down_temp                  = 88,
            .big_cluster_dvfs_opp       = 6,
            .gpu_clk_freq_source        = 2,
        },
        {
            .up_temp                    = 95,
            .down_temp                  = 93,
            .big_cluster_dvfs_opp       = 5,
            .gpu_clk_freq_source        = 1,
        },
        {
            .up_temp                    = 100,
            .down_temp                  = 98,
            .big_cluster_dvfs_opp       = 4,
            .gpu_clk_freq_source        = 0,
        },
    },
};

// clang-format on
static opp_info_t aml_opp_info = {
    .voltage_table = {
        {1022000, 0},
        {1011000, 3},
        {1001000, 6},
        {991000, 10},
        {981000, 13},
        {971000, 16},
        {961000, 20},
        {951000, 23},
        {941000, 26},
        {931000, 30},
        {921000, 33},
        {911000, 36},
        {901000, 40},
        {891000, 43},
        {881000, 46},
        {871000, 50},
        {861000, 53},
        {851000, 56},
        {841000, 60},
        {831000, 63},
        {821000, 67},
        {811000, 70},
        {801000, 73},
        {791000, 76},
        {781000, 80},
        {771000, 83},
        {761000, 86},
        {751000, 90},
        {741000, 93},
        {731000, 96},
        {721000, 100},
    },
    .opps = {
        {
            // 0
            .freq_hz = 100000000,
            .volt_mv = 731000,
        },
        {
            // 1
            .freq_hz = 250000000,
            .volt_mv = 731000,
        },
        {
            // 2
            .freq_hz = 500000000,
            .volt_mv = 731000,
        },
        {
            // 3
            .freq_hz = 667000000,
            .volt_mv = 731000,
        },
        {
            // 4
            .freq_hz = 1000000000,
            .volt_mv = 731000,
        },
        {
            // 5
            .freq_hz = 1200000000,
            .volt_mv = 731000,
        },
        {
            // 6
            .freq_hz = 1398000000,
            .volt_mv = 761000,
        },
        {
            // 7
            .freq_hz = 1512000000,
            .volt_mv = 791000,
        },
        {
            // 8
            .freq_hz = 1608000000,
            .volt_mv = 831000,
        },
        {
            // 9
            .freq_hz = 1704000000,
            .volt_mv = 861000,
        },
        {
            // 10
            .freq_hz = 1896000000,
            .volt_mv = 981000,
        },
    },
};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = THERMAL_CONFIG_METADATA,
        .data = &aml_astro_config,
        .len = sizeof(aml_astro_config),
    },
    {
        .type = VOLTAGE_DUTY_CYCLE_METADATA,
        .data = &aml_opp_info,
        .len = sizeof(aml_opp_info),
    },
};

static pbus_dev_t thermal_dev = {
    .name = "aml-thermal",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_THERMAL,
    .mmios = thermal_mmios,
    .mmio_count = countof(thermal_mmios),
    .clks = thermal_clk_gates,
    .clk_count = countof(thermal_clk_gates),
    .irqs = thermal_irqs,
    .irq_count = countof(thermal_irqs),
    .btis = thermal_btis,
    .bti_count = countof(thermal_btis),
    .metadata = thermal_metadata,
    .metadata_count = countof(thermal_metadata),
};

zx_status_t aml_thermal_init(aml_bus_t* bus) {
    // Configure the GPIO to be Output & set it to alternate
    // function 3 which puts in PWM_D mode.
    zx_status_t status = gpio_config_out(&bus->gpio, S905D2_PWM_D, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: gpio_config failed: %d\n", status);
        return status;
    }

    status = gpio_set_alt_function(&bus->gpio, S905D2_PWM_D, S905D2_PWM_D_FN);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: gpio_set_alt_function failed: %d\n", status);
        return status;
    }

    status = pbus_device_add(&bus->pbus, &thermal_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}
