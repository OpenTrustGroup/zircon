// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pretty/sizes.h>
#include <task-utils/walker.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum sort_order {
    UNSORTED,
    SORT_TIME_DELTA
};

typedef struct {
    struct list_node node;

    // has it been seen this pass?
    bool scanned;
    zx_duration_t delta_time;

    // information about the thread
    zx_koid_t proc_koid;
    zx_koid_t koid;
    zx_info_thread_t info;
    zx_info_thread_stats_t stats;
    char name[ZX_MAX_NAME_LEN];
    char proc_name[ZX_MAX_NAME_LEN];
} thread_info_t;

// arguments
static zx_duration_t delay = ZX_SEC(1);
static int count = -1;
static bool print_all = false;
static bool raw_time = false;
static enum sort_order sort_order = SORT_TIME_DELTA;

// active locals
static struct list_node thread_list = LIST_INITIAL_VALUE(thread_list);
static char last_process_name[ZX_MAX_NAME_LEN];
static zx_koid_t last_process_scanned;

// Return text representation of thread state.
static const char* state_string(const zx_info_thread_t* info) {
    if (info->wait_exception_port_type != ZX_EXCEPTION_PORT_TYPE_NONE) {
        return "excp";
    } else {
        switch (ZX_THREAD_STATE_BASIC(info->state)) {
        case ZX_THREAD_STATE_NEW:
            return "new";
        case ZX_THREAD_STATE_RUNNING:
            return "run";
        case ZX_THREAD_STATE_SUSPENDED:
            return "susp";
        case ZX_THREAD_STATE_BLOCKED:
            return "block";
        case ZX_THREAD_STATE_DYING:
            return "dying";
        case ZX_THREAD_STATE_DEAD:
            return "dead";
        default:
            return "???";
        }
    }
}

static zx_status_t process_callback(void* unused_ctx, int depth,
                                    zx_handle_t proc,
                                    zx_koid_t koid, zx_koid_t parent_koid) {
    last_process_scanned = koid;

    zx_status_t status = zx_object_get_property(
        proc, ZX_PROP_NAME, &last_process_name, sizeof(last_process_name));
    return status;
}

// Adds a thread's information to the thread_list
static zx_status_t thread_callback(void* unused_ctx, int depth,
                                   zx_handle_t thread,
                                   zx_koid_t koid, zx_koid_t parent_koid) {
    thread_info_t e = {};

    e.koid = koid;
    e.scanned = true;

    e.proc_koid = last_process_scanned;
    strlcpy(e.proc_name, last_process_name, sizeof(e.proc_name));

    zx_status_t status =
        zx_object_get_property(thread, ZX_PROP_NAME, e.name, sizeof(e.name));
    if (status != ZX_OK) {
        return status;
    }
    status = zx_object_get_info(
        thread, ZX_INFO_THREAD, &e.info, sizeof(e.info), NULL, NULL);
    if (status != ZX_OK) {
        return status;
    }
    status = zx_object_get_info(
        thread, ZX_INFO_THREAD_STATS, &e.stats, sizeof(e.stats), NULL, NULL);
    if (status != ZX_OK) {
        return status;
    }

    // see if this thread is in the list
    thread_info_t* temp;
    list_for_every_entry (&thread_list, temp, thread_info_t, node) {
        if (e.koid == temp->koid) {
            // mark it scanned, compute the delta time,
            // and copy the new state over
            temp->scanned = true;
            temp->delta_time =
                zx_duration_sub_duration(e.stats.total_runtime, temp->stats.total_runtime);
            temp->info = e.info;
            temp->stats = e.stats;
            return ZX_OK;
        }
    }

    // it wasn't in the list, add it
    thread_info_t* new_entry = malloc(sizeof(thread_info_t));
    *new_entry = e;

    list_add_tail(&thread_list, &new_entry->node);

    return ZX_OK;
}

static void sort_threads(enum sort_order order) {
    if (order == UNSORTED)
        return;

    struct list_node new_list = LIST_INITIAL_VALUE(new_list);

    // cheezy sort into second list, then swap back to first
    thread_info_t* e;
    while ((e = list_remove_head_type(&thread_list, thread_info_t, node))) {
        thread_info_t* t;

        bool found = false;
        list_for_every_entry (&new_list, t, thread_info_t, node) {
            if (order == SORT_TIME_DELTA) {
                if (e->delta_time > t->delta_time) {
                    list_add_before(&t->node, &e->node);
                    found = true;
                    break;
                }
            }
        }

        // walked off the end
        if (!found)
            list_add_tail(&new_list, &e->node);
    }

    list_move(&new_list, &thread_list);
}

static void print_threads(void) {
    thread_info_t* e;
    printf("%8s %8s %10s %5s %s\n",
           "PID", "TID", raw_time ? "TIME_NS" : "TIME%", "STATE", "NAME");

    int i = 0;
    list_for_every_entry (&thread_list, e, thread_info_t, node) {
        // only print threads that are active
        if (!print_all && e->delta_time == 0)
            continue;

        if (!raw_time) {
            double percent = 0;
            if (e->delta_time > 0)
                percent = e->delta_time / (double)delay * 100;

            printf("%8lu %8lu %10.2f %5s %s:%s\n",
                   e->proc_koid, e->koid, percent, state_string(&e->info),
                   e->proc_name, e->name);
        } else {
            printf("%8lu %8lu %10lu %5s %s:%s\n",
                   e->proc_koid, e->koid, e->delta_time, state_string(&e->info),
                   e->proc_name, e->name);
        }

        // only print the first count items (or all, if count < 0)
        if (++i == count)
            break;
    }
}

static void print_help(FILE* f) {
    fprintf(f, "Usage: top [options]\n");
    fprintf(f, "Options:\n");
    fprintf(f, " -a              Print all threads, even if inactive\n");
    fprintf(f, " -c <count>      Print the first count threads (default infinity)\n");
    fprintf(f, " -d <delay>      Delay in seconds (default 1 second)\n");
    fprintf(f, " -n <times>      Run this many times and then exit\n");
    fprintf(f, " -o <sort field> Sort by different fields (default is time)\n");
    fprintf(f, " -r              Print raw time in nanoseconds\n");
    fprintf(f, "\nSupported sort fields:\n");
    fprintf(f, "\tnone : no sorting, in job order\n");
    fprintf(f, "\ttime : sort by delta time between scans\n");
}

int main(int argc, char** argv) {
    int num_loops = -1;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            print_help(stdout);
            return 0;
        }
        if (!strcmp(arg, "-a")) {
            print_all = true;
        } else if (!strcmp(arg, "-d")) {
            delay = 0;
            if (i + 1 < argc) {
                delay = ZX_SEC(atoi(argv[i + 1]));
            }
            if (delay == 0) {
                fprintf(stderr, "Bad -d value '%s'\n", argv[i + 1]);
                print_help(stderr);
                return 1;
            }
            i++;
        } else if (!strcmp(arg, "-n")) {
            num_loops = 0;
            if (i + 1 < argc) {
                num_loops = atoi(argv[i + 1]);
            }
            if (num_loops == 0) {
                fprintf(stderr, "Bad -n value '%s'\n", argv[i + 1]);
                print_help(stderr);
                return 1;
            }
            i++;
        } else if (!strcmp(arg, "-c")) {
            count = 0;
            if (i + 1 < argc) {
                count = atoi(argv[i + 1]);
            }
            if (count == 0) {
                fprintf(stderr, "Bad count\n");
                print_help(stderr);
                return 1;
            }
            i++;
        } else if (!strcmp(arg, "-o")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Bad sort field\n");
                print_help(stderr);
                return 1;
            } else if (!strcmp(argv[i + 1], "none")) {
                sort_order = UNSORTED;
            } else if (!strcmp(argv[i + 1], "time")) {
                sort_order = SORT_TIME_DELTA;
            } else {
                fprintf(stderr, "Bad sort field\n");
                print_help(stderr);
                return 1;
            }
            i++;
        } else if (!strcmp(arg, "-r")) {
            raw_time = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_help(stderr);
            return 1;
        }
    }

    // set stdin to non blocking
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    int ret = 0;
    bool first_run = true;
    for (;;) {
        zx_time_t next_deadline = zx_deadline_after(delay);

        // mark all active threads as not scanned
        thread_info_t* e;
        list_for_every_entry (&thread_list, e, thread_info_t, node) {
            e->scanned = false;
        }

        // iterate the entire job tree
        zx_status_t status =
            walk_root_job_tree(NULL, process_callback, thread_callback, NULL);
        if (status != ZX_OK) {
            fprintf(stderr, "WARNING: walk_root_job_tree failed: %s (%d)\n",
                    zx_status_get_string(status), status);
            ret = 1;
        }

        // remove every entry that hasn't been scanned this pass
        thread_info_t* temp;
        list_for_every_entry_safe (&thread_list, e, temp, thread_info_t, node) {
            if (!e->scanned) {
                list_delete(&e->node);
                free(e);
            }
        }

        if (first_run) {
            // We don't have data until after we scan twice, since we're
            // computing deltas.
            first_run = false;
            continue;
        }

        // sort the list
        sort_threads(sort_order);

        // dump the list of threads
        print_threads();

        if (num_loops > 0) {
            if (--num_loops == 0) {
                break;
            }
        } else {
            // TODO: replace once ctrl-c works in the shell
            char c;
            int err;
            while ((err = read(STDIN_FILENO, &c, 1)) > 0) {
                if (c == 0x3)
                    return 0;
            }
        }

        zx_nanosleep(next_deadline);
    }

    return ret;
}
