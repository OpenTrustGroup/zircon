// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddk/protocol/display-controller.h>
#include <ddk/protocol/i2c-impl.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/edid/edid.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include "id-map.h"
#include "image.h"

namespace display {

class ClientProxy;
class Controller;
class DisplayConfig;

class DisplayInfo : public IdMappable<fbl::RefPtr<DisplayInfo>>,
                    public fbl::RefCounted<DisplayInfo> {
public:
    bool has_edid;
    edid::Edid edid;
    fbl::Vector<edid::timing_params_t> edid_timings;
    fbl::Vector<audio_stream_format_range_t> edid_audio_;
    display_params_t params;

    fbl::Array<uint8_t> edid_data_;
    fbl::Array<zx_pixel_format_t> pixel_formats_;
    fbl::Array<cursor_info_t> cursor_infos_;

    // Flag indicating that the display is ready to be published to clients.
    bool init_done = false;

    // A list of all images which have been sent to display driver. For multiple
    // images which are displayed at the same time, images with a lower z-order
    // occur first.
    list_node_t images = LIST_INITIAL_VALUE(images);
    // The number of layers in the applied configuration which are important for vsync (i.e.
    // that have images).
    uint32_t vsync_layer_count;

    // Set when a layer change occurs on this display and cleared in vsync
    // when the new layers are all active.
    bool pending_layer_change;
    // Flag indicating that a new configuration was delayed during a layer change
    // and should be reapplied after the layer change completes.
    bool delayed_apply;

    // True when we're in the process of switching between display clients.
    bool switching_client = false;
};

using ControllerParent = ddk::Device<Controller, ddk::Unbindable, ddk::Openable, ddk::OpenAtable>;
class Controller : public ControllerParent,
                   public ddk::EmptyProtocol<ZX_PROTOCOL_DISPLAY_CONTROLLER> {
public:
    Controller(zx_device_t* parent);

    static void PopulateDisplayMode(const edid::timing_params_t& params, display_mode_t* mode);

    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    zx_status_t DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags);
    void DdkUnbind();
    void DdkRelease();
    zx_status_t Bind(fbl::unique_ptr<display::Controller>* device_ptr);

    void OnDisplaysChanged(added_display_args_t* displays_added, uint32_t added_count,
                           uint64_t* displays_removed, uint32_t removed_count);
    void OnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                        void** handles, uint32_t handle_count);
    zx_status_t GetAudioFormat(uint64_t display_id, uint32_t fmt_idx,
                               audio_stream_format_range_t* fmt_out);
    void OnClientDead(ClientProxy* client);
    void SetVcMode(uint8_t mode);
    void ShowActiveDisplay();

    void ApplyConfig(DisplayConfig* configs[], int32_t count,
                     bool vc_client, uint32_t apply_stamp);

    void ReleaseImage(Image* image);

    // Calling GetPanelConfig requires holding |mtx()|, and it must be held
    // for as long as |edid| and |params| are retained.
    bool GetPanelConfig(uint64_t display_id, const fbl::Vector<edid::timing_params_t>** timings,
                        const display_params_t** params) __TA_NO_THREAD_SAFETY_ANALYSIS;
    // Calling GetSupportedPixelFormats requires holding |mtx()|
    bool GetSupportedPixelFormats(uint64_t display_id, fbl::Array<zx_pixel_format_t>* fmts_out)
                                  __TA_NO_THREAD_SAFETY_ANALYSIS;
    // Calling GetCursorInfo requires holding |mtx()|
    bool GetCursorInfo(uint64_t display_id, fbl::Array<cursor_info_t>* cursor_info_out)
                       __TA_NO_THREAD_SAFETY_ANALYSIS;

    display_controller_protocol_ops_t* ops() { return ops_.ops; }
    void* ops_ctx() { return ops_.ctx; }
    async::Loop& loop() { return loop_; }
    bool current_thread_is_loop() { return thrd_current() == loop_thread_; }
    mtx_t* mtx() { return &mtx_; }
private:
    void HandleClientOwnershipChanges() __TA_REQUIRES(mtx_);
    void PopulateDisplayTimings(const fbl::RefPtr<DisplayInfo>& info) __TA_EXCLUDES(mtx_);
    void PopulateDisplayAudio(const fbl::RefPtr<DisplayInfo>& info);

    // mtx_ is a global lock on state shared among clients.
    mtx_t mtx_;

    DisplayInfo::Map displays_ __TA_GUARDED(mtx_);
    bool vc_applied_;
    uint32_t applied_stamp_ = UINT32_MAX;

    ClientProxy* vc_client_ __TA_GUARDED(mtx_) = nullptr;
    bool vc_ready_ __TA_GUARDED(mtx_) ;
    ClientProxy* primary_client_ __TA_GUARDED(mtx_) = nullptr;
    bool primary_ready_ __TA_GUARDED(mtx_) ;
    uint8_t vc_mode_ __TA_GUARDED(mtx_) = fuchsia_display_VirtconMode_INACTIVE;
    ClientProxy* active_client_ __TA_GUARDED(mtx_) = nullptr;

    async::Loop loop_;
    thrd_t loop_thread_;
    display_controller_protocol_t ops_;
    i2c_impl_protocol_t i2c_ops_;
    bool has_i2c_ops_;
};

} // namespace display

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t display_controller_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
