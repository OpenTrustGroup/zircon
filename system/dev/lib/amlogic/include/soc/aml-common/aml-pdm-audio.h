// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddktl/mmio.h>
#include <fbl/unique_ptr.h>
#include <soc/aml-common/aml-audio-regs.h>
/*
    Presently assumes stereo input with both streams multiplexed on the same
    PDM input line. (TODO: support up to 8 channels to refactor gauss to use this)
*/

class AmlPdmDevice {

public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(AmlPdmDevice);

    static fbl::unique_ptr<AmlPdmDevice> Create(ddk::MmioBuffer pdm_mmio,
                                                ddk::MmioBuffer audio_mmio,
                                                ee_audio_mclk_src_t pdm_clk_src,
                                                uint32_t sclk_div,
                                                uint32_t dclk_div,
                                                aml_toddr_t toddr_dev);

    // Sets the buffer/length pointers for dma engine
    //  must resize in lower 32-bits of address space
    zx_status_t SetBuffer(zx_paddr_t buf, size_t len);

    /*
        Returns offset of dma pointer in the ring buffer
    */
    uint32_t GetRingPosition();

    /*
        Resets state of dma mechanisms and starts clocking data
        in from pdm bus with data written to start of ring buffer
    */
    uint64_t Start();

    /*
        Stops clocking stat in off PDM bus
        (physical pdm bus signals remain active)
    */
    void Stop();

    /*
        Synchronize the state of PDM bus signals with fifo/dma engine
    */
    void Sync();

    /*
        shuts down toddr, stops writing data to ring buffer
    */
    void Shutdown();

    uint32_t fifo_depth() const { return fifo_depth_; };

private:
    friend class fbl::unique_ptr<AmlPdmDevice>;

    AmlPdmDevice(ddk::MmioBuffer pdm_mmio, ddk::MmioBuffer audio_mmio,
                 ee_audio_mclk_src_t clk_src, uint32_t sysclk_div, uint32_t dclk_div,
                 aml_toddr_t toddr, uint32_t fifo_depth)
        : fifo_depth_(fifo_depth),
          toddr_ch_(toddr),
          clk_src_(clk_src),
          sysclk_div_(sysclk_div),
          dclk_div_(dclk_div),
          toddr_base_(GetToddrBase(toddr)),
          pdm_mmio_(fbl::move(pdm_mmio)),
          audio_mmio_(fbl::move(audio_mmio)){};

    ~AmlPdmDevice() = default;

    void ConfigFilters();

    /* Get the resgister block offset for our ddr block */
    static zx_off_t GetToddrBase(aml_toddr_t ch) {
        switch (ch) {
        case TODDR_A:
            return EE_AUDIO_TODDR_A_CTRL0;
        case TODDR_B:
            return EE_AUDIO_TODDR_B_CTRL0;
        case TODDR_C:
            return EE_AUDIO_TODDR_C_CTRL0;
        }
        //We should never get here, but if we do, make it hard to ignore
        ZX_PANIC("Invalid toddr channel specified!\n");
        return 0;
    }

    void AudioClkEna(uint32_t audio_blk_mask);
    void AudioClkDis(uint32_t audio_blk_mask);
    void InitRegs();
    void TODDREnable();
    void TODDRDisable();
    void PdmInDisable();
    void PdmInEnable();

    /* Get the resgister block offset for our ddr block */
    zx_off_t GetToddrOffset(zx_off_t off) {
        return toddr_base_ + off;
    }
    const uint32_t fifo_depth_;
    const aml_toddr_t toddr_ch_; // fromddr channel used by this instance
    const ee_audio_mclk_src_t clk_src_;
    const uint32_t sysclk_div_;
    const uint32_t dclk_div_;
    const zx_off_t toddr_base_; // base offset of frddr ch used by this instance
    const ddk::MmioBuffer pdm_mmio_;
    const ddk::MmioBuffer audio_mmio_;
};
