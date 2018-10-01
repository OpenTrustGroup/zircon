// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <lib/zx/resource.h>
#include <lib/zx/smc.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls/smc_service.h>

#include <gzos-shm/resource.h>

struct TestContext {
    zx::smc smc;
    zx::resource shm_rsc;
    zx::vmo shm_vmo;
    size_t shm_size;
    smc32_args smc_args;
};

static bool setup(TestContext* ctx) {
    BEGIN_HELPER;

    ASSERT_EQ(get_shm_resource(&ctx->shm_rsc),
              ZX_OK, "failed to get shm resource");

    ASSERT_EQ(zx::smc::create(0, &ctx->smc),
              ZX_OK, "failed to create smc object");

    zx_info_resource_t info;
    ASSERT_EQ(ctx->shm_rsc.get_info(ZX_INFO_RESOURCE, &info, sizeof(info),
                                    nullptr, nullptr),
              ZX_OK, "failed to get resource info");

    ctx->shm_size = info.size;
    ASSERT_EQ(zx::vmo::create_ns_mem(ctx->shm_rsc, info.base, info.size, &ctx->shm_vmo),
              ZX_OK, "failed to create vmo object");

    END_HELPER;
}

static bool smc_create_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    zx_info_handle_basic_t info = {};
    zx_status_t status = ctx.smc.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "handle should be valid");

    const zx_rights_t expected_rights = (ZX_RIGHTS_BASIC & (~ZX_RIGHT_DUPLICATE)) | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL;

    EXPECT_GT(info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(info.type, (uint32_t)ZX_OBJ_TYPE_SMC, "handle should be an smc");
    EXPECT_EQ(info.rights, expected_rights, "wrong set of rights");
    EXPECT_EQ(info.props, (uint32_t)ZX_OBJ_PROP_WAITABLE, "should have waitable property");
    EXPECT_EQ(info.related_koid, 0ULL, "smc don't have associated koid");

    END_TEST;
}

static bool smc_create_multiple_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    zx_handle_t handle;
    ASSERT_EQ(zx_smc_create(0, &handle),
              ZX_ERR_BAD_STATE, "smc object can not create twice");

    END_TEST;
}

static void* wait_smc_call(void* args) {
    auto ctx = reinterpret_cast<TestContext*>(args);
    auto& smc = ctx->smc;

    zx_status_t status = smc.wait_one(ZX_SMC_READABLE, zx::time::infinite(), NULL);
    if (status != ZX_OK)
        return NULL;

    status = smc.read(&ctx->smc_args);
    if (status == ZX_OK) {
        ctx->smc.set_result(SM_OK);
    }

    return NULL;
}

static bool smc_handle_request_good_path_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    pthread_t th;
    pthread_create(&th, NULL, wait_smc_call, &ctx);

    long smc_ret = -1;
    smc32_args_t expect_smc_args = {
        .smc_nr = SMC_SC_VIRTIO_START,
        .params = {0x123U, 0x456U, 0x789U},
    };
    ASSERT_EQ(zx_smc_call_test(ctx.smc.get(), &expect_smc_args, &smc_ret),
              ZX_OK, "failed to issue smc call");

    pthread_join(th, NULL);

    auto& smc_args = ctx.smc_args;
    EXPECT_EQ(smc_args.smc_nr, expect_smc_args.smc_nr, "wrong smc_nr");
    EXPECT_EQ(smc_args.params[0], expect_smc_args.params[0], "wrong param[0]");
    EXPECT_EQ(smc_args.params[1], expect_smc_args.params[1], "wrong param[1]");
    EXPECT_EQ(smc_args.params[2], expect_smc_args.params[2], "wrong param[2]");
    EXPECT_EQ(smc_ret, 0, "smc_ret != 0");

    END_TEST;
}

static void* issue_smc_call(void* args) {
    auto ctx = reinterpret_cast<TestContext*>(args);
    long smc_ret = -1;

    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_VIRTIO_START,
        .params = {0x123U, 0x456U, 0x789U},
    };

    zx_smc_call_test(ctx->smc.get(), &smc_args, &smc_ret);
    return NULL;
}

static bool smc_handle_request_bad_path_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    pthread_t th;
    pthread_create(&th, NULL, issue_smc_call, &ctx);

    auto& smc = ctx.smc;
    ASSERT_EQ(smc.wait_one(ZX_SMC_READABLE, zx::time::infinite(), NULL),
              ZX_OK, "wait smc");

    smc32_args_t smc_args;
    ASSERT_EQ(smc.read(&smc_args), ZX_OK, "smc_read");
    EXPECT_EQ(smc_args.smc_nr, (uint32_t)SMC_SC_VIRTIO_START, "wrong smc_nr");

    /* read twice should return error */
    ASSERT_EQ(smc.read(&smc_args), ZX_ERR_SHOULD_WAIT, "smc_read");

    ASSERT_EQ(smc.set_result(SM_OK), ZX_OK, "smc_set_result");

    /* set_result twice should return error */
    ASSERT_EQ(smc.set_result(SM_OK), ZX_ERR_BAD_STATE, "smc_set_result");

    ASSERT_EQ(pthread_join(th, NULL), 0, "pthread join");

    END_TEST;
}

static bool smc_shm_vmo_basic_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    zx_info_handle_basic_t basic_info = {};
    zx_status_t status = ctx.shm_vmo.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "handle should be valid");

    const zx_rights_t expected_rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO |
                                        ZX_RIGHT_MAP | ZX_RIGHT_MAP_NS;

    EXPECT_GT(basic_info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(basic_info.type, (uint32_t)ZX_OBJ_TYPE_VMO, "handle should be an vmo");
    EXPECT_EQ(basic_info.rights, expected_rights, "wrong set of rights");
    EXPECT_EQ(basic_info.props, (uint32_t)ZX_OBJ_PROP_WAITABLE, "should have waitable property");
    EXPECT_EQ(basic_info.related_koid, 0ULL, "vmo don't have associated koid");

    END_TEST;
}

static bool smc_shm_vmo_write_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    size_t vmo_size = ctx.shm_size;

    uintptr_t virt;
    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(),
                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE,
                          0, ctx.shm_vmo.get(), 0, vmo_size, &virt),
              ZX_OK, "failed to map shm vmo");
    ASSERT_NE(virt, 0UL, "shm va should not be zero");

    auto cleanup = fbl::MakeAutoCall([&virt, &vmo_size]() {
        zx_vmar_unmap(zx_vmar_root_self(), virt, vmo_size);
    });

    /* write test data to shm */
    for (size_t i = 0; i < vmo_size; i++) {
        ((uint8_t*)virt)[i] = (uint8_t)(i & 0xff);
    }

    /* verify test data in kernel space */
    long smc_ret = -1;
    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_VERIFY_SHM,
        .params = {0},
    };
    ASSERT_EQ(zx_smc_call_test(ctx.smc.get(), &smc_args, &smc_ret),
              ZX_OK, "failed to issue smc call");
    EXPECT_EQ(smc_ret, 0, "failed to verify shm data");

    END_TEST;
}

static bool smc_shm_vmo_read_test(void) {
    BEGIN_TEST;
    TestContext ctx;
    ASSERT_TRUE(setup(&ctx), "setup");

    size_t vmo_size = ctx.shm_size;

    uintptr_t virt;
    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(),
                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE,
                          0, ctx.shm_vmo.get(), 0, vmo_size, &virt),
              ZX_OK, "failed to map shm vmo");
    ASSERT_NE(virt, 0UL, "shm va should not be zero");

    auto cleanup = fbl::MakeAutoCall([&virt, &vmo_size]() {
        zx_vmar_unmap(zx_vmar_root_self(), virt, vmo_size);
    });

    /* notify kernel to write test data */
    long smc_ret = -1;
    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_WRITE_SHM,
        .params = {0},
    };
    ASSERT_EQ(zx_smc_call_test(ctx.smc.get(), &smc_args, &smc_ret),
              ZX_OK, "failed to issue smc call");
    EXPECT_EQ(smc_ret, 0, "failed to write shm data");

    /* verify test data */
    for (size_t i = 0; i < vmo_size; i++) {
        ASSERT_EQ(((uint8_t*)virt)[i], (uint8_t)((i & 0xff) ^ 0xaa), "verify test data fail");
    }

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
