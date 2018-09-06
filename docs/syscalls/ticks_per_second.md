# zx_ticks_per_second

## NAME

ticks_per_second - Read the number of high-precision timer ticks in a second.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_ticks_t zx_ticks_per_second(void)
```

## DESCRIPTION

**zx_ticks_per_second**() returns the number of high-precision timer ticks in a
second.

This can be used together with **zx_ticks_get**() to calculate the amount of
time elapsed between two subsequent calls to **zx_ticks_get**().

This value can vary from boot to boot of a given system. Once booted,
this value is guaranteed not to change.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_ticks_per_second**() returns the number of high-precision timer ticks in a
second.

## ERRORS

**zx_ticks_per_second**() does not report any error conditions.

## EXAMPLES

```
zx_ticks_t ticks_per_second = zx_ticks_per_second();
zx_ticks_t ticks_start = zx_ticks_get();

// do some more work

zx_ticks_t ticks_end = zx_ticks_get();
double elapsed_seconds = (ticks_end - ticks_start) / (double)ticks_per_second;

```

## SEE ALSO

[ticks_get](ticks_get.md)
