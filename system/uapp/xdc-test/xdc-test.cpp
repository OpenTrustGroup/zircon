// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/device/debug.h>
#include <zircon/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* const DEV_XDC_DIR = "/dev/class/usb-dbc";

static constexpr uint32_t BUFFER_SIZE = 10 * 1024;
static constexpr uint32_t DEFAULT_STREAM_ID = 1;

typedef struct {
    off_t file_size;
} file_header_t;

static void usage(const char* prog_name) {
    printf("usage:\n");
    printf("%s [options]\n", prog_name);
    printf("\nOptions\n");
    printf("  -i <stream id>  : ID of stream to transfer over, must be positive. Defaults to 1.\n"
           "  -f <filename>   : Name of file to transfer.\n");
}

static zx_status_t configure_xdc_device(const uint32_t stream_id, fbl::unique_fd& out_fd) {
    DIR* d = opendir(DEV_XDC_DIR);
    if (d == nullptr) {
        fprintf(stderr, "Could not open dir: \"%s\"\n", DEV_XDC_DIR);
        return ZX_ERR_BAD_STATE;
    }

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        int fd = openat(dirfd(d), de->d_name, O_RDWR);
        if (fd < 0) {
            continue;
        }
        zx_status_t status = static_cast<zx_status_t>(ioctl_debug_set_stream_id(fd, &stream_id));
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to set stream id %u for device \"%s/%s\", err: %d\n",
                    stream_id, DEV_XDC_DIR, de->d_name, status);
            close(fd);
            continue;
        }
        printf("Configured debug device \"%s/%s\", stream id %u\n",
               DEV_XDC_DIR, de->d_name, stream_id);
        out_fd.reset(fd);
        closedir(d);
        return ZX_OK;
    }
    closedir(d);

    fprintf(stderr, "No debug device found\n");
    return ZX_ERR_NOT_FOUND;
}

// Writes the file header to the xdc device and also stores it in out_file_header.
static zx_status_t write_file_header(fbl::unique_fd& file_fd, fbl::unique_fd& xdc_fd,
                                     file_header_t* out_file_header) {
    struct stat s;
    if (fstat(file_fd.get(), &s) < 0) {
        fprintf(stderr, "could not get size of file, err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    file_header_t file_header = { .file_size = s.st_size };
    unsigned char* buf = reinterpret_cast<unsigned char*>(&file_header);
    ssize_t res = write(xdc_fd.get(), buf, sizeof(file_header));
    if (sizeof(res) != sizeof(file_header)) {
        fprintf(stderr, "Fatal write err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    ZX_DEBUG_ASSERT(out_file_header != nullptr);
    memcpy(out_file_header, &file_header, sizeof(file_header));
    return ZX_OK;
}

// Reads from the src_fd and writes to the dest_fd until src_len bytes has been written,
// or a fatal error occurs while reading or writing.
static zx_status_t transfer(fbl::unique_fd& src_fd, off_t src_len, fbl::unique_fd& dest_fd) {
    printf("Transferring file of size %lld bytes.\n", src_len);

    fbl::unique_ptr<unsigned char*[]> buf(new unsigned char*[BUFFER_SIZE]);
    ssize_t res;
    off_t total_read = 0;
    while ((total_read < src_len) &&
           ((res = read(src_fd.get(), buf.get(), BUFFER_SIZE)) != 0)) {
        if (res < 0) {
            fprintf(stderr, "Fatal read error: %s\n", strerror(errno));
            return ZX_ERR_IO;
        }
        total_read += res;

        ssize_t buf_len = res;
        ssize_t total_written = 0;
        while (total_written < buf_len) {
            ssize_t res = write(dest_fd.get(), buf.get() + total_written, buf_len - total_written);
            if (res < 0) {
                fprintf(stderr, "Fatal write err: %s\n", strerror(errno));
                return ZX_ERR_IO;
            }
            total_written += res;
        }
    }
    return ZX_OK;
}

int main(int argc, char** argv) {
    auto print_usage = fbl::MakeAutoCall([argv]() { usage(argv[0]); });

    const char* filename = nullptr;
    uint32_t stream_id = DEFAULT_STREAM_ID;

    int opt;
    while ((opt = getopt(argc, argv, "i:f:")) != -1) {
        switch (opt) {
        case 'i':
            if (sscanf(optarg, "%u", &stream_id) != 1) {
                fprintf(stderr, "Failed to parse stream id: \"%s\"\n", optarg);
                return -1;
            }
            if (stream_id == 0) {
                fprintf(stderr, "Stream ID must be positive\n");
                return -1;
            }
            break;
        case 'f':
            filename = optarg;
            break;
        default:
            fprintf(stderr, "Invalid option\n");
            return -1;
        }
    }
    if (!filename) {
        fprintf(stderr, "No file specified\n");
        return -1;
    }
    // Finished parsing the arguments without error.
    print_usage.cancel();

    fbl::unique_fd xdc_fd;
    zx_status_t status = configure_xdc_device(stream_id, xdc_fd);
    if (status != ZX_OK) {
        return -1;
    }

    int file_flags = O_RDONLY;
    fbl::unique_fd file_fd(open(filename, file_flags));
    if (!file_fd) {
        fprintf(stderr, "Failed to open \"%s\", err %s\n", filename, strerror(errno));
        return -1;
    }
    file_header_t file_header;
    if (write_file_header(file_fd, xdc_fd, &file_header) != ZX_OK) {
        return -1;
    }
    status = transfer(file_fd, file_header.file_size, xdc_fd);
    if (status != ZX_OK) {
        return -1;
    }
    return 0;
}
