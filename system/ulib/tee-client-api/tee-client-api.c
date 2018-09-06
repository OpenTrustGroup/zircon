// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zircon/device/tee.h>

#include <tee-client-api/tee_client_api.h>

#define DEFAULT_TEE "/dev/class/tee/000"

static bool is_global_platform_compliant(int fd) {
    tee_ioctl_description_t tee_description;

    ssize_t ret = ioctl_tee_get_description(fd, &tee_description);

    return ret == sizeof(tee_description) ? tee_description.is_global_platform_compliant : false;
}

static void uuid_to_bytes(const TEEC_UUID* uuid, uint8_t out_bytes[TEE_IOCTL_UUID_SIZE]) {
    // Convert from TEEC_UUID to a raw byte array in network byte order. This is the format
    // that the underlying driver expects.
    *((uint32_t*)(out_bytes)) = htonl(uuid->timeLow);
    *((uint16_t*)(out_bytes + 4)) = htons(uuid->timeMid);
    *((uint16_t*)(out_bytes + 6)) = htons(uuid->timeHiAndVersion);
    memcpy(out_bytes + 8, uuid->clockSeqAndNode, sizeof(uuid->clockSeqAndNode));
}

static TEEC_Result convert_status_to_result(zx_status_t status) {
    switch (status) {
    case ZX_ERR_PEER_CLOSED:
        return TEEC_ERROR_COMMUNICATION;
    case ZX_ERR_INVALID_ARGS:
        return TEEC_ERROR_BAD_PARAMETERS;
    case ZX_ERR_NOT_SUPPORTED:
        return TEEC_ERROR_NOT_SUPPORTED;
    case ZX_ERR_NO_MEMORY:
        return TEEC_ERROR_OUT_OF_MEMORY;
    }
    return TEEC_ERROR_GENERIC;
}

TEEC_Result TEEC_InitializeContext(const char* name, TEEC_Context* context) {

    if (!context) {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    const char* tee_device = (name != NULL) ? name : DEFAULT_TEE;

    int fd = open(tee_device, O_RDWR);
    if (fd < 0) {
        return TEEC_ERROR_ITEM_NOT_FOUND;
    }

    if (!is_global_platform_compliant(fd)) {
        // This API is only designed to support TEEs that are Global Platform compliant.
        close(fd);
        return TEEC_ERROR_NOT_SUPPORTED;
    }
    context->imp.fd = fd;

    return TEEC_SUCCESS;
}

void TEEC_FinalizeContext(TEEC_Context* context) {
    if (context) {
        close(context->imp.fd);
    }
}

TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

void TEEC_ReleaseSharedMemory(TEEC_SharedMemory* sharedMem) {}

TEEC_Result TEEC_OpenSession(TEEC_Context* context,
                             TEEC_Session* session,
                             const TEEC_UUID* destination,
                             uint32_t connectionMethod,
                             const void* connectionData,
                             TEEC_Operation* operation,
                             uint32_t* returnOrigin) {
    TEEC_Result result = TEEC_SUCCESS;
    uint32_t return_origin = TEEC_ORIGIN_API;

    if (!context || !session) {
        result = TEEC_ERROR_BAD_PARAMETERS;
        return_origin = TEEC_ORIGIN_API;
        goto out;
    }

    // TODO(rjascani): Add support for operations on session open
    if (operation) {
        result = TEEC_ERROR_NOT_IMPLEMENTED;
        return_origin = TEEC_ORIGIN_API;
        goto out;
    }

    tee_ioctl_session_request_t session_request = {};
    tee_ioctl_session_t session_result = {};

    // Convert TEEC_UUID type from the TEE Client API to a raw byte array for the TEE device
    // interface.
    uuid_to_bytes(destination, session_request.trusted_app);

    ssize_t rc = ioctl_tee_open_session(context->imp.fd, &session_request, &session_result);
    if (rc < 0) {
        result = convert_status_to_result(rc);
        return_origin = TEEC_ORIGIN_COMMS;
        goto out;
    }
    result = session_result.return_code;
    return_origin = session_result.return_origin;
    if (result == TEEC_SUCCESS) {
        session->imp.session_id = session_result.session_id;
    }

out:
    if (returnOrigin) {
        *returnOrigin = return_origin;
    }
    return result;
}

void TEEC_CloseSession(TEEC_Session* session) {}

TEEC_Result TEEC_InvokeCommand(TEEC_Session* session,
                               uint32_t commandID,
                               TEEC_Operation* operation,
                               uint32_t* returnOrigin) {
    return TEEC_ERROR_NOT_IMPLEMENTED;
}

void TEEC_RequestCancellation(TEEC_Operation* operation) {}
