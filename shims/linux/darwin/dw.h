// The Darwin layer of the Linux host: what the Mac game's libSystem imports are bound to when the
// host is not macOS. The game's code has Darwin's structure layouts and constants compiled in
// (dw_consts.h has them, generated from the macOS SDK); these functions take and return those and
// call the host's C library.
//
// rc_image.c asks rc_host_symbol() for every import it binds. A name is, in this order: one of this
// layer's own functions or data (dw_bind.c's table), a function whose ABI is the same on both systems
// (the pass-through list there: the host's own, by dlsym), a C++ symbol (the host's libstdc++), or
// unresolved. Unresolved imports are listed at load with RC_LOG_UNRESOLVED and stop the game where
// one is called, so a missing function is named rather than run with the wrong ABI.
#ifndef DW_H
#define DW_H

#include <stddef.h>
#include <stdint.h>

#include "dw_consts.h"

// --- structures, as Darwin x86-64 lays them out ------------------------------------------------------
struct dw_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

struct dw_stat {  // struct stat with 64-bit inodes (stat$INODE64)
  int32_t st_dev;
  uint16_t st_mode;
  uint16_t st_nlink;
  uint64_t st_ino;
  uint32_t st_uid;
  uint32_t st_gid;
  int32_t st_rdev;
  struct dw_timespec st_atimespec;
  struct dw_timespec st_mtimespec;
  struct dw_timespec st_ctimespec;
  struct dw_timespec st_birthtimespec;
  int64_t st_size;
  int64_t st_blocks;
  int32_t st_blksize;
  uint32_t st_flags;
  uint32_t st_gen;
  int32_t st_lspare;
  int64_t st_qspare[2];
};

struct dw_dirent {  // struct dirent with 64-bit inodes (readdir$INODE64)
  uint64_t d_ino;
  uint64_t d_seekoff;
  uint16_t d_reclen;
  uint16_t d_namlen;
  uint8_t d_type;
  char d_name[1024];
};

DW_LAYOUT_CHECKS;

// struct _RuneLocale up to the tables <ctype.h>'s inline functions read.
struct dw_rune_locale {
  char magic[8];
  char encoding[32];
  void *sgetrune;
  void *sputrune;
  int32_t invalid_rune;
  uint32_t runetype[256];
  int32_t maplower[256];
  int32_t mapupper[256];
  char rest[256];  // range tables and the variable part: empty in the C locale
};

// --- the layer --------------------------------------------------------------------------------------
// The host symbol an import is bound to, or NULL (dw_bind.c). `name` is the import's name without
// the Mach-O underscore.
void *rc_host_symbol(const char *name);

int dw_errno_from_host(int host);   // a host errno value as Darwin's
int dw_open_flags_to_host(int dw);  // open(2) flags
void dw_stat_from_host(struct dw_stat *out, const void *host_stat);

// A path as the game means it on a volume that ignores letter case: `path` if it exists, else the
// existing path that differs only by case, written into buf; else `path` itself.
const char *dw_path(const char *path, char *buf, size_t cap);

#endif
