// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standard Includes
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

// DDK includes
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb.h>

// Zircon USB includes
#include <zircon/hw/usb-hub.h>
#include <zircon/hw/usb.h>
#include <sync/completion.h>

#include <dwc2/usb_dwc_regs.h>

#include <zircon/process.h>

#define NUM_HOST_CHANNELS 8
#define PAGE_MASK_4K (0xFFF)
#define USB_PAGE_START (USB_BASE & (~PAGE_MASK_4K))
#define USB_PAGE_SIZE (0x1000)
#define PAGE_REG_DELTA (USB_BASE - USB_PAGE_START)

#define MMIO_INDEX  0
#define IRQ_INDEX   0

// This is how many free requests we'll hang onto in our free request cache.
#define FREE_REQ_CACHE_THRESHOLD 1024

#define MAX_DEVICE_COUNT 65
#define ROOT_HUB_DEVICE_ID (MAX_DEVICE_COUNT - 1)

static volatile struct dwc_regs* regs = NULL;

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define IS_WORD_ALIGNED(ptr) ((ulong)(ptr) % sizeof(ulong) == 0)

// Log every 512th frame Overrun.
#define FRAME_OVERRUN_THRESHOLD 512
static uint32_t debug_frame_overrun_counter = 0;

typedef struct dwc_usb_device dwc_usb_device_t;

typedef enum dwc_endpoint_direction {
    DWC_EP_OUT = 0,
    DWC_EP_IN = 1,
} dwc_endpoint_direction_t;

typedef enum dwc_usb_data_toggle {
    DWC_TOGGLE_DATA0 = 0,
    DWC_TOGGLE_DATA1 = 2,
    DWC_TOGGLE_DATA2 = 1,
    DWC_TOGGLE_SETUP = 3,
} dwc_usb_data_toggle_t;

typedef enum dwc_ctrl_phase {
    CTRL_PHASE_SETUP = 1,
    CTRL_PHASE_DATA = 2,
    CTRL_PHASE_STATUS = 3,
} dwc_ctrl_phase_t;

typedef struct dwc_usb_transfer_request {
    list_node_t node;

    dwc_ctrl_phase_t ctrl_phase;
    usb_request_t* setup_req;

    size_t bytes_transferred;
    dwc_usb_data_toggle_t next_data_toggle;
    bool complete_split;

    // Number of packets queued for transfer before programming the channel.
    uint32_t packets_queued;

    // Number of bytes queued for transfer before programming the channel.
    uint32_t bytes_queued;

    // Total number of bytes in this transaction.
    uint32_t total_bytes_queued;

    bool short_attempt;

    usb_request_t* usb_req;

    uint32_t cspit_retries;

    // DEBUG
    uint32_t request_id;
} dwc_usb_transfer_request_t;

typedef struct dwc_usb_device {
    mtx_t devmtx;

    usb_speed_t speed;
    uint32_t hub_address;
    int port;
    uint32_t device_id;

    list_node_t endpoints;
} dwc_usb_device_t;

typedef struct dwc_usb {
    zx_device_t* zxdev;
    usb_bus_interface_t bus;
    zx_handle_t irq_handle;
    thrd_t irq_thread;
    zx_device_t* parent;

    // Pertaining to root hub transactions.
    mtx_t rh_req_mtx;
    completion_t rh_req_completion;
    list_node_t rh_req_head;

    // Pertaining to a free list of request structures.
    mtx_t free_req_mtx;
    list_node_t free_reqs;
    size_t free_req_count;  // Number of free requests on this list.

    // List of devices attached to this controller.
    dwc_usb_device_t usb_devices[MAX_DEVICE_COUNT];

    // Pertaining to requests scheduled against the mock root hub.
    mtx_t rh_status_mtx;
    dwc_usb_transfer_request_t* rh_intr_req;
    usb_port_status_t root_port_status;

    // Pertaining to the availability of channels on this device.
    mtx_t free_channel_mtx;
    completion_t free_channel_completion;
    uint8_t free_channels;
    uint32_t next_device_address;

    // Assign a new request ID to each request so that we know when it's scheduled
    // and when it's executed.
    uint32_t DBG_reqid;

    union dwc_host_channel_interrupts channel_interrupts[NUM_HOST_CHANNELS];
    completion_t channel_complete[NUM_HOST_CHANNELS];

    // Pertaining to threads waiting to schedule a packet on the next start of
    // frame on this device.
    mtx_t sof_waiters_mtx;
    uint n_sof_waiters;
    completion_t sof_waiters[NUM_HOST_CHANNELS];

    // Pool of free requests to reuse.
    usb_request_pool_t free_usb_reqs;
} dwc_usb_t;

typedef struct dwc_usb_endpoint {
    list_node_t node;
    uint8_t ep_address;

    mtx_t pending_request_mtx;
    list_node_t pending_requests; // List of requests pending for this endpoint.

    // Pointer to the device that owns this endpoint.
    dwc_usb_device_t* parent;

    usb_endpoint_descriptor_t desc;

    thrd_t request_scheduler_thread;
    completion_t request_pending_completion;
} dwc_usb_endpoint_t;

typedef struct dwc_usb_scheduler_thread_ctx {
    dwc_usb_endpoint_t* ep;
    dwc_usb_t* dwc;
} dwc_usb_scheduler_thread_ctx_t;

#define ALL_CHANNELS_FREE 0xff
static uint acquire_channel_blocking(dwc_usb_t* dwc);
static void release_channel(uint ch, dwc_usb_t* dwc);

#define MANUFACTURER_STRING 1
#define PRODUCT_STRING_2 2

static const uint8_t dwc_language_list[] =
    {4, /* bLength */ USB_DT_STRING, 0x09, 0x04, /* language ID */};
static const uint8_t dwc_manufacturer_string[] = // "Zircon"
    {16, /* bLength */ USB_DT_STRING, 'Z', 0, 'i', 0, 'r', 0, 'c', 0, 'o', 0, 'n', 0, 0, 0};
static const uint8_t dwc_product_string_2[] = // "USB 2.0 Root Hub"
    {
        36, /* bLength */ USB_DT_STRING, 'U', 0, 'S', 0, 'B', 0, ' ', 0, '2', 0, '.', 0, '0', 0, ' ', 0,
        'R', 0, 'o', 0, 'o', 0, 't', 0, ' ', 0, 'H', 0, 'u', 0, 'b', 0, 0, 0,
};

static const uint8_t* dwc_rh_string_table[] = {
    dwc_language_list,
    dwc_manufacturer_string,
    dwc_product_string_2,
};

// device descriptor for USB 2.0 root hub
static const usb_device_descriptor_t dwc_rh_descriptor = {
    .bLength = sizeof(usb_device_descriptor_t),
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = htole16(0x0200),
    .bDeviceClass = USB_CLASS_HUB,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 1,   // Single TT
    .bMaxPacketSize0 = 64,
    .idVendor = htole16(0x18D1),
    .idProduct = htole16(0xA002),
    .bcdDevice = htole16(0x0100),
    .iManufacturer = MANUFACTURER_STRING,
    .iProduct = PRODUCT_STRING_2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

// we are currently using the same configuration descriptors for both USB 2.0 and 3.0 root hubs
// this is not actually correct, but our usb-hub driver isn't sophisticated enough to notice
static const struct {
    usb_configuration_descriptor_t config;
    usb_interface_descriptor_t intf;
    usb_endpoint_descriptor_t endp;
} dwc_rh_config_descriptor = {
     .config = {
        .bLength = sizeof(usb_configuration_descriptor_t),
        .bDescriptorType = USB_DT_CONFIG,
        .wTotalLength = htole16(sizeof(dwc_rh_config_descriptor)),
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xE0,   // self powered
        .bMaxPower = 0,
    },
    .intf = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = USB_CLASS_HUB,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .endp = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN | 1,
        .bmAttributes = USB_ENDPOINT_INTERRUPT,
        .wMaxPacketSize = htole16(4),
        .bInterval = 12,
    },
};

static int endpoint_request_scheduler_thread(void* arg);

static inline bool is_roothub_request(dwc_usb_transfer_request_t* req) {
    usb_request_t* usb_req = req->usb_req;
    return usb_req->header.device_id == ROOT_HUB_DEVICE_ID;
}

static inline bool is_control_request(dwc_usb_transfer_request_t* req) {
    usb_request_t* usb_req = req->usb_req;
    return usb_req->header.ep_address == 0;
}

// Completes the usb request associated with a request then cleans up the request.
static void complete_request(
    dwc_usb_transfer_request_t* req,
    zx_status_t status,
    size_t length,
    dwc_usb_t* dwc) {
    if (req->setup_req) {
        usb_request_release(req->setup_req);
    }

    zxlogf(TRACE, "dwc-usb: complete request. id = %u, status = %d, "
            "length = %lu\n", req->request_id, status, length);

    usb_request_t* usb_req = req->usb_req;

    // Invalidate caches over this region since the DMA engine may have moved
    // data below us.
    if (status == ZX_OK) {
        usb_request_cache_flush_invalidate(usb_req, 0, length);
    }

    usb_request_complete(usb_req, status, length);

    // Put this back on the free list of requests, but make sure the free list
    // doesn't get too long.
    mtx_lock(&dwc->free_req_mtx);

    if (dwc->free_req_count >= FREE_REQ_CACHE_THRESHOLD) {
        // There are already too many requests on the free request list, just
        // throw this one away.
        free(req);
    } else {
        list_add_tail(&dwc->free_reqs, &req->node);
        dwc->free_req_count++;
    }

    mtx_unlock(&dwc->free_req_mtx);
}

static void dwc_complete_root_port_status_req(dwc_usb_t* dwc) {

    mtx_lock(&dwc->rh_status_mtx);

    if (dwc->root_port_status.wPortChange) {
        if (dwc->rh_intr_req && dwc->rh_intr_req->usb_req) {
            usb_request_t* usb_req = dwc->rh_intr_req->usb_req;
            uint16_t val = 0x2;
            usb_request_copyto(usb_req, (void*)&val, sizeof(val), 0);
            complete_request(dwc->rh_intr_req, ZX_OK, sizeof(val), dwc);
            dwc->rh_intr_req = NULL;
        }
    }
    mtx_unlock(&dwc->rh_status_mtx);
}

static void dwc_reset_host_port(void) {
    union dwc_host_port_ctrlstatus hw_status = regs->host_port_ctrlstatus;
    hw_status.enabled = 0;
    hw_status.connected_changed = 0;
    hw_status.enabled_changed = 0;
    hw_status.overcurrent_changed = 0;

    hw_status.reset = 1;
    regs->host_port_ctrlstatus = hw_status;

    // Spec defines that we must wait this long for a host port reset to settle
    // in.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(60)));

    hw_status.reset = 0;
    regs->host_port_ctrlstatus = hw_status;
}

static void dwc_host_port_power_on(void) {
    union dwc_host_port_ctrlstatus hw_status = regs->host_port_ctrlstatus;
    hw_status.enabled = 0;
    hw_status.connected_changed = 0;
    hw_status.enabled_changed = 0;
    hw_status.overcurrent_changed = 0;

    hw_status.powered = 1;
    regs->host_port_ctrlstatus = hw_status;
}

static zx_status_t wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected) {
    for (int i = 0; i < 100; i++) {
        if ((*ptr & bits) == expected) {
            return ZX_OK;
        }
        usleep(1000);
    }
    return ZX_ERR_TIMED_OUT;
}

static zx_status_t usb_dwc_softreset_core(void) {
    zx_status_t status = wait_bits(&regs->core_reset, DWC_AHB_MASTER_IDLE, DWC_AHB_MASTER_IDLE);
    if (status != ZX_OK) {
        return status;
    }

    regs->core_reset = DWC_SOFT_RESET;

    return wait_bits(&regs->core_reset, DWC_SOFT_RESET, 0);
}

static zx_status_t usb_dwc_setupcontroller(void) {
    const uint32_t rx_words = 1024;
    const uint32_t tx_words = 1024;
    const uint32_t ptx_words = 1024;

    regs->rx_fifo_size = rx_words;
    regs->nonperiodic_tx_fifo_size = (tx_words << 16) | rx_words;
    regs->host_periodic_tx_fifo_size = (ptx_words << 16) | (rx_words + tx_words);

    regs->ahb_configuration |= DWC_AHB_DMA_ENABLE | BCM_DWC_AHB_AXI_WAIT;

    union dwc_core_interrupts core_interrupt_mask;

    regs->core_interrupt_mask.val = 0;
    regs->core_interrupts.val = 0xffffffff;

    core_interrupt_mask.val = 0;
    core_interrupt_mask.host_channel_intr = 1;
    core_interrupt_mask.port_intr = 1;
    regs->core_interrupt_mask = core_interrupt_mask;

    regs->ahb_configuration |= DWC_AHB_INTERRUPT_ENABLE;

    return ZX_OK;
}

// Queue a transaction on the DWC root hub.

static void dwc_usb_request_queue_rh(dwc_usb_t* dwc,
                                     dwc_usb_transfer_request_t* req) {
    mtx_lock(&dwc->rh_req_mtx);

    list_add_tail(&dwc->rh_req_head, &req->node);

    mtx_unlock(&dwc->rh_req_mtx);

    // Signal to the processor thread to wake up and process this request.
    completion_signal(&dwc->rh_req_completion);
}

// Queue a transaction on external peripherals using the DWC host channels.
static void dwc_usb_request_queue_hw(dwc_usb_t* dwc,
                                     dwc_usb_transfer_request_t* req) {

    // Find the Device/Endpoint where this transaction is to be scheduled.
    usb_request_t* usb_req = req->usb_req;
    uint32_t device_id = usb_req->header.device_id;
    uint8_t ep_address = usb_req->header.ep_address;


    zxlogf(TRACE, "dwc-usb: queue usb req hw. dev_id = %u, ep = %u, req_id = %u, "
            "length = 0x%lx\n", device_id, ep_address, req->request_id,
            usb_req->header.length);

    assert(device_id < MAX_DEVICE_COUNT);
    dwc_usb_device_t* target_device = &dwc->usb_devices[device_id];
    assert(target_device);

    // Find the endpoint where this transaction should be scheduled.
    dwc_usb_endpoint_t* target_endpoint = NULL;
    dwc_usb_endpoint_t* ep_iter = NULL;
    list_for_every_entry (&target_device->endpoints, ep_iter, dwc_usb_endpoint_t, node) {
        if (ep_iter->ep_address == ep_address) {
            target_endpoint = ep_iter;
            break;
        }
    }
    assert(target_endpoint);

    if (ep_address == 0) {
        req->ctrl_phase = CTRL_PHASE_SETUP;
    }

    // Writeback any items pending on the cache. We don't want these to be
    // flushed during a DMA op.
    usb_request_cache_flush_invalidate(usb_req, 0, usb_req->header.length);

    // Append this transaction to the end of the Device/Endpoint's pending
    // transaction queue.
    mtx_lock(&target_endpoint->pending_request_mtx);
    list_add_tail(&target_endpoint->pending_requests, &req->node);
    mtx_unlock(&target_endpoint->pending_request_mtx);

    // Signal the Device/Endpoint to begin the transaction.
    completion_signal(&target_endpoint->request_pending_completion);
}

// Tries to take a request from the list of free request objects. If none are
// available, a new one is allocated
static dwc_usb_transfer_request_t* get_free_request(dwc_usb_t* dwc) {
    dwc_usb_transfer_request_t* result = NULL;

    mtx_lock(&dwc->free_req_mtx);

    if (list_is_empty(&dwc->free_reqs)) {
        // No more free requests, allocate a new one.
        // Make sure the free request count is consistent with the list.
        assert(dwc->free_req_count == 0);
        result = calloc(1, sizeof(*result));
    } else {
        // Take a request from the free list.
        result = list_remove_head_type(&dwc->free_reqs,
                                       dwc_usb_transfer_request_t, node);

        memset(result, 0, sizeof(*result));

        dwc->free_req_count--;
    }

    mtx_unlock(&dwc->free_req_mtx);

    return result;
}

static void do_dwc_usb_request_queue(dwc_usb_t* dwc, usb_request_t* usb_req) {
    // Once a usb request enters the low-level DWC stack, it is always encapsulated
    // by a dwc_usb_transfer_request_t.
    dwc_usb_transfer_request_t* req = get_free_request(dwc);
    if (!req) {
        // If we can't allocate memory for the request, complete the usb request with
        // a failure.
        usb_request_complete(usb_req, ZX_ERR_NO_MEMORY, 0);
        return;
    }

    // Initialize the request.
    req->usb_req = usb_req;

    req->request_id = dwc->DBG_reqid++;

    if (is_roothub_request(req)) {
        dwc_usb_request_queue_rh(dwc, req);
    } else {
        dwc_usb_request_queue_hw(dwc, req);
    }
}

size_t dwc_get_max_transfer_size(void* ctx, uint32_t device_id, uint8_t ep_address) {
    // Transfers limited to a single page until scatter/gather support is implemented
    return PAGE_SIZE;
}

static zx_status_t dwc_cancel_all(void* ctx, uint32_t device_id, uint8_t ep_address) {
    return ZX_ERR_NOT_SUPPORTED;
}

static void dwc_request_queue(void* ctx, usb_request_t* usb_req) {
    dwc_usb_t* usb_dwc = ctx;
    usb_header_t* header = &usb_req->header;

    if (usb_req->header.length > dwc_get_max_transfer_size(usb_dwc->zxdev, header->device_id,
                                                           header->ep_address)) {
        usb_request_complete(usb_req, ZX_ERR_INVALID_ARGS, 0);
    } else {
        dwc_usb_t* dwc = ctx;
        do_dwc_usb_request_queue(dwc, usb_req);
    }
}

static void dwc_unbind(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_unbind not implemented\n");
}

static void dwc_release(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_release not implemented\n");
}

static zx_protocol_device_t dwc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = dwc_unbind,
    .release = dwc_release,
};

static void dwc_set_bus_interface(void* ctx, usb_bus_interface_t* bus) {
    dwc_usb_t* dwc = ctx;

    if (bus) {
        memcpy(&dwc->bus, bus, sizeof(dwc->bus));
        usb_bus_add_device(&dwc->bus, ROOT_HUB_DEVICE_ID, 0, USB_SPEED_HIGH);
    } else {
        memset(&dwc->bus, 0, sizeof(dwc->bus));
    }
}

static size_t dwc_get_max_device_count(void* ctx) {
    return MAX_DEVICE_COUNT;
}

static zx_status_t dwc_enable_ep(void* _ctx, uint32_t device_id,
                                 usb_endpoint_descriptor_t* ep_desc,
                                 usb_ss_ep_comp_descriptor_t* ss_comp_desc,
                                 bool enable) {
    zxlogf(TRACE, "dwc_usb: enable_ep. dev_id = %u, ep = %u\n", device_id,
            ep_desc->bEndpointAddress);

    dwc_usb_t* dwc = _ctx;

    if (device_id == ROOT_HUB_DEVICE_ID) {
        // Nothing to be done for root hub.
        return ZX_OK;
    }

    // Disabling endpoints not supported at this time.
    assert(enable);

    dwc_usb_device_t* dev = &dwc->usb_devices[device_id];

    // Create a new endpoint.
    dwc_usb_endpoint_t* ep = calloc(1, sizeof(*ep));
    ep->ep_address = ep_desc->bEndpointAddress;
    list_initialize(&ep->pending_requests);
    ep->parent = dev;
    memcpy(&ep->desc, ep_desc, sizeof(*ep_desc));
    ep->request_pending_completion = COMPLETION_INIT;

    dwc_usb_scheduler_thread_ctx_t* ctx = malloc(sizeof(*ctx));
    ctx->ep = ep;
    ctx->dwc = dwc;

    thrd_create(
        &ep->request_scheduler_thread,
        endpoint_request_scheduler_thread,
        (void*)ctx);

    mtx_lock(&dev->devmtx);
    list_add_tail(&dev->endpoints, &ep->node);
    mtx_unlock(&dev->devmtx);

    return ZX_OK;
}

static uint64_t dwc_get_frame(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_get_frame not implemented\n");
    return ZX_OK;
}

zx_status_t dwc_config_hub(void* ctx, uint32_t device_id, usb_speed_t speed,
                           usb_hub_descriptor_t* descriptor) {
    // Not sure if DWC controller has to take any specific action here.
    return ZX_OK;
}

static void usb_control_complete(usb_request_t* usb_req, void* cookie) {
    completion_signal((completion_t*)cookie);
}

zx_status_t dwc_hub_device_added(void* _ctx, uint32_t hub_address, int port,
                                 usb_speed_t speed) {
    // Since a new device was just added it has a device address of 0 on the
    // bus until it is enumerated.
    zxlogf(INFO, "dwc_usb: hub device added, hub = %u, port = %d, speed = %d\n",
            hub_address, port, speed);

    dwc_usb_t* dwc = _ctx;

    dwc_usb_device_t* new_device = &dwc->usb_devices[0];
    dwc_usb_endpoint_t* ep0 = NULL;

    mtx_lock(&new_device->devmtx);

    new_device->hub_address = hub_address;
    new_device->port = port;
    new_device->speed = speed;

    // Find endpoint 0 on the default device (it should be the only endpoint);
    dwc_usb_endpoint_t* ep_iter = NULL;
    list_for_every_entry (&new_device->endpoints, ep_iter, dwc_usb_endpoint_t, node) {
        if (ep_iter->ep_address == 0) {
            ep0 = ep_iter;
            break;
        }
    }
    mtx_unlock(&new_device->devmtx);

    assert(ep0);

    // Since we don't know the Max Packet Size for the control endpoint of this
    // device yet, we set it to 8, which all devices are guaranteed to support.
    ep0->desc.wMaxPacketSize = 8;

    usb_request_t* get_desc = usb_request_pool_get(&dwc->free_usb_reqs, 64);
    if (get_desc == NULL) {
        zx_status_t status = usb_request_alloc(&get_desc, 64, 0);
        assert(status == ZX_OK);
    }

    completion_t completion = COMPLETION_INIT;

    get_desc->complete_cb = usb_control_complete;
    get_desc->cookie = &completion;
    get_desc->header.length = 8;
    get_desc->header.device_id = 0;

    get_desc->setup.bmRequestType = USB_ENDPOINT_IN;
    get_desc->setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    get_desc->setup.wValue = (USB_DT_DEVICE << 8);
    get_desc->setup.wIndex = 0;
    get_desc->setup.wLength = 8;

    dwc_request_queue(dwc, get_desc);
    completion_wait(&completion, ZX_TIME_INFINITE);

    usb_device_descriptor_t short_descriptor;
    usb_request_copyfrom(get_desc, &short_descriptor, get_desc->response.actual, 0);

    // Update the Max Packet Size of the control endpoint.
    ep0->desc.wMaxPacketSize = short_descriptor.bMaxPacketSize0;

    // Set the Device ID of the newly added device.
    usb_request_t* set_addr = usb_request_pool_get(&dwc->free_usb_reqs, 64);
    if (set_addr == NULL) {
        zx_status_t status = usb_request_alloc(&set_addr, 64, 0);
        assert(status == ZX_OK);
    }

    completion_reset(&completion);

    set_addr->complete_cb = usb_control_complete;
    set_addr->cookie = &completion;
    set_addr->header.length = 0;
    set_addr->header.device_id = 0;

    set_addr->setup.bmRequestType = USB_ENDPOINT_OUT;
    set_addr->setup.bRequest = USB_REQ_SET_ADDRESS;
    set_addr->setup.wValue = dwc->next_device_address;
    set_addr->setup.wIndex = 0;
    set_addr->setup.wLength = 0;

    dwc_request_queue(dwc, set_addr);
    completion_wait(&completion, ZX_TIME_INFINITE);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    usb_request_pool_add(&dwc->free_usb_reqs, set_addr);
    usb_request_pool_add(&dwc->free_usb_reqs, get_desc);

    mtx_lock(&dwc->usb_devices[dwc->next_device_address].devmtx);
    dwc->usb_devices[dwc->next_device_address].speed = speed;
    dwc->usb_devices[dwc->next_device_address].hub_address = hub_address;
    dwc->usb_devices[dwc->next_device_address].port = port;
    dwc->usb_devices[dwc->next_device_address].device_id = dwc->next_device_address;
    list_initialize(&dwc->usb_devices[dwc->next_device_address].endpoints);

    dwc_usb_endpoint_t* ctrl_endpoint = calloc(1, sizeof(*ctrl_endpoint));

    ctrl_endpoint->ep_address = 0;
    list_initialize(&ctrl_endpoint->pending_requests);
    ctrl_endpoint->parent = &dwc->usb_devices[dwc->next_device_address];
    ctrl_endpoint->desc.bLength = sizeof(ctrl_endpoint->desc);
    ctrl_endpoint->desc.bDescriptorType = USB_DT_ENDPOINT;
    ctrl_endpoint->desc.bEndpointAddress = 0;
    ctrl_endpoint->desc.bmAttributes = (USB_ENDPOINT_CONTROL);
    ctrl_endpoint->desc.wMaxPacketSize = short_descriptor.bMaxPacketSize0;
    ctrl_endpoint->desc.bInterval = 0;
    ctrl_endpoint->request_pending_completion = COMPLETION_INIT;

    list_add_tail(&dwc->usb_devices[dwc->next_device_address].endpoints, &ctrl_endpoint->node);

    dwc_usb_scheduler_thread_ctx_t* ctx = malloc(sizeof(*ctx));
    ctx->ep = ctrl_endpoint;
    ctx->dwc = dwc;

    thrd_create(
        &ctrl_endpoint->request_scheduler_thread,
        endpoint_request_scheduler_thread,
        (void*)ctx);

    mtx_unlock(&dwc->usb_devices[dwc->next_device_address].devmtx);

    usb_bus_add_device(&dwc->bus, dwc->next_device_address, hub_address, speed);

    dwc->next_device_address++;

    return ZX_OK;
}
zx_status_t dwc_hub_device_removed(void* ctx, uint32_t hub_address, int port) {
    zxlogf(ERROR, "dwc_usb: dwc_hub_device_removed not implemented\n");
    return ZX_OK;
}

zx_status_t dwc_reset_endpoint(void* ctx, uint32_t device_id, uint8_t ep_address) {
    return ZX_ERR_NOT_SUPPORTED;
}

static usb_hci_protocol_ops_t dwc_hci_protocol = {
    .request_queue = dwc_request_queue,
    .set_bus_interface = dwc_set_bus_interface,
    .get_max_device_count = dwc_get_max_device_count,
    .enable_endpoint = dwc_enable_ep,
    .get_current_frame = dwc_get_frame,
    .configure_hub = dwc_config_hub,
    .hub_device_added = dwc_hub_device_added,
    .hub_device_removed = dwc_hub_device_removed,
    .reset_endpoint = dwc_reset_endpoint,
    .get_max_transfer_size = dwc_get_max_transfer_size,
    .cancel_all = dwc_cancel_all,
};

static void dwc_handle_channel_irq(uint32_t channel, dwc_usb_t* dwc) {
    // Save the interrupt state of this channel.
    volatile struct dwc_host_channel* chanptr = &regs->host_channels[channel];
    dwc->channel_interrupts[channel] = chanptr->interrupts;

    // Clear the interrupt state of this channel.
    chanptr->interrupt_mask.val = 0;
    chanptr->interrupts.val = 0xffffffff;

    // Signal to the waiter that this channel is ready.
    completion_signal(&dwc->channel_complete[channel]);
}

static void dwc_handle_irq(dwc_usb_t* dwc) {
    union dwc_core_interrupts interrupts = regs->core_interrupts;

    if (interrupts.port_intr) {
        // Clear the interrupt.
        union dwc_host_port_ctrlstatus hw_status = regs->host_port_ctrlstatus;

        mtx_lock(&dwc->rh_status_mtx);

        dwc->root_port_status.wPortChange = 0;
        dwc->root_port_status.wPortStatus = 0;

        // This device only has one port.
        if (hw_status.connected)
            dwc->root_port_status.wPortStatus |= USB_PORT_CONNECTION;
        if (hw_status.enabled)
            dwc->root_port_status.wPortStatus |= USB_PORT_ENABLE;
        if (hw_status.suspended)
            dwc->root_port_status.wPortStatus |= USB_PORT_SUSPEND;
        if (hw_status.overcurrent)
            dwc->root_port_status.wPortStatus |= USB_PORT_OVER_CURRENT;
        if (hw_status.reset)
            dwc->root_port_status.wPortStatus |= USB_PORT_RESET;

        if (hw_status.speed == 2) {
            dwc->root_port_status.wPortStatus |= USB_PORT_LOW_SPEED;
        } else if (hw_status.speed == 0) {
            dwc->root_port_status.wPortStatus |= USB_PORT_HIGH_SPEED;
        }

        if (hw_status.connected_changed)
            dwc->root_port_status.wPortChange |= USB_C_PORT_CONNECTION;
        if (hw_status.enabled_changed)
            dwc->root_port_status.wPortChange |= USB_C_PORT_ENABLE;
        if (hw_status.overcurrent_changed)
            dwc->root_port_status.wPortChange |= USB_C_PORT_OVER_CURRENT;

        mtx_unlock(&dwc->rh_status_mtx);

        // Clear the interrupt.
        hw_status.enabled = 0;
        regs->host_port_ctrlstatus = hw_status;

        dwc_complete_root_port_status_req(dwc);
    }

    if (interrupts.sof_intr) {
        if ((regs->host_frame_number & 0x7) != 6) {
            for (size_t i = 0; i < NUM_HOST_CHANNELS; i++) {
                completion_signal(&dwc->sof_waiters[i]);
            }
        }
    }

    if (interrupts.host_channel_intr) {
        uint32_t chintr = regs->host_channels_interrupt;

        for (uint32_t ch = 0; ch < NUM_HOST_CHANNELS; ch++) {
            if ((1 << ch) & chintr) {
                dwc_handle_channel_irq(ch, dwc);
            }
        }
    }
}

// Thread to handle interrupts.
static int dwc_irq_thread(void* arg) {
    dwc_usb_t* dwc = (dwc_usb_t*)arg;

    while (1) {
        zx_status_t wait_res;
        uint64_t slots;

        wait_res = zx_interrupt_wait(dwc->irq_handle, &slots);
        if (wait_res != ZX_OK)
            zxlogf(ERROR, "dwc_usb: irq wait failed, retcode = %d\n", wait_res);

        dwc_handle_irq(dwc);
    }

    zxlogf(INFO, "dwc_usb: irq thread finished\n");
    return 0;
}

static zx_status_t dwc_host_port_set_feature(uint16_t feature) {
    if (feature == USB_FEATURE_PORT_POWER) {
        dwc_host_port_power_on();
        return ZX_OK;
    } else if (feature == USB_FEATURE_PORT_RESET) {
        dwc_reset_host_port();
        return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static void dwc_root_hub_get_descriptor(dwc_usb_transfer_request_t* req,
                                        dwc_usb_t* dwc) {
    usb_request_t* usb_req = req->usb_req;
    usb_setup_t* setup = &usb_req->setup;

    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);

    uint8_t desc_type = value >> 8;
    if (desc_type == USB_DT_DEVICE && index == 0) {
        if (length > sizeof(usb_device_descriptor_t))
            length = sizeof(usb_device_descriptor_t);
        usb_request_copyto(usb_req, &dwc_rh_descriptor, length, 0);
        complete_request(req, ZX_OK, length, dwc);
    } else if (desc_type == USB_DT_CONFIG && index == 0) {
        usb_configuration_descriptor_t* config_desc =
            (usb_configuration_descriptor_t*)&dwc_rh_config_descriptor;
        uint16_t desc_length = le16toh(config_desc->wTotalLength);
        if (length > desc_length)
            length = desc_length;
        usb_request_copyto(usb_req, &dwc_rh_config_descriptor, length, 0);
        complete_request(req, ZX_OK, length, dwc);
    } else if (value >> 8 == USB_DT_STRING) {
        uint8_t string_index = value & 0xFF;
        if (string_index < countof(dwc_rh_string_table)) {
            const uint8_t* string = dwc_rh_string_table[string_index];
            if (length > string[0])
                length = string[0];

            usb_request_copyto(usb_req, string, length, 0);
            complete_request(req, ZX_OK, length, dwc);
        } else {
            complete_request(req, ZX_ERR_NOT_SUPPORTED, 0, dwc);
        }
    }
}

static void dwc_process_root_hub_std_req(dwc_usb_transfer_request_t* req,
                                         dwc_usb_t* dwc) {
    usb_request_t* usb_req = req->usb_req;
    usb_setup_t* setup = &usb_req->setup;

    uint8_t request = setup->bRequest;

    if (request == USB_REQ_SET_ADDRESS) {
        complete_request(req, ZX_OK, 0, dwc);
    } else if (request == USB_REQ_GET_DESCRIPTOR) {
        dwc_root_hub_get_descriptor(req, dwc);
    } else if (request == USB_REQ_SET_CONFIGURATION) {
        complete_request(req, ZX_OK, 0, dwc);
    } else {
        complete_request(req, ZX_ERR_NOT_SUPPORTED, 0, dwc);
    }
}

static void dwc_process_root_hub_class_req(dwc_usb_transfer_request_t* req,
                                           dwc_usb_t* dwc) {
    usb_request_t* usb_req = req->usb_req;
    usb_setup_t* setup = &usb_req->setup;

    uint8_t request = setup->bRequest;
    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);

    if (request == USB_REQ_GET_DESCRIPTOR) {
        if (value == USB_HUB_DESC_TYPE << 8 && index == 0) {
            usb_hub_descriptor_t desc;
            memset(&desc, 0, sizeof(desc));
            desc.bDescLength = sizeof(desc);
            desc.bDescriptorType = value >> 8;
            desc.bNbrPorts = 1;
            desc.bPowerOn2PwrGood = 0;

            if (length > sizeof(desc))
                length = sizeof(desc);
            usb_request_copyto(usb_req, &desc, length, 0);
            complete_request(req, ZX_OK, length, dwc);
            return;
        }
    } else if (request == USB_REQ_SET_FEATURE) {
        zx_status_t res = dwc_host_port_set_feature(value);
        complete_request(req, res, 0, dwc);
    } else if (request == USB_REQ_CLEAR_FEATURE) {
        mtx_lock(&dwc->rh_status_mtx);
        uint16_t* change_bits = &(dwc->root_port_status.wPortChange);
        switch (value) {
        case USB_FEATURE_C_PORT_CONNECTION:
            *change_bits &= ~USB_C_PORT_CONNECTION;
            break;
        case USB_FEATURE_C_PORT_ENABLE:
            *change_bits &= ~USB_C_PORT_ENABLE;
            break;
        case USB_FEATURE_C_PORT_SUSPEND:
            *change_bits &= ~USB_PORT_SUSPEND;
            break;
        case USB_FEATURE_C_PORT_OVER_CURRENT:
            *change_bits &= ~USB_C_PORT_OVER_CURRENT;
            break;
        case USB_FEATURE_C_PORT_RESET:
            *change_bits &= ~USB_C_PORT_RESET;
            break;
        }
        mtx_unlock(&dwc->rh_status_mtx);
        complete_request(req, ZX_OK, 0, dwc);
    } else if (request == USB_REQ_GET_STATUS) {
        size_t length = usb_req->header.length;
        if (length > sizeof(dwc->root_port_status)) {
            length = sizeof(dwc->root_port_status);
        }

        mtx_lock(&dwc->rh_status_mtx);
        usb_request_copyto(usb_req, &dwc->root_port_status, length, 0);
        mtx_unlock(&dwc->rh_status_mtx);

        complete_request(req, ZX_OK, length, dwc);
    } else {
        complete_request(req, ZX_ERR_NOT_SUPPORTED, 0, dwc);
    }
}

static void dwc_process_root_hub_ctrl_req(dwc_usb_transfer_request_t* req, dwc_usb_t* dwc) {
    usb_request_t* usb_req = req->usb_req;
    usb_setup_t* setup = &usb_req->setup;

    if ((setup->bmRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
        dwc_process_root_hub_std_req(req, dwc);
    } else if ((setup->bmRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
        dwc_process_root_hub_class_req(req, dwc);
    } else {
        // Some unknown request type?
        assert(false);
    }
}

static void dwc_process_root_hub_request(dwc_usb_t* dwc,
                                         dwc_usb_transfer_request_t* req) {
    assert(req);

    if (is_control_request(req)) {
        dwc_process_root_hub_ctrl_req(req, dwc);
    } else {
        mtx_lock(&dwc->rh_status_mtx);
        dwc->rh_intr_req = req;
        mtx_unlock(&dwc->rh_status_mtx);

        dwc_complete_root_port_status_req(dwc);
    }
}

// Thread to handle queues transactions on the root hub.
static int dwc_root_hub_req_worker(void* arg) {
    dwc_usb_t* dwc = (dwc_usb_t*)arg;

    dwc->rh_req_completion = COMPLETION_INIT;

    while (true) {
        completion_wait(&dwc->rh_req_completion, ZX_TIME_INFINITE);

        mtx_lock(&dwc->rh_req_mtx);

        dwc_usb_transfer_request_t* req =
            list_remove_head_type(&dwc->rh_req_head,
                                  dwc_usb_transfer_request_t, node);

        if (list_is_empty(&dwc->rh_req_head)) {
            completion_reset(&dwc->rh_req_completion);
        }

        mtx_unlock(&dwc->rh_req_mtx);

        dwc_process_root_hub_request(dwc, req);
    }

    return -1;
}

static uint acquire_channel_blocking(dwc_usb_t* dwc) {
    int next_channel = -1;

    while (true) {
        mtx_lock(&dwc->free_channel_mtx);

        // A quick sanity check. We should never mark a channel that doesn't
        // exist on the system as free.
        assert((dwc->free_channels & ALL_CHANNELS_FREE) == dwc->free_channels);

        // Is there at least one channel that's free?
        next_channel = -1;
        if (dwc->free_channels) {
            next_channel = __builtin_ctz(dwc->free_channels);

            // Mark the bit in the free_channel bitfield = 0, meaning the
            // channel is in use.
            dwc->free_channels &= (ALL_CHANNELS_FREE ^ (1 << next_channel));
        }

        if (next_channel == -1) {
            completion_reset(&dwc->free_channel_completion);
        }

        mtx_unlock(&dwc->free_channel_mtx);

        if (next_channel >= 0) {
            return next_channel;
        }

        // We couldn't find a free channel, wait for somebody to tell us to
        // wake up and attempt to acquire a channel again.
        completion_wait(&dwc->free_channel_completion, ZX_TIME_INFINITE);
    }

    __UNREACHABLE;
}

static void release_channel(uint ch, dwc_usb_t* dwc) {
    assert(ch < DWC_NUM_CHANNELS);

    mtx_lock(&dwc->free_channel_mtx);

    dwc->free_channels |= (1 << ch);

    mtx_unlock(&dwc->free_channel_mtx);

    completion_signal(&dwc->free_channel_completion);
}

static void dwc_start_transaction(uint8_t chan,
                                  dwc_usb_transfer_request_t* req) {
    volatile struct dwc_host_channel* chanptr = &regs->host_channels[chan];
    union dwc_host_channel_split_control split_control;
    union dwc_host_channel_characteristics characteristics;
    union dwc_host_channel_interrupts interrupt_mask;

    chanptr->interrupt_mask.val = 0;
    chanptr->interrupts.val = 0xffffffff;

    split_control = chanptr->split_control;
    split_control.complete_split = req->complete_split;
    chanptr->split_control = split_control;

    uint next_frame = (regs->host_frame_number & 0xffff) + 1;

    if (!split_control.complete_split) {
        req->cspit_retries = 0;
    }

    characteristics = chanptr->characteristics;
    characteristics.odd_frame = next_frame & 1;
    characteristics.channel_enable = 1;
    chanptr->characteristics = characteristics;

    interrupt_mask.val = 0;
    interrupt_mask.channel_halted = 1;
    chanptr->interrupt_mask = interrupt_mask;
    regs->host_channels_interrupt_mask |= 1 << chan;
}

static union dwc_host_channel_interrupts dwc_await_channel_complete(uint32_t channel, dwc_usb_t* dwc) {
    completion_wait(&dwc->channel_complete[channel], ZX_TIME_INFINITE);
    completion_reset(&dwc->channel_complete[channel]);
    return dwc->channel_interrupts[channel];
}

static void dwc_start_transfer(uint8_t chan, dwc_usb_transfer_request_t* req,
                               dwc_usb_endpoint_t* ep) {
    volatile struct dwc_host_channel* chanptr;
    union dwc_host_channel_characteristics characteristics;
    union dwc_host_channel_split_control split_control;
    union dwc_host_channel_transfer transfer;
    void* data = NULL;

    dwc_usb_device_t* dev = ep->parent;
    usb_request_t* usb_req = req->usb_req;

    chanptr = &regs->host_channels[chan];
    characteristics.val = 0;
    split_control.val = 0;
    transfer.val = 0;
    req->short_attempt = false;

    characteristics.max_packet_size = ep->desc.wMaxPacketSize;
    characteristics.endpoint_number = ep->ep_address;
    characteristics.endpoint_type = usb_ep_type(&ep->desc);
    characteristics.device_address = dev->device_id;
    characteristics.packets_per_frame = 1;
    if (ep->parent->speed == USB_SPEED_HIGH) {
        characteristics.packets_per_frame +=
            ((ep->desc.wMaxPacketSize >> 11) & 0x3);
    }

    // Certain characteristics must be special cased for Control Endpoints.

    if (usb_ep_type(&ep->desc) == USB_ENDPOINT_CONTROL) {

        switch (req->ctrl_phase) {
        case CTRL_PHASE_SETUP: {
            assert(req->setup_req);
            characteristics.endpoint_direction = DWC_EP_OUT;

            phys_iter_t iter;
            zx_paddr_t phys;
            usb_request_physmap(req->setup_req);
            usb_request_phys_iter_init(&iter, req->setup_req, PAGE_SIZE);
            usb_request_phys_iter_next(&iter, &phys);
            data = (void*)phys;

            // Quick sanity check to make sure that we're actually tying to
            // transfer the correct number of bytes.
            assert(req->setup_req->header.length == sizeof(usb_setup_t));

            transfer.size = req->setup_req->header.length;

            transfer.packet_id = DWC_TOGGLE_SETUP;
            break;
        }
        case CTRL_PHASE_DATA: {
            characteristics.endpoint_direction =
                usb_req->setup.bmRequestType >> 7;

            phys_iter_t iter;
            zx_paddr_t phys;
            usb_request_physmap(usb_req);
            usb_request_phys_iter_init(&iter, usb_req, PAGE_SIZE);
            usb_request_phys_iter_next(&iter, &phys);
            data = (void*)phys + req->bytes_transferred;

            transfer.size = usb_req->header.length - req->bytes_transferred;

            usb_request_cache_flush_invalidate(usb_req, 0, transfer.size);

            if (req->bytes_transferred == 0) {
                transfer.packet_id = DWC_TOGGLE_DATA1;
            } else {
                transfer.packet_id = req->next_data_toggle;
            }

            break;
        }
        case CTRL_PHASE_STATUS:
            // If there was no DATA phase, the status transaction is IN to the
            // host. If there was a DATA phase, the status phase is in the
            // opposite direction of the DATA phase.
            if (usb_req->setup.wLength == 0) {
                characteristics.endpoint_direction = DWC_EP_IN;
            } else if ((usb_req->setup.bmRequestType >> 7) == DWC_EP_OUT) {
                characteristics.endpoint_direction = DWC_EP_IN;
            } else {
                characteristics.endpoint_direction = DWC_EP_OUT;
            }

            data = NULL;
            transfer.size = 0;
            transfer.packet_id = DWC_TOGGLE_DATA1;
            break;
        }
    } else {
        characteristics.endpoint_direction =
            (ep->ep_address & USB_ENDPOINT_DIR_MASK) >> 7;

        phys_iter_t iter;
        zx_paddr_t phys;
        usb_request_physmap(usb_req);
        usb_request_phys_iter_init(&iter, usb_req, PAGE_SIZE);
        usb_request_phys_iter_next(&iter, &phys);
        data = (void*)phys + req->bytes_transferred;

        transfer.size = usb_req->header.length - req->bytes_transferred;
        transfer.packet_id = req->next_data_toggle;
    }

    if (dev->speed != USB_SPEED_HIGH) {
        split_control.port_address = dev->port;
        split_control.hub_address = dev->hub_address;
        split_control.split_enable = 1;

        if (transfer.size > characteristics.max_packet_size) {
            transfer.size = characteristics.max_packet_size;
            req->short_attempt = true;
        }

        if (dev->speed == USB_SPEED_LOW)
            characteristics.low_speed = 1;
    }

    assert(IS_WORD_ALIGNED(data));
    data = data ? data : (void*)0xffffff00;
//TODO(gkalsi) what to do here?
//    data += BCM_SDRAM_BUS_ADDR_BASE;
    chanptr->dma_address = (uint32_t)(((uintptr_t)data) & 0xffffffff);
    assert(IS_WORD_ALIGNED(chanptr->dma_address));

    transfer.packet_count =
        DIV_ROUND_UP(transfer.size, characteristics.max_packet_size);

    if (transfer.packet_count == 0) {
        transfer.packet_count = 1;
    }

    req->bytes_queued = transfer.size;
    req->total_bytes_queued = transfer.size;
    req->packets_queued = transfer.packet_count;

    zxlogf(TRACE, "dwc_usb: programming request, req_id = 0x%x, "
            "channel = %u\n", req->request_id, chan);

    chanptr->characteristics = characteristics;
    chanptr->split_control = split_control;
    chanptr->transfer = transfer;

    dwc_start_transaction(chan, req);
}

static void await_sof_if_necessary(uint channel, dwc_usb_transfer_request_t* req,
                                   dwc_usb_endpoint_t* ep, dwc_usb_t* dwc) {
    if (usb_ep_type(&ep->desc) == USB_ENDPOINT_INTERRUPT &&
        !req->complete_split && ep->parent->speed != USB_SPEED_HIGH) {
        mtx_lock(&dwc->sof_waiters_mtx);

        if (dwc->n_sof_waiters == 0) {
            // If we're the first sof-waiter, enable the SOF interrupt.
            union dwc_core_interrupts core_interrupt_mask =
                regs->core_interrupt_mask;
            core_interrupt_mask.sof_intr = 1;
            regs->core_interrupt_mask = core_interrupt_mask;
        }

        dwc->n_sof_waiters++;

        mtx_unlock(&dwc->sof_waiters_mtx);
        // Block until we get a sof interrupt.

        completion_reset(&dwc->sof_waiters[channel]);
        completion_wait(&dwc->sof_waiters[channel], ZX_TIME_INFINITE);

        mtx_lock(&dwc->sof_waiters_mtx);

        dwc->n_sof_waiters--;

        if (dwc->n_sof_waiters == 0) {
            // If we're the last sof waiter, turn off the sof interrupt.
            union dwc_core_interrupts core_interrupt_mask =
                regs->core_interrupt_mask;
            core_interrupt_mask.sof_intr = 0;
            regs->core_interrupt_mask = core_interrupt_mask;
        }

        mtx_unlock(&dwc->sof_waiters_mtx);
    }
}

static bool handle_normal_channel_halted(uint channel,
                                         dwc_usb_transfer_request_t* req,
                                         dwc_usb_endpoint_t* ep,
                                         union dwc_host_channel_interrupts interrupts,
                                         dwc_usb_t* dwc) {
    volatile struct dwc_host_channel* chanptr = &regs->host_channels[channel];

    uint32_t packets_remaining = chanptr->transfer.packet_count;
    uint32_t packets_transferred = req->packets_queued - packets_remaining;

    usb_request_t* usb_req = req->usb_req;

    if (packets_transferred != 0) {
        uint32_t bytes_transferred = 0;
        union dwc_host_channel_characteristics characteristics =
            chanptr->characteristics;
        uint32_t max_packet_size = characteristics.max_packet_size;
        bool is_dir_in = characteristics.endpoint_direction == 1;

        if (is_dir_in) {
            bytes_transferred = req->bytes_queued - chanptr->transfer.size;
        } else {
            if (packets_transferred > 1) {
                bytes_transferred += max_packet_size * (packets_transferred - 1);
            }
            if (packets_remaining == 0 &&
                (req->total_bytes_queued % max_packet_size != 0 ||
                 req->total_bytes_queued == 0)) {
                bytes_transferred += req->total_bytes_queued;
            } else {
                bytes_transferred += max_packet_size;
            }
        }

        req->packets_queued -= packets_transferred;
        req->bytes_queued -= bytes_transferred;
        req->bytes_transferred += bytes_transferred;

        if ((req->packets_queued == 0) ||
            ((is_dir_in) &&
             (bytes_transferred < packets_transferred * max_packet_size))) {
            if (!interrupts.transfer_completed) {

                zxlogf(ERROR, "dwc_usb: xfer failed, irq = 0x%x\n",
                        interrupts.val);

                release_channel(channel, dwc);

                complete_request(req, ZX_ERR_IO, 0, dwc);

                return true;
            }


            if (req->short_attempt && req->bytes_queued == 0 &&
                (usb_ep_type(&ep->desc) != USB_ENDPOINT_INTERRUPT)) {
                req->complete_split = false;
                req->next_data_toggle = chanptr->transfer.packet_id;

                // Requeue the request, don't release the channel.
                mtx_lock(&ep->pending_request_mtx);
                list_add_head(&ep->pending_requests, &req->node);
                mtx_unlock(&ep->pending_request_mtx);
                completion_signal(&ep->request_pending_completion);

                return true;
            }

            if ((usb_ep_type(&ep->desc) == USB_ENDPOINT_CONTROL) &&
                (req->ctrl_phase < CTRL_PHASE_STATUS)) {
                req->complete_split = false;

                if (req->ctrl_phase == CTRL_PHASE_SETUP) {
                    req->bytes_transferred = 0;
                    req->next_data_toggle = DWC_TOGGLE_DATA1;
                }

                req->ctrl_phase++;

                // If there's no DATA phase, advance directly to STATUS phase.
                if (req->ctrl_phase == CTRL_PHASE_DATA && usb_req->header.length == 0) {
                    req->ctrl_phase++;
                }

                mtx_lock(&ep->pending_request_mtx);
                list_add_head(&ep->pending_requests, &req->node);
                mtx_unlock(&ep->pending_request_mtx);
                completion_signal(&ep->request_pending_completion);

                return true;
            }

            release_channel(channel, dwc);
            complete_request(req, ZX_OK, req->bytes_transferred, dwc);
            return true;
        } else {
            if (chanptr->split_control.split_enable) {
                req->complete_split = !req->complete_split;
            }

            // Restart the transaction.
            dwc_start_transaction(channel, req);
            return false;
        }
    } else {
        if (interrupts.ack_response_received &&
            chanptr->split_control.split_enable && !req->complete_split) {
            req->complete_split = true;
            dwc_start_transaction(channel, req);
            return false;
        } else {
            release_channel(channel, dwc);
            complete_request(req, ZX_ERR_IO, 0, dwc);
            return true;
        }
    }
}

static bool handle_channel_halted_interrupt(uint channel,
                                            dwc_usb_transfer_request_t* req,
                                            dwc_usb_endpoint_t* ep,
                                            union dwc_host_channel_interrupts interrupts,
                                            dwc_usb_t* dwc) {
    volatile struct dwc_host_channel* chanptr = &regs->host_channels[channel];

    if (interrupts.stall_response_received || interrupts.ahb_error ||
        interrupts.transaction_error || interrupts.babble_error ||
        interrupts.excess_transaction_error || interrupts.frame_list_rollover ||
        (interrupts.nyet_response_received && !req->complete_split) ||
        (interrupts.data_toggle_error &&
         chanptr->characteristics.endpoint_direction == 0)) {

        // There was an error on the bus.
        if (!interrupts.stall_response_received) {
            // It's totally okay for the EP to return stall so don't log it.

            zxlogf(ERROR, "dwc_usb: xfer failed, irq = 0x%x\n",
                    interrupts.val);
        }

        // Release the channel used for this transaction.
        release_channel(channel, dwc);

        // Complete the request with a failure.
        complete_request(req, ZX_ERR_IO, 0, dwc);

        return true;
    } else if (interrupts.frame_overrun) {
        if (++debug_frame_overrun_counter == FRAME_OVERRUN_THRESHOLD) {
            debug_frame_overrun_counter = 0;

            // A little coarse since we only log every nth frame overrun.
            zxlogf(INFO, "dwc_usb: requeued %d frame overruns, last one on "
                    "ep = %u, devid = %u\n", FRAME_OVERRUN_THRESHOLD,
                    ep->ep_address, ep->parent->device_id);
        }
        release_channel(channel, dwc);
        mtx_lock(&ep->pending_request_mtx);
        list_add_head(&ep->pending_requests, &req->node);
        mtx_unlock(&ep->pending_request_mtx);
        completion_signal(&ep->request_pending_completion);
        return true;
    } else if (interrupts.nak_response_received) {
        // Wait a defined period of time
        uint8_t bInterval = ep->desc.bInterval;
        zx_duration_t sleep_ns;

        req->next_data_toggle = chanptr->transfer.packet_id;

        if (usb_ep_type(&ep->desc) != USB_ENDPOINT_CONTROL) {
            release_channel(channel, dwc);
        } else {
            // Only release the channel if we're in the SETUP phase. The later
            // phases assume that the channel is already held when they retry.
            if (req->ctrl_phase == CTRL_PHASE_SETUP) {
                release_channel(channel, dwc);
            }
        }

        if (ep->parent->speed == USB_SPEED_HIGH) {
            sleep_ns = (1 << (bInterval - 1)) * 125000;
        } else {
            sleep_ns = ZX_MSEC(bInterval);
        }

        if (!sleep_ns) {
            sleep_ns = ZX_MSEC(1);
        }

        zx_nanosleep(zx_deadline_after(sleep_ns));
        await_sof_if_necessary(channel, req, ep, dwc);

        req->complete_split = false;

        // Requeue the transfer and signal the endpoint.
        mtx_lock(&ep->pending_request_mtx);
        list_add_head(&ep->pending_requests, &req->node);
        mtx_unlock(&ep->pending_request_mtx);
        completion_signal(&ep->request_pending_completion);
        return true;
    } else if (interrupts.nyet_response_received) {
        if (++req->cspit_retries >= 8) {
            req->complete_split = false;
        }

        // Wait half a microframe to retry a NYET, otherwise wait for the start
        // of the next frame.
        if (usb_ep_type(&ep->desc) != USB_ENDPOINT_INTERRUPT) {
            zx_nanosleep(zx_deadline_after(62500));
        }
        await_sof_if_necessary(channel, req, ep, dwc);
        zxlogf(TRACE, "dwc_usb: requeue nyet on ep = %u, devid = %u\n",
                ep->ep_address, ep->parent->device_id);

        dwc_start_transaction(channel, req);
        return false;
    } else {
        // Channel halted normally.
        return handle_normal_channel_halted(channel, req, ep, interrupts, dwc);
    }
}

// There is one instance of this thread per Device Endpoint.
// It is responsbile for managing requests on an endpoint.
static int endpoint_request_scheduler_thread(void* arg) {
    assert(arg);

    dwc_usb_scheduler_thread_ctx_t* ctx = (dwc_usb_scheduler_thread_ctx_t*)arg;

    dwc_usb_endpoint_t* self = ctx->ep;
    dwc_usb_t* dwc = ctx->dwc;

    // No need for this anymore.
    free(ctx);

    dwc_usb_data_toggle_t next_data_toggle = 0;
    uint channel = NUM_HOST_CHANNELS + 1;
    while (true) {
        zx_status_t res =
            completion_wait(&self->request_pending_completion, ZX_TIME_INFINITE);
        if (res != ZX_OK) {
            zxlogf(ERROR, "dwc_usb: completion wait failed, retcode = %d, "
                    "device_id = %u, ep = %u\n", res, self->parent->device_id,
                    self->ep_address);
            break;
        }

        // Attempt to take a request from the pending request queue.
        dwc_usb_transfer_request_t* req = NULL;
        mtx_lock(&self->pending_request_mtx);
        req = list_remove_head_type(&self->pending_requests,
                                    dwc_usb_transfer_request_t, node);

        if (list_is_empty(&self->pending_requests)) {
            completion_reset(&self->request_pending_completion);
        }

        mtx_unlock(&self->pending_request_mtx);
        assert(req);

        // Start this transfer.
        if (usb_ep_type(&self->desc) == USB_ENDPOINT_CONTROL) {
            switch (req->ctrl_phase) {
            case CTRL_PHASE_SETUP:
                // We're going to use a single channel for all three phases
                // of the request, so we're going to acquire one here and
                // hold onto it until the transaction is complete.
                channel = acquire_channel_blocking(dwc);

                // Allocate a usb request for the SETUP packet.
                req->setup_req = usb_request_pool_get(&dwc->free_usb_reqs, sizeof(usb_setup_t));
                if (req->setup_req == NULL) {
                    zx_status_t status =
                        usb_request_alloc(&req->setup_req, sizeof(usb_setup_t), 0);
                    assert(status == ZX_OK);
                }

                usb_request_t* setup_req = req->setup_req;
                // Copy the setup data into the setup usb request.
                usb_request_copyto(setup_req, &setup_req->setup, sizeof(usb_setup_t), 0);
                usb_request_cache_flush(setup_req, 0, sizeof(usb_setup_t));
                setup_req->header.length = sizeof(usb_setup_t);

                // Perform the SETUP phase of the control transfer.
                dwc_start_transfer(channel, req, self);
                break;
            case CTRL_PHASE_DATA:
                // The DATA phase doesn't care how many bytes the SETUP
                // phase transferred.
                dwc_start_transfer(channel, req, self);
                break;
            case CTRL_PHASE_STATUS:
                dwc_start_transfer(channel, req, self);
                break;
            }
        } else if (usb_ep_type(&self->desc) == USB_ENDPOINT_ISOCHRONOUS) {
            zxlogf(ERROR, "dwc_usb: isochronous endpoints not implemented\n");
            return -1;
        } else if (usb_ep_type(&self->desc) == USB_ENDPOINT_BULK) {
            req->next_data_toggle = next_data_toggle;
            channel = acquire_channel_blocking(dwc);
            dwc_start_transfer(channel, req, self);
        } else if (usb_ep_type(&self->desc) == USB_ENDPOINT_INTERRUPT) {
            req->next_data_toggle = next_data_toggle;
            channel = acquire_channel_blocking(dwc);
            await_sof_if_necessary(channel, req, self, dwc);
            dwc_start_transfer(channel, req, self);
        }

        // Wait for an interrupt on this channel.
        while (true) {
            union dwc_host_channel_interrupts interrupts =
                dwc_await_channel_complete(channel, dwc);

            volatile struct dwc_host_channel* chanptr = &regs->host_channels[channel];
            next_data_toggle = chanptr->transfer.packet_id;

            if (handle_channel_halted_interrupt(channel, req, self, interrupts, dwc))
                break;
        }
    }

    return -1;
}

static zx_status_t create_default_device(dwc_usb_t* dwc) {
    zx_status_t retval = ZX_OK;

    dwc_usb_device_t* default_device = &dwc->usb_devices[0];

    mtx_lock(&default_device->devmtx);

    default_device->speed = USB_SPEED_HIGH;
    default_device->hub_address = 0;
    default_device->port = 0;

    default_device->device_id = 0;

    list_initialize(&default_device->endpoints);

    // Create a control endpoint for the default device.
    dwc_usb_endpoint_t* ep0 = calloc(1, sizeof(*ep0));
    if (!ep0) {
        retval = ZX_ERR_NO_MEMORY;
    }

    ep0->ep_address = 0;

    list_initialize(&ep0->pending_requests);

    ep0->parent = default_device;

    ep0->desc.bLength = sizeof(ep0->desc);
    ep0->desc.bDescriptorType = USB_DT_ENDPOINT;
    ep0->desc.bEndpointAddress = 0; // Control endpoints have a size of 8;
    ep0->desc.bmAttributes = (USB_ENDPOINT_CONTROL);
    ep0->desc.wMaxPacketSize = 8;
    ep0->desc.bInterval = 0; // Ignored for ctrl endpoints.

    ep0->request_pending_completion = COMPLETION_INIT;

    list_add_tail(&default_device->endpoints, &ep0->node);

    dwc_usb_scheduler_thread_ctx_t* ctx = malloc(sizeof(*ctx));
    ctx->ep = ep0;
    ctx->dwc = dwc;

    // Start the request processor thread.
    thrd_create(
        &ep0->request_scheduler_thread,
        endpoint_request_scheduler_thread,
        (void*)ctx);

    mtx_unlock(&default_device->devmtx);
    return retval;
}

// Bind is the entry point for this driver.
static zx_status_t usb_dwc_bind(void* ctx, zx_device_t* dev) {
    zxlogf(TRACE, "usb_dwc: bind dev = %p\n", dev);

    dwc_usb_t* usb_dwc = NULL;

    platform_device_protocol_t proto;
    zx_status_t st = device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV, &proto);
    if (st != ZX_OK) {
        return st;
    }

    // Allocate a new device object for the bus.
    usb_dwc = calloc(1, sizeof(*usb_dwc));
    if (!usb_dwc) {
        zxlogf(ERROR, "usb_dwc: bind failed to allocate usb_dwc struct\n");
        return ZX_ERR_NO_MEMORY;
    }

    usb_dwc->free_channel_completion = COMPLETION_INIT;
    usb_dwc->free_channels = ALL_CHANNELS_FREE;
    usb_dwc->next_device_address = 1;
    usb_dwc->DBG_reqid = 0x1;
    usb_request_pool_init(&usb_dwc->free_usb_reqs);

    // Carve out some address space for this device.
    size_t mmio_size;
    zx_handle_t mmio_handle = ZX_HANDLE_INVALID;
    st = pdev_map_mmio(&proto, MMIO_INDEX, ZX_CACHE_POLICY_UNCACHED_DEVICE, (void **)&regs,
                       &mmio_size, &mmio_handle);
    if (st != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: bind failed to pdev_map_mmio.\n");
        goto error_return;
    }

    // Create an IRQ Handle for this device.
    st = pdev_map_interrupt(&proto, IRQ_INDEX, &usb_dwc->irq_handle);
    if (st != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: bind failed to map usb irq.\n");
        goto error_return;
    }

    usb_dwc->parent = dev;
    list_initialize(&usb_dwc->rh_req_head);

    // Initialize the free list.
    mtx_lock(&usb_dwc->free_req_mtx);
    list_initialize(&usb_dwc->free_reqs);
    mtx_unlock(&usb_dwc->free_req_mtx);

    if ((st = usb_dwc_softreset_core()) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed to reset core.\n");
        goto error_return;
    }

    if ((st = usb_dwc_setupcontroller()) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed setup controller.\n");
        goto error_return;
    }

    // Initialize all the channel completions.
    for (size_t i = 0; i < NUM_HOST_CHANNELS; i++) {
        usb_dwc->channel_complete[i] = COMPLETION_INIT;
        usb_dwc->sof_waiters[i] = COMPLETION_INIT;
    }

    // We create a mock device at device_id = 0 for enumeration purposes.
    // Any new device that connects to the bus is assigned this ID until we
    // set its address.
    if ((st = create_default_device(usb_dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed to create default device. "
                "retcode = %d\n", st);
        goto error_return;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dwc2",
        .ctx = usb_dwc,
        .ops = &dwc_device_proto,
        .proto_id = ZX_PROTOCOL_USB_HCI,
        .proto_ops = &dwc_hci_protocol,
    };

    if ((st = device_add(dev, &args, &usb_dwc->zxdev)) != ZX_OK) {
        free(usb_dwc);
        return st;
    }

    // Thread that responds to requests for the root hub.
    thrd_t root_hub_req_worker;
    thrd_create_with_name(&root_hub_req_worker, dwc_root_hub_req_worker,
                          usb_dwc, "dwc_root_hub_req_worker");
    thrd_detach(root_hub_req_worker);

    thrd_t irq_thread;
    thrd_create_with_name(&irq_thread, dwc_irq_thread, usb_dwc,
                          "dwc_irq_thread");
    thrd_detach(irq_thread);

    zxlogf(TRACE, "usb_dwc: bind success!\n");
    return ZX_OK;

error_return:
    if (regs) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)regs, mmio_size);
    }
    zx_handle_close(mmio_handle);
    if (usb_dwc) {
        zx_handle_close(usb_dwc->irq_handle);
        free(usb_dwc);
    }

    return st;
}

static zx_driver_ops_t usb_dwc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_dwc_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(dwc2, usb_dwc_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
ZIRCON_DRIVER_END(dwc2)
// clang-format on
