// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hiu.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include <limits.h>
#include "astro.h"

//clang-format off
static const pbus_gpio_t audio_gpios[] = {
    {
        // AUDIO_SOC_FAULT_L
        .gpio = S905D2_GPIOA(4),
    },
    {
        // SOC_AUDIO_EN
        .gpio = S905D2_GPIOA(5),
    },
};


static const pbus_mmio_t audio_mmios[] = {
    {
        .base = S905D2_EE_AUDIO_BASE,
        .length = S905D2_EE_AUDIO_LENGTH
    },
};

static const pbus_bti_t tdm_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_OUT,
    },
};

static const pbus_i2c_channel_t codec_i2c[] = {
    {
        .bus_id = 2,
        .address = 0x48,
    },
};

static pbus_dev_t aml_tdm_dev = {
    .name = "AstroAudio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_TDM,
    .gpios = audio_gpios,
    .gpio_count = countof(audio_gpios),
    .i2c_channels = codec_i2c,
    .i2c_channel_count = countof(codec_i2c),
    .mmios = audio_mmios,
    .mmio_count = countof(audio_mmios),
    .btis = tdm_btis,
    .bti_count = countof(tdm_btis),
};

//PDM input configurations
static const pbus_mmio_t pdm_mmios[] = {
    {
        .base = S905D2_EE_PDM_BASE,
        .length = S905D2_EE_PDM_LENGTH
    },
    {
        .base = S905D2_EE_AUDIO_BASE,
        .length = S905D2_EE_AUDIO_LENGTH
    },
};

static const pbus_bti_t pdm_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO_IN,
    },
};

static const pbus_dev_t aml_pdm_dev = {
    .name = "gauss-audio-in",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_ASTRO_PDM,
    .mmios = pdm_mmios,
    .mmio_count = countof(pdm_mmios),
    .btis = pdm_btis,
    .bti_count = countof(pdm_btis),
};

//clang-format on

zx_status_t astro_tdm_init(aml_bus_t* bus) {

    aml_hiu_dev_t hiu;
    zx_handle_t bti;
    zx_status_t status = iommu_get_bti(&bus->iommu, 0, 0, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "audio_bus_bind: iommu_get_bti failed: %d\n", status);
        return status;
    }

    status = s905d2_hiu_init(bti, &hiu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hiu_init: failed: %d\n", status);
        return status;
    }

    aml_pll_dev_t hifi_pll;
    s905d2_pll_init(&hiu, &hifi_pll, HIFI_PLL);
    status = s905d2_pll_set_rate(&hifi_pll, 1536000000);
    if (status != ZX_OK) {
        zxlogf(ERROR,"Invalid rate selected for hifipll\n");
        return status;
    }

    s905d2_pll_ena(&hifi_pll);

    // TDM pin assignments
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOA(1), S905D2_GPIOA_1_TDMB_SCLK_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOA(2), S905D2_GPIOA_2_TDMB_FS_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOA(3), S905D2_GPIOA_3_TDMB_D0_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOA(6), S905D2_GPIOA_6_TDMB_DIN3_FN);

    // PDM pin assignments
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOA(7), S905D2_GPIOA_7_PDM_DCLK_FN);
    gpio_impl_set_alt_function(&bus->gpio, S905D2_GPIOA(8), S905D2_GPIOA_8_PDM_DIN0_FN);

    gpio_impl_config_out(&bus->gpio, S905D2_GPIOA(5), 1);

    status = pbus_device_add(&bus->pbus, &aml_tdm_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "astro_tdm_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_device_add(&bus->pbus, &aml_pdm_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "astro_tdm_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}