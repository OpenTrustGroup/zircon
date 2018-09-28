// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddktl/mmio.h>
#include <fbl/unique_ptr.h>
#include <soc/aml-common/aml-audio-regs.h>

class AmlTdmDevice {

public:

    DISALLOW_COPY_ASSIGN_AND_MOVE(AmlTdmDevice);

    static constexpr int32_t kMclkDivBits = 16;
    static constexpr int32_t kSclkDivBits = 10;
    static constexpr int32_t kLRclkDivBits = 10;

    static fbl::unique_ptr<AmlTdmDevice> Create(ddk::MmioBuffer mmio,
                                                ee_audio_mclk_src_t src,
                                                aml_tdm_out_t tdm_dev,
                                                aml_frddr_t frddr_dev,
                                                aml_tdm_mclk_t mclk);

    //Configure an mclk channel divider
    zx_status_t SetMclkDiv(uint32_t div);
    //Configure an sclk/lclk generator block
    zx_status_t SetSclkDiv(uint32_t sdiv, uint32_t lrduty, uint32_t lrdiv);

    // Configures placement of data on the tdm bus
    void ConfigTdmOutSlot(uint8_t bit_offset, uint8_t num_slots,
                          uint8_t bits_per_slot, uint8_t bits_per_sample);

    // Sets the buffer/length pointers for dma engine
    //  must resize in lower 32-bits of address space
    zx_status_t SetBuffer(zx_paddr_t buf, size_t len);

    /*
        Returns offset of dma pointer in the ring buffer
    */
    uint32_t GetRingPosition();

    /*
        Resets state of dma mechanisms and starts clocking data
        onto tdm bus with data fetched from beginning of buffer
    */
    uint64_t Start();

    /*
        Stops clocking data out on the TDM bus
        (physical tdm bus signals remain active)
    */
    void Stop();

    /*
        Synchronize the state of TDM bus signals with fifo/dma engine
    */
    void Sync();

    /*
        Stops the clocking data, shuts down frddr, and quiets output signals
    */
    void Shutdown();

    uint32_t fifo_depth() const { return fifo_depth_;};

private:
    const uint32_t fifo_depth_;
    const aml_tdm_out_t tdm_ch_;   // tdm output block used by this instance
    const aml_frddr_t frddr_ch_;   // fromddr channel used by this instance
    const aml_tdm_mclk_t mclk_ch_; // mclk channel used by this instance
    const ee_audio_mclk_src_t clk_src_;
    const zx_off_t frddr_base_;    // base offset of frddr ch used by this instance
    const zx_off_t tdm_base_;      // base offset of our tdmout block
    const ddk::MmioBuffer mmio_;
    friend class fbl::unique_ptr<AmlTdmDevice>;

    AmlTdmDevice(ddk::MmioBuffer mmio, ee_audio_mclk_src_t clk_src,
        aml_tdm_out_t tdm, aml_frddr_t frddr, aml_tdm_mclk_t mclk,
        uint32_t fifo_depth) :
        fifo_depth_(fifo_depth),
        tdm_ch_(tdm),
        frddr_ch_(frddr),
        mclk_ch_(mclk),
        clk_src_(clk_src),
        frddr_base_(GetFrddrBase(frddr)),
        tdm_base_(GetTdmBase(tdm)),
        mmio_(fbl::move(mmio)) {};

    ~AmlTdmDevice() = default;

    /* Get the resgister block offset for our ddr block */
    static zx_off_t GetFrddrBase(aml_frddr_t ch) {
        switch (ch) {
        case FRDDR_A:
            return EE_AUDIO_FRDDR_A_CTRL0;
        case FRDDR_B:
            return EE_AUDIO_FRDDR_B_CTRL0;
        case FRDDR_C:
            return EE_AUDIO_FRDDR_C_CTRL0;
        }
        //We should never get here, but if we do, make it obvious
        assert(0);
        return 0;
    }
    /* Get the register block offset for our tdm block */
    static zx_off_t GetTdmBase(aml_tdm_out_t ch) {
        switch (ch) {
        case TDM_OUT_A:
            return EE_AUDIO_TDMOUT_A_CTRL0;
        case TDM_OUT_B:
            return EE_AUDIO_TDMOUT_B_CTRL0;
        case TDM_OUT_C:
            return EE_AUDIO_TDMOUT_C_CTRL0;
        }
        //We should never get here, but if we do, make it obvious
        assert(0);
        return 0;
    }

    void AudioClkEna(uint32_t audio_blk_mask);
    void AudioClkDis(uint32_t audio_blk_mask);
    void InitRegs();
    void FRDDREnable();
    void FRDDRDisable();
    void TdmOutDisable();
    void TdmOutEnable();

    /* Get the resgister block offset for our ddr block */
    zx_off_t GetFrddrOffset(zx_off_t off) {
        return frddr_base_ + off;
    }
    /* Get the register block offset for our tdm block */
    zx_off_t GetTdmOffset(zx_off_t off) {
        return tdm_base_ + off;
    }
};
