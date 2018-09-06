// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/device/clk.h>

int usage(const char* cmd) {
    fprintf(
        stderr,
        "\nInteract with clocks on the SOC:\n"
        "   %s measure                    Measures all clock values\n"
        "   %s measure -idx <idx>         Measure CLK idx\n"
        "   %s help                       Print this message\n",
        cmd,
        cmd,
        cmd);
    return -1;
}

// Returns "true" if the argument matches the prefix.
// In this case, moves the argument past the prefix.
bool prefix_match(const char** arg, const char* prefix) {
    if (!strncmp(*arg, prefix, strlen(prefix))) {
        *arg += strlen(prefix);
        return true;
    }
    return false;
}

// Gets the value of a particular field passed through
// command line.
const char* getValue(int argc, char** argv, const char* field) {
    int i = 1;
    while (i < argc - 1 && strcmp(argv[i], field) != 0) {
        ++i;
    }
    if (i >= argc - 1) {
        printf("NULL\n");
        return NULL;
    } else {
        return argv[i + 1];
    }
}

char* guess_dev(void) {
    char path[21]; // strlen("/dev/class/clock/###") + 1
    DIR* d = opendir("/dev/class/clock");
    if (!d) {
        return NULL;
    }

    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (strlen(de->d_name) != 3) {
            continue;
        }

        if (isdigit(de->d_name[0]) &&
            isdigit(de->d_name[1]) &&
            isdigit(de->d_name[2])) {
            sprintf(path, "/dev/class/clock/%.3s", de->d_name);
            closedir(d);
            return strdup(path);
        }
    }

    closedir(d);
    return NULL;
}

int measure_clk_util(int fd, uint32_t idx) {
    clk_freq_info_t info;
    ssize_t rc = ioctl_clk_measure(fd, &idx, &info);
    if (rc < 0) {
        fprintf(stderr, "ERROR: Failed to measure clock: %zd\n", rc);
        return rc;
    }
    printf("[%4d][%4d MHz] %s\n", idx, info.clk_freq, info.clk_name);
    return 0;
}

int measure_clk(const char* path, uint32_t idx, bool clk) {
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to open clock device: %d\n", fd);
        return -1;
    }

    uint32_t num_clocks = 0;
    ssize_t rc = ioctl_clk_get_count(fd, &num_clocks);
    if (rc < 0) {
        fprintf(stderr, "ERROR: Failed to get num_clocks: %zd\n", rc);
        return rc;
    }

    if (clk) {
        if (idx > num_clocks) {
            fprintf(stderr, "ERROR: Invalid clock index.\n");
            return -1;
        }
        return measure_clk_util(fd, idx);
    } else {
        for (uint32_t i = 0; i < num_clocks; i++) {
            rc = measure_clk_util(fd, i);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    int err = 0;
    const char* cmd = basename(argv[0]);
    char* path = NULL;
    const char* index = NULL;
    bool measure = false;
    bool clk = false;
    uint32_t idx = 0;

    // If no arguments passed, bail out after dumping
    // usage information.
    if (argc == 1) {
        return usage(cmd);
    }

    // Parse all args.
    while (argc > 1) {
        const char* arg = argv[1];
        if (prefix_match(&arg, "measure")) {
            measure = true;
        }
        if (prefix_match(&arg, "-idx")) {
            index = getValue(argc, argv, "-idx");
            clk = true;
            if (index) {
                idx = atoi(index);
            } else {
                fprintf(stderr, "Enter Valid CLK IDX.\n");
            }
        }
        if (prefix_match(&arg, "help")) {
            return usage(cmd);
        }
        argc--;
        argv++;
    }

    // Get the device path.
    path = guess_dev();
    if (!path) {
        fprintf(stderr, "No CLK device found.\n");
        return usage(cmd);
    }

    // Measure the clocks.
    if (measure) {
        err = measure_clk(path, idx, clk);
        if (err) {
            printf("Measure CLK failed.\n");
        }
    }
    return 0;
}
