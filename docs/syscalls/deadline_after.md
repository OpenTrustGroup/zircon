# zx_deadline_after

## NAME

deadline_after - Convert a time relative to now to an absolute deadline

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_time_t zx_deadline_after(zx_duration_t nanoseconds)
```

## DESCRIPTION

**zx_deadline_after**() is a utility for converting from now-relative durations
to absolute deadlines. If *nanoseconds* plus the current time is bigger than the
maximum value for ``zx_time_t``, the output is clamped to **ZX_TIME_INFINITE**.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_deadline_after**() returns the absolute time (with respect to **CLOCK_MONOTONIC**)
that is *nanoseconds* nanoseconds from now.

## ERRORS

**zx_deadline_after**() does not report any error conditions.

## EXAMPLES

```
// Sleep 50 milliseconds
zx_time_t deadline = zx_deadline_after(ZX_MSEC(50));
zx_nanosleep(deadline);
```

## SEE ALSO

[ticks_get](ticks_get.md)
