// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <ddk/protocol/platform-device.h>

__BEGIN_CDECLS
struct vim2_display;  // fwd decl
__END_CDECLS

#ifdef __cplusplus

#include <fbl/macros.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include "vim-audio-utils.h"
#include "vim-spdif-audio-stream.h"

namespace audio {
namespace vim2 {

class Vim2Audio {
  public:
    Vim2Audio() = default;
    ~Vim2Audio();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vim2Audio);

    // Ddk Hooks
    static zx_status_t DriverBind(zx_device_t* parent);
    void DdkUnbind();
    void DdkRelease();

    // Display driver hooks
    zx_status_t Init(const platform_device_protocol_t* pdev);
    void OnDisplayAdded(const struct vim2_display* display, uint64_t display_id);
    void OnDisplayRemoved(uint64_t display_id);

  private:
    zx::bti audio_bti_;
    fbl::RefPtr<Registers> regs_;
    fbl::RefPtr<RefCountedVmo> spdif_rb_vmo_;
    fbl::RefPtr<Vim2SpdifAudioStream> spdif_stream_;
};

}  // namespace vim2
}  // namespace audio

#endif  // __cplusplus

__BEGIN_CDECLS
typedef struct vim2_audio vim2_audio_t;

zx_status_t vim2_audio_create(const platform_device_protocol_t* pdev,
                              vim2_audio_t **out_audio);
void vim2_audio_shutdown(vim2_audio_t** inout_audio);

void vim2_audio_on_display_added(const struct vim2_display* display, uint64_t display_id);
void vim2_audio_on_display_removed(const struct vim2_display* display, uint64_t display_id);
__END_CDECLS
