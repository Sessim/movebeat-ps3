#include <lv2/mutex.h>

#include <cstdlib>
#include <cstring>

struct _reent;

namespace {

sys_lwmutex_t g_malloc_mutex;
volatile unsigned int g_malloc_mutex_state = 0;

bool ensureMallocMutex() {
    if (g_malloc_mutex_state == 2) {
        return true;
    }
    if (__sync_bool_compare_and_swap(&g_malloc_mutex_state, 0u, 1u)) {
        sys_lwmutex_attr_t attributes;
        attributes.attr_protocol = SYS_LWMUTEX_PROTOCOL_PRIO;
        attributes.attr_recursive = SYS_LWMUTEX_ATTR_RECURSIVE;
        std::memcpy(attributes.name, "mballoc", 8);
        const int result = sysLwMutexCreate(&g_malloc_mutex, &attributes);
        __sync_synchronize();
        g_malloc_mutex_state = result == 0 ? 2u : 3u;
    } else {
        while (g_malloc_mutex_state == 1u) {
            __asm__ volatile("or 27,27,27");
        }
    }
    return g_malloc_mutex_state == 2u;
}

} // namespace

// newlib's stock malloc lock is constructed after __glob_file_init in the
// current PS3DEV runtime. The latter allocates memory during static startup,
// so the stock implementation attempts to lock an uninitialized lwmutex and
// aborts before main(). Provide an equivalent lock with lazy initialization.
extern "C" void __malloc_lock(struct _reent*) {
    if (!ensureMallocMutex() || sysLwMutexLock(&g_malloc_mutex, 0) != 0) {
        std::abort();
    }
}

extern "C" void __malloc_unlock(struct _reent*) {
    if (g_malloc_mutex_state != 2u || sysLwMutexUnlock(&g_malloc_mutex) != 0) {
        std::abort();
    }
}
