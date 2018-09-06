// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/scpi.h>
#include <soc/aml-common/aml-thermal.h>
#include <zircon/device/thermal.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

static void aml_set_fan_level(aml_thermal_t* dev, uint32_t level) {
    switch (level) {
    case 0:
        gpio_write(&dev->gpio, FAN_CTL0, 0);
        gpio_write(&dev->gpio, FAN_CTL1, 0);
        break;
    case 1:
        gpio_write(&dev->gpio, FAN_CTL0, 1);
        gpio_write(&dev->gpio, FAN_CTL1, 0);
        break;
    case 2:
        gpio_write(&dev->gpio, FAN_CTL0, 0);
        gpio_write(&dev->gpio, FAN_CTL1, 1);
        break;
    case 3:
        gpio_write(&dev->gpio, FAN_CTL0, 1);
        gpio_write(&dev->gpio, FAN_CTL1, 1);
        break;
    default:
        break;
    }
    dev->current_fan_level = level;
}

static zx_status_t aml_notify_thermal_deamon(zx_handle_t port, uint32_t trip_id) {
    zx_port_packet_t thermal_port_packet;
    thermal_port_packet.key = trip_id;
    thermal_port_packet.type = ZX_PKT_TYPE_USER;
    return zx_port_queue(port, &thermal_port_packet);
}

static int aml_thermal_notify_thread(void* ctx) {
    aml_thermal_t* dev = ctx;
    uint32_t temperature;
    bool critical_temp_measure_taken = false;

    // Notify the thermal deamon about the default settings
    zx_status_t status = aml_notify_thermal_deamon(dev->port, dev->current_trip_idx);
    if (status != ZX_OK) {
        THERMAL_ERROR("Failed to send packet via port to Thermal Deamon: Thermal disabled");
        return status;
    }

    while (true) {
        status = scpi_get_sensor_value(&dev->scpi,
                                       dev->temp_sensor_id,
                                       &temperature);
        if (status != ZX_OK) {
            THERMAL_ERROR("Unable to get thermal sensor value: Thermal disabled\n");
            return status;
        }

        // Update the temperature in our context
        dev->current_temperature = temperature;

        uint32_t idx = dev->current_trip_idx;
        bool signal = true;

        if ((idx != dev->device->num_trip_points - 1) &&
            (temperature >= dev->device->trip_point_info[idx + 1].up_temp)) {
            // Triggered next trip point
            dev->current_trip_idx = idx + 1;
        } else if (idx != 0 && temperature < dev->device->trip_point_info[idx].down_temp) {
            // Triggered prev trip point
            dev->current_trip_idx = idx - 1;
            if (idx == dev->device->num_trip_points - 1) {
                // A prev trip point triggered, so the temperature
                // is falling down below the critical temperature
                // make a note of that
                critical_temp_measure_taken = false;
            }
        } else if ((idx == dev->device->num_trip_points - 1) &&
                   (temperature >= dev->device->critical_temp) &&
                   critical_temp_measure_taken != true) {
            // The device temperature is crossing the critical
            // temperature, set the CPU freq to the lowest possible
            // setting to ensure the temperature doesn't rise any further
            signal = false;
            critical_temp_measure_taken = true;
            status = scpi_set_dvfs_idx(&dev->scpi, BIG_CLUSTER_POWER_DOMAIN, 0);
            if (status != ZX_OK) {
                THERMAL_ERROR("Unable to set DVFS OPP for Big cluster\n");
                return status;
            }
            status = scpi_set_dvfs_idx(&dev->scpi, LITTLE_CLUSTER_POWER_DOMAIN, 0);
            if (status != ZX_OK) {
                THERMAL_ERROR("Unable to set DVFS OPP for Little cluster\n");
                return status;
            }
        } else {
            signal = false;
        }

        if (signal) {
            // Notify the thermal deamon about which trip point triggered
            status = aml_notify_thermal_deamon(dev->port, dev->current_trip_idx);
            if (status != ZX_OK) {
                THERMAL_ERROR("Failed to send packet via port to Thermal Deamon: Thermal disabled");
                return status;
            }
        }

        sleep(5);
    }
    return ZX_OK;
}

static zx_status_t aml_thermal_set_dvfs_opp(aml_thermal_t* dev,
                                            dvfs_info_t* info) {
    bool set_new_opp = false;

    if (info->power_domain == BIG_CLUSTER_POWER_DOMAIN) {
        if (dev->current_big_cluster_opp_idx != info->op_idx) {
            set_new_opp = true;
            dev->current_big_cluster_opp_idx = info->op_idx;
        }
    } else {
        if (dev->current_little_cluster_opp_idx != info->op_idx) {
            set_new_opp = true;
            dev->current_little_cluster_opp_idx = info->op_idx;
        }
    }

    if (set_new_opp) {
        return scpi_set_dvfs_idx(&dev->scpi, info->power_domain, info->op_idx);
    } else {
        return ZX_OK;
    }
}

static void aml_thermal_get_device_info(aml_thermal_t* dev,
                                        thermal_device_info_t* info) {
    memcpy(info, dev->device, sizeof(thermal_device_info_t));
}

static zx_status_t aml_thermal_get_state_change_port(aml_thermal_t* dev,
                                                     zx_handle_t* port) {
    return zx_handle_duplicate(dev->port, ZX_RIGHT_SAME_RIGHTS, port);
}

static void aml_thermal_release(void* ctx) {
    aml_thermal_t* thermal = ctx;
    zx_handle_close(thermal->port);
    int res;
    thrd_join(thermal->notify_thread, &res);
    free(thermal->device);
    free(thermal);
}

static zx_status_t aml_thermal_ioctl(void* ctx, uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len,
                                     size_t* out_actual) {
    aml_thermal_t* dev = ctx;
    zx_status_t status = ZX_OK;

    switch (op) {
    case IOCTL_THERMAL_GET_DEVICE_INFO: {
        if (out_len != sizeof(thermal_device_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        thermal_device_info_t info;
        aml_thermal_get_device_info(dev, &info);
        memcpy(out_buf, &info, sizeof(thermal_device_info_t));
        *out_actual = sizeof(thermal_device_info_t);
        return ZX_OK;
    }

    case IOCTL_THERMAL_GET_STATE_CHANGE_PORT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        status = aml_thermal_get_state_change_port(dev, out_buf);
        if (status != ZX_OK) {
            return status;
        }
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }

    case IOCTL_THERMAL_SET_FAN_LEVEL: {
        if (in_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t* fan_level = (uint32_t*)in_buf;
        aml_set_fan_level(dev, *fan_level);
        return ZX_OK;
    }

    case IOCTL_THERMAL_GET_FAN_LEVEL: {
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t* fan_level = (uint32_t*)out_buf;
        *fan_level = dev->current_fan_level;
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }

    case IOCTL_THERMAL_SET_DVFS_OPP: {
        if (in_len != sizeof(dvfs_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        dvfs_info_t* dvfs_info = (dvfs_info_t*)in_buf;
        return aml_thermal_set_dvfs_opp(dev, dvfs_info);
    }

    case IOCTL_THERMAL_GET_TEMPERATURE: {
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t* temperature = (uint32_t*)out_buf;
        *temperature = dev->current_temperature;
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }

    case IOCTL_THERMAL_GET_DVFS_INFO: {
        if (in_len != sizeof(uint32_t) || out_len != sizeof(scpi_opp_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t* power_domain = (uint32_t*)in_buf;
        if (*power_domain >= MAX_DVFS_DOMAINS) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(out_buf, &dev->device->opps[*power_domain],
               sizeof(scpi_opp_t));
        *out_actual = sizeof(scpi_opp_t);
        return ZX_OK;
    }

    case IOCTL_THERMAL_GET_DVFS_OPP: {
        if (in_len != sizeof(uint32_t) || out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t* power_domain = (uint32_t*)in_buf;
        uint32_t* opp_idx = (uint32_t*)out_buf;
        if (power_domain == BIG_CLUSTER_POWER_DOMAIN) {
            *opp_idx = dev->current_big_cluster_opp_idx;
        } else {
            *opp_idx = dev->current_little_cluster_opp_idx;
        }
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_protocol_device_t aml_thermal_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_thermal_release,
    .ioctl = aml_thermal_ioctl,
};

static zx_status_t aml_thermal_init(aml_thermal_t* thermal) {
    pdev_device_info_t info;
    zx_status_t status = pdev_get_device_info(&thermal->pdev, &info);
    if (status != ZX_OK) {
        THERMAL_ERROR("pdev_get_device_info failed\n");
        return status;
    }

    // Configure the GPIOs
    for (uint32_t i = 0; i < info.gpio_count; i++) {
        status = gpio_config_out(&thermal->gpio, i, 0);
        if (status != ZX_OK) {
            THERMAL_ERROR("gpio_config failed\n");
            return status;
        }
    }

    // Create the thermal event
    status = zx_port_create(0, &thermal->port);
    if (status != ZX_OK) {
        THERMAL_ERROR("Unable to create thermal port\n");
        return status;
    }

    thermal->current_trip_idx = 0;

    // Populate DVFS info
    status = scpi_get_dvfs_info(&thermal->scpi, BIG_CLUSTER_POWER_DOMAIN,
                                &thermal->device->opps[0]);
    if (status != ZX_OK) {
        THERMAL_ERROR("scpi_get_dvfs_info for big cluster failed %d\n", status);
        return status;
    }

    status = scpi_get_dvfs_info(&thermal->scpi, LITTLE_CLUSTER_POWER_DOMAIN,
                                &thermal->device->opps[1]);
    if (status != ZX_OK) {
        THERMAL_ERROR("scpi_get_dvfs_info for little cluster failed %d\n", status);
        return status;
    }

    // Populate thermal sensor info
    status = scpi_get_sensor(&thermal->scpi, "aml_thermal", &thermal->temp_sensor_id);
    if (status != ZX_OK) {
        THERMAL_ERROR("Unable to get thermal sensor information: Thermal disabled\n");
        return status;
    }
    return ZX_OK;
}

static zx_status_t aml_thermal_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    aml_thermal_t* thermal = calloc(1, sizeof(aml_thermal_t));
    if (!thermal) {
        return ZX_ERR_NO_MEMORY;
    }

    thermal->device = calloc(1, sizeof(thermal_device_info_t));
    if (!thermal->device) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &thermal->pdev);
    if (status != ZX_OK) {
        THERMAL_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &thermal->gpio);
    if (status != ZX_OK) {
        THERMAL_ERROR("Could not get GPIO protocol\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_SCPI, &thermal->scpi);
    if (status != ZX_OK) {
        THERMAL_ERROR("Could not get SCPI protocol\n");
        goto fail;
    }

    // Populate board specific information
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, thermal->device,
                                 sizeof(*thermal->device), &actual);
    if (status != ZX_OK || actual != sizeof(thermal_device_info_t)) {
        THERMAL_ERROR("Could not get metadata\n");
        goto fail;
    }

    status = aml_thermal_init(thermal);
    if (status != ZX_OK) {
        THERMAL_ERROR("Thermal init failed\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim-thermal",
        .ctx = thermal,
        .ops = &aml_thermal_device_protocol,
        .proto_id = ZX_PROTOCOL_THERMAL,
    };

    status = device_add(parent, &args, &thermal->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_create_with_name(&thermal->notify_thread, aml_thermal_notify_thread,
                          thermal, "aml_thermal_notify_thread");

    return ZX_OK;

fail:
    zx_handle_close(thermal->port);
    free(thermal->device);
    free(thermal);
    return status;
}

static zx_driver_ops_t aml_thermal_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_thermal_bind,
};

ZIRCON_DRIVER_BEGIN(aml_thermal, aml_thermal_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SCPI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_THERMAL),
ZIRCON_DRIVER_END(aml_thermal)
