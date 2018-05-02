// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

// The name of the file containing stdout and stderr of each test.
static const char kOutputFileName[] = "stdout-and-stderr.txt";

typedef enum {
    SUCCESS,
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
} test_result_t;

// Represents a single test result that can be appended to a linked list.
typedef struct test {
    list_node_t node;
    test_result_t result;
    int rc; // Return code.
    // TODO(ZX-2050): Track duration of test binary.
    char name[0];
} test_t;

static zx::time now(void) {
    return zx::clock::get(ZX_CLOCK_MONOTONIC);
}

// Creates a new test_t and appends it to the linked list |tests|.
//
// |tests| is a linked list to which a new test_result_t will be appended.
// |name| is the name of the test.
// |result| is the result of trying to execute the test.
// |rc| is the return code of the test.
static void record_test_result(list_node_t* tests, const char* name, test_result_t result, int rc) {
    size_t name_len = strlen(name) + 1;
    test_t* test = static_cast<test_t*>(malloc(sizeof(test_t) + name_len));
    test->result = result;
    test->rc = rc;
    memcpy(test->name, name, name_len);
    list_add_tail(tests, &test->node);
}

// Represents the aggregate of all test results.
static list_node_t tests = LIST_INITIAL_VALUE(tests);

// We want the default to be the same, whether the test is run by us
// or run standalone. Do this by leaving the verbosity unspecified unless
// provided by the user.
static signed char verbosity = -1;

// The watchdog timeout, in seconds, or -1 if unset (-> use default).
static int watchdog_timeout_seconds = -1;

static const char* default_test_dirs[] = {
    // zircon builds place everything in ramdisks so tests are located in /boot
    "/boot/test/core", "/boot/test/libc", "/boot/test/ddk", "/boot/test/sys",
    "/boot/test/fs",
    // layers above garnet use fs images rather than ramdisks and place tests in /system
    "/system/test/core", "/system/test/libc", "/system/test/ddk", "/system/test/sys",
    "/system/test/fs",
};
#define DEFAULT_NUM_TEST_DIRS (sizeof(default_test_dirs)/sizeof(default_test_dirs[0]))

static bool parse_test_names(char* input, char*** output, int* output_len) {
    // Count number of names via delimiter ','.
    int num_test_names = 0;
    for (char* tmp = input; tmp != nullptr; tmp = strchr(tmp, ',')) {
        num_test_names++;
        tmp++;
    }

    // Allocate space for names.
    char** test_names = (char**) malloc(sizeof(char*) * num_test_names);
    if (test_names == nullptr) {
        return false;
    }

    // Tokenize the input string into names.
    char *next_token;
    test_names[0] = strtok_r(input, ",", &next_token);
    for (int i = 1; i < num_test_names; i++) {
        char* tmp = strtok_r(nullptr, ",", &next_token);
        if (tmp == nullptr) {
            free(test_names);
            return false;
        }
        test_names[i] = tmp;
    }
    *output = test_names;
    *output_len = num_test_names;
    return true;
}

static bool match_test_names(const char* dirent_name, const char** filter_names,
                             const int num_filter_names) {
    // Always match when there are no test names to filter by.
    if (num_filter_names <= 0) {
        return true;
    }
    for (int i = 0; i < num_filter_names; i++) {
        if (!strncmp(filter_names[i], dirent_name, NAME_MAX)) {
            return true;
        }
    }
    return false;
}

// Ensures a directory exists by creating it and its parents if it doesn't.
static int mkdir_all(const char* dirn) {
    char dir[PATH_MAX];
    size_t bytes_to_copy = strlcpy(dir, dirn, sizeof(dir));
    if (bytes_to_copy >= sizeof(dir)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Fast path: check if the directory already exists.
    struct stat s;
    if (!stat(dir, &s)) {
        return 0;
    }

    // Slow path: create the directory and its parents.
    for (size_t slash = 0u; dir[slash]; slash++) {
        if (slash != 0u && dir[slash] == '/') {
            dir[slash] = '\0';
            if (mkdir(dir, 0755) && errno != EEXIST) {
                return -1;
            }
            dir[slash] = '/';
        }
    }
    if (mkdir(dir, 0755) && errno != EEXIST) {
        return -1;
    }
    return 0;
}

// Invokes a test binary and prints results.
//
// |path| specifies the path to the binary.
// |out| is a file stream to which the test binary's output will be written. May be
// nullptr.
//
// Returns true if the test binary successfully executes and has a return code of zero.
static bool run_test(const char* path, FILE* out) {
    int fds[2];
    // This arithmetic is invalid if verbosity < 0, but in that case by setting argc = 1.
    char verbose_opt[] = {'v','=', static_cast<char>(verbosity + '0'), 0};
    const char* argv[] = {path, verbose_opt};
    int argc = verbosity >= 0 ? 2 : 1;

    launchpad_t* lp = nullptr;
    zx_status_t status = ZX_OK;
    zx_handle_t test_job = ZX_HANDLE_INVALID;
    status = zx_job_create(zx_job_default(), 0, &test_job);
    if (status != ZX_OK) {
      printf("FAILURE: zx_job_create() returned %d\n", status);
      return false;
    }
    status = zx_object_set_property(test_job, ZX_PROP_NAME, "run-test", 8);
    if (status != ZX_OK) {
      printf("FAILURE: zx_object_set_property() returned %d\n", status);
      goto fail;
    }
    status = launchpad_create(test_job, path, &lp);
    if (status != ZX_OK) {
      printf("FAILURE: launchpad_create() returned %d\n", status);
      goto fail;
    }
    status = launchpad_load_from_file(lp, argv[0]);
    if (status != ZX_OK) {
      printf("FAILURE: launchpad_load_from_file() returned %d\n", status);
      goto fail;
    }
    status = launchpad_clone(lp, LP_CLONE_FDIO_ALL | LP_CLONE_ENVIRON);
    if (status != ZX_OK) {
      printf("FAILURE: launchpad_clone() returned %d\n", status);
      goto fail;
    }
    if (out != nullptr) {
        if (pipe(fds)) {
          printf("FAILURE: Failed to create pipe: %s\n", strerror(errno));
          goto fail;
        }
        status = launchpad_clone_fd(lp, fds[1], STDOUT_FILENO);
        if (status != ZX_OK) {
          printf("FAILURE: launchpad_clone_fd() returned %d\n", status);
          goto fail;
        }
        status = launchpad_transfer_fd(lp, fds[1], STDERR_FILENO);
        if (status != ZX_OK) {
          printf("FAILURE: launchpad_transfer_fd() returned %d\n", status);
          goto fail;
        }
    }
    launchpad_set_args(lp, argc, argv);
    const char* errmsg;
    zx_handle_t handle;
    status = launchpad_go(lp, &handle, &errmsg);
    lp = nullptr;
    if (status != ZX_OK) {
        printf("FAILURE: Failed to launch %s: %d: %s\n", path, status, errmsg);
        record_test_result(&tests, path, FAILED_TO_LAUNCH, 0);
        goto fail;
    }
    // Tee output.
    if (out != nullptr) {
        char buf[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(fds[0], buf, 1024)) > 0) {
            fwrite(buf, 1, bytes_read, out);
            fwrite(buf, 1, bytes_read, stdout);
        }
    }
    status = zx_object_wait_one(handle, ZX_PROCESS_TERMINATED,
                                ZX_TIME_INFINITE, nullptr);
    if (status != ZX_OK) {
        printf("FAILURE: Failed to wait for process exiting %s: %d\n", path, status);
        record_test_result(&tests, path, FAILED_TO_WAIT, 0);
        goto fail;
    }

    // read the return code
    zx_info_process_t proc_info;
    status = zx_object_get_info(handle, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
    zx_handle_close(handle);

    if (status < 0) {
        printf("FAILURE: Failed to get process return code %s: %d\n", path, status);
        record_test_result(&tests, path, FAILED_TO_RETURN_CODE, 0);
        goto fail;
    }

    if (proc_info.return_code != 0) {
        printf("FAILURE: %s exited with nonzero status: %d\n", path, proc_info.return_code);
        record_test_result(&tests, path, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
        goto fail;
    }

    zx_task_kill(test_job);
    zx_handle_close(test_job);
    printf("PASSED: %s passed\n", path);
    record_test_result(&tests, path, SUCCESS, 0);
    return true;
fail:
    if (lp) {
      launchpad_destroy(lp);
    }
    zx_task_kill(test_job);
    zx_handle_close(test_job);
    return false;
}

// Sets *out to "parent/child".
//
// |parent| is the parent path.
// |child| is the child path.
// |out| is the destination for the joined path.
// |out_len| is the amount of space available at that location.
//
// Returns non-zero on failure with errno set.
static int join_path(const char* parent, const char* child, char* out, const size_t out_len) {
    size_t path_len = snprintf(out, out_len, "%s/%s", parent, child);
    if (path_len >= out_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

// Opens "parent/child" for writing.
//
// |parent| is the parent path.
// |child| is the child path.
//
// Returns nullptr on failure, with errno set.
static FILE* join_and_open(const char* parent, const char* child) {
    char output_path[PATH_MAX];
    if (join_path(parent, child, output_path, sizeof(output_path))) {
        return nullptr;
    }
    return fopen(output_path, "w");
}

// Executes all test binaries in a directory (non-recursive).
//
// |dirn| is the directory to search.
// |filter_names| is a list of test names to filter on (i.e. tests whose names
// don't match are skipped). May be nullptr.
// |num_filter_names| is the length of |filter_names|.
// |output_dir| is the output directory for test output, passed in via -o.
// |num_tests| is an output parameter which will be set to the number of test
// binaries executed.
// |num_failed| is an output parameter which will be set to the number of test
// binaries that failed.
//
// Returns false if any test binary failed, true otherwise.
static bool run_tests_in_dir(const char* dirn, const char** filter_names, const int num_filter_names,
                             const char* output_dir, int* num_tests, int* num_failed) {
    DIR* dir = opendir(dirn);
    if (dir == nullptr) {
        return false;
    }

    struct dirent* de;
    struct stat stat_buf;
    int test_count = 0;
    int failed_count = 0;

    // Iterate over the files in dir, setting up the output for test binaries
    // and executing them via run_test as they're found. Skips over test binaries
    // whose names aren't in filter_names.
    //
    // TODO(mknyszek): Iterate over these dirents (or just discovered test binaries)
    // in a deterministic order.
    while ((de = readdir(dir)) != nullptr) {
        const char* test_name = de->d_name;
        if (!match_test_names(test_name, filter_names, num_filter_names)) {
            continue;
        }

        char test_path[PATH_MAX];
        snprintf(test_path, sizeof(test_path), "%s/%s", dirn, test_name);
        if (stat(test_path, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }

        if (verbosity) {
            printf(
                "\n------------------------------------------------\n"
                "RUNNING TEST: %s\n\n",
                test_name);
        }

        // If output_dir was specified, ask run_test to redirect stdout/stderr
        // to a file whose name is based on the test name.
        FILE* out = nullptr;
        if (output_dir != nullptr) {
            char test_output_dir[PATH_MAX];
            if (join_path(output_dir, test_path, test_output_dir, sizeof(test_output_dir))) {
              printf("Error: Could not construct output dir for test %s: %s\n", test_name,
                     strerror(errno));
              return false;
            }
            if (mkdir_all(test_output_dir)) {
                printf("Error: Could not output directory for test %s: %s\n", test_name,
                       strerror(errno));
                return false;
            }
            out = join_and_open(test_output_dir, kOutputFileName);
            if (out == nullptr) {
                printf("Error: Could not open output file for test %s: %s\n", test_name,
                       strerror(errno));
                return false;
            }
        }

        // Execute the test binary.
        if (!run_test(test_path, out)) {
            failed_count++;
        }

        // Clean up the output file.
        if (out != nullptr && fclose(out)) {
            printf("FAILURE: Failed to close output file for test %s: %s\n", de->d_name,
                   strerror(errno));
            continue;
        }

        test_count++;
    }

    closedir(dir);
    *num_failed = failed_count;
    *num_tests = test_count;
    return failed_count == 0;
}

// Writes a JSON summary of test results given a linked-list of tests.
//
// |tests| is a linked-list of test results.
// |summary_json| is the file stream to write the JSON summary to.
//
// Returns non-zero on failure, with errno set.
static int write_summary_json(const list_node_t* tests, FILE* summary_json) {
    int test_count = 0;
    test_t* test = nullptr;
    test_t* temp = nullptr;
    fprintf(summary_json, "{\"tests\":[\n");
    list_for_every_entry_safe (tests, test, temp, test_t, node) {
        if (test_count != 0) {
            fprintf(summary_json, ",\n");
        }
        fprintf(summary_json, "{");

        // Write the name of the test.
        fprintf(summary_json, "\"name\":\"%s\"", test->name);

        // Write the path to the output file, relative to the test output root
        // (i.e. what's passed in via -o). The test name is already a path to
        // the test binary on the target, so to make this a relative path, we
        // only have to skip leading '/' characters in the test name.
        char buf[PATH_MAX];
        if (join_path(test->name, kOutputFileName, buf, sizeof(buf))) {
            return -1;
        }
        char *output_file = buf;
        for (; *output_file == '/'; output_file++);
        fprintf(summary_json, ",\"output_file\":\"%s\"", output_file);

        // Write the result of the test, which is either PASS or FAIL. We only
        // have one PASS condition in test_result_t, which is SUCCESS.
        fprintf(summary_json, ",\"result\":\"%s\"", test->result == SUCCESS ? "PASS" : "FAIL");

        fprintf(summary_json, "}");
        test_count++;
    }
    fprintf(summary_json, "\n]}\n");
    return 0;
}

// Resolves a set of globs into the same glob_t.
//
// |globs| is an array of glob patterns.
// |num_globs| is the number of glob patterns in |globs|.
// |resolved| is the output glob_t.
//
// Returns a glob error (see glob.h), but will never return GLOB_NOMATCH. Note
// also that GLOB_ABORTED will never be returned, because we use the GLOB_ERR
// flag and thus the only error returned is the fatal GLOB_NOSPACE.
static int resolve_test_globs(const char** globs, const int num_globs, glob_t* resolved) {
    // Zero out the number of paths found because in the event of a single path
    // and GLOB_NOMATCH, it may not happen, and we don't return GLOB_NOMATCH.
    resolved->gl_pathc = 0;
    for (int i = 0; i < num_globs; i++) {
        int err = glob(globs[i], i>0 ? GLOB_APPEND : 0, nullptr, resolved);

        // Ignore a lack of matches.
        if (err && err != GLOB_NOMATCH) {
            return err;
        }
    }
    return 0;
}

int usage(char* name) {
    fprintf(stderr,
            "usage: %s [-q|-v] [-S|-s] [-M|-m] [-L|-l] [-P|-p] [-a]\n"
            "    [-w timeout] [-t test names] [-o directory]       \n"
            "    [directory globs ...]                             \n"
            "\n"
            "The optional [directory globs...] is a list of        \n"
            "globs which match directories containing tests to run,\n"
            "non-recursively. Note that non-directories captured by\n"
            "a glob will be silently ignored. If not specified, the\n"
            "default set of directories is:                        \n", name);
    for (size_t i = 0; i < DEFAULT_NUM_TEST_DIRS; i++) {
        fprintf(stderr, "   %s", default_test_dirs[i]);
        if (i < DEFAULT_NUM_TEST_DIRS - 1) {
            fprintf(stderr, ",\n");
        } else {
            fprintf(stderr, "\n\n");
        }

    }
    fprintf(stderr,
            "options:                                              \n"
            "   -h: See this message                               \n"
            "   -v: Verbose output                                 \n"
            "   -q: Quiet output                                   \n"
            "   -S: Turn ON  Small tests         (on by default)   \n"
            "   -s: Turn OFF Small tests                           \n"
            "   -M: Turn ON  Medium tests        (on by default)   \n"
            "   -m: Turn OFF Medium tests                          \n"
            "   -L: Turn ON  Large tests         (off by default)  \n"
            "   -l: Turn OFF Large tests                           \n"
            "   -P: Turn ON Performance tests    (off by default)  \n"
            "   -p: Turn OFF Performance tests                     \n"
            "   -a: Turn on All tests                              \n"
            "   -t: Filter tests by name                           \n"
            "       (accepts a comma-separated list)               \n"
            "   -o: Write test output to a directory               \n"
            "   -w: Watchdog timeout                               \n"
            "       (accepts the timeout value in seconds)         \n"
            "       The default is up to each test.                \n"
            "\n"
            "If -o is enabled, then a JSON summary of the test     \n"
            "results will be written to a file named 'summary.json'\n"
            "under the desired directory, in addition to each      \n"
            "test's standard output and error.                     \n"
            "The summary contains a listing of the tests executed  \n"
            "by full path (e.g. /boot/test/core/futex_test) as well\n"
            "as whether the test passed or failed. For details, see\n"
            "//system/uapp/runtests/summary-schema.json            \n"
            "\n"
            "The test selection options -[sSmMlLpP] only work for  \n"
            "tests that support the RUNTESTS_TEST_CLASS environment\n"
            "variable.                                             \n"
            "The watchdog timeout option -w only works for tests   \n"
            "that support the RUNTESTS_WATCHDOG_TIMEOUT environment\n"
            "variable.                                             \n"
            );
    return -1;
}

int main(int argc, char** argv) {
    unsigned int test_types = TEST_DEFAULT;
    int num_filter_names = 0;
    const char** filter_names = nullptr;
    int num_test_globs = 0;
    const char** test_globs = nullptr;
    const char* output_dir = nullptr;

    zx::time start_time = now();

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-q") == 0) {
            verbosity = 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            printf("verbose output. enjoy.\n");
            verbosity = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            test_types &= ~TEST_SMALL;
        } else if (strcmp(argv[i], "-m") == 0) {
            test_types &= ~TEST_MEDIUM;
        } else if (strcmp(argv[i], "-l") == 0) {
            test_types &= ~TEST_LARGE;
        } else if (strcmp(argv[i], "-p") == 0) {
            test_types &= ~TEST_PERFORMANCE;
        } else if (strcmp(argv[i], "-S") == 0) {
            test_types |= TEST_SMALL;
        } else if (strcmp(argv[i], "-M") == 0) {
            test_types |= TEST_MEDIUM;
        } else if (strcmp(argv[i], "-L") == 0) {
            test_types |= TEST_LARGE;
        } else if (strcmp(argv[i], "-P") == 0) {
            test_types |= TEST_PERFORMANCE;
        } else if (strcmp(argv[i], "-a") == 0) {
            test_types |= TEST_ALL;
        } else if (strcmp(argv[i], "-h") == 0) {
            return usage(argv[0]);
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                return usage(argv[0]);
            } else if (!parse_test_names(argv[i + 1], (char***)&filter_names,
                                         &num_filter_names)) {
                printf("Error: Could not parse test names\n");
                return -1;
            }
            i++;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                return usage(argv[0]);
            }
            output_dir = (const char*)argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-w") == 0) {
            if (i + 1 >= argc) {
                return usage(argv[0]);
            }
            char* timeout_str = argv[++i];
            char* end;
            long timeout = strtol(timeout_str, &end, 0);
            if (*timeout_str == '\0' || *end != '\0' ||
                timeout < 0 || timeout > INT_MAX) {
                fprintf(stderr, "Error: bad timeout\n");
                return 1;
            }
            watchdog_timeout_seconds = (int) timeout;
            i++;
        } else if (argv[i][0] != '-') {
            // Treat the rest of the argv array as a list of directory globs.
            num_test_globs = argc - i;
            test_globs = (const char**)&argv[i];
            break;
        } else {
            return usage(argv[0]);
        }
        i++;
    }

    // Configure the types of tests which are meant to be executed by putting
    // it in an environment variable. Test executables can consume this environment
    // variable and process it as they would like.
    char test_opt[32];
    snprintf(test_opt, sizeof(test_opt), "%u", test_types);
    if (setenv(TEST_ENV_NAME, test_opt, 1) != 0) {
        printf("Error: Could not set %s environment variable\n", TEST_ENV_NAME);
        return -1;
    }

    // If set, configure the watchdog timeout to use.
    if (watchdog_timeout_seconds >= 0) {
        char timeout_str[32];
        snprintf(timeout_str, sizeof(timeout_str), "%d", watchdog_timeout_seconds);
        if (setenv(WATCHDOG_ENV_NAME, timeout_str, 1) != 0) {
            printf("Error: Could not set %s environment variable\n", WATCHDOG_ENV_NAME);
            return -1;
        }
    } else {
        // Ensure we don't pass on any existing value. This is intentional:
        // If -w is not specified then that means the watchdog is unspecified,
        // period.
        unsetenv(WATCHDOG_ENV_NAME);
    }

    // If we got no test globs, just set it to the default test dirs so we can
    // use glob patterns there too.
    if (test_globs == nullptr) {
        num_test_globs = DEFAULT_NUM_TEST_DIRS;
        test_globs = default_test_dirs;
    }

    // Takes test_globs and resolves them, putting the result in test_dirs, which
    // is used by the rest of the code. Note that by this point test_globs will
    // not be nullptr.
    glob_t resolved_globs;
    if (resolve_test_globs(test_globs, num_test_globs, &resolved_globs)) {
        printf("Error: Failed to resolve globs\n");
        return -1;
    }
    // TODO(mknyszek): Sort test_dirs in order to make running tests more
    // deterministic.
    size_t num_test_dirs = resolved_globs.gl_pathc;
    const char** test_dirs = (const char**)resolved_globs.gl_pathv;

    struct stat st;
    if (output_dir != nullptr && stat(output_dir, &st) < 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
        printf("Error: Could not open %s\n", output_dir);
        return -1;
    }

    int failed_count = 0;
    int total_count = 0;
    for (size_t i = 0; i < num_test_dirs; i++) {
        // In the event of failures around a directory not existing or being an empty node
        // we will continue to the next entries rather than aborting. This allows us to handle
        // different sets of default test directories.
        if (stat(test_dirs[i], &st) < 0) {
            printf("Could not open %s, skipping...\n", test_dirs[i]);
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            // Silently skip non-directories, as they may have been picked up in
            // the glob.
            continue;
        }

        // Resolve an absolute path to the test directory to ensure output
        // directory names will never collide.
        char abs_test_dir[PATH_MAX];
        if (realpath(test_dirs[i], abs_test_dir) == nullptr) {
            printf("Error: Could not resolve path %s: %s\n", test_dirs[i], strerror(errno));
            continue;
        }

        // Ensure the output directory for this test binary's output exists.
        if (output_dir != nullptr) {
            char buf[PATH_MAX];
            size_t path_len = snprintf(buf, sizeof(buf), "%s/%s", output_dir, abs_test_dir);
            if (path_len >= sizeof(buf)) {
                printf("Error: Output path is too long: %s/%s\n", output_dir, abs_test_dir);
                return -1;
            }
            if (mkdir_all(buf)) {
                printf("Error: Could not create output directory %s: %s\n", buf,
                       strerror(errno));
                return -1;
            }
        }

        int num_tests = 0;
        int num_failed = 0;
        run_tests_in_dir(test_dirs[i], filter_names, num_filter_names,
                         output_dir, &num_tests, &num_failed);
        total_count += num_tests;
        failed_count += num_failed;
    }
    free(filter_names);

    // It's not catastrophic if we can't unset it; we're just trying to clean up
    unsetenv(TEST_ENV_NAME);
    unsetenv(WATCHDOG_ENV_NAME);

    if (output_dir != nullptr) {
        char summary_path[PATH_MAX];
        snprintf(summary_path, sizeof(summary_path), "%s/summary.json", output_dir);
        FILE* summary_json = fopen(summary_path, "w");
        if (summary_json == nullptr) {
            printf("Error: Could not open JSON summary file.\n");
            return -1;
        }
        if (write_summary_json(&tests, summary_json)) {
            printf("Error: Failed to write JSON summary.\n");
            return -1;
        }
        if (fclose(summary_json)) {
            printf("Error: Could not close JSON summary.\n");
            return -1;
        }

        // Sync output filesystem.
        int fd = open(output_dir, O_RDONLY);
        if (fd < 0) {
            printf("Warning: Could not open %s for syncing", output_dir);
        } else if (syncfs(fd)) {
            printf("Warning: Could not sync parent filesystem of %s", output_dir);
        } else {
            close(fd);
        }
    }

    // Display any failed tests, and free the test results.
    if (failed_count) {
        printf("\nThe following tests failed:\n");
    }
    test_t* test = nullptr;
    test_t* temp = nullptr;
    list_for_every_entry_safe (&tests, test, temp, test_t, node) {
        switch (test->result) {
        case SUCCESS:
            break;
        case FAILED_TO_LAUNCH:
            printf("%s: failed to launch\n", test->name);
            break;
        case FAILED_TO_WAIT:
            printf("%s: failed to wait\n", test->name);
            break;
        case FAILED_TO_RETURN_CODE:
            printf("%s: failed to return exit code\n", test->name);
            break;
        case FAILED_NONZERO_RETURN_CODE:
            printf("%s: returned nonzero: %d\n", test->name, test->rc);
            break;
        default:
            printf("%s: unknown result\n", test->name);
            break;
        }
        free(test);
    }

    // Free the glob structure.
    if (test_globs != nullptr) {
        globfree(&resolved_globs);
    }

    // TODO(ZX-2051): Include total duration in summary.json.
    zx::time end_time = now();
    uint64_t time_taken_ms = (end_time - start_time).to_msecs();

    // Print this last, since some infra recipes will shut down the fuchsia
    // environment once it appears.
    printf("\nSUMMARY: Ran %d tests: %d failed (%" PRIu64 ".%03u sec)\n",
           total_count, failed_count,
           time_taken_ms / 1000, (unsigned) (time_taken_ms % 1000));

    return failed_count ? 1 : 0;
}
