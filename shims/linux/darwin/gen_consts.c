// Run on macOS: prints dw_consts.h, the Darwin values of the constants the game passes to libSystem.
// The Linux host's Darwin layer (this folder) translates them to the host's. Regenerate with gen_consts.sh.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define C(n) printf("#define DW_%s %d\n", #n, (int)(n));
#define X(n) printf("#define DW_%s 0x%llx\n", #n, (unsigned long long)(n));
#define O(t, f) printf("_Static_assert(offsetof(struct dw_" #t ", " #f ") == %zu, \"" #t "." #f "\");\n", offsetof(struct t, f));
#define S(t) printf("_Static_assert(sizeof(struct dw_" #t ") == %zu, \"sizeof " #t "\");\n", sizeof(struct t));

int main(void) {
  puts("// Generated on macOS by gen_consts.c from the SDK's headers: Darwin's values, as the game's code has them.");
  puts("// Do not edit; run shims/linux/darwin/gen_consts.sh.");
  puts("#ifndef DW_CONSTS_H\n#define DW_CONSTS_H\n");
  puts("// errno");
  C(EPERM) C(ENOENT) C(ESRCH) C(EINTR) C(EIO) C(ENXIO) C(E2BIG) C(ENOEXEC) C(EBADF) C(ECHILD) C(EDEADLK) C(ENOMEM)
  C(EACCES) C(EFAULT) C(ENOTBLK) C(EBUSY) C(EEXIST) C(EXDEV) C(ENODEV) C(ENOTDIR) C(EISDIR) C(EINVAL) C(ENFILE)
  C(EMFILE) C(ENOTTY) C(ETXTBSY) C(EFBIG) C(ENOSPC) C(ESPIPE) C(EROFS) C(EMLINK) C(EPIPE) C(EDOM) C(ERANGE)
  C(EAGAIN) C(EINPROGRESS) C(EALREADY) C(ENOTSOCK) C(EDESTADDRREQ) C(EMSGSIZE) C(EPROTOTYPE) C(ENOPROTOOPT)
  C(EPROTONOSUPPORT) C(ESOCKTNOSUPPORT) C(ENOTSUP) C(EPFNOSUPPORT) C(EAFNOSUPPORT) C(EADDRINUSE) C(EADDRNOTAVAIL)
  C(ENETDOWN) C(ENETUNREACH) C(ENETRESET) C(ECONNABORTED) C(ECONNRESET) C(ENOBUFS) C(EISCONN) C(ENOTCONN)
  C(ESHUTDOWN) C(ETOOMANYREFS) C(ETIMEDOUT) C(ECONNREFUSED) C(ELOOP) C(ENAMETOOLONG) C(EHOSTDOWN) C(EHOSTUNREACH)
  C(ENOTEMPTY) C(EUSERS) C(EDQUOT) C(ESTALE) C(EREMOTE) C(ENOLCK) C(ENOSYS) C(EOVERFLOW) C(ECANCELED) C(EIDRM)
  C(ENOMSG) C(EILSEQ) C(EBADMSG) C(EMULTIHOP) C(ENODATA) C(ENOLINK) C(ENOSR) C(ENOSTR) C(EPROTO) C(ETIME)
  C(EOPNOTSUPP) C(ENOTRECOVERABLE) C(EOWNERDEAD)
  puts("\n// open(2), fcntl(2)");
  X(O_RDONLY) X(O_WRONLY) X(O_RDWR) X(O_ACCMODE) X(O_NONBLOCK) X(O_APPEND) X(O_SYNC) X(O_NOFOLLOW) X(O_CREAT)
  X(O_TRUNC) X(O_EXCL) X(O_NOCTTY) X(O_DIRECTORY) X(O_CLOEXEC)
  C(F_DUPFD) C(F_GETFD) C(F_SETFD) C(F_GETFL) C(F_SETFL) C(F_NOCACHE) C(F_FULLFSYNC) C(F_RDAHEAD) C(F_GETPATH)
  puts("\n// mmap(2)");
  X(MAP_SHARED) X(MAP_PRIVATE) X(MAP_FIXED) X(MAP_ANON)
  puts("\n// setlocale(3)");
  C(LC_ALL) C(LC_COLLATE) C(LC_CTYPE) C(LC_MONETARY) C(LC_NUMERIC) C(LC_TIME) C(LC_MESSAGES)
  puts("\n// pthread");
  C(PTHREAD_CREATE_JOINABLE) C(PTHREAD_CREATE_DETACHED) C(PTHREAD_MUTEX_NORMAL) C(PTHREAD_MUTEX_ERRORCHECK)
  C(PTHREAD_MUTEX_RECURSIVE) C(SCHED_OTHER) C(SCHED_FIFO) C(SCHED_RR)
  {
    // the first word of the static initializers (their "signature")
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER, r = PTHREAD_RECURSIVE_MUTEX_INITIALIZER, e = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    printf("#define DW_MUTEX_SIG_INIT 0x%lxul\n#define DW_MUTEX_SIG_RECURSIVE 0x%lxul\n#define DW_MUTEX_SIG_ERRORCHECK 0x%lxul\n"
           "#define DW_COND_SIG_INIT 0x%lxul\n", *(unsigned long *)&m, *(unsigned long *)&r, *(unsigned long *)&e, *(unsigned long *)&c);
    printf("#define DW_SIZEOF_PTHREAD_MUTEX %zu\n#define DW_SIZEOF_PTHREAD_COND %zu\n#define DW_SIZEOF_PTHREAD_ATTR %zu\n"
           "#define DW_SIZEOF_PTHREAD_MUTEXATTR %zu\n#define DW_SIZEOF_PTHREAD_CONDATTR %zu\n#define DW_SIZEOF_PTHREAD_KEY %zu\n",
           sizeof(pthread_mutex_t), sizeof(pthread_cond_t), sizeof(pthread_attr_t), sizeof(pthread_mutexattr_t),
           sizeof(pthread_condattr_t), sizeof(pthread_key_t));
  }
  puts("\n// signals");
  C(SIGHUP) C(SIGINT) C(SIGQUIT) C(SIGILL) C(SIGTRAP) C(SIGABRT) C(SIGFPE) C(SIGKILL) C(SIGBUS) C(SIGSEGV)
  C(SIGSYS) C(SIGPIPE) C(SIGALRM) C(SIGTERM) C(SIGUSR1) C(SIGUSR2)
  puts("\n// layouts: checked where the structures are declared (dw.h), with these lines");
  puts("#define DW_LAYOUT_CHECKS \\");
#undef O
#undef S
#define O(t, d, f) printf("  _Static_assert(offsetof(struct " #d ", " #f ") == %zu, \"" #d "." #f "\"); \\\n", offsetof(struct t, f));
#define S(t, d) printf("  _Static_assert(sizeof(struct " #d ") == %zu, \"sizeof " #d "\"); \\\n", sizeof(struct t));
  O(stat, dw_stat, st_dev) O(stat, dw_stat, st_mode) O(stat, dw_stat, st_nlink) O(stat, dw_stat, st_ino)
  O(stat, dw_stat, st_uid) O(stat, dw_stat, st_gid) O(stat, dw_stat, st_rdev) O(stat, dw_stat, st_atimespec)
  O(stat, dw_stat, st_mtimespec) O(stat, dw_stat, st_ctimespec) O(stat, dw_stat, st_birthtimespec)
  O(stat, dw_stat, st_size) O(stat, dw_stat, st_blocks) O(stat, dw_stat, st_blksize) O(stat, dw_stat, st_flags)
  S(stat, dw_stat)
  O(dirent, dw_dirent, d_ino) O(dirent, dw_dirent, d_seekoff) O(dirent, dw_dirent, d_reclen)
  O(dirent, dw_dirent, d_namlen) O(dirent, dw_dirent, d_type) O(dirent, dw_dirent, d_name)
  S(dirent, dw_dirent)
  puts("  _Static_assert(1, \"\")");
  puts("\n#endif");
  return 0;
}
