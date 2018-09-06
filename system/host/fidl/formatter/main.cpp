// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fidl/formatter.h>
#include <fidl/identifier_table.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>

namespace {

void Usage(const std::string& argv0) {
    std::cout
        << "usage: " << argv0 << " <options> <files>\n"
                                 "\n"
                                 " * `-i, --in-place` Formats file in place\n"
                                 "\n"
                                 " * `-h, --help`. Prints this help, and exit immediately.\n"
                                 "\n";
    std::cout.flush();
}

[[noreturn]] void FailWithUsage(const std::string& argv0, const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    Usage(argv0);
    exit(1);
}

[[noreturn]] void Fail(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    exit(1);
}

bool Format(const fidl::SourceFile& source_file, fidl::IdentifierTable* identifier_table,
            fidl::ErrorReporter* error_reporter, std::string& output) {
    fidl::Lexer lexer(source_file, identifier_table);
    fidl::Parser parser(&lexer, error_reporter);
    std::unique_ptr<fidl::raw::File> ast = parser.Parse();
    if (!parser.Ok()) {
        return false;
    }
    fidl::raw::FormattingTreeVisitor visitor;
    visitor.OnFile(ast);
    output = *visitor.formatted_output();
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argc);
    for (int i = 0; i < argc; i++) {
        args[i] = argv[i];
    }

    bool in_place = false;
    int pos = 1;
    // Process options
    while (pos < args.size() && args[pos] != "--" && args[pos].find("-") == 0) {
        if (args[pos] == "-i" || args[pos] == "--in-place") {
            in_place = true;
        } else if (args[pos] == "-h" || args[pos] == "--help") {
            Usage(args[0]);
            exit(0);
        } else {
            FailWithUsage(args[0], "Unknown argument: %s\n", args[pos].c_str());
        }
        pos++;
    }

    if (pos >= args.size()) {
        // TODO: Should probably read a file from stdin, instead.
        FailWithUsage(args[0], "No files provided");
    }

    fidl::SourceManager source_manager;

    // Process filenames
    for (; pos < args.size(); pos++) {
        std::string arg = args[pos];
        if (!source_manager.CreateSource(arg.data())) {
            Fail("Couldn't read in source data from %s\n", arg.data());
        }
    }

    fidl::IdentifierTable identifier_table;
    fidl::ErrorReporter error_reporter;
    for (const auto& source_file : source_manager.sources()) {
        std::string output;
        if (!Format(*source_file, &identifier_table, &error_reporter, output)) {
            error_reporter.PrintReports();
            return 1;
        }
        FILE* out_file;
        if (in_place) {
            const char* filename = source_file->filename().data();
            out_file = fopen(filename, "w+");
            if (out_file == nullptr) {
                std::string error = "Fail: cannot open file: ";
                error.append(filename);
                error.append(":\n");
                error.append(strerror(errno));
                Fail(error.c_str());
            }
        } else {
            out_file = stdout;
        }
        fprintf(out_file, "%s", output.c_str());
    }
}
