// Recompiler runtime: the Mach-O definitions rc_image.c reads, for hosts whose system headers
// do not have <mach-o/loader.h> (Linux). Layouts and values are the Mach-O file format's; on
// macOS the system header is used instead and tools/ws7/recomp/test checks the two agree.
#ifndef RC_MACHO_H
#define RC_MACHO_H

#include <stdint.h>

#define MH_MAGIC_64 0xfeedfacfu
#define CPU_ARCH_ABI64 0x01000000
#define CPU_TYPE_X86 7
#define CPU_TYPE_X86_64 (CPU_TYPE_X86 | CPU_ARCH_ABI64)

struct mach_header_64 {
  uint32_t magic;
  int32_t cputype;
  int32_t cpusubtype;
  uint32_t filetype;
  uint32_t ncmds;
  uint32_t sizeofcmds;
  uint32_t flags;
  uint32_t reserved;
};

struct load_command {
  uint32_t cmd;
  uint32_t cmdsize;
};

#define LC_REQ_DYLD 0x80000000u
#define LC_LOAD_DYLIB 0xcu
#define LC_LOAD_WEAK_DYLIB (0x18u | LC_REQ_DYLD)
#define LC_SEGMENT_64 0x19u
#define LC_REEXPORT_DYLIB (0x1fu | LC_REQ_DYLD)
#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY (0x22u | LC_REQ_DYLD)
#define LC_LOAD_UPWARD_DYLIB (0x23u | LC_REQ_DYLD)

#define SEG_PAGEZERO "__PAGEZERO"

struct segment_command_64 {
  uint32_t cmd;
  uint32_t cmdsize;
  char segname[16];
  uint64_t vmaddr;
  uint64_t vmsize;
  uint64_t fileoff;
  uint64_t filesize;
  int32_t maxprot;
  int32_t initprot;
  uint32_t nsects;
  uint32_t flags;
};

struct section_64 {
  char sectname[16];
  char segname[16];
  uint64_t addr;
  uint64_t size;
  uint32_t offset;
  uint32_t align;
  uint32_t reloff;
  uint32_t nreloc;
  uint32_t flags;
  uint32_t reserved1;
  uint32_t reserved2;
  uint32_t reserved3;
};

#define SECTION_TYPE 0x000000ffu
#define S_MOD_INIT_FUNC_POINTERS 0x9u
#define S_MOD_TERM_FUNC_POINTERS 0xau

union lc_str {
  uint32_t offset;
};

struct dylib {
  union lc_str name;
  uint32_t timestamp;
  uint32_t current_version;
  uint32_t compatibility_version;
};

struct dylib_command {
  uint32_t cmd;
  uint32_t cmdsize;
  struct dylib dylib;
};

struct dyld_info_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t rebase_off;
  uint32_t rebase_size;
  uint32_t bind_off;
  uint32_t bind_size;
  uint32_t weak_bind_off;
  uint32_t weak_bind_size;
  uint32_t lazy_bind_off;
  uint32_t lazy_bind_size;
  uint32_t export_off;
  uint32_t export_size;
};

#define REBASE_OPCODE_MASK 0xF0
#define REBASE_IMMEDIATE_MASK 0x0F
#define REBASE_OPCODE_DONE 0x00
#define REBASE_OPCODE_SET_TYPE_IMM 0x10
#define REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x20
#define REBASE_OPCODE_ADD_ADDR_ULEB 0x30
#define REBASE_OPCODE_ADD_ADDR_IMM_SCALED 0x40
#define REBASE_OPCODE_DO_REBASE_IMM_TIMES 0x50
#define REBASE_OPCODE_DO_REBASE_ULEB_TIMES 0x60
#define REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB 0x70
#define REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB 0x80

#define BIND_OPCODE_MASK 0xF0
#define BIND_IMMEDIATE_MASK 0x0F
#define BIND_OPCODE_DONE 0x00
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM 0x10
#define BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB 0x20
#define BIND_OPCODE_SET_DYLIB_SPECIAL_IMM 0x30
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40
#define BIND_OPCODE_SET_TYPE_IMM 0x50
#define BIND_OPCODE_SET_ADDEND_SLEB 0x60
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70
#define BIND_OPCODE_ADD_ADDR_ULEB 0x80
#define BIND_OPCODE_DO_BIND 0x90
#define BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB 0xA0
#define BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED 0xB0
#define BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB 0xC0

#define EXPORT_SYMBOL_FLAGS_KIND_MASK 0x03
#define EXPORT_SYMBOL_FLAGS_KIND_REGULAR 0x00
#define EXPORT_SYMBOL_FLAGS_REEXPORT 0x08
#define EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER 0x10

#endif
