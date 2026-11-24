// The Darwin layer: libSystem functions whose arguments, results or data differ from the host's (dw.h).
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "dw.h"
#include "rc_file.h"

// --- errno ------------------------------------------------------------------------------------------
// The game reads and writes errno through __error(). It gets a per-thread Darwin value: a host error
// seen since the last call is translated into it (and the host's cleared, so the same error is seen
// again next time); what the game writes itself stays until then.
int dw_errno_from_host(int e) {
  switch (e) {
#define E(n) \
  case n:   \
    return DW_##n;
    E(EAGAIN) E(EINPROGRESS) E(EALREADY) E(ENOTSOCK) E(EDESTADDRREQ) E(EMSGSIZE) E(EPROTOTYPE) E(ENOPROTOOPT)
    E(EPROTONOSUPPORT) E(ESOCKTNOSUPPORT) E(EOPNOTSUPP) E(EPFNOSUPPORT) E(EAFNOSUPPORT) E(EADDRINUSE)
    E(EADDRNOTAVAIL) E(ENETDOWN) E(ENETUNREACH) E(ENETRESET) E(ECONNABORTED) E(ECONNRESET) E(ENOBUFS) E(EISCONN)
    E(ENOTCONN) E(ESHUTDOWN) E(ETOOMANYREFS) E(ETIMEDOUT) E(ECONNREFUSED) E(ELOOP) E(ENAMETOOLONG) E(EHOSTDOWN)
    E(EHOSTUNREACH) E(ENOTEMPTY) E(EUSERS) E(EDQUOT) E(ESTALE) E(EREMOTE) E(ENOLCK) E(ENOSYS) E(EOVERFLOW)
    E(ECANCELED) E(EIDRM) E(ENOMSG) E(EILSEQ) E(EBADMSG) E(EMULTIHOP) E(ENODATA) E(ENOLINK) E(ENOSR) E(ENOSTR)
    E(EPROTO) E(ETIME) E(ENOTRECOVERABLE) E(EOWNERDEAD) E(EDEADLK)
#undef E
  }
  return e;  // 1..34 are the same numbers on both, EDEADLK and EAGAIN apart
}

static __thread int dw_errno;

int *dw___error(void) {
  int h = errno;
  if (h) {
    dw_errno = dw_errno_from_host(h);
    errno = 0;
  }
  return &dw_errno;
}

// --- paths ------------------------------------------------------------------------------------------
// The Mac game's volume ignores letter case and the game relies on it. Each component that does not
// exist as written is looked up in its folder without regard to case.
const char *dw_path(const char *path, char *buf, size_t cap) {
  if (!path || !*path || access(path, F_OK) == 0) return path;
  size_t n = strlen(path);
  if (n + 1 > cap) return path;
  char out[PATH_MAX];
  size_t o = 0;
  const char *p = path;
  if (*p == '/') out[o++] = '/';
  while (*p == '/') p++;
  while (*p) {
    const char *e = strchr(p, '/');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (o + len + 2 > sizeof out) return path;
    out[o] = 0;
    char cand[PATH_MAX];
    snprintf(cand, sizeof cand, "%s%.*s", out, (int)len, p);
    const char *use = 0;
    char found[NAME_MAX + 1];
    if (access(cand, F_OK) == 0) use = p;
    else {
      DIR *d = opendir(o ? out : ".");
      if (d) {
        for (struct dirent *de; (de = readdir(d));)
          if (strlen(de->d_name) == len && !strncasecmp(de->d_name, p, len)) {
            snprintf(found, sizeof found, "%s", de->d_name);
            use = found;
            break;
          }
        closedir(d);
      }
    }
    if (!use) {  // nothing by that name: the rest is taken as written (a file about to be made)
      snprintf(buf, cap, "%s%s", out, p);
      return buf;
    }
    memcpy(out + o, use, len);
    o += len;
    p += len;
    if (*p == '/') {
      out[o++] = '/';
      while (*p == '/') p++;
    }
  }
  out[o] = 0;
  snprintf(buf, cap, "%s", out);
  return buf;
}

// --- files ------------------------------------------------------------------------------------------
int dw_open_flags_to_host(int f) {
  int h = f & DW_O_ACCMODE;  // O_RDONLY, O_WRONLY, O_RDWR: the same values
#define F(n) \
  if (f & DW_##n) h |= n;
  F(O_NONBLOCK) F(O_APPEND) F(O_SYNC) F(O_NOFOLLOW) F(O_CREAT) F(O_TRUNC) F(O_EXCL) F(O_NOCTTY) F(O_DIRECTORY)
  F(O_CLOEXEC)
#undef F
  return h;
}

void dw_stat_from_host(struct dw_stat *o, const void *host) {
  const struct stat *s = host;
  memset(o, 0, sizeof *o);
  o->st_dev = (int32_t)s->st_dev;
  o->st_mode = (uint16_t)s->st_mode;  // the S_IF* and permission bits are the same
  o->st_nlink = (uint16_t)(s->st_nlink > 0xffff ? 0xffff : s->st_nlink);
  o->st_ino = s->st_ino;
  o->st_uid = s->st_uid;
  o->st_gid = s->st_gid;
  o->st_rdev = (int32_t)s->st_rdev;
  o->st_atimespec = (struct dw_timespec){s->st_atim.tv_sec, s->st_atim.tv_nsec};
  o->st_mtimespec = (struct dw_timespec){s->st_mtim.tv_sec, s->st_mtim.tv_nsec};
  o->st_ctimespec = (struct dw_timespec){s->st_ctim.tv_sec, s->st_ctim.tv_nsec};
  o->st_birthtimespec = o->st_ctimespec;
  o->st_size = s->st_size;
  o->st_blocks = s->st_blocks;
  o->st_blksize = (int32_t)s->st_blksize;
}

// The file functions the thunks call directly (gen_thunks.py: FILE_FUNCS), in front of rc_file.c.
int rc_host_rc_file_open(const char *path, int flags, int mode) {
  char b[PATH_MAX];
  return rc_file_open(dw_path(path, b, sizeof b), dw_open_flags_to_host(flags), mode);
}
int rc_host_rc_file_close(int fd) { return rc_file_close(fd); }
ssize_t rc_host_rc_file_read(int fd, void *buf, size_t n) { return rc_file_read(fd, buf, n); }
int rc_host_rc_file_fstat(int fd, struct dw_stat *st) {
  struct stat h;
  int r = rc_file_fstat(fd, &h);
  if (!r) dw_stat_from_host(st, &h);
  return r;
}
int rc_host_rc_file_stat(const char *path, struct dw_stat *st) {
  char b[PATH_MAX];
  struct stat h;
  int r = rc_file_stat(dw_path(path, b, sizeof b), &h);
  if (!r) dw_stat_from_host(st, &h);
  return r;
}
FILE *rc_host_rc_file_fopen(const char *path, const char *mode) {
  char b[PATH_MAX];
  return rc_file_fopen(dw_path(path, b, sizeof b), mode);
}
DIR *rc_host_rc_file_opendir(const char *path) {
  char b[PATH_MAX];
  return rc_file_opendir(dw_path(path, b, sizeof b));
}
struct dw_dirent *rc_host_rc_file_readdir(DIR *d) {
  static __thread struct dw_dirent e;  // readdir's result is valid until the next call on the stream
  struct dirent *h = rc_file_readdir(d);
  if (!h) return 0;
  memset(&e, 0, offsetof(struct dw_dirent, d_name));
  e.d_ino = h->d_ino;
  e.d_type = h->d_type;  // the DT_* values are the same
  size_t n = strlen(h->d_name);
  if (n > sizeof e.d_name - 1) n = sizeof e.d_name - 1;
  memcpy(e.d_name, h->d_name, n);
  e.d_name[n] = 0;
  e.d_namlen = (uint16_t)n;
  e.d_reclen = sizeof e;
  return &e;
}
int rc_host_rc_file_closedir(DIR *d) { return rc_file_closedir(d); }

int dw_mkdir(const char *path, unsigned mode) {
  char b[PATH_MAX];
  return mkdir(dw_path(path, b, sizeof b), mode & 07777);
}
int dw_unlink(const char *path) {
  char b[PATH_MAX];
  return unlink(dw_path(path, b, sizeof b));
}
int dw_remove(const char *path) {
  char b[PATH_MAX];
  return remove(dw_path(path, b, sizeof b));
}
int dw_rename(const char *from, const char *to) {
  char a[PATH_MAX], b[PATH_MAX];
  return rename(dw_path(from, a, sizeof a), dw_path(to, b, sizeof b));
}
int dw_chdir(const char *path) {
  char b[PATH_MAX];
  return chdir(dw_path(path, b, sizeof b));
}

// fcntl: the commands the game can mean something by on this host. The third argument is an int for all of them.
int dw_fcntl(int fd, int cmd, long arg) {
  switch (cmd) {
    case DW_F_DUPFD: return fcntl(fd, F_DUPFD, (int)arg);
    case DW_F_GETFD: return fcntl(fd, F_GETFD);
    case DW_F_SETFD: return fcntl(fd, F_SETFD, (int)arg);  // FD_CLOEXEC is 1 on both
    case DW_F_GETFL: {
      int h = fcntl(fd, F_GETFL), d;
      if (h < 0) return h;
      d = h & O_ACCMODE;
      if (h & O_NONBLOCK) d |= DW_O_NONBLOCK;
      if (h & O_APPEND) d |= DW_O_APPEND;
      return d;
    }
    case DW_F_SETFL: return fcntl(fd, F_SETFL, dw_open_flags_to_host((int)arg) & ~O_ACCMODE);
    case DW_F_NOCACHE:
    case DW_F_RDAHEAD:
    case DW_F_FULLFSYNC: return 0;  // hints to the Mac's file cache
  }
  errno = EINVAL;
  return -1;
}

// --- variadic output and input -----------------------------------------------------------------------
// The thunks hand the v* functions a pointer to consecutive 8-byte argument slots, which is what a
// va_list is on Darwin arm64 (runtime/rc_va.c). An AArch64 Linux va_list is a structure: one whose
// register save areas are used up reads every argument from __stack, which is those slots.
#define DW_VA(ap, slots) \
  va_list ap;            \
  memset(&ap, 0, sizeof ap); \
  ap.__stack = (slots)

int rc_host_vprintf(const char *fmt, void *slots) { DW_VA(ap, slots); return vprintf(fmt, ap); }
int rc_host_vfprintf(FILE *f, const char *fmt, void *slots) { DW_VA(ap, slots); return vfprintf(f, fmt, ap); }
int rc_host_vsprintf(char *s, const char *fmt, void *slots) { DW_VA(ap, slots); return vsprintf(s, fmt, ap); }
int rc_host_vsnprintf(char *s, size_t n, const char *fmt, void *slots) { DW_VA(ap, slots); return vsnprintf(s, n, fmt, ap); }
int rc_host_vasprintf(char **s, const char *fmt, void *slots) { DW_VA(ap, slots); return vasprintf(s, fmt, ap); }
int rc_host_vsscanf(const char *s, const char *fmt, void *slots) { DW_VA(ap, slots); return vsscanf(s, fmt, ap); }
int rc_host___vsprintf_chk(char *s, int flag, size_t slen, const char *fmt, void *slots) {
  (void)flag;
  DW_VA(ap, slots);
  return slen == (size_t)-1 ? vsprintf(s, fmt, ap) : vsnprintf(s, slen, fmt, ap);
}
// NSLogv(format, args): the format is an NSString; there is no Foundation here yet, so the call is only noted.
void rc_host_NSLogv(void *format, void *slots) {
  (void)format; (void)slots;
  fputs("[dw] NSLog (not shown: no Foundation on this host)\n", stderr);
}
int rc_host_select(int n, void *r, void *w, void *e, void *timeout) { return select(n, r, w, e, timeout); }

// --- ctype -------------------------------------------------------------------------------------------
// <ctype.h>'s inline functions read _DefaultRuneLocale.__runetype[c] for c < 256 and call __maskrune,
// __tolower and __toupper for the rest. The C locale: only ASCII has a class.
struct dw_rune_locale dw__DefaultRuneLocale;
int dw___mb_cur_max = 1;

__attribute__((constructor)) static void dw_ctype_init(void) {
  struct dw_rune_locale *l = &dw__DefaultRuneLocale;
  memcpy(l->magic, "RuneMagA", 8);
  strcpy(l->encoding, "NONE");
  l->invalid_rune = 0xfffd;
  for (int c = 0; c < 256; c++) {
    uint32_t t = 0;
    if (c < 128) {
      if (isalpha(c)) t |= 0x00000100;   // _CTYPE_A
      if (iscntrl(c)) t |= 0x00000200;   // _CTYPE_C
      if (isdigit(c)) t |= 0x00000400 | (uint32_t)(c - '0');  // _CTYPE_D, and the digit's value
      if (isgraph(c)) t |= 0x00000800;   // _CTYPE_G
      if (islower(c)) t |= 0x00001000;   // _CTYPE_L
      if (ispunct(c)) t |= 0x00002000;   // _CTYPE_P
      if (isspace(c)) t |= 0x00004000;   // _CTYPE_S
      if (isupper(c)) t |= 0x00008000;   // _CTYPE_U
      if (isxdigit(c)) t |= 0x00010000 | (uint32_t)(isdigit(c) ? c - '0' : (c | 32) - 'a' + 10);  // _CTYPE_X
      if (c == ' ' || c == '\t') t |= 0x00020000;  // _CTYPE_B
      if (isprint(c)) t |= 0x00040000;   // _CTYPE_R
    }
    l->runetype[c] = t;
    l->maplower[c] = c < 128 ? tolower(c) : c;
    l->mapupper[c] = c < 128 ? toupper(c) : c;
  }
}
int dw___maskrune(int c, unsigned long f) {
  return (int)((c >= 0 && c < 256 ? dw__DefaultRuneLocale.runetype[c] : 0) & (uint32_t)f);
}
int dw___tolower(int c) { return c >= 0 && c < 256 ? dw__DefaultRuneLocale.maplower[c] : c; }
int dw___toupper(int c) { return c >= 0 && c < 256 ? dw__DefaultRuneLocale.mapupper[c] : c; }

// --- locale ------------------------------------------------------------------------------------------
char *dw_setlocale(int cat, const char *loc) {
  int h;
  switch (cat) {
    case DW_LC_ALL: h = LC_ALL; break;
    case DW_LC_COLLATE: h = LC_COLLATE; break;
    case DW_LC_CTYPE: h = LC_CTYPE; break;
    case DW_LC_MONETARY: h = LC_MONETARY; break;
    case DW_LC_NUMERIC: h = LC_NUMERIC; break;
    case DW_LC_TIME: h = LC_TIME; break;
    case DW_LC_MESSAGES: h = LC_MESSAGES; break;
    default: return 0;
  }
  return setlocale(h, loc);
}

// --- time, Mach, the system ---------------------------------------------------------------------------
uint64_t dw_mach_absolute_time(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
int dw_mach_timebase_info(uint32_t *info) {  // { numer, denom }: mach_absolute_time is in nanoseconds here
  info[0] = info[1] = 1;
  return 0;
}
uint32_t dw_mach_task_self_ = 0x103;
int dw_mach_port_type(uint32_t task, uint32_t name, uint32_t *type) {
  (void)task; (void)name; (void)type;
  return 5;  // KERN_FAILURE
}

float dw___exp10f(float x) { return exp10f(x); }
double dw___exp10(double x) { return exp10(x); }
struct dw_float2 { float sinval, cosval; };
struct dw_float2 dw___sincosf_stret(float x) {
  struct dw_float2 r;
  sincosf(x, &r.sinval, &r.cosval);
  return r;
}
void dw_memset_pattern16(void *b, const void *pattern, size_t len) {
  uint8_t *p = b;
  for (size_t i = 0; i < len; i++) p[i] = ((const uint8_t *)pattern)[i & 15];
}
void dw___bzero(void *p, size_t n) { memset(p, 0, n); }

int32_t dw_OSAtomicAdd32(int32_t amount, volatile int32_t *v) { return __atomic_add_fetch(v, amount, __ATOMIC_SEQ_CST); }
int32_t dw_OSAtomicIncrement32(volatile int32_t *v) { return __atomic_add_fetch(v, 1, __ATOMIC_SEQ_CST); }
int32_t dw_OSAtomicDecrement32(volatile int32_t *v) { return __atomic_sub_fetch(v, 1, __ATOMIC_SEQ_CST); }
_Bool dw_OSAtomicCompareAndSwap32(int32_t old, int32_t new, volatile int32_t *v) {
  return __atomic_compare_exchange_n(v, &old, new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
_Bool dw_OSAtomicCompareAndSwap64(int64_t old, int64_t new, volatile int64_t *v) {
  return __atomic_compare_exchange_n(v, &old, new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// sysctlbyname: the names a game asks for; anything else is ENOENT, and logged once per name asked.
int dw_sysctlbyname(const char *name, void *old, size_t *oldlen, void *new, size_t newlen) {
  (void)new; (void)newlen;
  uint64_t v;
  int is64 = 0;
  if (!strcmp(name, "hw.ncpu") || !strcmp(name, "hw.logicalcpu") || !strcmp(name, "hw.physicalcpu") ||
      !strcmp(name, "hw.activecpu") || !strcmp(name, "hw.logicalcpu_max") || !strcmp(name, "hw.physicalcpu_max"))
    v = (uint64_t)sysconf(_SC_NPROCESSORS_ONLN);
  else if (!strcmp(name, "hw.memsize")) {
    v = (uint64_t)sysconf(_SC_PHYS_PAGES) * (uint64_t)sysconf(_SC_PAGESIZE);
    is64 = 1;
  } else if (!strcmp(name, "hw.pagesize")) v = 4096;  // the x86 game's page
  else if (!strcmp(name, "hw.cpufrequency") || !strcmp(name, "hw.cpufrequency_max")) { v = 2400000000ull; is64 = 1; }
  else {
    fprintf(stderr, "[dw] sysctlbyname(%s): not known here\n", name);
    errno = ENOENT;
    return -1;
  }
  size_t want = is64 ? 8 : 4;
  if (old && oldlen) {
    if (*oldlen >= 8) { memcpy(old, &v, 8); want = 8; }
    else if (*oldlen >= 4) { uint32_t w = (uint32_t)v; memcpy(old, &w, 4); want = 4; }
    else { errno = ENOMEM; return -1; }
  }
  if (oldlen) *oldlen = want;
  return 0;
}
int dw_sysctl(int *mib, unsigned n, void *old, size_t *oldlen, void *new, size_t newlen) {
  (void)new; (void)newlen;
  // { CTL_HW (6), HW_NCPU (3) | HW_MEMSIZE (24) | HW_PAGESIZE (7) }
  if (n == 2 && mib[0] == 6) {
    if (mib[1] == 3) return dw_sysctlbyname("hw.ncpu", old, oldlen, 0, 0);
    if (mib[1] == 24) return dw_sysctlbyname("hw.memsize", old, oldlen, 0, 0);
    if (mib[1] == 7) return dw_sysctlbyname("hw.pagesize", old, oldlen, 0, 0);
  }
  fprintf(stderr, "[dw] sysctl({%d, %d}): not known here\n", n > 0 ? mib[0] : -1, n > 1 ? mib[1] : -1);
  errno = ENOENT;
  return -1;
}

// Signals: the game's handlers are for its own crash report. They are not installed.
int dw_sigaction(int sig, const void *act, void *old) {
  (void)sig; (void)act;
  if (old) memset(old, 0, 16);  // struct sigaction: { handler, sa_mask (4), sa_flags (4) }
  return 0;
}
void *dw_signal(int sig, void *handler) {
  (void)sig; (void)handler;
  return 0;  // SIG_DFL
}
int dw_tcgetattr(int fd, void *t) { (void)fd; (void)t; errno = ENOTTY; return -1; }
int dw_tcsetattr(int fd, int a, const void *t) { (void)fd; (void)a; (void)t; errno = ENOTTY; return -1; }

// dyld: the game asks to be told about loaded images (its crash reporter). Nothing is reported.
void dw__dyld_register_func_for_add_image(void *fn) { (void)fn; }
