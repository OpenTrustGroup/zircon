// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <hw/inout.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <threads.h>

#include "errors.h"

#define xprintf(fmt...) zxlogf(SPEW, fmt)

/* EC commands */
#define EC_CMD_READ 0x80
#define EC_CMD_WRITE 0x81
#define EC_CMD_QUERY 0x84

/* EC status register bits */
#define EC_SC_SCI_EVT (1 << 5)
#define EC_SC_IBF (1 << 1)
#define EC_SC_OBF (1 << 0)

/* Thread signals */
#define IRQ_RECEIVED ZX_EVENT_SIGNALED
#define EC_THREAD_SHUTDOWN ZX_USER_SIGNAL_0
#define EC_THREAD_SHUTDOWN_DONE ZX_USER_SIGNAL_1

typedef struct acpi_ec_device {
    zx_device_t* zxdev;

    ACPI_HANDLE acpi_handle;

    // PIO addresses for EC device
    uint16_t cmd_port;
    uint16_t data_port;

    // GPE for EC events
    ACPI_HANDLE gpe_block;
    UINT32 gpe;

    // thread for processing events from the EC
    thrd_t evt_thread;

    zx_handle_t interrupt_event;

    bool gpe_setup : 1;
    bool thread_setup : 1;
    bool ec_space_setup : 1;
} acpi_ec_device_t;

static ACPI_STATUS get_ec_handle(ACPI_HANDLE, UINT32, void*, void**);
static ACPI_STATUS get_ec_gpe_info(ACPI_HANDLE, ACPI_HANDLE*, UINT32*);
static ACPI_STATUS get_ec_ports(ACPI_HANDLE, uint16_t*, uint16_t*);

static ACPI_STATUS ec_space_setup_handler(ACPI_HANDLE Region, UINT32 Function,
                                          void* HandlerContext, void** ReturnContext);
static ACPI_STATUS ec_space_request_handler(UINT32 Function, ACPI_PHYSICAL_ADDRESS Address,
                                            UINT32 BitWidth, UINT64* Value,
                                            void* HandlerContext, void* RegionContext);

static zx_status_t wait_for_interrupt(acpi_ec_device_t* dev);
static zx_status_t execute_read_op(acpi_ec_device_t* dev, uint8_t addr, uint8_t* val);
static zx_status_t execute_write_op(acpi_ec_device_t* dev, uint8_t addr, uint8_t val);
static zx_status_t execute_query_op(acpi_ec_device_t* dev, uint8_t* val);

// Execute the EC_CMD_READ operation.  Requires the ACPI global lock be held.
static zx_status_t execute_read_op(acpi_ec_device_t* dev, uint8_t addr, uint8_t* val) {
    // Issue EC command
    outp(dev->cmd_port, EC_CMD_READ);

    // Wait for EC to read the command so we can write the address
    while (inp(dev->cmd_port) & EC_SC_IBF) {
        zx_status_t status = wait_for_interrupt(dev);
        if (status != ZX_OK) {
            return status;
        }
    }

    // Specify the address
    outp(dev->data_port, addr);

    // Wait for EC to read the address and write a response
    while ((inp(dev->cmd_port) & (EC_SC_OBF | EC_SC_IBF)) != EC_SC_OBF) {
        zx_status_t status = wait_for_interrupt(dev);
        if (status != ZX_OK) {
            return status;
        }
    }

    // Read the response
    *val = inp(dev->data_port);
    return ZX_OK;
}

// Execute the EC_CMD_WRITE operation.  Requires the ACPI global lock be held.
static zx_status_t execute_write_op(acpi_ec_device_t* dev, uint8_t addr, uint8_t val) {
    // Issue EC command
    outp(dev->cmd_port, EC_CMD_WRITE);

    // Wait for EC to read the command so we can write the address
    while (inp(dev->cmd_port) & EC_SC_IBF) {
        zx_status_t status = wait_for_interrupt(dev);
        if (status != ZX_OK) {
            return status;
        }
    }

    // Specify the address
    outp(dev->data_port, addr);

    // Wait for EC to read the address
    while (inp(dev->cmd_port) & EC_SC_IBF) {
        zx_status_t status = wait_for_interrupt(dev);
        if (status != ZX_OK) {
            return status;
        }
    }

    // Write the data
    outp(dev->data_port, val);

    // Wait for EC to read the data
    while (inp(dev->cmd_port) & EC_SC_IBF) {
        zx_status_t status = wait_for_interrupt(dev);
        if (status != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

// Execute the EC_CMD_QUERY operation.  Requires the ACPI global lock be held.
static zx_status_t execute_query_op(acpi_ec_device_t* dev, uint8_t* event) {
    // Query EC command
    outp(dev->cmd_port, EC_CMD_QUERY);

    // Wait for EC to respond
    while ((inp(dev->cmd_port) & (EC_SC_OBF | EC_SC_IBF)) != EC_SC_OBF) {
        zx_status_t status = wait_for_interrupt(dev);
        if (status != ZX_OK) {
            return status;
        }
    }

    *event = inp(dev->data_port);
    return ZX_OK;
}

static ACPI_STATUS ec_space_setup_handler(ACPI_HANDLE Region, UINT32 Function,
                                          void* HandlerContext, void** ReturnContext) {
    acpi_ec_device_t* dev = HandlerContext;
    *ReturnContext = dev;

    if (Function == ACPI_REGION_ACTIVATE) {
        xprintf("acpi-ec: Setting up EC region\n");
        return AE_OK;
    } else if (Function == ACPI_REGION_DEACTIVATE) {
        xprintf("acpi-ec: Tearing down EC region\n");
        return AE_OK;
    } else {
        return AE_SUPPORT;
    }
}

static ACPI_STATUS ec_space_request_handler(UINT32 Function, ACPI_PHYSICAL_ADDRESS Address,
                                            UINT32 BitWidth, UINT64* Value,
                                            void* HandlerContext, void* RegionContext) {
    acpi_ec_device_t* dev = HandlerContext;

    if (BitWidth != 8 && BitWidth != 16 && BitWidth != 32 && BitWidth != 64) {
        return AE_BAD_PARAMETER;
    }
    if (Address > UINT8_MAX || Address - 1 + BitWidth / 8 > UINT8_MAX) {
        return AE_BAD_PARAMETER;
    }

    UINT32 global_lock;
    while (AcpiAcquireGlobalLock(0xFFFF, &global_lock) != AE_OK)
        ;

    // NB: The processing of the read/write ops below will generate interrupts,
    // which will unfortunately cause spurious wakeups on the event thread.  One
    // design that would avoid this is to have that thread responsible for
    // processing these EC address space requests, but an attempt at an
    // implementation failed due to apparent deadlocks against the Global Lock.

    const size_t bytes = BitWidth / 8;
    ACPI_STATUS status = AE_OK;
    uint8_t* value_bytes = (uint8_t*)Value;
    if (Function == ACPI_WRITE) {
        for (size_t i = 0; i < bytes; ++i) {
            zx_status_t zx_status = execute_write_op(dev, Address + i, value_bytes[i]);
            if (zx_status != ZX_OK) {
                status = AE_ERROR;
                goto finish;
            }
        }
    } else {
        *Value = 0;
        for (size_t i = 0; i < bytes; ++i) {
            zx_status_t zx_status = execute_read_op(dev, Address + i, value_bytes + i);
            if (zx_status != ZX_OK) {
                status = AE_ERROR;
                goto finish;
            }
        }
    }

finish:
    AcpiReleaseGlobalLock(global_lock);
    return status;
}

static zx_status_t wait_for_interrupt(acpi_ec_device_t* dev) {
    uint32_t pending;
    zx_status_t status = zx_object_wait_one(dev->interrupt_event,
                                            IRQ_RECEIVED | EC_THREAD_SHUTDOWN,
                                            ZX_TIME_INFINITE,
                                            &pending);
    if (status != ZX_OK) {
        printf("acpi-ec: thread wait failed: %d\n", status);
        zx_object_signal(dev->interrupt_event, 0, EC_THREAD_SHUTDOWN_DONE);
        return status;
    }

    if (pending & EC_THREAD_SHUTDOWN) {
        zx_object_signal(dev->interrupt_event, 0, EC_THREAD_SHUTDOWN_DONE);
        return ZX_ERR_STOP;
    }

    /* Clear interrupt */
    zx_object_signal(dev->interrupt_event, IRQ_RECEIVED, 0);
    return ZX_OK;
}

static int acpi_ec_thread(void* arg) {
    acpi_ec_device_t* dev = arg;
    UINT32 global_lock;

    while (1) {
        zx_status_t zx_status = wait_for_interrupt(dev);
        if (zx_status != ZX_OK) {
            goto exiting_without_lock;
        }

        while (AcpiAcquireGlobalLock(0xFFFF, &global_lock) != AE_OK)
            ;

        uint8_t status;
        bool processed_evt = false;
        while ((status = inp(dev->cmd_port)) & EC_SC_SCI_EVT) {
            uint8_t event_code;
            zx_status_t zx_status = execute_query_op(dev, &event_code);
            if (zx_status != ZX_OK) {
                goto exiting_with_lock;
            }

            if (event_code != 0) {
                char method[5] = {0};
                snprintf(method, sizeof(method), "_Q%02x", event_code);
                xprintf("acpi-ec: Invoking method %s\n", method);
                AcpiEvaluateObject(dev->acpi_handle, method, NULL, NULL);
                xprintf("acpi-ec: Invoked method %s\n", method);
            } else {
                xprintf("acpi-ec: Spurious event?\n");
            }

            processed_evt = true;

            /* Clear interrupt before we check EVT again, to prevent a spurious
             * interrupt later.  There could be two sources of that spurious
             * wakeup: Either we handled two events back-to-back, or we didn't
             * wait for the OBF interrupt above. */
            zx_object_signal(dev->interrupt_event, IRQ_RECEIVED, 0);
        }

        if (!processed_evt) {
            xprintf("acpi-ec: Spurious wakeup, no evt: %#x\n", status);
        }

        AcpiReleaseGlobalLock(global_lock);
    }

exiting_with_lock:
    AcpiReleaseGlobalLock(global_lock);
exiting_without_lock:
    xprintf("acpi-ec: thread terminated\n");
    return 0;
}

static uint32_t raw_ec_event_gpe_handler(ACPI_HANDLE gpe_dev, uint32_t gpe_num, void* ctx) {
    acpi_ec_device_t* dev = ctx;
    zx_object_signal(dev->interrupt_event, 0, IRQ_RECEIVED);
    return ACPI_REENABLE_GPE;
}

static ACPI_STATUS get_ec_handle(
    ACPI_HANDLE object,
    UINT32 nesting_level,
    void* context,
    void** ret) {

    *(ACPI_HANDLE*)context = object;
    return AE_OK;
}

static ACPI_STATUS get_ec_gpe_info(
    ACPI_HANDLE ec_handle, ACPI_HANDLE* gpe_block, UINT32* gpe) {
    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    ACPI_STATUS status = AcpiEvaluateObject(
        ec_handle, (char*)"_GPE", NULL, &buffer);
    if (status != AE_OK) {
        return status;
    }

    /* According to section 12.11 of ACPI v6.1, a _GPE object on this device
     * evaluates to either an integer specifying bit in the GPEx_STS blocks
     * to use, or a package specifying which GPE block and which bit inside
     * that block to use. */
    ACPI_OBJECT* gpe_obj = buffer.Pointer;
    if (gpe_obj->Type == ACPI_TYPE_INTEGER) {
        *gpe_block = NULL;
        *gpe = gpe_obj->Integer.Value;
    } else if (gpe_obj->Type == ACPI_TYPE_PACKAGE) {
        if (gpe_obj->Package.Count != 2) {
            goto bailout;
        }
        ACPI_OBJECT* block_obj = &gpe_obj->Package.Elements[0];
        ACPI_OBJECT* gpe_num_obj = &gpe_obj->Package.Elements[1];
        if (block_obj->Type != ACPI_TYPE_LOCAL_REFERENCE) {
            goto bailout;
        }
        if (gpe_num_obj->Type != ACPI_TYPE_INTEGER) {
            goto bailout;
        }
        *gpe_block = block_obj->Reference.Handle;
        *gpe = gpe_num_obj->Integer.Value;
    } else {
        goto bailout;
    }
    ACPI_FREE(buffer.Pointer);
    return AE_OK;

bailout:
    xprintf("Failed to intepret EC GPE number");
    ACPI_FREE(buffer.Pointer);
    return AE_BAD_DATA;
}

struct ec_ports_callback_ctx {
    uint16_t* data_port;
    uint16_t* cmd_port;
    unsigned int resource_num;
};

static ACPI_STATUS get_ec_ports_callback(
    ACPI_RESOURCE* Resource, void* Context) {
    struct ec_ports_callback_ctx* ctx = Context;

    if (Resource->Type == ACPI_RESOURCE_TYPE_END_TAG) {
        return AE_OK;
    }

    /* The spec says there will be at most 3 resources */
    if (ctx->resource_num >= 3) {
        return AE_BAD_DATA;
    }
    /* The third resource only exists on HW-Reduced platforms, which we don't
     * support at the moment. */
    if (ctx->resource_num == 2) {
        xprintf("RESOURCE TYPE %d\n", Resource->Type);
        return AE_NOT_IMPLEMENTED;
    }

    /* The two resources we're expecting are both address regions.  First the
     * data one, then the command one.  We assume they're single IO ports. */
    if (Resource->Type != ACPI_RESOURCE_TYPE_IO) {
        return AE_SUPPORT;
    }
    if (Resource->Data.Io.Maximum != Resource->Data.Io.Minimum) {
        return AE_SUPPORT;
    }

    uint16_t port = Resource->Data.Io.Minimum;
    if (ctx->resource_num == 0) {
        *ctx->data_port = port;
    } else {
        *ctx->cmd_port = port;
    }

    ctx->resource_num++;
    return AE_OK;
}

static ACPI_STATUS get_ec_ports(
    ACPI_HANDLE ec_handle, uint16_t* data_port, uint16_t* cmd_port) {
    struct ec_ports_callback_ctx ctx = {
        .data_port = data_port,
        .cmd_port = cmd_port,
        .resource_num = 0,
    };

    return AcpiWalkResources(ec_handle, (char*)"_CRS", get_ec_ports_callback, &ctx);
}

static void acpi_ec_release(void* ctx) {
    acpi_ec_device_t* dev = ctx;

    if (dev->ec_space_setup) {
        AcpiRemoveAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_EC, ec_space_request_handler);
    }

    if (dev->gpe_setup) {
        AcpiDisableGpe(dev->gpe_block, dev->gpe);
        AcpiRemoveGpeHandler(dev->gpe_block, dev->gpe, raw_ec_event_gpe_handler);
    }

    if (dev->interrupt_event != ZX_HANDLE_INVALID) {
        if (dev->thread_setup) {
            /* Shutdown the EC thread */
            zx_object_signal(dev->interrupt_event, 0, EC_THREAD_SHUTDOWN);
            zx_object_wait_one(dev->interrupt_event, EC_THREAD_SHUTDOWN_DONE, ZX_TIME_INFINITE, NULL);
            thrd_join(dev->evt_thread, NULL);
        }

        zx_handle_close(dev->interrupt_event);
    }

    free(dev);
}

static zx_status_t acpi_ec_suspend(void* ctx, uint32_t flags) {
    acpi_ec_device_t* dev = ctx;

    if (flags != DEVICE_SUSPEND_FLAG_MEXEC) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    AcpiRemoveAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_EC, ec_space_request_handler);
    dev->ec_space_setup = false;

    AcpiDisableGpe(dev->gpe_block, dev->gpe);
    AcpiRemoveGpeHandler(dev->gpe_block, dev->gpe, raw_ec_event_gpe_handler);
    dev->gpe_setup = false;

    zx_object_signal(dev->interrupt_event, 0, EC_THREAD_SHUTDOWN);
    zx_object_wait_one(dev->interrupt_event, EC_THREAD_SHUTDOWN_DONE, ZX_TIME_INFINITE, NULL);
    thrd_join(dev->evt_thread, NULL);
    zx_handle_close(dev->interrupt_event);
    dev->interrupt_event = ZX_HANDLE_INVALID;
    return ZX_OK;
}

static zx_protocol_device_t acpi_ec_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = acpi_ec_release,
    .suspend = acpi_ec_suspend,
};

zx_status_t ec_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
    xprintf("acpi-ec: init\n");

    acpi_ec_device_t* dev = calloc(1, sizeof(acpi_ec_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    dev->acpi_handle = acpi_handle;

    zx_status_t err = zx_event_create(0, &dev->interrupt_event);
    if (err != ZX_OK) {
        xprintf("acpi-ec: Failed to create event: %d\n", err);
        acpi_ec_release(dev);
        return err;
    }

    ACPI_STATUS status = get_ec_gpe_info(acpi_handle, &dev->gpe_block, &dev->gpe);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to decode GPE info: %d\n", status);
        goto acpi_error;
    }

    status = get_ec_ports(
        acpi_handle, &dev->data_port, &dev->cmd_port);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to decode comm info: %d\n", status);
        goto acpi_error;
    }

    /* Setup GPE handling */
    status = AcpiInstallGpeHandler(
        dev->gpe_block, dev->gpe, ACPI_GPE_EDGE_TRIGGERED,
        raw_ec_event_gpe_handler, dev);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to install GPE %d: %x\n", dev->gpe, status);
        goto acpi_error;
    }
    status = AcpiEnableGpe(dev->gpe_block, dev->gpe);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to enable GPE %d: %x\n", dev->gpe, status);
        AcpiRemoveGpeHandler(dev->gpe_block, dev->gpe, raw_ec_event_gpe_handler);
        goto acpi_error;
    }
    dev->gpe_setup = true;

    /* TODO(teisenbe): This thread should ideally be at a high priority, since
       it takes the ACPI global lock which is shared with SMM. */
    int ret = thrd_create_with_name(&dev->evt_thread, acpi_ec_thread, dev, "acpi-ec-evt");
    if (ret != thrd_success) {
        xprintf("acpi-ec: Failed to create thread\n");
        acpi_ec_release(dev);
        return ZX_ERR_INTERNAL;
    }
    dev->thread_setup = true;

    status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_EC,
                                            ec_space_request_handler,
                                            ec_space_setup_handler,
                                            dev);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to install ec space handler\n");
        acpi_ec_release(dev);
        return acpi_to_zx_status(status);
    }
    dev->ec_space_setup = true;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-ec",
        .ctx = dev,
        .ops = &acpi_ec_device_proto,
        .proto_id = ZX_PROTOCOL_MISC,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        xprintf("acpi-ec: could not add device! err=%d\n", status);
        acpi_ec_release(dev);
        return status;
    }

    printf("acpi-ec: initialized\n");
    return ZX_OK;

acpi_error:
    acpi_ec_release(dev);
    return acpi_to_zx_status(status);
}
