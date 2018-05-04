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
#include <zircon/syscalls/smc.h>

static bool smc_create_test(void) {
    BEGIN_TEST;

    zx_handle_t smc_handle;
    zx_handle_t shm_vmo_handle;
    zx_info_smc_t smc_info = {};
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle, &shm_vmo_handle), ZX_OK, "failed to create smc object");

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

    EXPECT_EQ(zx_handle_close(smc_handle), ZX_OK, "failed to close smc handle");
    EXPECT_EQ(zx_handle_close(shm_vmo_handle), ZX_OK, "failed to close vmo handle");

    END_TEST;
}

static bool smc_create_multiple_test(void) {
    BEGIN_TEST;

    zx_handle_t smc_handle1;
    zx_handle_t shm_vmo_handle1;
    zx_handle_t smc_handle2;
    zx_handle_t shm_vmo_handle2;
    zx_info_smc_t smc_info = {};
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle1, &shm_vmo_handle1), ZX_OK,
            "failed to create smc object");
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle2, &shm_vmo_handle2), ZX_ERR_BAD_STATE,
            "smc object can not create twice");

    EXPECT_EQ(zx_handle_close(smc_handle1), ZX_OK, "failed to close smc handle");
    EXPECT_EQ(zx_handle_close(shm_vmo_handle1), ZX_OK, "failed to close vmo handle");

    END_TEST;
}

static bool smc_handle_request_test(void) {
    BEGIN_TEST;

    zx_handle_t smc_handle;
    zx_handle_t shm_vmo_handle;
    zx_info_smc_t smc_info = {};
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle, &shm_vmo_handle), ZX_OK, "failed to create smc object");

    /* trigger fake smc request from smc kernel object */
    ASSERT_EQ(zx_object_signal(smc_handle, 0, ZX_SMC_FAKE_REQUEST), ZX_OK,
              "failed to signal smc kernel object");

    smc32_args_t smc_args = {};
    ASSERT_EQ(zx_smc_wait_for_request(smc_handle, &smc_args, sizeof(smc32_args_t)), ZX_OK,
              "failed to wait for smc request");

    EXPECT_EQ(smc_args.smc_nr, 0x534d43UL, "wrong smc_nr");
    EXPECT_EQ(smc_args.params[0], 0x70617230UL, "wrong param[0]");
    EXPECT_EQ(smc_args.params[1], 0x70617231UL, "wrong param[1]");
    EXPECT_EQ(smc_args.params[2], 0x70617232UL, "wrong param[2]");

    EXPECT_EQ(zx_smc_set_result(smc_handle, smc_args.smc_nr), ZX_OK, "failed to set result");

    /* wait for test result signal from smc kernel object */
    zx_signals_t s = ZX_SIGNAL_NONE;
    EXPECT_EQ(zx_object_wait_one(smc_handle, ZX_USER_SIGNAL_ALL, ZX_TIME_INFINITE, &s), ZX_OK,
              "failed at object wait syscall");
    EXPECT_EQ(s & ZX_USER_SIGNAL_ALL, ZX_SMC_TEST_PASS, "got unexpected smc result");

    EXPECT_EQ(zx_handle_close(smc_handle), ZX_OK, "failed to close smc handle");
    EXPECT_EQ(zx_handle_close(shm_vmo_handle), ZX_OK, "failed to close vmo handle");

    END_TEST;
}

static bool smc_shm_vmo_basic_test(void) {
    BEGIN_TEST;

    zx_handle_t smc_handle;
    zx_handle_t shm_vmo_handle;
    zx_info_smc_t smc_info = {};
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle, &shm_vmo_handle), ZX_OK, "failed to create smc object");

    zx_info_handle_basic_t basic_info = {};
    zx_status_t status = zx_object_get_info(shm_vmo_handle, ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "handle should be valid");

    const zx_rights_t expected_rights = ZX_RIGHTS_IO | ZX_RIGHT_MAP;

    EXPECT_GT(basic_info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(basic_info.type, (uint32_t)ZX_OBJ_TYPE_VMO, "handle should be an vmo");
    EXPECT_EQ(basic_info.rights, expected_rights, "wrong set of rights");
    EXPECT_EQ(basic_info.props, (uint32_t)ZX_OBJ_PROP_WAITABLE, "should have waitable property");
    EXPECT_EQ(basic_info.related_koid, 0ULL, "vmo don't have associated koid");

    zx_handle_t dup_handle;
    ASSERT_EQ(zx_handle_duplicate(shm_vmo_handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle),
            ZX_ERR_ACCESS_DENIED, "shm vmo can't be duplicated");

    EXPECT_EQ(zx_handle_close(smc_handle), ZX_OK, "failed to close smc handle");
    EXPECT_EQ(zx_handle_close(shm_vmo_handle), ZX_OK, "failed to close vmo handle");

    END_TEST;
}

/* TODO(james): share memory should be mapped as non-secure in page table */
static bool smc_shm_vmo_write_test(void) {
    BEGIN_TEST;

    zx_handle_t smc_handle;
    zx_handle_t shm_vmo_handle;
    zx_info_smc_t smc_info = {};
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle, &shm_vmo_handle), ZX_OK, "failed to create smc object");

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

    /* notify smc kernel object to verify data */
    ASSERT_EQ(zx_object_signal(smc_handle, 0, ZX_SMC_VERIFY_SHM), ZX_OK,
              "failed to signal smc kernel object");

    zx_signals_t s = ZX_SIGNAL_NONE;
    EXPECT_EQ(zx_object_wait_one(smc_handle, ZX_SMC_TEST_PASS | ZX_SMC_TEST_FAIL, ZX_TIME_INFINITE, &s),
              ZX_OK, "failed at object wait syscall");
    EXPECT_EQ(s & ZX_USER_SIGNAL_ALL, ZX_SMC_TEST_PASS, "failed to verify shm data");

    EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), virt, vmo_size), ZX_OK, "failed to unmap shm");

    EXPECT_EQ(zx_handle_close(smc_handle), ZX_OK, "failed to close smc handle");
    EXPECT_EQ(zx_handle_close(shm_vmo_handle), ZX_OK, "failed to close vmo handle");

    END_TEST;
}

static bool smc_shm_vmo_read_test(void) {
    BEGIN_TEST;

    zx_handle_t smc_handle;
    zx_handle_t shm_vmo_handle;
    zx_info_smc_t smc_info = {};
    ASSERT_EQ(zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t), &smc_handle, &shm_vmo_handle), ZX_OK, "failed to create smc object");

    size_t vmo_size = smc_info.ns_shm.size;

    uintptr_t virt;
    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), 0, shm_vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt),
              ZX_OK, "failed to map shm vmo");
    ASSERT_NE(virt, 0UL, "shm va should not be zero");

    /* notify smc kernel object to write test data to shm */
    ASSERT_EQ(zx_object_signal(smc_handle, 0, ZX_SMC_WRITE_SHM), ZX_OK,
              "failed to signal smc kernel object");

    zx_signals_t s = ZX_SIGNAL_NONE;
    ASSERT_EQ(zx_object_wait_one(smc_handle, ZX_SMC_TEST_PASS | ZX_SMC_TEST_FAIL, ZX_TIME_INFINITE, &s),
              ZX_OK, "failed at object wait syscall");
    ASSERT_EQ(s & ZX_USER_SIGNAL_ALL, ZX_SMC_TEST_PASS, "failed to write shm data");

    /* verify test data */
    for(size_t i = 0; i < vmo_size; i++) {
        ASSERT_EQ(((uint8_t*)virt)[i], (uint8_t)((i & 0xff) ^ 0xaa), "verify test data fail");
    }

    EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), virt, vmo_size), ZX_OK, "failed to unmap shm");

    EXPECT_EQ(zx_handle_close(smc_handle), ZX_OK, "failed to close smc handle");
    EXPECT_EQ(zx_handle_close(shm_vmo_handle), ZX_OK, "failed to close vmo handle");

    END_TEST;
}

BEGIN_TEST_CASE(smc_tests)
RUN_TEST(smc_create_test)
RUN_TEST(smc_create_multiple_test)
RUN_TEST(smc_handle_request_test)
RUN_TEST(smc_shm_vmo_basic_test)
RUN_TEST(smc_shm_vmo_write_test)
RUN_TEST(smc_shm_vmo_read_test)
END_TEST_CASE(smc_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
