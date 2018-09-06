# zx_vmar_protect

## NAME

vmar_protect - set protection of virtual memory pages

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmar_protect(zx_handle_t handle, uint32_t options,
                            zx_vaddr_t addr, uint64_t len);
```

## DESCRIPTION

**vmar_protect**() alters the access protections for the memory mappings
in the range of *len* bytes starting from *addr*. The *options* argument should
be a bitwise-or of one or more of the following:
- **ZX_VM_PERM_READ**  Map as readable.  It is an error if *vmar*
  does not have *ZX_VM_CAN_MAP_READ* permissions or the *vmar* handle does
  not have the *ZX_RIGHT_READ* right.  It is also an error if the VMO handle
  used to create the mapping did not have the *ZX_RIGHT_READ* right.
- **ZX_VM_PERM_WRITE**  Map as writable.  It is an error if *vmar*
  does not have *ZX_VM_CAN_MAP_WRITE* permissions or the *vmar* handle does
  not have the *ZX_RIGHT_WRITE* right.  It is also an error if the VMO handle
  used to create the mapping did not have the *ZX_RIGHT_WRITE* right.
- **ZX_VM_PERM_EXECUTE**  Map as executable.  It is an error if *vmar*
  does not have *ZX_VM_CAN_MAP_EXECUTE* permissions or the *vmar* handle does
  not have the *ZX_RIGHT_EXECUTE* right.  It is also an error if the VMO handle
  used to create the mapping did not have the *ZX_RIGHT_EXECUTE* right.

*len* must be page-aligned.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vmar_protect**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *vmar_handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *vmar_handle* is not a VMAR handle.

**ZX_ERR_INVALID_ARGS**  *prot_flags* is an unsupported combination of flags
(e.g., **ZX_VM_PERM_WRITE** but not **ZX_VM_PERM_READ**), *addr* is
not page-aligned, *len* is 0, or some subrange of the requested range is
occupied by a subregion.

**ZX_ERR_NOT_FOUND**  Some subrange of the requested range is not mapped.

**ZX_ERR_ACCESS_DENIED**  *vmar_handle* does not have the proper rights for the
requested change, the original VMO handle used to create the mapping did not
have the rights for the requested change, or the VMAR itself does not allow
the requested change.

## NOTES

## SEE ALSO

[vmar_allocate](vmar_allocate.md),
[vmar_destroy](vmar_destroy.md),
[vmar_map](vmar_map.md),
[vmar_unmap](vmar_unmap.md).
