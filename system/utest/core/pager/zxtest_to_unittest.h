// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_PAGER_ZXTEST_TO_UNITTEST_H_
#define ZIRCON_SYSTEM_UTEST_CORE_PAGER_ZXTEST_TO_UNITTEST_H_

#include <unittest/unittest.h>

// Macro mappings for zxtest to unittest where unittest doesn't provide them
#define ASSERT_OK(x) ASSERT_EQ(ZX_OK, (x), #x)
#define EXPECT_OK(x) EXPECT_EQ(ZX_OK, (x), #x)

// [2018 shim] Framework shim: make the zxtest-style ASSERT_* macros
// void-returning so upstream (F7) test bodies compile verbatim, i.e. with no
// BEGIN_TEST/END_TEST wrapper and no bool return statement. zxtest's TEST()
// bodies are void and its ASSERT_* return void on failure, whereas the 2018
// unittest framework's ASSERT_* expand to `return false` (RET_FALSE) and thus
// require a bool-returning function bracketed by BEGIN_TEST/END_TEST.
//
// The whole of unittest's comparison and failure reporting (which sets
// current_test_info->all_ok = false and prints the diagnostic) is reused; only
// the fail-action is swapped from RET_FALSE to RET_VOID (a bare `return`). The
// bool pass/fail result the runner expects is recovered from all_ok by the
// TEST()/VMO_VMAR_TEST() trampoline in test_helper.h. EXPECT_* already use
// DONOT_RET (they never return), so they are void-body-safe unchanged.
#define RET_VOID                  \
    do {                          \
        unittest_returns_early(); \
        return;                   \
    } while (0)

#undef ASSERT_TRUE
#define ASSERT_TRUE(actual, ...) UT_TRUE(actual, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_FALSE
#define ASSERT_FALSE(actual, ...) UT_FALSE(actual, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_NULL
#define ASSERT_NULL(actual, ...) UT_NULL(actual, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_NONNULL
#define ASSERT_NONNULL(actual, ...) UT_NONNULL(actual, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_NOT_NULL
#define ASSERT_NOT_NULL(actual, ...) UT_NONNULL(actual, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_EQ
#define ASSERT_EQ(lhs, rhs, ...) UT_CMP(==, lhs, rhs, #lhs, #rhs, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_NE
#define ASSERT_NE(lhs, rhs, ...) UT_CMP(!=, lhs, rhs, #lhs, #rhs, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_GT
#define ASSERT_GT(lhs, rhs, ...) UT_CMP(>, lhs, rhs, #lhs, #rhs, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_LT
#define ASSERT_LT(lhs, rhs, ...) UT_CMP(<, lhs, rhs, #lhs, #rhs, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_GE
#define ASSERT_GE(lhs, rhs, ...) UT_CMP(>=, lhs, rhs, #lhs, #rhs, RET_VOID, ##__VA_ARGS__)
#undef ASSERT_LE
#define ASSERT_LE(lhs, rhs, ...) UT_CMP(<=, lhs, rhs, #lhs, #rhs, RET_VOID, ##__VA_ARGS__)

namespace zxtest {
class Test {
public:
    virtual ~Test() = default;
    virtual void SetUp() {}
    virtual void TearDown() {}
};
} // namespace zxtest

#endif  // ZIRCON_SYSTEM_UTEST_CORE_PAGER_ZXTEST_TO_UNITTEST_H_
