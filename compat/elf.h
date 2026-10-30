/* Minimal elf.h for building pack_reloc on macOS */
#ifndef COMPAT_ELF_H
#define COMPAT_ELF_H
#include <stdint.h>

typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t Elf32_Sword;
typedef uint32_t Elf32_Word;

#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4

typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half e_type, e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff, e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct {
	Elf32_Word sh_name, sh_type, sh_flags;
	Elf32_Addr sh_addr;
	Elf32_Off sh_offset;
	Elf32_Word sh_size, sh_link, sh_info, sh_addralign, sh_entsize;
} Elf32_Shdr;

typedef struct { Elf32_Addr r_offset; Elf32_Word r_info; } Elf32_Rel;
typedef struct { Elf32_Addr r_offset; Elf32_Word r_info; Elf32_Sword r_addend; } Elf32_Rela;

#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))

#define SHT_RELA 4
#define SHT_REL 9
#define R_ARM_RELATIVE 23

#endif
