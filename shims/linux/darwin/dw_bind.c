// The Darwin layer: what each of the game's imports is bound to on a Linux host (dw.h).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "dw.h"

// --- this layer's own functions and data ---------------------------------------------------------------
#define DW_OWN(X)                                                                                              \
  X(__error) X(__maskrune) X(__tolower) X(__toupper) X(__exp10) X(__exp10f) X(__sincosf_stret) X(__bzero)        \
  X(memset_pattern16) X(setlocale) X(mach_absolute_time) X(mach_timebase_info) X(mach_port_type)                \
  X(OSAtomicAdd32) X(OSAtomicIncrement32) X(OSAtomicDecrement32) X(OSAtomicCompareAndSwap32)                    \
  X(OSAtomicCompareAndSwap64) X(sysctl) X(sysctlbyname) X(sigaction) X(signal) X(tcgetattr) X(tcsetattr)        \
  X(_dyld_register_func_for_add_image) X(mkdir) X(unlink) X(remove) X(rename) X(chdir) X(fcntl)                 \
  X(pthread_mutex_init) X(pthread_mutex_destroy) X(pthread_mutex_lock) X(pthread_mutex_trylock)                 \
  X(pthread_mutex_unlock) X(pthread_mutexattr_init) X(pthread_mutexattr_settype) X(pthread_cond_init)           \
  X(pthread_cond_destroy) X(pthread_cond_signal) X(pthread_cond_broadcast) X(pthread_cond_wait)                 \
  X(pthread_cond_timedwait) X(pthread_condattr_init) X(pthread_attr_init) X(pthread_attr_setdetachstate)        \
  X(pthread_attr_setstacksize) X(pthread_attr_getschedparam) X(pthread_attr_getinheritsched)                    \
  X(pthread_getschedparam) X(pthread_setschedparam) X(sched_get_priority_max) X(sched_get_priority_min)         \
  X(pthread_key_create) X(pthread_key_delete) X(pthread_getspecific) X(pthread_setspecific)                     \
  X(pthread_setname_np) X(pthread_mach_thread_np) X(pthread_yield_np) X(semaphore_create)                       \
  X(semaphore_destroy) X(semaphore_signal) X(semaphore_wait) X(sem_open) X(sem_close) X(socket)                 \
  X(gethostbyname) X(getifaddrs) X(freeifaddrs) X(inet_addr) X(inet_ntoa)

#define X(n) extern char dw_##n;
DW_OWN(X)
#undef X
extern char dw__DefaultRuneLocale, dw___mb_cur_max, dw_mach_task_self_, dw_pthread_attr_noop, dw_net_refuse;

static const struct { const char *name; void *p; } own[] = {
#define X(n) {#n, &dw_##n},
    DW_OWN(X)
#undef X
    {"_DefaultRuneLocale", &dw__DefaultRuneLocale},
    {"___mb_cur_max", &dw___mb_cur_max},
    {"mach_task_self_", &dw_mach_task_self_},
    // scheduling attributes: accepted, the host's scheduling is used
    {"pthread_attr_setinheritsched", &dw_pthread_attr_noop},
    {"pthread_attr_setschedparam", &dw_pthread_attr_noop},
    {"pthread_attr_setschedpolicy", &dw_pthread_attr_noop},
    {"pthread_attr_setscope", &dw_pthread_attr_noop},
    // the network is down
    {"bind", &dw_net_refuse}, {"connect", &dw_net_refuse}, {"getpeername", &dw_net_refuse},
    {"getsockname", &dw_net_refuse}, {"getsockopt", &dw_net_refuse}, {"setsockopt", &dw_net_refuse},
    {"recv", &dw_net_refuse}, {"recvfrom", &dw_net_refuse}, {"send", &dw_net_refuse},
    {"sendto", &dw_net_refuse}, {"shutdown", &dw_net_refuse},
};

// --- the host's own, where the two systems agree on the arguments, the result and what they point at ----
// (numbers, strings, memory, FILE as an opaque pointer, struct tm, time_t, timeval for reading, pthread_t)
static const char *const same[] = {
    "acos", "acosf", "asin", "asinf", "atan", "atan2", "atan2f", "atanf", "ceil", "ceilf", "cos", "cosf", "cosh",
    "exp", "exp2", "exp2f", "expf", "floor", "floorf", "fmod", "fmodf", "frexp", "ldexp", "log", "log10", "log10f",
    "logf", "modf", "pow", "powf", "sin", "sinf", "sinh", "tan", "tanf", "tanh",
    "bzero", "memchr", "memcmp", "memcpy", "memmove", "memset", "strcasecmp", "strcat", "strchr", "strcmp", "strcoll",
    "strcpy", "strcspn", "strdup", "strerror", "strlen", "strncasecmp", "strncat", "strncmp", "strncpy", "strpbrk",
    "strrchr", "strspn", "strstr", "strtok_r", "__memcpy_chk", "__memmove_chk", "__memset_chk", "__strcat_chk",
    "__strcpy_chk",
    "abort", "atof", "atoi", "atoll", "calloc", "free", "malloc", "realloc", "qsort", "rand", "srand", "strtod",
    "strtol", "strtoll", "strtoul", "strtoull", "exit", "_Exit", "getenv", "system", "__cxa_atexit",
    "__stack_chk_fail", "__stack_chk_guard", "__udivti3", "__umodti3", "_Unwind_Resume",
    "fclose", "feof", "ferror", "fflush", "fgets", "fileno", "fputc", "fputs", "fread", "fseek", "fseeko", "ftell",
    "ftello", "fwrite", "putchar", "puts", "rewind", "setvbuf", "tmpnam",
    "clock", "difftime", "gmtime", "localtime", "localtime_r", "mktime", "time", "timegm", "strftime",
    "gettimeofday", "usleep", "localeconv", "mbtowc", "wctomb",
    "close", "read", "write", "getcwd", "getegid", "geteuid", "getgid", "getpid", "getuid", "poll",
    "pthread_attr_destroy", "pthread_condattr_destroy", "pthread_mutexattr_destroy", "pthread_create",
    "pthread_detach", "pthread_exit", "pthread_join", "pthread_self",
};

void *rc_host_symbol(const char *name) {
  for (size_t i = 0; i < sizeof own / sizeof *own; i++)
    if (!strcmp(own[i].name, name)) return own[i].p;
  if (!strcmp(name, "__stdinp")) return &stdin;
  if (!strcmp(name, "__stdoutp")) return &stdout;
  if (!strcmp(name, "__stderrp")) return &stderr;
  for (size_t i = 0; i < sizeof same / sizeof *same; i++)
    if (!strcmp(same[i], name)) return dlsym(RTLD_DEFAULT, name);
  // C++: the host's libstdc++ has the same ABI as the libstdc++ 4.2 the game was built with
  if (name[0] == '_' && name[1] == 'Z') return dlsym(RTLD_DEFAULT, name);
  if (!strncmp(name, "__cxa_", 6) || !strncmp(name, "__gxx_", 6) || !strcmp(name, "__dynamic_cast"))
    return dlsym(RTLD_DEFAULT, name);
  return 0;
}
