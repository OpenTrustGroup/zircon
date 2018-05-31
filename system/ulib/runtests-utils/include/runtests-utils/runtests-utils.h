// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper functions for running test binaries and recording their results.

#pragma once

#include <inttypes.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/vector.h>
#include <lib/async-loop/cpp/loop.h>

namespace runtests {

// Status of launching a test subprocess.
enum LaunchStatus {
    SUCCESS,
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
};

// Represents the result of a single test run.
struct Result {
    fbl::String name; // argv[0].
    LaunchStatus launch_status;
    int64_t return_code; // Only valid if launch_status == SUCCESS or FAILED_NONZERO_RETURN_CODE.
    // TODO(ZX-2050): Track duration of test binary.

    // Constructor really only needed until we have C++14, which will allow call-sites to use
    // aggregate initializer syntax.
    Result(const char* name_arg, LaunchStatus launch_status_arg, int64_t return_code_arg)
        : name(name_arg), launch_status(launch_status_arg), return_code(return_code_arg) {}
};

// Function that invokes a test binary and writes its output to a file.
//
// |argv| is the commandline to use to run the test program.
// |argc| is the number of strings in argv.
// |out| is a file stream to which the test binary's output will be written. May be
//   nullptr, in which output will not be redirected.
//
using RunTestFn = fbl::Function<Result(const char* argv[], int argc, FILE* out)>;

// Splits |input| by ',' and appends the results onto |output|.
// Empty strings are not put into output.
void ParseTestNames(fbl::StringPiece input, fbl::Vector<fbl::String>* output);

// Returns true iff |name| is equal to one of strings in |whitelist|.
bool IsInWhitelist(fbl::StringPiece name, const fbl::Vector<fbl::String>& whitelist);

// Ensures |dir_name| exists by creating it and its parents if it doesn't.
// Returns 0 on success, else an error code compatible with errno.
int MkDirAll(fbl::StringPiece dir_name);

// Returns "|parent|/|child|". Unless child is absolute, in which case it returns |child|.
//
// |parent| is the parent path.
// |child| is the child path.
fbl::String JoinPath(fbl::StringPiece parent, fbl::StringPiece child);

// Writes a JSON summary of test results given a sequence of results.
//
// |results| are the run results to summarize.
// |output_file_basename| is base name of output file.
// |syslog_path| is the file path where syslogs are written.
// |summary_json| is the file stream to write the JSON summary to.
//
// Returns 0 on success, else an error code compatible with errno.
int WriteSummaryJSON(const fbl::Vector<Result>& results,
                     const fbl::StringPiece output_file_basename,
                     const fbl::StringPiece syslog_path,
                     FILE* summary_json);

// Resolves a set of globs.
//
// |globs| is an array of glob patterns.
// |num_globs| is the number of strings in |globs|.
// |resolved| will hold the results of resolving |globs|.
//
// Returns 0 on success, else an error code from glob.h.
int ResolveGlobs(const char* const* globs, int num_globs, fbl::Vector<fbl::String>* resolved);

// Executes all test binaries in a directory (non-recursive).
//
// |run_test| is the function used to invoke the test binaries.
// |dir_path| is the directory to search.
// |filter_names| is a list of test names to filter on (i.e. tests whose names
//   don't match are skipped). May be empty, in which case all tests will be run.
// |output_dir| is the output directory for all the tests' output. May be nullptr, in which case
//   output will not be captured.
// |output_file_basename| is the basename of the tests' output files. May be nullptr only if
//   |output_dir| is also nullptr.
//   Each test's standard output and standard error will be written to
//   |output_dir|/<test binary path>/|output_file_basename|.
// |verbosity| if > 0 is converted to a string and passed as an additional argument to the
//   tests, so argv = {test_path, "v=<verbosity>"}. Also if > 0, this function prints more output
//   to stdout than otherwise.
// |num_failed| is an output parameter which will be set to the number of test
//   binaries that failed.
// |results| is an output paramater to which run results will be appended.
//
// Returns false if any test binary failed, true otherwise.
bool RunTestsInDir(const RunTestFn& run_test, const fbl::StringPiece dir_path,
                   const fbl::Vector<fbl::String>& filter_names, const char* output_dir,
                   const char* output_file_basename, signed char verbosity,
                   int* num_failed, fbl::Vector<Result>* results);

} // namespace runtests
