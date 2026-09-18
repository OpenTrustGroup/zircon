// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_PAGER_TEST_HELPER_H_
#define ZIRCON_SYSTEM_UTEST_CORE_PAGER_TEST_HELPER_H_

#include "zxtest_to_unittest.h"
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

// fit::defer mapping
namespace fit {
template <typename T>
auto defer(T&& t) { return fbl::MakeAutoCall(fbl::forward<T>(t)); }
} // namespace fit

inline uint64_t zx_system_get_page_size() { return getpagesize(); }

// VMO_VMAR_TEST and TEST macros register automatically with 2018 unittest.
typedef bool (*pager_test_func_t)();
struct pager_test_info {
    const char* name;
    pager_test_func_t func;
    pager_test_info* next;
};

struct pager_test_registrar {
    static pager_test_info*& head() { static pager_test_info* h = nullptr; return h; }
    static pager_test_info*& tail() { static pager_test_info* t = nullptr; return t; }

    pager_test_registrar(pager_test_info* info) {
        if (!head()) {
            head() = tail() = info;
        } else {
            tail()->next = info;
            tail() = info;
        }
    }
};

// unittest's |current_test_info| is a static, and so is private to each
// translation unit (see the BEGIN_HELPER commentary in unittest.h). RUN_NAMED_TEST
// therefore only establishes it for the file that expands BEGIN_TEST_CASE, which
// is pager.cpp. A test body in any other source file would find it null and the
// ASSERT_*/EXPECT_* shims would abort with "Invalid state for EXPECT/ASSERT".
//
// Establish a context in the body's own translation unit when the test runner has
// not already provided one. Where the runner has (pager.cpp), its info is left
// alone, so reporting and crash registration there are unchanged.
#define PAGER_TEST_RUN_BODY(call) \
    struct test_info* pager_saved_info = current_test_info; \
    struct test_info pager_local_info = { true, nullptr }; \
    if (current_test_info == nullptr) { \
        current_test_info = &pager_local_info; \
    } \
    call; \
    bool pager_all_ok = current_test_info->all_ok; \
    current_test_info = pager_saved_info; \
    return pager_all_ok;

// The test body a porter writes after TEST()/VMO_VMAR_TEST() is void, matching
// upstream zxtest: it uses the void ASSERT_* shim (zxtest_to_unittest.h) and
// carries no BEGIN_TEST/END_TEST. A bool trampoline named as the registered
// entry point runs that void body and reports the result the 2018 unittest
// runner expects by returning current_test_info->all_ok, which the shimmed
// ASSERT_*/EXPECT_* have been accumulating.
#define TEST(suite, name) \
    void test_body_##suite##_##name(); \
    bool test_##suite##_##name() { \
        PAGER_TEST_RUN_BODY(test_body_##suite##_##name()) \
    } \
    static pager_test_info info_##suite##_##name = { #name, test_##suite##_##name, nullptr }; \
    static pager_test_registrar reg_##suite##_##name(&info_##suite##_##name); \
    void test_body_##suite##_##name()

#define VMO_VMAR_TEST(suite, name) \
    void name##_inner(bool check_vmar); \
    bool test_##suite##_##name##_Vmar() { PAGER_TEST_RUN_BODY(name##_inner(true)) } \
    bool test_##suite##_##name##_Vmo() { PAGER_TEST_RUN_BODY(name##_inner(false)) } \
    static pager_test_info info_##suite##_##name##_Vmar = { #name "_Vmar", test_##suite##_##name##_Vmar, nullptr }; \
    static pager_test_registrar reg_##suite##_##name##_Vmar(&info_##suite##_##name##_Vmar); \
    static pager_test_info info_##suite##_##name##_Vmo = { #name "_Vmo", test_##suite##_##name##_Vmo, nullptr }; \
    static pager_test_registrar reg_##suite##_##name##_Vmo(&info_##suite##_##name##_Vmo); \
    void name##_inner(bool check_vmar)

#endif  // ZIRCON_SYSTEM_UTEST_CORE_PAGER_TEST_HELPER_H_
