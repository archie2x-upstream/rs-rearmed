// The Darwin layer: pthreads, Mach and POSIX semaphores, and the network the game is refused (dw.h).
//
// A mutex or a condition variable of the game's is not used as the host's object in place: the game
// keeps some at addresses aligned for x86 only (a field of a packed structure), where the host's
// atomic operations fault on AArch64. The game's storage holds a tag and a pointer to a host object
// made for it instead. A statically initialized one has Darwin's signature in its first word; it gets
// its host object the first time it is used. Attribute objects hold no atomics and are used in place
// (Darwin's are at least as large as glibc's: attr 64 >= 64, mutexattr 16 >= 4, condattr 16 >= 4).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dw.h"

_Static_assert(DW_SIZEOF_PTHREAD_MUTEX >= 16 && DW_SIZEOF_PTHREAD_COND >= 16, "room for a tag and a pointer");
_Static_assert(sizeof(pthread_attr_t) <= DW_SIZEOF_PTHREAD_ATTR, "host attr fits in Darwin's");
_Static_assert(sizeof(pthread_mutexattr_t) <= DW_SIZEOF_PTHREAD_MUTEXATTR, "host mutexattr fits in Darwin's");
_Static_assert(sizeof(pthread_condattr_t) <= DW_SIZEOF_PTHREAD_CONDATTR, "host condattr fits in Darwin's");

#define TAG_MUTEX 0x21584554554d7764ull  // "dwMUTEX!"
#define TAG_COND 0x21444e4f43647764ull   // "dwdCOND!"

static pthread_mutex_t init_mu = PTHREAD_MUTEX_INITIALIZER;

// The two words of the game's storage, read and written bytewise: the address may be unaligned.
static uint64_t word(const void *p, int i) {
  uint64_t v;
  memcpy(&v, (const char *)p + 8 * i, 8);
  return v;
}
static void set_word(void *p, int i, uint64_t v) { memcpy((char *)p + 8 * i, &v, 8); }

static pthread_mutex_t *new_mutex(const pthread_mutexattr_t *attr, int type) {
  pthread_mutex_t *h = malloc(sizeof *h);
  if (!h) abort();
  if (attr) pthread_mutex_init(h, attr);
  else {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, type);
    pthread_mutex_init(h, &a);
    pthread_mutexattr_destroy(&a);
  }
  return h;
}
static pthread_mutex_t *mutex_of(void *m) {
  if (word(m, 0) == TAG_MUTEX) return (pthread_mutex_t *)(uintptr_t)word(m, 1);
  pthread_mutex_lock(&init_mu);
  uint64_t sig = word(m, 0);
  if (sig != TAG_MUTEX) {
    // Darwin's static initializers, or storage never initialized (taken as a normal mutex)
    int type = sig == DW_MUTEX_SIG_RECURSIVE ? PTHREAD_MUTEX_RECURSIVE
               : sig == DW_MUTEX_SIG_ERRORCHECK ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL;
    set_word(m, 1, (uint64_t)(uintptr_t)new_mutex(0, type));
    set_word(m, 0, TAG_MUTEX);
  }
  pthread_mutex_unlock(&init_mu);
  return (pthread_mutex_t *)(uintptr_t)word(m, 1);
}
static pthread_cond_t *cond_of(void *c) {
  if (word(c, 0) == TAG_COND) return (pthread_cond_t *)(uintptr_t)word(c, 1);
  pthread_mutex_lock(&init_mu);
  if (word(c, 0) != TAG_COND) {
    pthread_cond_t *h = malloc(sizeof *h);
    if (!h) abort();
    pthread_cond_init(h, 0);
    set_word(c, 1, (uint64_t)(uintptr_t)h);
    set_word(c, 0, TAG_COND);
  }
  pthread_mutex_unlock(&init_mu);
  return (pthread_cond_t *)(uintptr_t)word(c, 1);
}

int dw_pthread_mutex_init(void *m, const void *attr) {
  pthread_mutex_lock(&init_mu);
  set_word(m, 1, (uint64_t)(uintptr_t)new_mutex(attr, PTHREAD_MUTEX_NORMAL));
  set_word(m, 0, TAG_MUTEX);
  pthread_mutex_unlock(&init_mu);
  return 0;
}
int dw_pthread_mutex_destroy(void *m) {
  if (word(m, 0) != TAG_MUTEX) return 0;  // a static one never used
  pthread_mutex_t *h = (pthread_mutex_t *)(uintptr_t)word(m, 1);
  int r = pthread_mutex_destroy(h);
  if (r) return dw_errno_from_host(r);
  set_word(m, 0, 0);
  free(h);
  return 0;
}
int dw_pthread_mutex_lock(void *m) { return pthread_mutex_lock(mutex_of(m)); }
int dw_pthread_mutex_trylock(void *m) {
  int r = pthread_mutex_trylock(mutex_of(m));
  return r == EBUSY ? DW_EBUSY : dw_errno_from_host(r);
}
int dw_pthread_mutex_unlock(void *m) { return pthread_mutex_unlock(mutex_of(m)); }
int dw_pthread_mutexattr_init(void *a) {
  memset(a, 0, DW_SIZEOF_PTHREAD_MUTEXATTR);
  return pthread_mutexattr_init(a);
}
int dw_pthread_mutexattr_settype(void *a, int type) {
  int h = type == DW_PTHREAD_MUTEX_RECURSIVE    ? PTHREAD_MUTEX_RECURSIVE
          : type == DW_PTHREAD_MUTEX_ERRORCHECK ? PTHREAD_MUTEX_ERRORCHECK
                                                : PTHREAD_MUTEX_NORMAL;
  return pthread_mutexattr_settype(a, h);
}

int dw_pthread_cond_init(void *c, const void *attr) {
  pthread_cond_t *h = malloc(sizeof *h);
  if (!h) abort();
  pthread_cond_init(h, attr);
  pthread_mutex_lock(&init_mu);
  set_word(c, 1, (uint64_t)(uintptr_t)h);
  set_word(c, 0, TAG_COND);
  pthread_mutex_unlock(&init_mu);
  return 0;
}
int dw_pthread_cond_destroy(void *c) {
  if (word(c, 0) != TAG_COND) return 0;
  pthread_cond_t *h = (pthread_cond_t *)(uintptr_t)word(c, 1);
  int r = pthread_cond_destroy(h);
  if (r) return dw_errno_from_host(r);
  set_word(c, 0, 0);
  free(h);
  return 0;
}
int dw_pthread_cond_signal(void *c) { return pthread_cond_signal(cond_of(c)); }
int dw_pthread_cond_broadcast(void *c) { return pthread_cond_broadcast(cond_of(c)); }
int dw_pthread_cond_wait(void *c, void *m) { return pthread_cond_wait(cond_of(c), mutex_of(m)); }
int dw_pthread_cond_timedwait(void *c, void *m, const struct timespec *abstime) {
  return dw_errno_from_host(pthread_cond_timedwait(cond_of(c), mutex_of(m), abstime));
}
int dw_pthread_condattr_init(void *a) {
  memset(a, 0, DW_SIZEOF_PTHREAD_CONDATTR);
  return pthread_condattr_init(a);
}

// Thread attributes: the stack size and the detach state are honoured; scheduling is the host's.
int dw_pthread_attr_init(void *a) {
  memset(a, 0, DW_SIZEOF_PTHREAD_ATTR);
  return pthread_attr_init(a);
}
int dw_pthread_attr_setdetachstate(void *a, int s) {
  return pthread_attr_setdetachstate(a, s == DW_PTHREAD_CREATE_DETACHED ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE);
}
int dw_pthread_attr_setstacksize(void *a, size_t n) {
  long min = sysconf(_SC_THREAD_STACK_MIN);
  if (min > 0 && n < (size_t)min) n = (size_t)min;
  return pthread_attr_setstacksize(a, n);
}
int dw_pthread_attr_noop(void) { return 0; }
int dw_pthread_attr_getschedparam(const void *a, int *param) {
  (void)a;
  param[0] = 31;  // Darwin's default priority
  return 0;
}
int dw_pthread_attr_getinheritsched(const void *a, int *inherit) {
  (void)a;
  *inherit = 1;  // PTHREAD_INHERIT_SCHED
  return 0;
}
int dw_pthread_getschedparam(pthread_t t, int *policy, int *param) {
  (void)t;
  if (policy) *policy = DW_SCHED_OTHER;
  if (param) param[0] = 31;
  return 0;
}
int dw_pthread_setschedparam(pthread_t t, int policy, const int *param) {
  (void)t; (void)policy; (void)param;
  return 0;
}
int dw_sched_get_priority_max(int policy) { (void)policy; return 47; }  // Darwin's range, which the game computes within
int dw_sched_get_priority_min(int policy) { (void)policy; return 15; }

int dw_pthread_key_create(uint64_t *key, void (*dtor)(void *)) {
  pthread_key_t k;
  int r = pthread_key_create(&k, dtor);
  if (!r) *key = k;  // pthread_key_t is an unsigned long on Darwin
  return r;
}
int dw_pthread_key_delete(uint64_t key) { return pthread_key_delete((pthread_key_t)key); }
void *dw_pthread_getspecific(uint64_t key) { return pthread_getspecific((pthread_key_t)key); }
int dw_pthread_setspecific(uint64_t key, const void *v) { return pthread_setspecific((pthread_key_t)key, v); }

int dw_pthread_setname_np(const char *name) {
  char b[16];
  snprintf(b, sizeof b, "%s", name ? name : "");
  return pthread_setname_np(pthread_self(), b);
}
uint32_t dw_pthread_mach_thread_np(pthread_t t) {
  (void)t;
  return (uint32_t)gettid();
}
void dw_pthread_yield_np(void) { sched_yield(); }

// --- Mach semaphores: a handle (a port name on Darwin) to a host semaphore ----------------------------
#define NSEM 1024
static sem_t *sems[NSEM];
static pthread_mutex_t sem_mu = PTHREAD_MUTEX_INITIALIZER;

int dw_semaphore_create(uint32_t task, uint32_t *out, int policy, int value) {
  (void)task; (void)policy;
  sem_t *s = malloc(sizeof *s);
  if (!s || sem_init(s, 0, (unsigned)value)) { free(s); return 3; }  // KERN_NO_SPACE
  pthread_mutex_lock(&sem_mu);
  int k = 1;
  while (k < NSEM && sems[k]) k++;
  if (k < NSEM) sems[k] = s;
  pthread_mutex_unlock(&sem_mu);
  if (k == NSEM) { sem_destroy(s); free(s); return 3; }
  *out = (uint32_t)k;
  return 0;
}
static sem_t *sem_at(uint32_t h) { return h > 0 && h < NSEM ? sems[h] : 0; }
int dw_semaphore_destroy(uint32_t task, uint32_t h) {
  (void)task;
  pthread_mutex_lock(&sem_mu);
  sem_t *s = sem_at(h);
  if (s) sems[h] = 0;
  pthread_mutex_unlock(&sem_mu);
  if (!s) return 4;  // KERN_INVALID_ARGUMENT
  sem_destroy(s);
  free(s);
  return 0;
}
int dw_semaphore_signal(uint32_t h) {
  sem_t *s = sem_at(h);
  return s ? (sem_post(s), 0) : 4;
}
int dw_semaphore_wait(uint32_t h) {
  sem_t *s = sem_at(h);
  if (!s) return 4;
  while (sem_wait(s) && errno == EINTR) {}
  return 0;
}

// POSIX named semaphores. sem_open is variadic (mode, value follow O_CREAT); the universal thunk
// passes the integer registers, so they arrive as plain arguments.
void *dw_sem_open(const char *name, int oflag, unsigned mode, unsigned value) {
  sem_t *s = sem_open(name, dw_open_flags_to_host(oflag), (mode_t)(mode & 0777), value);
  return s == SEM_FAILED ? (void *)-1 : (void *)s;  // SEM_FAILED is (sem_t *)-1 on Darwin
}
int dw_sem_close(void *s) { return sem_close(s); }

// --- network: refused --------------------------------------------------------------------------------
// The game must never reach Ubisoft or Steam. On macOS a guard library refuses its connections; here
// the socket calls themselves say the network is down.
static int net_down(void) {
  errno = ENETDOWN;
  return -1;
}
int dw_socket(int d, int t, int p) { (void)d; (void)t; (void)p; return net_down(); }
int dw_net_refuse(void) { return net_down(); }
void *dw_gethostbyname(const char *name) { (void)name; return 0; }
int dw_getifaddrs(void **out) { *out = 0; return 0; }  // no interfaces
void dw_freeifaddrs(void *p) { (void)p; }
uint32_t dw_inet_addr(const char *s) {
  unsigned a, b, c, d;
  if (!s || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0xffffffffu;
  return a | b << 8 | c << 16 | d << 24;  // network byte order, as a little-endian word
}
char *dw_inet_ntoa(uint32_t in) {
  static __thread char b[16];
  snprintf(b, sizeof b, "%u.%u.%u.%u", in & 255, in >> 8 & 255, in >> 16 & 255, in >> 24);
  return b;
}
