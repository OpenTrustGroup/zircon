#include "libc.h"
#include "locale_impl.h"
#include "threads_impl.h"

locale_t __uselocale(locale_t new) {
    thrd_t self = __thrd_current();
    locale_t old = self->locale;
    locale_t global = &libc.global_locale;

    if (new)
        self->locale = new == LC_GLOBAL_LOCALE ? global : new;

    return old == global ? LC_GLOBAL_LOCALE : old;
}

weak_alias(__uselocale, uselocale);
