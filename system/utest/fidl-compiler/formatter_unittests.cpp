// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>
#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/formatter.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <fidl/tree_visitor.h>

#include "examples.h"
#include "test_library.h"

namespace {

std::map<std::string, std::string> formatted_output_;

void InitializeContents() {
    for (auto element : Examples::map()) {
        TestLibrary library(element.first, element.second);
        std::unique_ptr<fidl::raw::File> ast;
        library.Parse(ast);

        fidl::raw::FormattingTreeVisitor visitor;
        visitor.OnFile(ast);

        formatted_output_[element.first] = *visitor.formatted_output();
    }
}

// Tests that repeatedly applying the formatter results in no change.
bool idempotence_test() {
    BEGIN_TEST;

    for (auto element : formatted_output_) {
        TestLibrary library(element.first, element.second);
        std::unique_ptr<fidl::raw::File> ast;
        EXPECT_TRUE(library.Parse(ast));

        fidl::raw::FormattingTreeVisitor visitor;
        visitor.OnFile(ast);

        EXPECT_STR_EQ(element.second.c_str(),
                      visitor.formatted_output()->c_str(), "Applying formatting multiple times produces different results");
    }

    END_TEST;
}

bool basic_formatting_rules_test() {
    BEGIN_TEST;

    std::regex trailing_ws(".*\\s+$");
    std::regex top_level_decl("^\\s*(?:struct|enum|union)\\s+.*");
    std::regex attribute("\\s*\\[[A-Za-z]+\\]\\s*");
    std::regex comment("\\s*//.*");

    // Break the output into lines
    for (auto element : formatted_output_) {
        std::stringstream ss(element.second);
        std::string line;
        std::vector<std::string> lines;

        while (std::getline(ss, line, '\n')) {
            lines.push_back(line);

            // RULE: No trailing whitespace
            ASSERT_FALSE(std::regex_search(line, trailing_ws), "Trailing whitespace found");

            // RULE: No tab characters
            ASSERT_TRUE(line.find_first_of('\t') == std::string::npos, "Tab character found");

            // RULE: 4 space indents (at least)
            ASSERT_TRUE(!isspace(line[0]) || line.find_first_of("    ") == 0, "<4 space indent found");
        }

        // RULE: Separate top-level declarations for struct, enum, and union with one
        // blank line.
        for (int i = 0; i < lines.size(); i++) {
            if (std::regex_search(lines[i], top_level_decl)) {
                int line_to_check = i - 1;
                // Just means there is a top-level decl on the first line.
                if (line_to_check < 0) {
                    continue;
                }
                // Back up to before attributes and comments.
                while (std::regex_search(lines[line_to_check], attribute) ||
                       std::regex_search(lines[line_to_check], comment)) {
                    line_to_check--;
                    ASSERT_GE(line_to_check, 0);
                }

                std::string full = lines[i - 1] + lines[i];
                ASSERT_EQ(lines[line_to_check].size(), 0, "No blank line found before top level decl");
            }
        }

        // RULE: End the file with exactly one newline (no blank lines at the end).
        ASSERT_NE(lines[lines.size() - 1].size(), 0, "No newline at EOF");
    }

    END_TEST;
}

bool golden_file_test() {
    BEGIN_TEST;
    std::string good_output;
    std::string formatted_bad_output;

    for (auto element : Examples::map()) {
        if (element.first.find("testdata/goodformat.fidl") != std::string::npos) {
            good_output = Examples::map()[element.first];
        } else if (element.first.find("testdata/badformat.fidl") != std::string::npos) {
            formatted_bad_output = formatted_output_[element.first];
        }
    }

    ASSERT_GT(good_output.size(), 0);
    ASSERT_GT(formatted_bad_output.size(), 0);

    ASSERT_STR_EQ(good_output.c_str(), formatted_bad_output.c_str(), "Formatting for badformat.fidl looks weird");
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(formatter_tests);
InitializeContents();
RUN_TEST(idempotence_test);
RUN_TEST(basic_formatting_rules_test);
RUN_TEST(golden_file_test);
END_TEST_CASE(formatter_tests);
