// Generated on macOS by gen_consts.c from the SDK's headers: Darwin's values, as the game's code has them.
// Do not edit; run shims/linux/darwin/gen_consts.sh.
#ifndef DW_CONSTS_H
#define DW_CONSTS_H

// errno
#define DW_EPERM 1
#define DW_ENOENT 2
#define DW_ESRCH 3
#define DW_EINTR 4
#define DW_EIO 5
#define DW_ENXIO 6
#define DW_E2BIG 7
#define DW_ENOEXEC 8
#define DW_EBADF 9
#define DW_ECHILD 10
#define DW_EDEADLK 11
#define DW_ENOMEM 12
#define DW_EACCES 13
#define DW_EFAULT 14
#define DW_ENOTBLK 15
#define DW_EBUSY 16
#define DW_EEXIST 17
#define DW_EXDEV 18
#define DW_ENODEV 19
#define DW_ENOTDIR 20
#define DW_EISDIR 21
#define DW_EINVAL 22
#define DW_ENFILE 23
#define DW_EMFILE 24
#define DW_ENOTTY 25
#define DW_ETXTBSY 26
#define DW_EFBIG 27
#define DW_ENOSPC 28
#define DW_ESPIPE 29
#define DW_EROFS 30
#define DW_EMLINK 31
#define DW_EPIPE 32
#define DW_EDOM 33
#define DW_ERANGE 34
#define DW_EAGAIN 35
#define DW_EINPROGRESS 36
#define DW_EALREADY 37
#define DW_ENOTSOCK 38
#define DW_EDESTADDRREQ 39
#define DW_EMSGSIZE 40
#define DW_EPROTOTYPE 41
#define DW_ENOPROTOOPT 42
#define DW_EPROTONOSUPPORT 43
#define DW_ESOCKTNOSUPPORT 44
#define DW_ENOTSUP 45
#define DW_EPFNOSUPPORT 46
#define DW_EAFNOSUPPORT 47
#define DW_EADDRINUSE 48
#define DW_EADDRNOTAVAIL 49
#define DW_ENETDOWN 50
#define DW_ENETUNREACH 51
#define DW_ENETRESET 52
#define DW_ECONNABORTED 53
#define DW_ECONNRESET 54
#define DW_ENOBUFS 55
#define DW_EISCONN 56
#define DW_ENOTCONN 57
#define DW_ESHUTDOWN 58
#define DW_ETOOMANYREFS 59
#define DW_ETIMEDOUT 60
#define DW_ECONNREFUSED 61
#define DW_ELOOP 62
#define DW_ENAMETOOLONG 63
#define DW_EHOSTDOWN 64
#define DW_EHOSTUNREACH 65
#define DW_ENOTEMPTY 66
#define DW_EUSERS 68
#define DW_EDQUOT 69
#define DW_ESTALE 70
#define DW_EREMOTE 71
#define DW_ENOLCK 77
#define DW_ENOSYS 78
#define DW_EOVERFLOW 84
#define DW_ECANCELED 89
#define DW_EIDRM 90
#define DW_ENOMSG 91
#define DW_EILSEQ 92
#define DW_EBADMSG 94
#define DW_EMULTIHOP 95
#define DW_ENODATA 96
#define DW_ENOLINK 97
#define DW_ENOSR 98
#define DW_ENOSTR 99
#define DW_EPROTO 100
#define DW_ETIME 101
#define DW_EOPNOTSUPP 102
#define DW_ENOTRECOVERABLE 104
#define DW_EOWNERDEAD 105

// open(2), fcntl(2)
#define DW_O_RDONLY 0x0
#define DW_O_WRONLY 0x1
#define DW_O_RDWR 0x2
#define DW_O_ACCMODE 0x3
#define DW_O_NONBLOCK 0x4
#define DW_O_APPEND 0x8
#define DW_O_SYNC 0x80
#define DW_O_NOFOLLOW 0x100
#define DW_O_CREAT 0x200
#define DW_O_TRUNC 0x400
#define DW_O_EXCL 0x800
#define DW_O_NOCTTY 0x20000
#define DW_O_DIRECTORY 0x100000
#define DW_O_CLOEXEC 0x1000000
#define DW_F_DUPFD 0
#define DW_F_GETFD 1
#define DW_F_SETFD 2
#define DW_F_GETFL 3
#define DW_F_SETFL 4
#define DW_F_NOCACHE 48
#define DW_F_FULLFSYNC 51
#define DW_F_RDAHEAD 45
#define DW_F_GETPATH 50

// mmap(2)
#define DW_MAP_SHARED 0x1
#define DW_MAP_PRIVATE 0x2
#define DW_MAP_FIXED 0x10
#define DW_MAP_ANON 0x1000

// setlocale(3)
#define DW_LC_ALL 0
#define DW_LC_COLLATE 1
#define DW_LC_CTYPE 2
#define DW_LC_MONETARY 3
#define DW_LC_NUMERIC 4
#define DW_LC_TIME 5
#define DW_LC_MESSAGES 6

// pthread
#define DW_PTHREAD_CREATE_JOINABLE 1
#define DW_PTHREAD_CREATE_DETACHED 2
#define DW_PTHREAD_MUTEX_NORMAL 0
#define DW_PTHREAD_MUTEX_ERRORCHECK 1
#define DW_PTHREAD_MUTEX_RECURSIVE 2
#define DW_SCHED_OTHER 1
#define DW_SCHED_FIFO 4
#define DW_SCHED_RR 2
#define DW_MUTEX_SIG_INIT 0x32aaaba7ul
#define DW_MUTEX_SIG_RECURSIVE 0x32aaaba2ul
#define DW_MUTEX_SIG_ERRORCHECK 0x32aaaba1ul
#define DW_COND_SIG_INIT 0x3cb0b1bbul
#define DW_SIZEOF_PTHREAD_MUTEX 64
#define DW_SIZEOF_PTHREAD_COND 48
#define DW_SIZEOF_PTHREAD_ATTR 64
#define DW_SIZEOF_PTHREAD_MUTEXATTR 16
#define DW_SIZEOF_PTHREAD_CONDATTR 16
#define DW_SIZEOF_PTHREAD_KEY 8

// signals
#define DW_SIGHUP 1
#define DW_SIGINT 2
#define DW_SIGQUIT 3
#define DW_SIGILL 4
#define DW_SIGTRAP 5
#define DW_SIGABRT 6
#define DW_SIGFPE 8
#define DW_SIGKILL 9
#define DW_SIGBUS 10
#define DW_SIGSEGV 11
#define DW_SIGSYS 12
#define DW_SIGPIPE 13
#define DW_SIGALRM 14
#define DW_SIGTERM 15
#define DW_SIGUSR1 30
#define DW_SIGUSR2 31

// layouts: checked where the structures are declared (dw.h), with these lines
#define DW_LAYOUT_CHECKS \
  _Static_assert(offsetof(struct dw_stat, st_dev) == 0, "dw_stat.st_dev"); \
  _Static_assert(offsetof(struct dw_stat, st_mode) == 4, "dw_stat.st_mode"); \
  _Static_assert(offsetof(struct dw_stat, st_nlink) == 6, "dw_stat.st_nlink"); \
  _Static_assert(offsetof(struct dw_stat, st_ino) == 8, "dw_stat.st_ino"); \
  _Static_assert(offsetof(struct dw_stat, st_uid) == 16, "dw_stat.st_uid"); \
  _Static_assert(offsetof(struct dw_stat, st_gid) == 20, "dw_stat.st_gid"); \
  _Static_assert(offsetof(struct dw_stat, st_rdev) == 24, "dw_stat.st_rdev"); \
  _Static_assert(offsetof(struct dw_stat, st_atimespec) == 32, "dw_stat.st_atimespec"); \
  _Static_assert(offsetof(struct dw_stat, st_mtimespec) == 48, "dw_stat.st_mtimespec"); \
  _Static_assert(offsetof(struct dw_stat, st_ctimespec) == 64, "dw_stat.st_ctimespec"); \
  _Static_assert(offsetof(struct dw_stat, st_birthtimespec) == 80, "dw_stat.st_birthtimespec"); \
  _Static_assert(offsetof(struct dw_stat, st_size) == 96, "dw_stat.st_size"); \
  _Static_assert(offsetof(struct dw_stat, st_blocks) == 104, "dw_stat.st_blocks"); \
  _Static_assert(offsetof(struct dw_stat, st_blksize) == 112, "dw_stat.st_blksize"); \
  _Static_assert(offsetof(struct dw_stat, st_flags) == 116, "dw_stat.st_flags"); \
  _Static_assert(sizeof(struct dw_stat) == 144, "sizeof dw_stat"); \
  _Static_assert(offsetof(struct dw_dirent, d_ino) == 0, "dw_dirent.d_ino"); \
  _Static_assert(offsetof(struct dw_dirent, d_seekoff) == 8, "dw_dirent.d_seekoff"); \
  _Static_assert(offsetof(struct dw_dirent, d_reclen) == 16, "dw_dirent.d_reclen"); \
  _Static_assert(offsetof(struct dw_dirent, d_namlen) == 18, "dw_dirent.d_namlen"); \
  _Static_assert(offsetof(struct dw_dirent, d_type) == 20, "dw_dirent.d_type"); \
  _Static_assert(offsetof(struct dw_dirent, d_name) == 21, "dw_dirent.d_name"); \
  _Static_assert(sizeof(struct dw_dirent) == 1048, "sizeof dw_dirent"); \
  _Static_assert(1, "")

#endif
