/*
 * Copyright (c) 2013-2016 Google Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Reference:
 * ARM document DEN 0028A: SMC CALLING CONVENTION
 * version: 0.9.0
 */

#include <debug.h>
#include <err.h>
#include <trace.h>
#include <kernel/mutex.h>
#include <lib/sm.h>
#include <object/c_user_smc_service.h>
#include <lk/init.h>
#include <string.h>
#include <arch/ops.h>
#if WITH_LIB_VERSION
#include <lib/version.h>
#include <config-buildid.h>
#endif

#define LOCAL_TRACE 0

static mutex_t smc_table_lock = MUTEX_INITIAL_VALUE(smc_table_lock);

#define TRACE_SMC(msg, args)    do {            \
    u_int _i;                   \
    LTRACEF("%s\n", msg);               \
    LTRACEF("SMC: 0x%x (%s entity %u function 0x%x)\n", \
            (args)->smc_nr,         \
            SMC_IS_FASTCALL(args->smc_nr) ? "Fastcall" : "Stdcall", \
            SMC_ENTITY(args->smc_nr), SMC_FUNCTION(args->smc_nr)); \
    for(_i = 0; _i < SMC_NUM_PARAMS; _i++)      \
        LTRACEF("param%u: 0x%x\n", _i, (args)->params[_i]); \
} while (0)

long smc_undefined(smc32_args_t *args)
{
    TRACE_SMC("Undefined monitor call!", args);
    return SM_ERR_UNDEFINED_SMC;
}

/* Restarts should never be dispatched like this */
static long smc_restart_stdcall(smc32_args_t *args)
{
    TRACE_SMC("Unexpected stdcall restart!", args);
    return SM_ERR_UNEXPECTED_RESTART;
}

/*
 * Switch to secure mode and return. This function does no work on its own,
 * but if an interrupt is pending, it will be handled, and can in turn trigger a
 * context switch that will perform other secure work.
 */
static long smc_nop_stdcall(smc32_args_t *args)
{
    return 0;
}

/*
 * parameterized nop call handler
 */
static long smc_nop_secure_monitor(smc32_args_t *args)
{
    return (!args->params[0]) ? 0 : SM_ERR_UNDEFINED_SMC;
}

static smc32_handler_t sm_stdcall_function_table[] = {
    [SMC_FUNCTION(SMC_SC_RESTART_LAST)] = smc_restart_stdcall,
    [SMC_FUNCTION(SMC_SC_LOCKED_NOP)] = smc_nop_stdcall,
    [SMC_FUNCTION(SMC_SC_RESTART_FIQ)] = smc_restart_stdcall,
    [SMC_FUNCTION(SMC_SC_NOP)] = smc_undefined, /* reserve slot in table, not called */
};

static long smc_stdcall_secure_monitor(smc32_args_t *args)
{
    u_int function = SMC_FUNCTION(args->smc_nr);
    smc32_handler_t handler_fn = NULL;

    if (function < countof(sm_stdcall_function_table))
        handler_fn = sm_stdcall_function_table[function];

    if (!handler_fn)
        handler_fn = smc_undefined;

    return handler_fn(args);
}

static long smc_cpu_suspend(smc32_args_t *args)
{
    lk_init_level_all(LK_INIT_FLAG_CPU_SUSPEND);

    return 0;
}

static long smc_cpu_resume(smc32_args_t *args)
{
    lk_init_level_all(LK_INIT_FLAG_CPU_RESUME);

    return 0;
}

#if WITH_LIB_VERSION

#ifdef BUILDID
#define BUILDID_STR "\nbuildid: " TOSTRING(BUILDID)
#else
#define BUILDID_STR ""
#endif

static char verion_string[] = "version:" "\narch: " ARCH "\nplatform: " PLATFORM "\ntarget: " TARGET "\nproject: " PROJECT BUILDID_STR;

static long smc_get_version_str(smc32_args_t *args)
{
    int32_t index = args->params[0];
    size_t version_len = strlen(verion_string);

    if (index == -1)
        return version_len;

    if ((size_t)index >= version_len)
        return SM_ERR_INVALID_PARAMETERS;

    return verion_string[index];
}
#endif

smc32_handler_t sm_fastcall_function_table[] = {
    [SMC_FUNCTION(SMC_FC_GET_NEXT_IRQ)] = smc_intc_get_next_irq,
    [SMC_FUNCTION(SMC_FC_CPU_SUSPEND)] = smc_cpu_suspend,
    [SMC_FUNCTION(SMC_FC_CPU_RESUME)] = smc_cpu_resume,
#if WITH_LIB_VERSION
    [SMC_FUNCTION(SMC_FC_GET_VERSION_STR)] = smc_get_version_str,
#endif
    [SMC_FUNCTION(SMC_FC_API_VERSION)] = smc_sm_api_version,
};

uint32_t sm_nr_fastcall_functions = countof(sm_fastcall_function_table);

/* SMC dispatch tables */
smc32_handler_t sm_fastcall_table[SMC_NUM_ENTITIES] = {
    [0 ... SMC_ENTITY_RESERVED - 1] = notify_smc_service,
    [SMC_ENTITY_RESERVED ... SMC_ENTITY_TRUSTED_APP - 1] = smc_undefined,
    [SMC_ENTITY_TRUSTED_APP ... SMC_ENTITY_SECURE_MONITOR - 1] = notify_smc_service,
    [SMC_ENTITY_SECURE_MONITOR] = smc_fastcall_secure_monitor,
    [SMC_ENTITY_SECURE_MONITOR + 1 ... SMC_NUM_ENTITIES - 1] = notify_smc_service
};

smc32_handler_t sm_nopcall_table[SMC_NUM_ENTITIES] = {
    [0] = smc_nop_secure_monitor,
    [1 ... SMC_NUM_ENTITIES - 1] = smc_undefined
};

smc32_handler_t sm_stdcall_table[SMC_NUM_ENTITIES] = {
    [0 ... SMC_ENTITY_RESERVED - 1] = notify_smc_service,
    [SMC_ENTITY_RESERVED ... SMC_ENTITY_TRUSTED_APP - 1] = smc_undefined,
    [SMC_ENTITY_TRUSTED_APP ... SMC_ENTITY_SECURE_MONITOR - 1] = notify_smc_service,
    [SMC_ENTITY_SECURE_MONITOR] = smc_stdcall_secure_monitor,
    [SMC_ENTITY_SECURE_MONITOR + 1 ... SMC_NUM_ENTITIES - 1] = notify_smc_service
};
