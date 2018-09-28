// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

// Test that a duplicate attribute is caught, and nicely reported.
bool no_two_same_attribute_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

[dup = "first", dup = "second"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "Duplicate attribute with name 'dup'");

    END_TEST;
}

// Test that doc comments and doc attributes clash are properly checked.
bool no_two_same_doc_attribute_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

/// first
[Doc = "second"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "Duplicate attribute with name 'Doc'");

    END_TEST;
}

// Test that TODO
bool no_two_same_attribute_on_library_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
[dup = "first"]
library fidl.test.dupattributes;

)FIDL");
    EXPECT_TRUE(library.Compile());

    EXPECT_FALSE(library.AddSourceFile("dup_attributes_second.fidl", R"FIDL(
[dup = "second"]
library fidl.test.dupattributes;

)FIDL"));
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "Duplicate attribute with name 'dup'");

    END_TEST;
}


} // namespace

BEGIN_TEST_CASE(dup_attributes_tests);
RUN_TEST(no_two_same_attribute_test);
RUN_TEST(no_two_same_doc_attribute_test);
RUN_TEST(no_two_same_attribute_on_library_test);
END_TEST_CASE(dup_attributes_tests);
