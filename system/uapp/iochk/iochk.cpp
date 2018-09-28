// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <block-client/cpp/client.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/mapped-vmo.h>
#include <lib/zx/fifo.h>
#include <lib/zx/thread.h>

#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/device/skip-block.h>
#include <lib/zircon-internal/xorshiftrand.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

namespace {

constexpr char kUsageMessage[] = R"""(
usage: iochk [OPTIONS] <device>

    -bs block_size - number of bytes to treat as a unit (default=device block size)
    -t thread# - the number of threads to run (default=1)
    -c block_count - number of blocks to read (default=the whole device)
    -o offset - block-size offset to start reading from (default=0)
    -s seed - the seed to use for pseudorandom testing
    --live-dangerously - skip confirmation prompt
    --skip - verify skip-block interface instead of block interface
)""";

constexpr uint64_t kBlockHeader = 0xdeadbeef;

// Flags.
bool skip = false;
uint32_t start_block = 0;
size_t block_size = 0;
uint32_t block_count = 0;

// Constant after init.
uint64_t base_seed;

// Not thread safe.
class ProgressBar {
public:
    ProgressBar()
        : total_work_(0) {}
    ProgressBar(uint32_t block_count, size_t num_threads)
        : total_work_(static_cast<uint32_t>(static_cast<int>(block_count * log(block_count)) *
                                            num_threads)) {}

    ProgressBar(const ProgressBar& other) = default;
    ProgressBar& operator=(const ProgressBar& other) = default;

    void Update(uint32_t was_read) {
        int old_progress = static_cast<int>(100 * blocks_read_ / total_work_);
        blocks_read_ += was_read;
        int progress = static_cast<int>(100 * blocks_read_ / total_work_);

        if (old_progress != progress) {
            int ticks = 40;
            char str[ticks + 1];
            memset(str, ' ', ticks);
            memset(str, '=', ticks * progress / 100);
            str[ticks] = '\0';
            printf("\r[%s] %02d%%", str, progress);
            fflush(stdout);
        }
        if (progress == 100) {
            printf("\n");
        }
    }

private:
    uint32_t total_work_;
    uint32_t blocks_read_ = 0;
};

// Context for thread workers.
class WorkContext {
public:
    WorkContext(fbl::unique_fd fd, ProgressBar progress)
        : fd(fbl::move(fd)), progress(progress) {}
    ~WorkContext() {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(WorkContext);

    // File descriptor to device being tested.
    fbl::unique_fd fd;
    // Implementation specific information.
    struct {
        block_client::Client client;
        block_info_t info = {};
    } block;
    struct {
        skip_block_partition_info_t info = {};
    } skip;
    // Protects |iochk_failure| and |progress|
    fbl::Mutex lock;
    bool iochk_failure = false;
    ProgressBar progress;
};

// Interface to abstract over block/skip-block device interface differences.
class Checker {
public:
    // Fills the device with data based on location in the block.
    virtual zx_status_t Fill(uint32_t start, uint32_t count) { return ZX_ERR_NOT_SUPPORTED; }

    // Validates that data in specified was region on device is what was written
    // by Fill.
    virtual zx_status_t Check(uint32_t start, uint32_t count) { return ZX_ERR_NOT_SUPPORTED; }

    virtual ~Checker() = default;
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Checker);

protected:
    Checker(void* buffer)
        : buffer_(buffer) {}

    void GenerateBlockData(int block_idx, size_t length) const {
        // Block size should be a multiple of sizeof(uint64_t), but assert just to be safe
        ZX_ASSERT(length % sizeof(uint64_t) == 0);

        rand64_t seed_gen = RAND63SEED(base_seed + block_idx);
        for (int i = 0; i < 10; i++) {
            rand64(&seed_gen);
        }
        rand64_t data_gen = RAND63SEED(rand64(&seed_gen));

        auto* buf = static_cast<uint64_t*>(buffer_);
        size_t idx = 0;
        uint64_t data = kBlockHeader | (static_cast<uint64_t>(block_idx) << 32);

        while (idx < length / sizeof(uint64_t)) {
            buf[idx] = data;
            data = rand64(&data_gen);
            idx++;
        }
    }

    int CheckBlockData(int block_idx, size_t length) const {
        rand64_t seed_gen = RAND63SEED(base_seed + block_idx);
        for (int i = 0; i < 10; i++) {
            rand64(&seed_gen);
        }
        rand64_t data_gen = RAND63SEED(rand64(&seed_gen));

        auto* buf = static_cast<uint64_t*>(buffer_);
        uint64_t expected = kBlockHeader | (static_cast<uint64_t>(block_idx) << 32);
        size_t idx = 0;

        while (idx < length / sizeof(uint64_t)) {
            if (buf[idx] != expected) {
                printf("inital read verification failed: "
                       "block_idx=%d offset=%zu expected=0x%016lx val=0x%016lx\n",
                       block_idx, idx, expected, buf[idx]);
                return ZX_ERR_INTERNAL;
            }
            idx++;
            expected = rand64(&data_gen);
        }
        return 0;
    }

    void* buffer_;
};

class BlockChecker : public Checker {
public:
    static zx_status_t Initialize(const fbl::unique_fd& fd, block_info_t info,
                                  block_client::Client& client,
                                  fbl::unique_ptr<Checker>* checker) {
        fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
        zx_status_t status = fzl::MappedVmo::Create(block_size, "", &mapped_vmo);
        if (status != ZX_OK) {
            printf("Failled to create MappedVmo\n");
            return status;
        }

        zx_handle_t dup;
        status = zx_handle_duplicate(mapped_vmo->GetVmo(), ZX_RIGHT_SAME_RIGHTS, &dup);
        if (status != ZX_OK) {
            printf("cannot duplicate handle\n");
            return status;
        }

        size_t s;
        vmoid_t vmoid;
        if ((s = ioctl_block_attach_vmo(fd.get(), &dup, &vmoid) != sizeof(vmoid_t))) {
            printf("cannot attach vmo for init %lu\n", s);
            return ZX_ERR_IO;
        }

        groupid_t group = next_txid_.fetch_add(1);
        ZX_ASSERT(group < MAX_TXN_GROUP_COUNT);

        checker->reset(new BlockChecker(fbl::move(mapped_vmo), info, client, vmoid, group));
        return ZX_OK;
    }

    static void ResetAtomic() {
        next_txid_.store(0);
    }

    virtual zx_status_t Fill(uint32_t start, uint32_t count) override {
        for (uint32_t block_idx = start; block_idx < count; block_idx++) {
            uint64_t length = (info_.block_size * info_.block_count) - (block_idx * block_size);
            if (length > block_size) {
                length = block_size;
            }

            GenerateBlockData(block_idx, block_size);
            block_fifo_request_t request = {
                .opcode = BLOCKIO_WRITE,
                .reqid = 0,
                .group = group_,
                .vmoid = vmoid_,
                .length = static_cast<uint32_t>(length / info_.block_size),
                .vmo_offset = 0,
                .dev_offset = (block_idx * block_size) / info_.block_size,
            };
            zx_status_t st;
            if ((st = client_.Transaction(&request, 1)) != ZX_OK) {
                printf("write block_fifo_txn error %d\n", st);
                return st;
            }
        }
        return ZX_OK;
    }

    virtual zx_status_t Check(uint32_t start, uint32_t count) override {
        for (uint32_t block_idx = start; block_idx < count; block_idx++) {
            uint64_t length = (info_.block_size * info_.block_count) - (block_idx * block_size);
            if (length > block_size) {
                length = block_size;
            }

            block_fifo_request_t request = {
                .opcode = BLOCKIO_READ,
                .reqid = 0,
                .group = group_,
                .vmoid = vmoid_,
                .length = static_cast<uint32_t>(length / info_.block_size),
                .vmo_offset = 0,
                .dev_offset = (block_idx * block_size) / info_.block_size,
            };
            zx_status_t st;
            if ((st = client_.Transaction(&request, 1)) != ZX_OK) {
                printf("read block_fifo_txn error %d\n", st);
                return st;
            }
            if ((st = CheckBlockData(block_idx, length)) != ZX_OK) {
                return st;
            }
        }
        return ZX_OK;
    }

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockChecker);

private:
    BlockChecker(fbl::unique_ptr<fzl::MappedVmo> mapped_vmo, block_info_t info,
                 block_client::Client& client, vmoid_t vmoid, groupid_t group)
        : Checker(mapped_vmo->GetData()), mapped_vmo_(fbl::move(mapped_vmo)), info_(info),
          client_(client), vmoid_(vmoid), group_(group) {}
    ~BlockChecker() = default;

    static fbl::atomic<uint16_t> next_txid_;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo_;
    block_info_t info_;
    block_client::Client& client_;
    vmoid_t vmoid_;
    groupid_t group_;
};

fbl::atomic<uint16_t> BlockChecker::next_txid_;

class SkipBlockChecker : public Checker {
public:
    static zx_status_t Initialize(fbl::unique_fd& fd, skip_block_partition_info_t info,
                                  fbl::unique_ptr<Checker>* checker) {
        fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
        zx_status_t status = fzl::MappedVmo::Create(block_size, "", &mapped_vmo);
        if (status != ZX_OK) {
            printf("Failled to create MappedVmo\n");
            return status;
        }

        checker->reset(new SkipBlockChecker(fbl::move(mapped_vmo), fd, info));
        return ZX_OK;
    }

    virtual zx_status_t Fill(uint32_t start, uint32_t count) override {
        for (uint32_t block_idx = start; block_idx < count; block_idx++) {
            uint64_t length = (info_.block_size_bytes * info_.partition_block_count) -
                              (block_idx * block_size);
            if (length > block_size) {
                length = block_size;
            }

            zx_handle_t dup;
            zx_status_t st = zx_handle_duplicate(mapped_vmo_->GetVmo(), ZX_RIGHT_SAME_RIGHTS, &dup);
            if (st != ZX_OK) {
                printf("cannot duplicate handle\n");
                return st;
            }

            GenerateBlockData(block_idx, block_size);
            skip_block_rw_operation_t request = {
                .vmo = dup,
                .vmo_offset = 0,
                .block = static_cast<uint32_t>((block_idx * block_size) / info_.block_size_bytes),
                .block_count = static_cast<uint32_t>(length / info_.block_size_bytes),
            };
            bool bad_block_grown;
            ssize_t s = ioctl_skip_block_write(fd_.get(), &request, &bad_block_grown);
            if (s < static_cast<ssize_t>(sizeof(bad_block_grown))) {
                printf("ioctl_skip_block_write error %zd\n", s);
                return s < 0 ? static_cast<zx_status_t>(s) : ZX_ERR_IO;
            }
        }
        return ZX_OK;
    }

    virtual zx_status_t Check(uint32_t start, uint32_t count) override {
        for (uint32_t block_idx = start; block_idx < count; block_idx++) {
            uint64_t length = (info_.block_size_bytes * info_.partition_block_count) -
                              (block_idx * block_size);
            if (length > block_size) {
                length = block_size;
            }

            zx_handle_t dup;
            zx_status_t st = zx_handle_duplicate(mapped_vmo_->GetVmo(), ZX_RIGHT_SAME_RIGHTS, &dup);
            if (st != ZX_OK) {
                printf("cannot duplicate handle\n");
                return st;
            }

            skip_block_rw_operation_t request = {
                .vmo = dup,
                .vmo_offset = 0,
                .block = static_cast<uint32_t>((block_idx * block_size) / info_.block_size_bytes),
                .block_count = static_cast<uint32_t>(length / info_.block_size_bytes),
            };
            st = static_cast<zx_status_t>(ioctl_skip_block_read(fd_.get(), &request));
            if (st != ZX_OK) {
                printf("read block_fifo_txn error %d\n", st);
                return st;
            }
            if ((st = CheckBlockData(block_idx, length)) != ZX_OK) {
                return st;
            }
        }
        return ZX_OK;
    }

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(SkipBlockChecker);

private:
    SkipBlockChecker(fbl::unique_ptr<fzl::MappedVmo> mapped_vmo, fbl::unique_fd& fd,
                     skip_block_partition_info_t info)
        : Checker(mapped_vmo->GetData()), mapped_vmo_(fbl::move(mapped_vmo)), fd_(fd), info_(info) {}
    ~SkipBlockChecker() = default;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo_;
    fbl::unique_fd& fd_;
    skip_block_partition_info_t info_;
};

zx_status_t InitializeChecker(WorkContext& ctx, fbl::unique_ptr<Checker>* checker) {
    return skip ? SkipBlockChecker::Initialize(ctx.fd, ctx.skip.info, checker)
                : BlockChecker::Initialize(ctx.fd, ctx.block.info, ctx.block.client, checker);
}

zx_status_t InitializeDevice(WorkContext& ctx) {
    fbl::unique_ptr<Checker> checker;
    zx_status_t status;
    if ((status = InitializeChecker(ctx, &checker)) != ZX_OK) {
        printf("Failed to alloc resources to init device\n");
        return status;
    }

    printf("writing test data to device...\n");
    fflush(stdout);
    if ((status = checker->Fill(start_block, block_count)) != ZX_OK) {
        printf("failed to write test data\n");
        return status;
    }
    printf("done\n");

    printf("verifying test data...\n");
    fflush(stdout);
    if ((status = checker->Check(start_block, block_count)) != ZX_OK) {
        printf("failed to verify test data\n");
        return status;
    }
    printf("done\n");

    return 0;
}

int DoWork(void* arg) {
    auto* ctx = static_cast<WorkContext*>(arg);

    fbl::unique_ptr<Checker> checker;
    zx_status_t status;
    if ((status = InitializeChecker(*ctx, &checker)) != ZX_OK) {
        printf("Failed to alloc resources to init device\n");
        return status;
    }

    auto tid = static_cast<uintptr_t>(zx::thread::self()->get());
    rand32_t seed_gen = RAND32SEED(static_cast<uint32_t>(base_seed + tid));
    for (int i = 0; i < 20; i++) {
    }
    rand32_t work_gen = RAND32SEED(rand32(&seed_gen));
    // The expected number of random pages we need to hit all of them is
    // approx n*log(n) (the coupon collector problem)
    uint32_t blocks_left = static_cast<uint32_t>(block_count * log(block_count));

    while (blocks_left > 0 && !ctx->iochk_failure) {
        uint32_t to_read = (rand32(&work_gen) % blocks_left) + 1;
        uint32_t work_offset = rand32(&work_gen) % block_count;
        if (work_offset + to_read > block_count) {
            to_read = block_count - work_offset;
        }

        zx_status_t status;
        if (rand32(&work_gen) % 2) {
            status = checker->Check(start_block + work_offset, to_read);
        } else {
            status = checker->Fill(start_block + work_offset, to_read);
        }

        fbl::AutoLock al(&ctx->lock);
        if (status != ZX_OK) {
            ctx->iochk_failure = true;
        } else if (!ctx->iochk_failure) {
            ctx->progress.Update(to_read);
            blocks_left -= to_read;
        }
    }

    return 0;
}

uint64_t Number(const char* str) {
    char* end;
    uint64_t n = strtoull(str, &end, 10);

    uint64_t m = 1;
    switch (*end) {
    case 'G':
    case 'g':
        m = 1024 * 1024 * 1024;
        break;
    case 'M':
    case 'm':
        m = 1024 * 1024;
        break;
    case 'K':
    case 'k':
        m = 1024;
        break;
    }
    return m * n;
}

int Usage(void) {
    printf("%s\n", kUsageMessage);
    return -1;
}

} // namespace

int iochk(int argc, char** argv) {
    const char* device = argv[argc - 1];
    fbl::unique_fd fd(open(device, O_RDONLY));
    if (fd.get() < 0) {
        printf("cannot open '%s'\n", device);
        return Usage();
    }

    bool seed_set = false;
    size_t num_threads = 1;
    bool confirmed = false;
    char** end = argv + argc - 1;
    argv++;
    while (argv < end) {
        if (strcmp(*argv, "-t") == 0) {
            num_threads = atoi(argv[1]);
            argv += 2;
        } else if (strcmp(*argv, "-c") == 0) {
            block_count = atoi(argv[1]);
            argv += 2;
        } else if (strcmp(*argv, "-o") == 0) {
            start_block = atoi(argv[1]);
            argv += 2;
        } else if (strcmp(*argv, "-bs") == 0) {
            block_size = Number(argv[1]);
            argv += 2;
        } else if (strcmp(*argv, "-s") == 0) {
            base_seed = atoll(argv[1]);
            seed_set = true;
            argv += 2;
        } else if (strcmp(*argv, "--live-dangerously") == 0) {
            confirmed = true;
            argv++;
        } else if (strcmp(*argv, "--skip") == 0) {
            skip = true;
            argv++;
        } else if (strcmp(*argv, "-h") == 0 ||
                   strcmp(*argv, "--help") == 0) {
            return Usage();
        } else {
            printf("Invalid arg %s\n", *argv);
            return Usage();
        }
    }

    if (!confirmed) {
        constexpr char kWarning[] = "\033[0;31mWARNING\033[0m";
        printf("%s: iochk is a destructive operation.\n", kWarning);
        printf("%s: All data on %s in the given range will be overwritten.\n",
               kWarning, device);
        printf("%s: Type 'y' to continue, 'n' or ESC to cancel:\n", kWarning);
        for (;;) {
            char c;
            ssize_t r = read(STDIN_FILENO, &c, 1);
            if (r < 0) {
                printf("Error reading from stdin\n");
                return -1;
            }
            if (c == 'y' || c == 'Y') {
                break;
            } else if (c == 'n' || c == 'N' || c == 27) {
                return 0;
            }
        }
    }

    if (!seed_set) {
        base_seed = zx_clock_get_monotonic();
    }
    printf("seed is %ld\n", base_seed);

    WorkContext ctx(fbl::move(fd), ProgressBar());

    if (skip) {
        // Skip Block Device Setup.
        skip_block_partition_info_t info;
        ssize_t s = ioctl_skip_block_get_partition_info(ctx.fd.get(), &info);
        if (s != sizeof(info)) {
            printf("unable to get skip-block partition info: %zd\n", s);
            printf("fd: %d\n", ctx.fd.get());
            return -1;
        }
        printf("opened %s - block_size_bytes=%zu, partition_block_count=%lu\n", device,
               info.block_size_bytes, info.partition_block_count);

        ctx.skip.info = info;

        if (block_size == 0) {
            block_size = info.block_size_bytes;
        } else if (block_size % info.block_size_bytes != 0) {
            printf("block-size is not a multiple of device block size\n");
            return -1;
        }
        uint32_t dev_blocks_per_block = static_cast<uint32_t>(block_size / info.block_size_bytes);

        if (dev_blocks_per_block * start_block >= info.partition_block_count) {
            printf("offset past end of device\n");
            return -1;
        }

        if (block_count == 0) {
            block_count = static_cast<uint32_t>((info.partition_block_count +
                                                 dev_blocks_per_block - 1) /
                                                dev_blocks_per_block);
        } else if (dev_blocks_per_block * (block_count + start_block) >=
                   dev_blocks_per_block + info.partition_block_count) {
            // Don't allow blocks to start past the end of the device
            printf("block_count+offset too large\n");
            return -1;
        }
    } else {
        // Block Device Setup.
        block_info_t info;
        if (ioctl_block_get_info(ctx.fd.get(), &info) != sizeof(info)) {
            printf("unable to get block info\n");
            return -1;
        }
        printf("opened %s - block_size=%u, block_count=%lu\n",
               device, info.block_size, info.block_count);

        ctx.block.info = info;

        if (block_size == 0) {
            block_size = static_cast<uint32_t>(info.block_size);
        } else if (block_size % info.block_size != 0) {
            printf("block-size is not a multiple of device block size\n");
            return -1;
        }
        uint32_t dev_blocks_per_block = static_cast<uint32_t>(block_size / info.block_size);

        if (dev_blocks_per_block * start_block >= info.block_count) {
            printf("offset past end of device\n");
            return -1;
        }

        if (block_count == 0) {
            block_count = static_cast<uint32_t>((info.block_count + dev_blocks_per_block - 1) /
                                                dev_blocks_per_block);
        } else if (dev_blocks_per_block * (block_count + start_block) >=
                   dev_blocks_per_block + info.block_count) {
            // Don't allow blocks to start past the end of the device
            printf("block_count+offset too large\n");
            return -1;
        }

        if (info.max_transfer_size < block_size) {
            printf("block-size is larger than max transfer size (%d)\n", info.max_transfer_size);
            return -1;
        }

        zx::fifo fifo;
        if (ioctl_block_get_fifos(ctx.fd.get(), fifo.reset_and_get_address()) != sizeof(fifo)) {
            printf("cannot get fifo for device\n");
            return -1;
        }

        if (block_client::Client::Create(fbl::move(fifo), &ctx.block.client) != ZX_OK) {
            printf("cannot create block client for device\n");
            return -1;
        }

        BlockChecker::ResetAtomic();
    }

    ctx.progress = ProgressBar(block_count, num_threads);

    if (InitializeDevice(ctx)) {
        printf("device initialization failed\n");
        return -1;
    }

    // Reset before launching any worker threads.
    if (!skip) {
        BlockChecker::ResetAtomic();
    }

    printf("starting worker threads...\n");
    thrd_t threads[num_threads];

    if (num_threads > MAX_TXN_GROUP_COUNT) {
        printf("number of threads capped at %u\n", MAX_TXN_GROUP_COUNT);
        num_threads = MAX_TXN_GROUP_COUNT;
    }

    for (auto& thread : threads) {
        if (thrd_create(&thread, DoWork, &ctx) != thrd_success) {
            printf("thread creation failed\n");
            return -1;
        }
    }

    for (auto& thread : threads) {
        thrd_join(thread, nullptr);
    }

    // Reset after launching worker threads to avoid hitting the capacity.
    if (!skip) {
        BlockChecker::ResetAtomic();
    }

    if (!ctx.iochk_failure) {
        printf("re-verifying device...\n");
        fflush(stdout);
        fbl::unique_ptr<Checker> checker;
        zx_status_t status;
        if ((status = InitializeChecker(ctx, &checker)) != ZX_OK) {
            printf("failed to initialize verification thread\n");
            return status;
        }
        if (checker->Check(start_block, block_count) != ZX_OK) {
            printf("failed to re-verify test data\n");
            ctx.iochk_failure = true;
        } else {
            printf("done\n");
        }
    }

    if (!ctx.iochk_failure) {
        printf("iochk completed successfully\n");
        return 0;
    } else {
        printf("iochk failed (seed was %ld)\n", base_seed);
        return -1;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return Usage();
    }

    int res = iochk(argc, argv);
    return res;
}
