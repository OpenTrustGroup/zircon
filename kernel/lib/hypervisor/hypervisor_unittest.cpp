// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <hypervisor/guest_physical_address_space.h>
#include <vm/vm.h>
#include <vm/pmm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <unittest.h>

static bool hypervisor_supported() {
#ifdef ARCH_ARM64
    if (arm64_get_boot_el() < 2) {
        unittest_printf("Hypervisor not supported\n");
        return false;
    }
#endif
    return true;
}

static zx_status_t get_paddr(void* context, size_t offset, size_t index, paddr_t pa) {
    *static_cast<paddr_t*>(context) = pa;
    return ZX_OK;
}

static zx_status_t create_vmo(size_t vmo_size, fbl::RefPtr<VmObject>* vmo_out) {
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(0, vmo_size, &vmo);
    if (status != ZX_OK)
        return status;

    uint64_t committed = 0;
    status = vmo->CommitRange(0, vmo->size(), &committed);
    if (status != ZX_OK)
        return status;
    if (committed != vmo->size())
        return ZX_ERR_BAD_STATE;

    *vmo_out = vmo;
    return ZX_OK;
}

static zx_status_t create_gpas(fbl::RefPtr<VmObject> guest_phys_mem,
                               fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace>* gpas) {
#ifdef ARCH_ARM64
    return hypervisor::GuestPhysicalAddressSpace::Create(guest_phys_mem, 1 /* vmid */, gpas);
#elif ARCH_X86
    return hypervisor::GuestPhysicalAddressSpace::Create(guest_phys_mem, gpas);
#endif
}

static bool guest_physical_address_space_unmap_range() {
    BEGIN_TEST;

    if (!hypervisor_supported()) {
        return true;
    }

    // Setup
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = create_vmo(PAGE_SIZE, &vmo);
    EXPECT_EQ(ZX_OK, status, "Failed to setup vmo.\n");
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
    status = create_gpas(vmo, &gpas);
    EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace.\n");

    // Unmap page.
    status = gpas->UnmapRange(0, PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "Failed to unmap page from GuestPhysicalAddressSpace.\n");

    // Verify GetPage for unmapped address fails.
    paddr_t gpas_paddr;
    status = gpas->GetPage(0, &gpas_paddr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status,
              "GetPage returning unexpected value for unmapped address.\n");
    END_TEST;
}

static bool guest_physical_address_space_get_page_not_present() {
    BEGIN_TEST;

    if (!hypervisor_supported()) {
        return true;
    }

    // Setup
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = create_vmo(PAGE_SIZE, &vmo);
    EXPECT_EQ(ZX_OK, status, "Failed to setup vmo.\n");
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
    status = create_gpas(vmo, &gpas);
    EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace.\n");

    // Query unmapped address.
    paddr_t gpas_paddr = 0;
    status = gpas->GetPage(UINTPTR_MAX, &gpas_paddr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status,
              "GetPage returning unexpected value for unmapped address.\n");

    END_TEST;
}

static bool guest_physical_address_space_get_page() {
    BEGIN_TEST;

    if (!hypervisor_supported()) {
        return true;
    }

    // Setup
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = create_vmo(PAGE_SIZE, &vmo);
    EXPECT_EQ(ZX_OK, status, "Failed to setup vmo.\n");
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
    status = create_gpas(vmo, &gpas);
    EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace.\n");

    // Read expected physical address from the VMO.
    paddr_t vmo_paddr = 0;
    status = vmo->Lookup(0, PAGE_SIZE, 0, get_paddr, &vmo_paddr);
    EXPECT_EQ(ZX_OK, status, "Failed to lookup physical address of VMO.\n");
    EXPECT_NE(0u, vmo_paddr, "Failed to lookup physical address of VMO.\n");

    // Read physical address from GPAS & compare with address read from VMO.
    paddr_t gpas_paddr = 0;
    status = gpas->GetPage(0, &gpas_paddr);
    EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace.\n");
    EXPECT_EQ(vmo_paddr, gpas_paddr,
              "Incorrect physical address returned from GuestPhysicalAddressSpace::GetPage.\n");

    END_TEST;
}

static bool guest_physical_address_space_get_page_complex() {
    BEGIN_TEST;

    if (!hypervisor_supported()) {
        return true;
    }

    // Test GetPage with a less trivial VMAR configuration.
    //
    //                  0 -->+--------+
    //                       |  Root  |
    //                       |  VMO   |
    //      ROOT_VMO_SIZE -->---------+ +--------+
    //                       |        | | Second |
    // ROOT_VMO_SIZE +       |        | | VMO    |
    //    SECOND_VMO_SIZE -->---------+ +--------+
    //                       |  Root  | | Shadow |
    //                       |  VMAR  | | VMAR   |
    //                        ~~~~~~~~   ~~~~~~~~
    //
    // The 'Root VMO/VMAR' is the default configuration when initializing
    // GuestPhysicalAddressSpace with a VMO size of 'PAGE_SIZE'. This test
    // allocates a second VMAR and VMO and attaches them both into the 'Root
    // VMAR' to ensure we correctly locate addresses in these structures.
    const uint ROOT_VMO_SIZE = PAGE_SIZE;
    const uint SECOND_VMO_SIZE = PAGE_SIZE;

    // Setup
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = create_vmo(ROOT_VMO_SIZE, &vmo);
    EXPECT_EQ(ZX_OK, status, "Failed to setup vmo.\n");
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
    status = create_gpas(vmo, &gpas);
    EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace.\n");

    // Allocate second VMAR, offset one page into the root.
    fbl::RefPtr<VmAddressRegion> root_vmar = gpas->aspace()->RootVmar();
    fbl::RefPtr<VmAddressRegion> shadow_vmar;
    status = root_vmar->CreateSubVmar(ROOT_VMO_SIZE, root_vmar->size() - ROOT_VMO_SIZE,
                                      /* align_pow2 */ 0, root_vmar->flags() | VMAR_FLAG_SPECIFIC,
                                      "test_vmar1", &shadow_vmar);
    EXPECT_EQ(ZX_OK, status, "Failed to create shadow VMAR.\n");

    // Allocate second VMO; we'll map the original VMO on top of this one.
    fbl::RefPtr<VmObject> vmo2;
    status = create_vmo(SECOND_VMO_SIZE, &vmo2);
    EXPECT_EQ(ZX_OK, status, "Failed allocate second VMO.\n");

    // Map second VMO into second VMAR.
    fbl::RefPtr<VmMapping> mapping;
    uint mmu_flags =
        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;
    status = shadow_vmar->CreateVmMapping(
        /* mapping_offset */ 0, vmo2->size(), /* align_pow2 */ 0, VMAR_FLAG_SPECIFIC, vmo2,
        /* vmar_offset */ 0, mmu_flags, "vmo2", &mapping);
    EXPECT_EQ(ZX_OK, status, "Failed to map vmo into shadow vmar.");

    // Read expected physical address from the VMO.
    paddr_t vmo_paddr = 0;
    status = vmo2->Lookup(0, PAGE_SIZE, 0, get_paddr, &vmo_paddr);
    EXPECT_EQ(ZX_OK, status, "Failed to lookup physical address of VMO.\n");
    EXPECT_NE(0u, vmo_paddr, "Failed to lookup physical address of VMO.\n");

    // Read physical address from GPAS.
    paddr_t gpas_paddr = 0;
    status = gpas->GetPage(ROOT_VMO_SIZE, &gpas_paddr);
    EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace.\n");
    EXPECT_EQ(vmo_paddr, gpas_paddr,
              "Incorrect physical address returned from GuestPhysicalAddressSpace::GetPage.\n");
    END_TEST;
}

static bool guest_physical_address_space_map_interrupt_controller() {
    BEGIN_TEST;

    if (!hypervisor_supported()) {
        return true;
    }

    // Allocate VMO.
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(0, PAGE_SIZE, &vmo);
    EXPECT_EQ(status, ZX_OK, "vmobject creation\n");
    EXPECT_NONNULL(vmo, "Failed to allocate VMO\n");

    // Setup GuestPhysicalAddressSpace.
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
    status = create_gpas(vmo, &gpas);
    EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");

    // Allocate a page to use as the APIC page.
    paddr_t paddr = 0;
    vm_page_t* vm_page = pmm_alloc_page(0, &paddr);
    EXPECT_NONNULL(vm_page, "Unable to allocate a page\n");

    // Map APIC page in an arbitrary location.
    const vaddr_t APIC_ADDRESS = 0xffff0000;
    status = gpas->MapInterruptController(APIC_ADDRESS, paddr, PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "Failed to map APIC page\n");

    // Cleanup
    pmm_free_page(vm_page);
    END_TEST;
}

// Use the function name as the test name
#define HYPERVISOR_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(hypervisor_tests)
HYPERVISOR_UNITTEST(guest_physical_address_space_unmap_range)
HYPERVISOR_UNITTEST(guest_physical_address_space_get_page)
HYPERVISOR_UNITTEST(guest_physical_address_space_get_page_complex)
HYPERVISOR_UNITTEST(guest_physical_address_space_get_page_not_present)
HYPERVISOR_UNITTEST(guest_physical_address_space_map_interrupt_controller)
UNITTEST_END_TESTCASE(hypervisor_tests, "hypervisor_tests", "Hypervisor unit tests.");
