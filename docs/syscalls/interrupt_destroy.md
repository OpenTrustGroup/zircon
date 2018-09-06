# zx_interrupt_destroy

## NAME

interrupt_destroy - destroys an interrupt object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_destroy(zx_handle_t handle);
```

## DESCRIPTION

**interrupt_destroy**() "destroys" an interrupt object, putting it in a state
where any **interrupt_wait**() operations on it will return ZX_ERR_CANCELED,
and it is unbound from any ports it was bound to.

This provides a clean shut down mechanism.  Closing the last handle to the
interrupt object results in similar cancelation but could result in use-after-close
of the handle.

If the interrupt object is bound to a port when cancelation happens, if it
has not yet triggered, or it has triggered but the packet has not yet been
received by a caller of **port_wait**(), success is returned and any packets
in flight are removed.  Otherwise, **ZX_ERR_NOT_FOUND** is returned, indicating
that the packet has been read but the interrupt has not been re-armed by calling
**zx_interrupt_ack**().

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**interrupt_destroy**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is an invalid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not an interrupt object.

**ZX_ERR_NOT_FOUND**  *handle* was bound (and now no longer is) but was not
being waited for.

**ZX_ERR_ACCESS_DENIED** *handle* lacks **ZX_RIGHT_WRITE**.

## SEE ALSO

[interrupt_ack](interrupt_ack.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_create](interrupt_create.md),
[interrupt_trigger](interrupt_trigger.md),
[interrupt_wait](interrupt_wait.md),
[port_wait](port_wait.md),
[handle_close](handle_close.md).
