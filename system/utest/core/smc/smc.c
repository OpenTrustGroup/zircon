// Copyright 2018 Open Trust Group
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc_service.h>

static zx_handle_t smc_handle;
static zx_handle_t shm_vmo_handle;
static zx_info_smc_t smc_info = {};

static bool setup(void) {
    BEGIN_HELPER;

    if (smc_handle != ZX_HANDLE_INVALID) {
        zx_handle_close(smc_handle);
        smc_handle = ZX_HANDLE_INVALID;
    }
    if (shm_vmo_handle != ZX_HANDLE_INVALID) {
        zx_handle_close(shm_vmo_handle);
        shm_vmo_handle = ZX_HANDLE_INVALID;
    }

    ASSERT_EQ(zx_smc_create(0, &smc_info, &smc_handle, &shm_vmo_handle),
              ZX_OK, "failed to create smc object");
    END_HELPER;
}

static bool tear_down(void) {
    BEGIN_HELPER;
    ASSERT_EQ(zx_handle_close(smc_handle), ZX_OK, "failed to close smc handle");
    smc_handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_handle_close(shm_vmo_handle), ZX_OK, "failed to close vmo handle");
    shm_vmo_handle = ZX_HANDLE_INVALID;
    END_HELPER;
}

static bool smc_create_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    EXPECT_GT(smc_info.ns_shm.base_phys, (uint32_t)0, "ns-shm pa should not be zero");
    EXPECT_GT(smc_info.ns_shm.size, (uint32_t)0, "ns-shm size should not be zero");
    EXPECT_EQ(smc_info.ns_shm.base_phys % PAGE_SIZE, (uint32_t)0, "ns-shm pa should be page aligned");
    EXPECT_EQ(smc_info.ns_shm.size % PAGE_SIZE, (uint32_t)0, "ns-shm size should be page aligned");
    EXPECT_EQ(smc_info.ns_shm.use_cache, (bool)true, "default ns-shm cache policy is enabled");

    zx_info_handle_basic_t info = {};
    zx_status_t status = zx_object_get_info(smc_handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "handle should be valid");

    const zx_rights_t expected_rights = (ZX_RIGHTS_BASIC & (~ZX_RIGHT_DUPLICATE)) | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL;

    EXPECT_GT(info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(info.type, (uint32_t)ZX_OBJ_TYPE_SMC, "handle should be an smc");
    EXPECT_EQ(info.rights, expected_rights, "wrong set of rights");
    EXPECT_EQ(info.props, (uint32_t)ZX_OBJ_PROP_WAITABLE, "should have waitable property");
    EXPECT_EQ(info.related_koid, 0ULL, "smc don't have associated koid");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

static bool smc_create_multiple_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    zx_handle_t h1, h2;
    zx_info_smc_t tmp_smc_info = {};

    ASSERT_EQ(zx_smc_create(0, &tmp_smc_info, &h1, &h2),
              ZX_ERR_BAD_STATE, "smc object can not create twice");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

static void* wait_smc_call(void* args) {
    zx_status_t status = zx_object_wait_one(smc_handle, ZX_SMC_READABLE,
                                            ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK)
        return NULL;

    smc32_args_t* smc_args = (smc32_args_t*)args;
    status = zx_smc_read(smc_handle, smc_args);
    if (status == ZX_OK) {
        zx_smc_set_result(smc_handle, SM_OK);
    }

    return NULL;
}

static bool smc_handle_request_good_path_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    pthread_t th;
    smc32_args_t actual_smc_args = {};
    pthread_create(&th, NULL, wait_smc_call, (void*)&actual_smc_args);

    long smc_ret = -1;
    smc32_args_t expect_smc_args = {
        .smc_nr = SMC_SC_VIRTIO_START,
        .params = {0x123U, 0x456U, 0x789U},
    };
    ASSERT_EQ(zx_smc_call_test(smc_handle, &expect_smc_args, &smc_ret),
              ZX_OK, "failed to issue smc call");

    pthread_join(th, NULL);

    EXPECT_EQ(actual_smc_args.smc_nr, expect_smc_args.smc_nr, "wrong smc_nr");
    EXPECT_EQ(actual_smc_args.params[0], expect_smc_args.params[0], "wrong param[0]");
    EXPECT_EQ(actual_smc_args.params[1], expect_smc_args.params[1], "wrong param[1]");
    EXPECT_EQ(actual_smc_args.params[2], expect_smc_args.params[2], "wrong param[2]");
    EXPECT_EQ(smc_ret, 0, "smc_ret != 0");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

static void* issue_smc_call(void* args) {
    long smc_ret = -1;
    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_VIRTIO_START,
        .params = {0x123U, 0x456U, 0x789U},
    };

    zx_smc_call_test(smc_handle, &smc_args, &smc_ret);
    return NULL;
}

static bool smc_handle_request_bad_path_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    pthread_t th;
    pthread_create(&th, NULL, issue_smc_call, NULL);

    ASSERT_EQ(zx_object_wait_one(smc_handle, ZX_SMC_READABLE, ZX_TIME_INFINITE, NULL),
              ZX_OK, "wait smc");

    smc32_args_t smc_args;
    ASSERT_EQ(zx_smc_read(smc_handle, &smc_args), ZX_OK, "smc_read");
    EXPECT_EQ(smc_args.smc_nr, (uint32_t)SMC_SC_VIRTIO_START, "wrong smc_nr");

    /* read twice should return error */
    ASSERT_EQ(zx_smc_read(smc_handle, &smc_args), ZX_ERR_SHOULD_WAIT, "smc_read");

    ASSERT_EQ(zx_smc_set_result(smc_handle, SM_OK), ZX_OK, "smc_set_result");

    /* set_result twice should return error */
    ASSERT_EQ(zx_smc_set_result(smc_handle, SM_OK), ZX_ERR_BAD_STATE, "smc_set_result");

    ASSERT_EQ(pthread_join(th, NULL), 0, "pthread join");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

static bool smc_shm_vmo_basic_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    zx_info_handle_basic_t basic_info = {};
    zx_status_t status = zx_object_get_info(shm_vmo_handle, ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "handle should be valid");

    const zx_rights_t expected_rights = ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_MAP_NS;

    EXPECT_GT(basic_info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(basic_info.type, (uint32_t)ZX_OBJ_TYPE_VMO, "handle should be an vmo");
    EXPECT_EQ(basic_info.rights, expected_rights, "wrong set of rights");
    EXPECT_EQ(basic_info.props, (uint32_t)ZX_OBJ_PROP_WAITABLE, "should have waitable property");
    EXPECT_EQ(basic_info.related_koid, 0ULL, "vmo don't have associated koid");

    zx_handle_t dup_handle;
    ASSERT_EQ(zx_handle_duplicate(shm_vmo_handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle),
            ZX_ERR_ACCESS_DENIED, "shm vmo can't be duplicated");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

static bool smc_shm_vmo_write_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    size_t vmo_size = smc_info.ns_shm.size;

    uintptr_t virt;
    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), 0, shm_vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt),
              ZX_OK, "failed to map shm vmo");
    ASSERT_NE(virt, 0UL, "shm va should not be zero");

    /* write test data to shm */
    for(size_t i = 0; i < vmo_size; i++) {
        ((uint8_t*)virt)[i] = (uint8_t)(i & 0xff);
    }

    /* verify test data in kernel space */
    long smc_ret = -1;
    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_VERIFY_SHM,
    };
    ASSERT_EQ(zx_smc_call_test(smc_handle, &smc_args, &smc_ret),
              ZX_OK, "failed to issue smc call");
    EXPECT_EQ(smc_ret, 0, "failed to verify shm data");

    EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), virt, vmo_size), ZX_OK, "failed to unmap shm");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

static bool smc_shm_vmo_read_test(void) {
    BEGIN_TEST;
    ASSERT_TRUE(setup(), "setup");

    size_t vmo_size = smc_info.ns_shm.size;

    uintptr_t virt;
    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), 0, shm_vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt),
              ZX_OK, "failed to map shm vmo");
    ASSERT_NE(virt, 0UL, "shm va should not be zero");

    /* notify kernel to write test data */
    long smc_ret = -1;
    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_WRITE_SHM,
    };
    ASSERT_EQ(zx_smc_call_test(smc_handle, &smc_args, &smc_ret),
              ZX_OK, "failed to issue smc call");
    EXPECT_EQ(smc_ret, 0, "failed to write shm data");

    /* verify test data */
    for(size_t i = 0; i < vmo_size; i++) {
        ASSERT_EQ(((uint8_t*)virt)[i], (uint8_t)((i & 0xff) ^ 0xaa), "verify test data fail");
    }

    EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), virt, vmo_size), ZX_OK, "failed to unmap shm");

    ASSERT_TRUE(tear_down(), "tear down");
    END_TEST;
}

BEGIN_TEST_CASE(smc_tests)
RUN_TEST(smc_create_test)
RUN_TEST(smc_create_multiple_test)
RUN_TEST(smc_handle_request_good_path_test)
RUN_TEST(smc_handle_request_bad_path_test)
RUN_TEST(smc_shm_vmo_basic_test)
RUN_TEST(smc_shm_vmo_write_test)
RUN_TEST(smc_shm_vmo_read_test)
END_TEST_CASE(smc_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
