# zx_vmar_allocate

## NAME

vmar_allocate - allocate a new subregion

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmar_allocate(zx_handle_t parent_vmar, zx_vm_option_t options,
                             uint64_t offset, uint64_t size,
                             zx_handle_t* child_vmar, zx_vaddr_t* child_addr)
```

## DESCRIPTION

Creates a new VMAR within the one specified by *parent_vmar*.

*options* is a bit vector that contains one more of the following:
- **ZX_VM_COMPACT**  A hint to the kernel that allocations and mappings
  within the newly created subregion should be kept close together.   See the
  NOTES section below for discussion.
- **ZX_VM_SPECIFIC**  Use the *offset* to place the mapping, invalid if
  vmar does not have the **ZX_VM_CAN_MAP_SPECIFIC** permission.  *offset*
  is an offset relative to the base address of the parent region.  It is an error
  to specify an address range that overlaps with another VMAR or mapping.
- **ZX_VM_CAN_MAP_SPECIFIC**  The new VMAR can have subregions/mappings
  created with **ZX_VM_SPECIFIC**.  It is NOT an error if the parent does
  not have *ZX_VM_CAN_MAP_SPECIFIC* permissions.
- **ZX_VM_CAN_MAP_READ**  The new VMAR can contain readable mappings.
  It is an error if the parent does not have *ZX_VM_CAN_MAP_READ* permissions.
- **ZX_VM_CAN_MAP_WRITE**  The new VMAR can contain writable mappings.
  It is an error if the parent does not have *ZX_VM_CAN_MAP_WRITE* permissions.
- **ZX_VM_CAN_MAP_EXECUTE**  The new VMAR can contain executable mappings.
  It is an error if the parent does not have *ZX_VM_CAN_MAP_EXECUTE* permissions.

*offset* must be 0 if *options* does not have **ZX_VM_SPECIFIC** set.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vmar_allocate**() returns **ZX_OK**, the absolute base address of the
subregion (via *child_addr*), and a handle to the new subregion (via
*child_vmar*) on success.  The base address will be page-aligned and non-zero.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *parent_vmar* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *parent_vmar* is not a VMAR handle.

**ZX_ERR_BAD_STATE**  *parent_vmar* refers to a destroyed VMAR.

**ZX_ERR_INVALID_ARGS**  *child_vmar* or *child_addr* are not valid, *offset* is
non-zero when *ZX_VM_SPECIFIC* is not given, *offset* and *size* describe
an unsatisfiable allocation due to exceeding the region bounds, *offset*
or *size* is not page-aligned, or *size* is 0.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_ACCESS_DENIED**  Insufficient privileges to make the requested allocation.

## NOTES

### Deallocation

The address space occupied by a VMAR will remain allocated (within its
parent VMAR) until the VMAR is destroyed by calling **vmar_destroy**().

Note that just closing the VMAR's handle does not deallocate the address
space occupied by the VMAR.

### The COMPACT flag

The kernel interprets this flag as a request to reduce sprawl in allocations.
While this does not necessitate reducing the absolute entropy of the allocated
addresses, there will potentially be a very high correlation between allocations.
This is a trade-off that the developer can make to increase locality of
allocations and reduce the number of page tables necessary, if they are willing
to have certain addresses be more correlated.

## SEE ALSO

[vmar_destroy](vmar_destroy.md),
[vmar_map](vmar_map.md),
[vmar_protect](vmar_protect.md),
[vmar_unmap](vmar_unmap.md).
