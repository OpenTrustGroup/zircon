// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <lib/unittest/unittest.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>
#include <zircon/types.h>

static const uint kArchRwFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

// Allocates a single page, translates it to a vm_page_t and frees it.
static bool pmm_smoke_test() {
    BEGIN_TEST;
    paddr_t pa;

    vm_page_t* page = pmm_alloc_page(0, &pa);
    EXPECT_NE(nullptr, page, "pmm_alloc single page");
    EXPECT_NE(0u, pa, "pmm_alloc single page");

    vm_page_t* page2 = paddr_to_vm_page(pa);
    EXPECT_EQ(page2, page, "paddr_to_vm_page on single page");

    auto ret = pmm_free_page(page);
    EXPECT_EQ(1u, ret, "pmm_free_page on single page");
    END_TEST;
}

// Allocates a bunch of pages then frees them.
static bool pmm_large_alloc_test() {
    BEGIN_TEST;
    list_node list = LIST_INITIAL_VALUE(list);

    static const size_t alloc_count = 1024;

    auto count = pmm_alloc_pages(alloc_count, 0, &list);
    EXPECT_EQ(alloc_count, count, "pmm_alloc_pages a bunch of pages count");
    EXPECT_EQ(alloc_count, list_length(&list),
              "pmm_alloc_pages a bunch of pages list count");

    auto ret = pmm_free(&list);
    EXPECT_EQ(alloc_count, ret, "pmm_free_page on a list of pages");
    END_TEST;
}

// Allocates too many pages and makes sure it fails nicely.
static bool pmm_oversized_alloc_test() {
    BEGIN_TEST;
    list_node list = LIST_INITIAL_VALUE(list);

    static const size_t alloc_count =
        (128 * 1024 * 1024 * 1024ULL) / PAGE_SIZE; // 128GB

    auto count = pmm_alloc_pages(alloc_count, 0, &list);
    EXPECT_NE(alloc_count, 0, "pmm_alloc_pages too many pages count > 0");
    EXPECT_NE(alloc_count, count, "pmm_alloc_pages too many pages count");
    EXPECT_EQ(count, list_length(&list),
              "pmm_alloc_pages too many pages list count");

    auto ret = pmm_free(&list);
    EXPECT_EQ(count, ret, "pmm_free_page on a list of pages");
    END_TEST;
}

static uint32_t test_rand(uint32_t seed) {
    return (seed = seed * 1664525 + 1013904223);
}

// fill a region of memory with a pattern based on the address of the region
static void fill_region(uintptr_t seed, void* _ptr, size_t len) {
    uint32_t* ptr = (uint32_t*)_ptr;

    ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

    uint32_t val = (uint32_t)seed;
#if UINTPTR_MAX > UINT32_MAX
    val ^= (uint32_t)(seed >> 32);
#endif
    for (size_t i = 0; i < len / 4; i++) {
        ptr[i] = val;

        val = test_rand(val);
    }
}

// test a region of memory against a known pattern
static bool test_region(uintptr_t seed, void* _ptr, size_t len) {
    uint32_t* ptr = (uint32_t*)_ptr;

    ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

    uint32_t val = (uint32_t)seed;
#if UINTPTR_MAX > UINT32_MAX
    val ^= (uint32_t)(seed >> 32);
#endif
    for (size_t i = 0; i < len / 4; i++) {
        if (ptr[i] != val) {
            unittest_printf("value at %p (%zu) is incorrect: 0x%x vs 0x%x\n", &ptr[i], i, ptr[i],
                            val);
            return false;
        }

        val = test_rand(val);
    }

    return true;
}

static bool fill_and_test(void* ptr, size_t len) {
    BEGIN_TEST;

    // fill it with a pattern
    fill_region((uintptr_t)ptr, ptr, len);

    // test that the pattern is read back properly
    auto result = test_region((uintptr_t)ptr, ptr, len);
    EXPECT_TRUE(result, "testing region for corruption");

    END_TEST;
}

// Allocates a region in kernel space, reads/writes it, then destroys it.
static bool vmm_alloc_smoke_test() {
    BEGIN_TEST;
    static const size_t alloc_size = 256 * 1024;

    // allocate a region of memory
    void* ptr;
    auto kaspace = VmAspace::kernel_aspace();
    auto err = kaspace->Alloc(
        "test", alloc_size, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NE(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // free the region
    err = kaspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
    EXPECT_EQ(0, err, "VmAspace::FreeRegion region of memory");
    END_TEST;
}

// Allocates a contiguous region in kernel space, reads/writes it,
// then destroys it.
static bool vmm_alloc_contiguous_smoke_test() {
    BEGIN_TEST;
    static const size_t alloc_size = 256 * 1024;

    // allocate a region of memory
    void* ptr;
    auto kaspace = VmAspace::kernel_aspace();
    auto err = kaspace->AllocContiguous("test",
                                        alloc_size, &ptr, 0,
                                        VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::AllocContiguous region of memory");
    EXPECT_NE(nullptr, ptr, "VmAspace::AllocContiguous region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // test that it is indeed contiguous
    unittest_printf("testing that region is contiguous\n");
    paddr_t last_pa = 0;
    for (size_t i = 0; i < alloc_size / PAGE_SIZE; i++) {
        paddr_t pa = vaddr_to_paddr((uint8_t*)ptr + i * PAGE_SIZE);
        if (last_pa != 0) {
            EXPECT_EQ(pa, last_pa + PAGE_SIZE, "region is contiguous");
        }

        last_pa = pa;
    }

    // free the region
    err = kaspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
    EXPECT_EQ(0, err, "VmAspace::FreeRegion region of memory");
    END_TEST;
}

// Allocates a new address space and creates a few regions in it,
// then destroys it.
static bool multiple_regions_test() {
    BEGIN_TEST;
    void* ptr;
    static const size_t alloc_size = 16 * 1024;

    fbl::RefPtr<VmAspace> aspace = VmAspace::Create(0, "test aspace");
    EXPECT_NE(nullptr, aspace, "VmAspace::Create pointer");

    vmm_aspace_t* old_aspace = get_current_thread()->aspace;
    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(aspace.get()));

    // allocate region 0
    zx_status_t err = aspace->Alloc("test0", alloc_size, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NE(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // allocate region 1
    err = aspace->Alloc("test1", 16384, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NE(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // allocate region 2
    err = aspace->Alloc("test2", 16384, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NE(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    vmm_set_active_aspace(old_aspace);

    // free the address space all at once
    err = aspace->Destroy();
    EXPECT_EQ(0, err, "VmAspace::Destroy");
    END_TEST;
}

static bool vmm_alloc_zero_size_fails() {
    BEGIN_TEST;
    const size_t zero_size = 0;
    void* ptr;
    zx_status_t err = VmAspace::kernel_aspace()->Alloc(
        "test", zero_size, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, err, "");
    END_TEST;
}

static bool vmm_alloc_bad_specific_pointer_fails() {
    BEGIN_TEST;
    // bad specific pointer
    void* ptr = (void*)1;
    zx_status_t err = VmAspace::kernel_aspace()->Alloc(
        "test", 16384, &ptr, 0,
        VmAspace::VMM_FLAG_VALLOC_SPECIFIC | VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, err, "");
    END_TEST;
}

static bool vmm_alloc_contiguous_missing_flag_commit_fails() {
    BEGIN_TEST;
    // should have VmAspace::VMM_FLAG_COMMIT
    const uint zero_vmm_flags = 0;
    void* ptr;
    zx_status_t err = VmAspace::kernel_aspace()->AllocContiguous(
        "test", 4096, &ptr, 0, zero_vmm_flags, kArchRwFlags);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, err, "");
    END_TEST;
}

static bool vmm_alloc_contiguous_zero_size_fails() {
    BEGIN_TEST;
    const size_t zero_size = 0;
    void* ptr;
    zx_status_t err = VmAspace::kernel_aspace()->AllocContiguous(
        "test", zero_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, err, "");
    END_TEST;
}

// Allocates a vm address space object directly, allows it to go out of scope.
static bool vmaspace_create_smoke_test() {
    BEGIN_TEST;
    auto aspace = VmAspace::Create(0, "test aspace");
    aspace->Destroy();
    END_TEST;
}

// Allocates a vm address space object directly, maps something on it,
// allows it to go out of scope.
static bool vmaspace_alloc_smoke_test() {
    BEGIN_TEST;
    auto aspace = VmAspace::Create(0, "test aspace2");

    void* ptr;
    auto err = aspace->Alloc("test", PAGE_SIZE, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(ZX_OK, err, "allocating region\n");

    // destroy the aspace, which should drop all the internal refs to it
    aspace->Destroy();

    // drop the ref held by this pointer
    aspace.reset();
    END_TEST;
}

// Doesn't do anything, just prints all aspaces.
// Should be run after all other tests so that people can manually comb
// through the output for leaked test aspaces.
static bool dump_all_aspaces() {
    BEGIN_TEST;
    unittest_printf("verify there are no test aspaces left around\n");
    DumpAllAspaces(/*verbose*/ true);
    END_TEST;
}

// Creates a vm object.
static bool vmo_create_test() {
    BEGIN_TEST;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, PAGE_SIZE, &vmo);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_TRUE(vmo, "");
    EXPECT_FALSE(vmo->is_contiguous(), "vmo is not contig\n");
    END_TEST;
}

// Creates a vm object, commits memory.
static bool vmo_commit_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    uint64_t committed;
    auto ret = vmo->CommitRange(0, alloc_size, &committed);
    EXPECT_EQ(0, ret, "committing vm object\n");
    EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), committed,
              "committing vm object\n");
    END_TEST;
}

// Creates a paged VMO, pins it, and tries operations that should unpin it.
static bool vmo_pin_test() {
    BEGIN_TEST;

    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    status = vmo->Pin(PAGE_SIZE, alloc_size);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "pinning out of range\n");
    status = vmo->Pin(PAGE_SIZE, 0);
    EXPECT_EQ(ZX_OK, status, "pinning range of len 0\n");
    status = vmo->Pin(alloc_size + PAGE_SIZE, 0);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "pinning out-of-range of len 0\n");

    status = vmo->Pin(PAGE_SIZE, 3 * PAGE_SIZE);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");
    status = vmo->Pin(0, alloc_size);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");

    uint64_t n;
    status = vmo->CommitRange(PAGE_SIZE, 3 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_OK, status, "committing range\n");

    status = vmo->Pin(0, alloc_size);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");
    status = vmo->Pin(PAGE_SIZE, 4 * PAGE_SIZE);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");
    status = vmo->Pin(0, 4 * PAGE_SIZE);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "pinning uncommitted range\n");

    status = vmo->Pin(PAGE_SIZE, 3 * PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "pinning committed range\n");

    status = vmo->DecommitRange(PAGE_SIZE, 3 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
    status = vmo->DecommitRange(PAGE_SIZE, PAGE_SIZE, &n);
    EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
    status = vmo->DecommitRange(3 * PAGE_SIZE, PAGE_SIZE, &n);
    EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");

    vmo->Unpin(PAGE_SIZE, 3 * PAGE_SIZE);

    status = vmo->DecommitRange(PAGE_SIZE, 3 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

    status = vmo->CommitRange(PAGE_SIZE, 3 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_OK, status, "committing range\n");
    status = vmo->Pin(PAGE_SIZE, 3 * PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "pinning committed range\n");

    status = vmo->Resize(0);
    EXPECT_EQ(ZX_ERR_BAD_STATE, status, "resizing pinned range\n");

    vmo->Unpin(PAGE_SIZE, 3 * PAGE_SIZE);

    status = vmo->Resize(0);
    EXPECT_EQ(ZX_OK, status, "resizing unpinned range\n");

    END_TEST;
}

// Creates a page VMO and pins the same pages multiple times
static bool vmo_multiple_pin_test() {
    BEGIN_TEST;

    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    uint64_t n;
    status = vmo->CommitRange(0, alloc_size, &n);
    EXPECT_EQ(ZX_OK, status, "committing range\n");

    status = vmo->Pin(0, alloc_size);
    EXPECT_EQ(ZX_OK, status, "pinning whole range\n");
    status = vmo->Pin(PAGE_SIZE, 4 * PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "pinning subrange\n");

    for (unsigned int i = 1; i < VM_PAGE_OBJECT_MAX_PIN_COUNT; ++i) {
        status = vmo->Pin(0, PAGE_SIZE);
        EXPECT_EQ(ZX_OK, status, "pinning first page max times\n");
    }
    status = vmo->Pin(0, PAGE_SIZE);
    EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "page is pinned too much\n");

    vmo->Unpin(0, alloc_size);
    status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
    status = vmo->DecommitRange(5 * PAGE_SIZE, alloc_size - 5 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

    vmo->Unpin(PAGE_SIZE, 4 * PAGE_SIZE);
    status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE, &n);
    EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

    for (unsigned int i = 2; i < VM_PAGE_OBJECT_MAX_PIN_COUNT; ++i) {
        vmo->Unpin(0, PAGE_SIZE);
    }
    status = vmo->DecommitRange(0, PAGE_SIZE, &n);
    EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting unpinned range\n");

    vmo->Unpin(0, PAGE_SIZE);
    status = vmo->DecommitRange(0, PAGE_SIZE, &n);
    EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

    END_TEST;
}

// Creates a vm object, commits odd sized memory.
static bool vmo_odd_size_commit_test() {
    BEGIN_TEST;
    static const size_t alloc_size = 15;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    uint64_t committed;
    auto ret = vmo->CommitRange(0, alloc_size, &committed);
    EXPECT_EQ(0, ret, "committing vm object\n");
    EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), committed,
              "committing vm object\n");
    END_TEST;
}

static bool vmo_create_physical_test() {
    BEGIN_TEST;

    paddr_t pa;
    vm_page_t* vm_page = pmm_alloc_page(0, &pa);
    uint32_t cache_policy;

    ASSERT_TRUE(vm_page, "");

    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    EXPECT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_OK, vmo->GetMappingCachePolicy(&cache_policy), "try get");
    EXPECT_EQ(ARCH_MMU_FLAG_UNCACHED, cache_policy, "check initial cache policy");
    EXPECT_TRUE(vmo->is_contiguous(), "check contiguous");

    pmm_free_page(vm_page);

    END_TEST;
}

// Creates a vm object that commits contiguous memory.
static bool vmo_create_contiguous_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, alloc_size, 0, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    EXPECT_TRUE(vmo, "vmobject creation\n");

    EXPECT_TRUE(vmo->is_contiguous(), "vmo is contig\n");

    paddr_t last_pa;
    auto lookup_func = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
        paddr_t* last_pa = static_cast<paddr_t*>(ctx);
        if (index != 0 && *last_pa + PAGE_SIZE != pa) {
            return ZX_ERR_BAD_STATE;
        }
        *last_pa = pa;
        return ZX_OK;
    };
    status = vmo->Lookup(0, alloc_size, 0, lookup_func, &last_pa);
    EXPECT_EQ(status, ZX_OK, "vmo lookup\n");

    END_TEST;
}

// Make sure decommitting is disallowed
static bool vmo_contiguous_decommit_test() {
    BEGIN_TEST;

    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, alloc_size, 0, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    uint64_t n;
    status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE, &n);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");
    status = vmo->DecommitRange(0, 4 * PAGE_SIZE, &n);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");
    status = vmo->DecommitRange(alloc_size - PAGE_SIZE, PAGE_SIZE, &n);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");

    // Make sure all pages are still present and contiguous
    paddr_t last_pa;
    auto lookup_func = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
        paddr_t* last_pa = static_cast<paddr_t*>(ctx);
        if (index != 0 && *last_pa + PAGE_SIZE != pa) {
            return ZX_ERR_BAD_STATE;
        }
        *last_pa = pa;
        return ZX_OK;
    };
    status = vmo->Lookup(0, alloc_size, 0, lookup_func, &last_pa);
    ASSERT_EQ(status, ZX_OK, "vmo lookup\n");

    END_TEST;
}

// Creats a vm object, maps it, precommitted.
static bool vmo_precommitted_map_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                                     0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ZX_OK, ret, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(ZX_OK, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, demand paged.
static bool vmo_demand_paged_map_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                                     0, 0, kArchRwFlags);
    EXPECT_EQ(ret, ZX_OK, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(ZX_OK, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, drops ref before unmapping.
static bool vmo_dropped_ref_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(fbl::move(vmo), "test", 0, alloc_size, &ptr,
                                     0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ret, ZX_OK, "mapping object");

    EXPECT_NULL(vmo, "dropped ref to object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(ZX_OK, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, fills it with data, unmaps,
// maps again somewhere else.
static bool vmo_remap_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                                     0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ZX_OK, ret, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(ZX_OK, err, "unmapping object");

    // map it again
    ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                                0, VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ret, ZX_OK, "mapping object");

    // test that the pattern is still valid
    bool result = test_region((uintptr_t)ptr, ptr, alloc_size);
    EXPECT_TRUE(result, "testing region for corruption");

    err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(ZX_OK, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, fills it with data, maps it a second time and
// third time somwehere else.
static bool vmo_double_remap_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test0", 0, alloc_size, &ptr,
                                     0, 0, kArchRwFlags);
    EXPECT_EQ(ZX_OK, ret, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // map it again
    void* ptr2;
    ret = ka->MapObjectInternal(vmo, "test1", 0, alloc_size, &ptr2,
                                0, 0, kArchRwFlags);
    EXPECT_EQ(ret, ZX_OK, "mapping object second time");
    EXPECT_NE(ptr, ptr2, "second mapping is different");

    // test that the pattern is still valid
    bool result = test_region((uintptr_t)ptr, ptr2, alloc_size);
    EXPECT_TRUE(result, "testing region for corruption");

    // map it a third time with an offset
    void* ptr3;
    static const size_t alloc_offset = PAGE_SIZE;
    ret = ka->MapObjectInternal(vmo, "test2", alloc_offset, alloc_size - alloc_offset,
                                &ptr3, 0, 0, kArchRwFlags);
    EXPECT_EQ(ret, ZX_OK, "mapping object third time");
    EXPECT_NE(ptr3, ptr2, "third mapping is different");
    EXPECT_NE(ptr3, ptr, "third mapping is different");

    // test that the pattern is still valid
    int mc =
        memcmp((uint8_t*)ptr + alloc_offset, ptr3, alloc_size - alloc_offset);
    EXPECT_EQ(0, mc, "testing region for corruption");

    ret = ka->FreeRegion((vaddr_t)ptr3);
    EXPECT_EQ(ZX_OK, ret, "unmapping object third time");

    ret = ka->FreeRegion((vaddr_t)ptr2);
    EXPECT_EQ(ZX_OK, ret, "unmapping object second time");

    ret = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(ZX_OK, ret, "unmapping object");
    END_TEST;
}

static bool vmo_read_write_smoke_test() {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;

    // create object
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(0, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    // create test buffer
    fbl::AllocChecker ac;
    fbl::Array<uint8_t> a(new (&ac) uint8_t[alloc_size], alloc_size);
    EXPECT_TRUE(ac.check(), "");
    fill_region(99, a.get(), alloc_size);

    // write to it, make sure it seems to work with valid args
    zx_status_t err = vmo->Write(a.get(), 0, 0);
    EXPECT_EQ(ZX_OK, err, "writing to object");

    err = vmo->Write(a.get(), 0, 37);
    EXPECT_EQ(ZX_OK, err, "writing to object");

    err = vmo->Write(a.get(), 99, 37);
    EXPECT_EQ(ZX_OK, err, "writing to object");

    // can't write past end
    err = vmo->Write(a.get(), 0, alloc_size + 47);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

    // can't write past end
    err = vmo->Write(a.get(), 31, alloc_size + 47);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

    // should return an error because out of range
    err = vmo->Write(a.get(), alloc_size + 99, 42);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

    // map the object
    auto ka = VmAspace::kernel_aspace();
    uint8_t* ptr;
    err = ka->MapObjectInternal(vmo, "test", 0, alloc_size, (void**)&ptr,
                                0, 0, kArchRwFlags);
    EXPECT_EQ(ZX_OK, err, "mapping object");

    // write to it at odd offsets
    err = vmo->Write(a.get(), 31, 4197);
    EXPECT_EQ(ZX_OK, err, "writing to object");
    int cmpres = memcmp(ptr + 31, a.get(), 4197);
    EXPECT_EQ(0, cmpres, "reading from object");

    // write to it, filling the object completely
    err = vmo->Write(a.get(), 0, alloc_size);
    EXPECT_EQ(ZX_OK, err, "writing to object");

    // test that the data was actually written to it
    bool result = test_region(99, ptr, alloc_size);
    EXPECT_TRUE(result, "writing to object");

    // unmap it
    ka->FreeRegion((vaddr_t)ptr);

    // test that we can read from it
    fbl::Array<uint8_t> b(new (&ac) uint8_t[alloc_size], alloc_size);
    EXPECT_TRUE(ac.check(), "can't allocate buffer");

    err = vmo->Read(b.get(), 0, alloc_size);
    EXPECT_EQ(ZX_OK, err, "reading from object");

    // validate the buffer is valid
    cmpres = memcmp(b.get(), a.get(), alloc_size);
    EXPECT_EQ(0, cmpres, "reading from object");

    // read from it at an offset
    err = vmo->Read(b.get(), 31, 4197);
    EXPECT_EQ(ZX_OK, err, "reading from object");
    cmpres = memcmp(b.get(), a.get() + 31, 4197);
    EXPECT_EQ(0, cmpres, "reading from object");
    END_TEST;
}

static bool vmo_cache_test() {
    BEGIN_TEST;

    paddr_t pa;
    vm_page_t* vm_page = pmm_alloc_page(0, &pa);
    auto ka = VmAspace::kernel_aspace();
    uint32_t cache_policy = ARCH_MMU_FLAG_UNCACHED_DEVICE;
    uint32_t cache_policy_get;
    void* ptr;

    EXPECT_TRUE(vm_page, "");
    // Test that the flags set/get properly
    {
        fbl::RefPtr<VmObject> vmo;
        zx_status_t status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
        EXPECT_EQ(status, ZX_OK, "vmobject creation\n");
        EXPECT_TRUE(vmo, "vmobject creation\n");
        EXPECT_EQ(ZX_OK, vmo->GetMappingCachePolicy(&cache_policy_get), "try get");
        EXPECT_NE(cache_policy, cache_policy_get, "check initial cache policy");
        EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "try set");
        EXPECT_EQ(ZX_OK, vmo->GetMappingCachePolicy(&cache_policy_get), "try get");
        EXPECT_EQ(cache_policy, cache_policy_get, "compare flags");
    }

    // Test valid flags
    for (uint32_t i = 0; i <= ARCH_MMU_FLAG_CACHE_MASK; i++) {
        fbl::RefPtr<VmObject> vmo;
        zx_status_t status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
        EXPECT_EQ(status, ZX_OK, "vmobject creation\n");
        EXPECT_TRUE(vmo, "vmobject creation\n");
        EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "try setting valid flags");
    }

    // Test invalid flags
    for (uint32_t i = ARCH_MMU_FLAG_CACHE_MASK + 1; i < 32; i++) {
        fbl::RefPtr<VmObject> vmo;
        zx_status_t status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
        EXPECT_EQ(status, ZX_OK, "vmobject creation\n");
        EXPECT_TRUE(vmo, "vmobject creation\n");
        EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(i), "try set with invalid flags");
    }

    // Test valid flags with invalid flags
    {
        fbl::RefPtr<VmObject> vmo;
        zx_status_t status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
        EXPECT_EQ(status, ZX_OK, "vmobject creation\n");
        EXPECT_TRUE(vmo, "vmobject creation\n");
        EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x5), "bad 0x5");
        EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xA), "bad 0xA");
        EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x55), "bad 0x55");
        EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xAA), "bad 0xAA");
    }

    // Test that changing policy while mapped is blocked
    {
        fbl::RefPtr<VmObject> vmo;
        zx_status_t status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
        EXPECT_EQ(status, ZX_OK, "vmobject creation\n");
        EXPECT_TRUE(vmo, "vmobject creation\n");
        EXPECT_EQ(ZX_OK, ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0,
                                               kArchRwFlags),
                  "map vmo");
        EXPECT_EQ(ZX_ERR_BAD_STATE, vmo->SetMappingCachePolicy(cache_policy),
                  "set flags while mapped");
        EXPECT_EQ(ZX_OK, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
        EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "set flags after unmapping");
        EXPECT_EQ(ZX_OK, ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0,
                                               kArchRwFlags),
                  "map vmo again");
        EXPECT_EQ(ZX_OK, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
    }

    pmm_free_page(vm_page);
    END_TEST;
}

static bool vmo_lookup_test() {
    BEGIN_TEST;

    static const size_t alloc_size = PAGE_SIZE * 16;
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");

    size_t pages_seen = 0;
    auto lookup_fn = [](void* context, size_t offset, size_t index, paddr_t pa) {
        size_t* pages_seen = static_cast<size_t*>(context);
        (*pages_seen)++;
        return ZX_OK;
    };
    status = vmo->Lookup(0, alloc_size, 0, lookup_fn, &pages_seen);
    EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "lookup on uncommitted pages\n");
    EXPECT_EQ(0u, pages_seen, "lookup on uncommitted pages\n");
    pages_seen = 0;

    uint64_t committed;
    status = vmo->CommitRange(PAGE_SIZE, PAGE_SIZE, &committed);
    EXPECT_EQ(ZX_OK, status, "committing vm object\n");
    EXPECT_EQ(static_cast<size_t>(PAGE_SIZE), committed, "committing vm object\n");

    // Should fail, since first page isn't mapped
    status = vmo->Lookup(0, alloc_size, 0, lookup_fn, &pages_seen);
    EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "lookup on partially committed pages\n");
    EXPECT_EQ(0u, pages_seen, "lookup on partially committed pages\n");
    pages_seen = 0;

    // Should fail, but see the mapped page
    status = vmo->Lookup(PAGE_SIZE, alloc_size - PAGE_SIZE, 0, lookup_fn, &pages_seen);
    EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "lookup on partially committed pages\n");
    EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
    pages_seen = 0;

    // Should succeed
    status = vmo->Lookup(PAGE_SIZE, PAGE_SIZE, 0, lookup_fn, &pages_seen);
    EXPECT_EQ(ZX_OK, status, "lookup on partially committed pages\n");
    EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
    pages_seen = 0;

    // Commit the rest
    status = vmo->CommitRange(0, alloc_size, &committed);
    EXPECT_EQ(ZX_OK, status, "committing vm object\n");
    EXPECT_EQ(alloc_size - PAGE_SIZE, committed, "committing vm object\n");

    status = vmo->Lookup(0, alloc_size, 0, lookup_fn, &pages_seen);
    EXPECT_EQ(ZX_OK, status, "lookup on partially committed pages\n");
    EXPECT_EQ(alloc_size / PAGE_SIZE, pages_seen, "lookup on partially committed pages\n");

    END_TEST;
}

// TODO(ZX-1431): The ARM code's error codes are always ZX_ERR_INTERNAL, so
// special case that.
#if ARCH_ARM64
#define MMU_EXPECT_EQ(exp, act, msg) EXPECT_EQ(ZX_ERR_INTERNAL, act, msg)
#else
#define MMU_EXPECT_EQ(exp, act, msg) EXPECT_EQ(exp, act, msg)
#endif

static bool arch_noncontiguous_map() {
    BEGIN_TEST;

    // Get some phys pages to test on
    paddr_t phys[3];
    struct list_node phys_list = LIST_INITIAL_VALUE(phys_list);
    size_t count = pmm_alloc_pages(fbl::count_of(phys), 0, &phys_list);
    EXPECT_EQ(count, fbl::count_of(phys), "");
    {
        size_t i = 0;
        vm_page_t* p;
        list_for_every_entry(&phys_list, p, vm_page_t, free.node) {
            phys[i] = vm_page_to_paddr(p);
            ++i;
        }
    }

    zx_status_t status;
    {
        ArchVmAspace aspace;
        status = aspace.Init(USER_ASPACE_BASE, USER_ASPACE_SIZE, 0);
        EXPECT_EQ(ZX_OK, status, "failed to init aspace\n");

        // Attempt to map a set of vm_page_t
        size_t mapped;
        vaddr_t base = USER_ASPACE_BASE + 10 * PAGE_SIZE;
        status = aspace.Map(base, phys, fbl::count_of(phys), ARCH_MMU_FLAG_PERM_READ, &mapped);
        EXPECT_EQ(ZX_OK, status, "failed first map\n");
        EXPECT_EQ(fbl::count_of(phys), mapped, "weird first map\n");
        for (size_t i = 0; i < fbl::count_of(phys); ++i) {
            paddr_t paddr;
            uint mmu_flags;
            status = aspace.Query(base + i * PAGE_SIZE, &paddr, &mmu_flags);
            EXPECT_EQ(ZX_OK, status, "bad first map\n");
            EXPECT_EQ(phys[i], paddr, "bad first map\n");
            EXPECT_EQ(ARCH_MMU_FLAG_PERM_READ, mmu_flags, "bad first map\n");
        }

        // Attempt to map again, should fail
        status = aspace.Map(base, phys, fbl::count_of(phys), ARCH_MMU_FLAG_PERM_READ, &mapped);
        MMU_EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");

        // Attempt to map partially ovelapping, should fail
        status = aspace.Map(base + 2 * PAGE_SIZE, phys, fbl::count_of(phys),
                            ARCH_MMU_FLAG_PERM_READ, &mapped);
        MMU_EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");
        status = aspace.Map(base - 2 * PAGE_SIZE, phys, fbl::count_of(phys),
                            ARCH_MMU_FLAG_PERM_READ, &mapped);
        MMU_EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, status, "double map\n");

        // No entries should have been created by the partial failures
        status = aspace.Query(base - 2 * PAGE_SIZE, nullptr, nullptr);
        EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
        status = aspace.Query(base - PAGE_SIZE, nullptr, nullptr);
        EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
        status = aspace.Query(base + 3 * PAGE_SIZE, nullptr, nullptr);
        EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");
        status = aspace.Query(base + 4 * PAGE_SIZE, nullptr, nullptr);
        EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "bad first map\n");

        status = aspace.Destroy();
        EXPECT_EQ(ZX_OK, status, "failed to destroy aspace\n");
    }

    pmm_free(&phys_list);

    END_TEST;
}

// Use the function name as the test name
#define VM_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(vm_tests)
VM_UNITTEST(pmm_smoke_test)
// runs the system out of memory, uncomment for debugging
//VM_UNITTEST(pmm_large_alloc_test)
//VM_UNITTEST(pmm_oversized_alloc_test)
VM_UNITTEST(vmm_alloc_smoke_test)
VM_UNITTEST(vmm_alloc_contiguous_smoke_test)
VM_UNITTEST(multiple_regions_test)
VM_UNITTEST(vmm_alloc_zero_size_fails)
VM_UNITTEST(vmm_alloc_bad_specific_pointer_fails)
VM_UNITTEST(vmm_alloc_contiguous_missing_flag_commit_fails)
VM_UNITTEST(vmm_alloc_contiguous_zero_size_fails)
VM_UNITTEST(vmaspace_create_smoke_test)
VM_UNITTEST(vmaspace_alloc_smoke_test)
VM_UNITTEST(vmo_create_test)
VM_UNITTEST(vmo_pin_test)
VM_UNITTEST(vmo_multiple_pin_test)
VM_UNITTEST(vmo_commit_test)
VM_UNITTEST(vmo_odd_size_commit_test)
VM_UNITTEST(vmo_create_physical_test)
VM_UNITTEST(vmo_create_contiguous_test)
VM_UNITTEST(vmo_contiguous_decommit_test)
VM_UNITTEST(vmo_precommitted_map_test)
VM_UNITTEST(vmo_demand_paged_map_test)
VM_UNITTEST(vmo_dropped_ref_test)
VM_UNITTEST(vmo_remap_test)
VM_UNITTEST(vmo_double_remap_test)
VM_UNITTEST(vmo_read_write_smoke_test)
VM_UNITTEST(vmo_cache_test)
VM_UNITTEST(vmo_lookup_test)
VM_UNITTEST(arch_noncontiguous_map)
// Uncomment for debugging
// VM_UNITTEST(dump_all_aspaces)  // Run last
UNITTEST_END_TESTCASE(vm_tests, "vmtests", "Virtual memory tests");
