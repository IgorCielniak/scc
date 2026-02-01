#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>


void fail(char *msg)
{
  printf("sccas: %s\n", msg);
  exit(1);
}

enum { WORD_SIZE = 8 };
enum { ASM_BUF_SIZE = 4096 };
enum { IMM_KIND_LITERAL = 0, IMM_KIND_DATA = 1, IMM_KIND_LABEL = 2, IMM_KIND_BRANCH = 3 };
enum {
  LEA, IMM, JMP, JSR, BZ, BNZ, ENT, ADJ, LEV,
  LI, LC, SI, SC, PSH, OR, XOR, AND,
  EQ, NE, LT, GT, LE, GE, SHL, SHR,
  ADD, SUB, MUL, DIV, MOD,
  OPEN, READ, CLOS, WRIT, PRTF, MALC, FREE, MSET, MCMP,
  EXIT, FORKSYS, EXECVP, WAITPID, UNLINKSYS, GETPID
};

enum { BUFFER_DATA_INDEX = 0, BUFFER_LEN_INDEX = 1, BUFFER_CAP_INDEX = 2 };

void buffer_init(long *buf)
{
  buf[BUFFER_DATA_INDEX] = 0;
  buf[BUFFER_LEN_INDEX] = 0;
  buf[BUFFER_CAP_INDEX] = 0;
}

void buffer_reserve(long *buf, int need)
{
  int cap = (int)buf[BUFFER_CAP_INDEX];
  int len = (int)buf[BUFFER_LEN_INDEX];
  char *data = (char *)buf[BUFFER_DATA_INDEX];
  char *new_data;
  int new_cap;
  int i;
  if (cap >= need) return;
  new_cap = cap ? cap * 2 : 1024;
  while (new_cap < need) new_cap = new_cap * 2;
  new_data = (char *)malloc(new_cap);
  if (!new_data) fail("out of memory");
  i = 0;
  while (i < len) {
    new_data[i] = data ? data[i] : 0;
    ++i;
  }
  if (data) free(data);
  buf[BUFFER_DATA_INDEX] = (long)new_data;
  buf[BUFFER_CAP_INDEX] = new_cap;
}

void buffer_append_byte(long *buf, int value)
{
  int len = (int)buf[BUFFER_LEN_INDEX];
  char *data;
  if (len + 1 > (int)buf[BUFFER_CAP_INDEX]) buffer_reserve(buf, len + 1);
  data = (char *)buf[BUFFER_DATA_INDEX];
  data[len] = (char)(value & 255);
  buf[BUFFER_LEN_INDEX] = len + 1;
}

void buffer_append_bytes(long *buf, char *src, int count)
{
  int len;
  int i;
  char *data;
  if (count <= 0) return;
  len = (int)buf[BUFFER_LEN_INDEX];
  if (len + count > (int)buf[BUFFER_CAP_INDEX]) buffer_reserve(buf, len + count);
  data = (char *)buf[BUFFER_DATA_INDEX];
  i = 0;
  while (i < count) {
    data[len + i] = src[i];
    ++i;
  }
  buf[BUFFER_LEN_INDEX] = len + count;
}

void buffer_append_zeros(long *buf, int count)
{
  int len;
  int i;
  char *data;
  if (count <= 0) return;
  len = (int)buf[BUFFER_LEN_INDEX];
  if (len + count > (int)buf[BUFFER_CAP_INDEX]) buffer_reserve(buf, len + count);
  data = (char *)buf[BUFFER_DATA_INDEX];
  i = 0;
  while (i < count) {
    data[len + i] = 0;
    ++i;
  }
  buf[BUFFER_LEN_INDEX] = len + count;
}

long align_to(long value, long align)
{
  long mask;
  if (align <= 0) return value;
  mask = align - 1;
  return (value + mask) & ~mask;
}

void buffer_append_u16(long *buf, int value)
{
  buffer_append_byte(buf, value & 255);
  buffer_append_byte(buf, (value >> 8) & 255);
}

void buffer_append_u32(long *buf, int value)
{
  int i = 0;
  while (i < 4) {
    buffer_append_byte(buf, (value >> (i * 8)) & 255);
    ++i;
  }
}

void buffer_append_u64(long *buf, long value)
{
  int i = 0;
  while (i < 8) {
    buffer_append_byte(buf, (int)((value >> (i * 8)) & 255));
    ++i;
  }
}

void buffer_write_u32(long *buf, long offset, int value)
{
  int i = 0;
  int len = (int)buf[BUFFER_LEN_INDEX];
  char *data = (char *)buf[BUFFER_DATA_INDEX];
  if (offset < 0 || offset + 4 > len) fail("buffer write out of range");
  while (i < 4) {
    data[offset + i] = (char)((value >> (i * 8)) & 255);
    ++i;
  }
}

void buffer_write_u64(long *buf, long offset, long value)
{
  int i = 0;
  int len = (int)buf[BUFFER_LEN_INDEX];
  char *data = (char *)buf[BUFFER_DATA_INDEX];
  if (offset < 0 || offset + 8 > len) fail("buffer write out of range");
  while (i < 8) {
    data[offset + i] = (char)((value >> (i * 8)) & 255);
    ++i;
  }
}

void buffer_write_u16(long *buf, long offset, int value)
{
  int len = (int)buf[BUFFER_LEN_INDEX];
  char *data = (char *)buf[BUFFER_DATA_INDEX];
  if (offset < 0 || offset + 2 > len) fail("buffer write out of range");
  data[offset] = (char)(value & 255);
  data[offset + 1] = (char)((value >> 8) & 255);
}

int buffer_append_cstring(long *buf, char *str)
{
  int offset = (int)buf[BUFFER_LEN_INDEX];
  int i = 0;
  while (str && str[i]) {
    buffer_append_byte(buf, (int)(str[i] & 255));
    ++i;
  }
  buffer_append_byte(buf, 0);
  return offset;
}

void buffer_append_buffer(long *dst, long *src)
{
  int len = (int)src[BUFFER_LEN_INDEX];
  if (len <= 0) return;
  buffer_append_bytes(dst, (char *)src[BUFFER_DATA_INDEX], len);
}

void buffer_pad_to(long *buf, long new_len)
{
  long len = buf[BUFFER_LEN_INDEX];
  if (new_len <= len) return;
  buffer_append_zeros(buf, (int)(new_len - len));
}

enum { FIXUP_LIST_ITEMS_INDEX = 0, FIXUP_LIST_COUNT_INDEX = 1, FIXUP_LIST_CAP_INDEX = 2 };
enum { FIXUP_ENTRY_STRIDE = 4 };

enum {
  FIX_LABEL = 0,
  FIX_DATA = 1,
  FIX_GOT = 2,
  FIX_BSS = 3,
  FIX_SPECIAL = 4,
  FIX_GOT_ENTRY = 5
};

enum { RELOC_LIST_ITEMS_INDEX = 0, RELOC_LIST_COUNT_INDEX = 1, RELOC_LIST_CAP_INDEX = 2 };
enum { RELOC_ENTRY_STRIDE = 4 };

enum {
  R_X86_64_NONE = 0,
  R_X86_64_64 = 1,
  R_X86_64_PC32 = 2,
  R_X86_64_GOT32 = 3,
  R_X86_64_PLT32 = 4,
  R_X86_64_COPY = 5,
  R_X86_64_GLOB_DAT = 6,
  R_X86_64_JUMP_SLOT = 7,
  R_X86_64_RELATIVE = 8
};

int stack_slots;
int bss_size;
int entry_index;
int label_count;
int ins_count;
int data_size;

int *ins_op;
int *ins_kind;
long *ins_value;
char **labels;
char *data_bytes;
char *ins_seen;
char *data_seen;
long *label_offsets;

enum {
  SPECIAL_LABEL_EXIT = 0,
  SPECIAL_LABEL_COUNT
};

long *special_label_offsets;

enum {
  BSS_VM_STACK = 0,
  BSS_SLOT_COUNT
};

long *text_buffer;
long *data_buffer;
long *text_fixups;

enum {
  SYM_PRINTF = 0,
  SYM_MALLOC,
  SYM_FREE,
  SYM_MEMSET,
  SYM_MEMCMP,
  SYM_OPEN,
  SYM_READ,
  SYM_WRITE,
  SYM_CLOSE,
  SYM_EXIT,
  SYM_FORK,
  SYM_EXECVP,
  SYM_WAITPID,
  SYM_UNLINK,
  SYM_GETPID,
  SYM_COUNT
};

char *external_symbol_used;
long *external_stub_offsets;
long *external_got_offsets;
long *got_relocations;
long *bss_slot_offsets;
long total_bss_size;
long program_bss_offset;
long vm_stack_size_bytes;
int *external_dynsym_index;
char *interp_path;

char *get_external_symbol_name(int sym)
{
  if (sym == SYM_PRINTF) return "printf";
  if (sym == SYM_MALLOC) return "malloc";
  if (sym == SYM_FREE) return "free";
  if (sym == SYM_MEMSET) return "memset";
  if (sym == SYM_MEMCMP) return "memcmp";
  if (sym == SYM_OPEN) return "open";
  if (sym == SYM_READ) return "read";
  if (sym == SYM_WRITE) return "write";
  if (sym == SYM_CLOSE) return "close";
  if (sym == SYM_EXIT) return "exit";
  if (sym == SYM_FORK) return "fork";
  if (sym == SYM_EXECVP) return "execvp";
  if (sym == SYM_WAITPID) return "waitpid";
  if (sym == SYM_UNLINK) return "unlink";
  if (sym == SYM_GETPID) return "getpid";
  return 0;
}

void fixup_list_init(long *list)
{
  list[FIXUP_LIST_ITEMS_INDEX] = 0;
  list[FIXUP_LIST_COUNT_INDEX] = 0;
  list[FIXUP_LIST_CAP_INDEX] = 0;
}

void fixup_list_add(long *list, int offset, int type, int target, int addend)
{
  int count = (int)list[FIXUP_LIST_COUNT_INDEX];
  int cap = (int)list[FIXUP_LIST_CAP_INDEX];
  int need = count + 1;
  int new_cap;
  int *items;
  int *old_items;
  int i;
  int base;
  if (need > cap) {
    new_cap = cap ? cap * 2 : 64;
    items = (int *)malloc(sizeof(int) * new_cap * FIXUP_ENTRY_STRIDE);
    if (!items) fail("out of memory");
    old_items = (int *)list[FIXUP_LIST_ITEMS_INDEX];
    i = 0;
    while (i < count * FIXUP_ENTRY_STRIDE) {
      items[i] = old_items ? old_items[i] : 0;
      ++i;
    }
    if (old_items) free(old_items);
    list[FIXUP_LIST_ITEMS_INDEX] = (long)items;
    list[FIXUP_LIST_CAP_INDEX] = new_cap;
  }
  items = (int *)list[FIXUP_LIST_ITEMS_INDEX];
  base = count * FIXUP_ENTRY_STRIDE;
  items[base] = offset;
  items[base + 1] = type;
  items[base + 2] = target;
  items[base + 3] = addend;
  list[FIXUP_LIST_COUNT_INDEX] = count + 1;
  if (type == FIX_GOT && target >= 0 && target < SYM_COUNT) external_symbol_used[target] = 1;
}

char *asm_buf;
int asm_fd;
int asm_len;
char *hex_digits;

enum {
  REG_RAX = 0,
  REG_RCX = 1,
  REG_RDX = 2,
  REG_RBX = 3,
  REG_RSP = 4,
  REG_RBP = 5,
  REG_RSI = 6,
  REG_RDI = 7,
  REG_R8  = 8,
  REG_R9  = 9,
  REG_R10 = 10,
  REG_R11 = 11,
  REG_R12 = 12,
  REG_R13 = 13,
  REG_R14 = 14,
  REG_R15 = 15
};

int reg_rex_bit(int reg)
{
  return reg >= 8 ? 1 : 0;
}

int reg_low_bits(int reg)
{
  return reg & 7;
}

void emit_rex(long *buf, int w, int reg, int index, int base)
{
  int r_bit;
  int x_bit;
  int b_bit;
  int value;
  r_bit = reg >= 0 ? reg_rex_bit(reg) : 0;
  x_bit = index >= 0 ? reg_rex_bit(index) : 0;
  b_bit = base >= 0 ? reg_rex_bit(base) : 0;
  if (!w && !r_bit && !x_bit && !b_bit) return;
  value = 0x40;
  if (w) value = value | 0x08;
  if (r_bit) value = value | 0x04;
  if (x_bit) value = value | 0x02;
  if (b_bit) value = value | 0x01;
  buffer_append_byte(buf, value);
}

void emit_modrm(long *buf, int mod, int reg, int rm)
{
  int value = (mod << 6) | ((reg & 7) << 3) | (rm & 7);
  buffer_append_byte(buf, value);
}

void emit_disp8(long *buf, int disp)
{
  buffer_append_byte(buf, disp & 255);
}

void emit_disp32(long *buf, int disp)
{
  int i = 0;
  while (i < 4) {
    buffer_append_byte(buf, (disp >> (i * 8)) & 255);
    ++i;
  }
}

void emit_mem_operand(long *buf, int reg_field, int base_reg, int disp)
{
  int mod;
  int needs_disp32 = 0;
  int needs_disp8 = 0;
  int rm;
  if (disp == 0 && reg_low_bits(base_reg) != 5) mod = 0;
  else if (disp >= -128 && disp <= 127) { mod = 1; needs_disp8 = 1; }
  else { mod = 2; needs_disp32 = 1; }
  rm = reg_low_bits(base_reg);
  if (rm == 4) emit_modrm(buf, mod, reg_field, 4);
  else emit_modrm(buf, mod, reg_field, rm);
  if (rm == 4) {
    int sib = (0 << 6) | (4 << 3) | reg_low_bits(base_reg);
    buffer_append_byte(buf, sib);
  }
  if (mod == 0 && reg_low_bits(base_reg) == 5) {
    emit_disp32(buf, disp);
  }
  else if (needs_disp8) emit_disp8(buf, disp);
  else if (needs_disp32) emit_disp32(buf, disp);
}

void emit_rel32_placeholder(long *buf, long *fixups, int type, int target, int addend)
{
  int patch_offset = (int)buf[BUFFER_LEN_INDEX];
  emit_disp32(buf, 0);
  fixup_list_add(fixups, patch_offset, type, target, addend);
}

void emit_mov_reg_imm64(long *buf, int reg, long value)
{
  int i;
  emit_rex(buf, 1, -1, -1, reg);
  buffer_append_byte(buf, 0xB8 + reg_low_bits(reg));
  i = 0;
  while (i < 8) {
    buffer_append_byte(buf, (value >> (i * 8)) & 255);
    ++i;
  }
}

void emit_mov_reg_reg(long *buf, int dst, int src)
{
  emit_rex(buf, 1, dst, -1, src);
  buffer_append_byte(buf, 0x8B);
  emit_modrm(buf, 3, reg_low_bits(dst), reg_low_bits(src));
}

void emit_mov_reg_mem(long *buf, int dst, int base, int disp)
{
  emit_rex(buf, 1, dst, -1, base);
  buffer_append_byte(buf, 0x8B);
  emit_mem_operand(buf, reg_low_bits(dst), base, disp);
}

void emit_mov_mem_reg(long *buf, int base, int disp, int src)
{
  emit_rex(buf, 1, src, -1, base);
  buffer_append_byte(buf, 0x89);
  emit_mem_operand(buf, reg_low_bits(src), base, disp);
}

void emit_movzx_rax_al(long *buf)
{
  emit_rex(buf, 1, REG_RAX, -1, REG_RAX);
  buffer_append_byte(buf, 0x0F);
  buffer_append_byte(buf, 0xB6);
  emit_modrm(buf, 3, reg_low_bits(REG_RAX), reg_low_bits(REG_RAX));
}

void emit_mov_mem8_reg8(long *buf, int base, int disp, int src)
{
  emit_rex(buf, 0, src, -1, base);
  buffer_append_byte(buf, 0x88);
  emit_mem_operand(buf, reg_low_bits(src), base, disp);
}

void emit_mov_reg8_mem8(long *buf, int dst, int base, int disp)
{
  emit_rex(buf, 0, dst, -1, base);
  buffer_append_byte(buf, 0x8A);
  emit_mem_operand(buf, reg_low_bits(dst), base, disp);
}

void emit_movsx_eax_mem8(long *buf, int base, int disp)
{
  emit_rex(buf, 1, REG_RAX, -1, base);
  buffer_append_byte(buf, 0x0F);
  buffer_append_byte(buf, 0xBE);
  emit_mem_operand(buf, reg_low_bits(REG_RAX), base, disp);
}

void emit_add_reg_imm8(long *buf, int reg, int value)
{
  emit_rex(buf, 1, -1, -1, reg);
  buffer_append_byte(buf, 0x83);
  emit_modrm(buf, 3, 0, reg_low_bits(reg));
  emit_disp8(buf, value);
}

void emit_sub_reg_imm8(long *buf, int reg, int value)
{
  emit_rex(buf, 1, -1, -1, reg);
  buffer_append_byte(buf, 0x83);
  emit_modrm(buf, 3, 5, reg_low_bits(reg));
  emit_disp8(buf, value);
}

void emit_lea_reg_base_disp(long *buf, int dst, int base, int disp)
{
  emit_rex(buf, 1, dst, -1, base);
  buffer_append_byte(buf, 0x8D);
  emit_mem_operand(buf, reg_low_bits(dst), base, disp);
}

void emit_lea_rip_relative(long *buf, long *fixups, int dst, int type, int target, int addend)
{
  emit_rex(buf, 1, dst, -1, -1);
  buffer_append_byte(buf, 0x8D);
  emit_modrm(buf, 0, reg_low_bits(dst), 5);
  emit_rel32_placeholder(buf, fixups, type, target, addend);
}

void emit_mov_rip_relative(long *buf, long *fixups, int dst, int type, int target, int addend)
{
  emit_rex(buf, 1, dst, -1, -1);
  buffer_append_byte(buf, 0x8B);
  emit_modrm(buf, 0, reg_low_bits(dst), 5);
  emit_rel32_placeholder(buf, fixups, type, target, addend);
}

void emit_add_reg_imm(long *buf, int reg, long value)
{
  int use_disp8 = value >= -128 && value <= 127;
  emit_rex(buf, 1, -1, -1, reg);
  if (use_disp8) {
    buffer_append_byte(buf, 0x83);
    emit_modrm(buf, 3, 0, reg_low_bits(reg));
    emit_disp8(buf, (int)value);
  }
  else {
    buffer_append_byte(buf, 0x81);
    emit_modrm(buf, 3, 0, reg_low_bits(reg));
    emit_disp32(buf, (int)value);
  }
}

void emit_sub_reg_imm(long *buf, int reg, long value)
{
  int use_disp8 = value >= -128 && value <= 127;
  emit_rex(buf, 1, -1, -1, reg);
  if (use_disp8) {
    buffer_append_byte(buf, 0x83);
    emit_modrm(buf, 3, 5, reg_low_bits(reg));
    emit_disp8(buf, (int)value);
  }
  else {
    buffer_append_byte(buf, 0x81);
    emit_modrm(buf, 3, 5, reg_low_bits(reg));
    emit_disp32(buf, (int)value);
  }
}

void emit_jmp_rel32(long *buf, long *fixups, int type, int target, int addend)
{
  buffer_append_byte(buf, 0xE9);
  emit_rel32_placeholder(buf, fixups, type, target, addend);
}

void emit_call_rel32(long *buf, long *fixups, int type, int target, int addend)
{
  buffer_append_byte(buf, 0xE8);
  emit_rel32_placeholder(buf, fixups, type, target, addend);
}

void emit_jcc(long *buf, long *fixups, int opcode, int type, int target, int addend)
{
  buffer_append_byte(buf, 0x0F);
  buffer_append_byte(buf, opcode);
  emit_rel32_placeholder(buf, fixups, type, target, addend);
}

void emit_jmp_rip_relative_mem(long *buf, long *fixups, int type, int target, int addend)
{
  buffer_append_byte(buf, 0xFF);
  emit_modrm(buf, 0, 4, 5);
  emit_rel32_placeholder(buf, fixups, type, target, addend);
}

void emit_binop_rr(long *buf, int opcode, int dst, int src)
{
  emit_rex(buf, 1, dst, -1, src);
  buffer_append_byte(buf, opcode);
  emit_modrm(buf, 3, reg_low_bits(dst), reg_low_bits(src));
}

void emit_cmp_rr(long *buf, int left, int right)
{
  emit_rex(buf, 1, right, -1, left);
  buffer_append_byte(buf, 0x39);
  emit_modrm(buf, 3, reg_low_bits(right), reg_low_bits(left));
}

void emit_test_rr(long *buf, int left, int right)
{
  emit_rex(buf, 1, right, -1, left);
  buffer_append_byte(buf, 0x85);
  emit_modrm(buf, 3, reg_low_bits(right), reg_low_bits(left));
}

void emit_setcc(long *buf, int code)
{
  buffer_append_byte(buf, 0x0F);
  buffer_append_byte(buf, code);
  emit_modrm(buf, 3, 0, reg_low_bits(REG_RAX));
}

void emit_movzx_rax_al_reg(long *buf)
{
  emit_rex(buf, 1, REG_RAX, -1, REG_RAX);
  buffer_append_byte(buf, 0x0F);
  buffer_append_byte(buf, 0xB6);
  emit_modrm(buf, 3, reg_low_bits(REG_RAX), reg_low_bits(REG_RAX));
}

void emit_mov_reg_from_reg(long *buf, int dst, int src)
{
  emit_rex(buf, 1, dst, -1, src);
  buffer_append_byte(buf, 0x89);
  emit_modrm(buf, 3, reg_low_bits(src), reg_low_bits(dst));
}

void emit_imul_rr(long *buf, int dst, int src)
{
  emit_rex(buf, 1, dst, -1, src);
  buffer_append_byte(buf, 0x0F);
  buffer_append_byte(buf, 0xAF);
  emit_modrm(buf, 3, reg_low_bits(dst), reg_low_bits(src));
}

void emit_idiv_reg(long *buf, int reg)
{
  emit_rex(buf, 1, -1, -1, reg);
  buffer_append_byte(buf, 0xF7);
  emit_modrm(buf, 3, 7, reg_low_bits(reg));
}

void emit_shift_cl(long *buf, int subcode, int reg)
{
  emit_rex(buf, 1, -1, -1, reg);
  buffer_append_byte(buf, 0xD3);
  emit_modrm(buf, 3, subcode, reg_low_bits(reg));
}

void emit_push_reg(long *buf, int reg)
{
  if (reg >= REG_R8) {
    emit_rex(buf, 0, -1, -1, reg);
    buffer_append_byte(buf, 0x50 + reg_low_bits(reg));
  }
  else buffer_append_byte(buf, 0x50 + reg_low_bits(reg));
}

void emit_pop_reg(long *buf, int reg)
{
  if (reg >= REG_R8) {
    emit_rex(buf, 0, -1, -1, reg);
    buffer_append_byte(buf, 0x58 + reg_low_bits(reg));
  }
  else buffer_append_byte(buf, 0x58 + reg_low_bits(reg));
}

void emit_call_reg(long *buf, int reg)
{
  emit_rex(buf, 0, -1, -1, reg);
  buffer_append_byte(buf, 0xFF);
  emit_modrm(buf, 3, 2, reg_low_bits(reg));
}

void emit_jmp_reg(long *buf, int reg)
{
  emit_rex(buf, 0, -1, -1, reg);
  buffer_append_byte(buf, 0xFF);
  emit_modrm(buf, 3, 4, reg_low_bits(reg));
}

void emit_ret(long *buf)
{
  buffer_append_byte(buf, 0xC3);
}

void emit_cqo(long *buf)
{
  buffer_append_byte(buf, 0x48);
  buffer_append_byte(buf, 0x99);
}

void reloc_list_init(long *list)
{
  list[RELOC_LIST_ITEMS_INDEX] = 0;
  list[RELOC_LIST_COUNT_INDEX] = 0;
  list[RELOC_LIST_CAP_INDEX] = 0;
}

void reloc_list_add(long *list, long offset, int symbol, int type, long addend)
{
  int count = (int)list[RELOC_LIST_COUNT_INDEX];
  int cap = (int)list[RELOC_LIST_CAP_INDEX];
  int need = count + 1;
  int new_cap;
  long *items;
  long *old_items;
  int i;
  int base;
  if (need > cap) {
    new_cap = cap ? cap * 2 : 32;
    items = (long *)malloc(sizeof(long) * new_cap * RELOC_ENTRY_STRIDE);
    if (!items) fail("out of memory");
    old_items = (long *)list[RELOC_LIST_ITEMS_INDEX];
    i = 0;
    while (i < count * RELOC_ENTRY_STRIDE) {
      items[i] = old_items ? old_items[i] : 0;
      ++i;
    }
    if (old_items) free(old_items);
    list[RELOC_LIST_ITEMS_INDEX] = (long)items;
    list[RELOC_LIST_CAP_INDEX] = new_cap;
  }
  items = (long *)list[RELOC_LIST_ITEMS_INDEX];
  base = count * RELOC_ENTRY_STRIDE;
  items[base] = offset;
  items[base + 1] = symbol;
  items[base + 2] = type;
  items[base + 3] = addend;
  list[RELOC_LIST_COUNT_INDEX] = count + 1;
}

int str_len(char *s)
{
  int n = 0;
  if (!s) return 0;
  while (s[n]) ++n;
  return n;
}

void copy_bytes(char *dst, char *src, int count)
{
  int i = 0;
  while (i < count) {
    dst[i] = src[i];
    ++i;
  }
}

char *dup_range(char *src, int len)
{
  char *dst = malloc(len + 1);
  if (!dst) fail("out of memory");
  copy_bytes(dst, src, len);
  dst[len] = 0;
  return dst;
}

char *dup_string(char *src)
{
  return dup_range(src, str_len(src));
}

int str_eq(char *a, char *b)
{
  int i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    ++i;
  }
  return a[i] == 0 && b[i] == 0;
}

int starts_with(char *s, char *prefix)
{
  int i = 0;
  while (prefix[i]) {
    if (s[i] != prefix[i]) return 0;
    ++i;
  }
  return 1;
}

char *next_token(char **cursor)
{
  char *p = *cursor;
  char *start;
  while (*p == ' ' || *p == '\t') ++p;
  if (!*p) {
    *cursor = p;
    return 0;
  }
  start = p;
  while (*p && *p != ' ' && *p != '\t') ++p;
  if (*p) {
    *p = 0;
    ++p;
  }
  while (*p == ' ' || *p == '\t') ++p;
  *cursor = p;
  return start;
}

long parse_number_from_string(char *tok, char *ctx)
{
  long value = 0;
  int neg = 0;
  int i = 0;
  if (!tok || !tok[0]) fail(ctx);
  if (tok[0] == '-') { neg = 1; ++i; }
  if (!tok[i]) fail(ctx);
  while (tok[i]) {
    if (tok[i] < '0' || tok[i] > '9') fail(ctx);
    value = value * 10 + (tok[i] - '0');
    ++i;
  }
  if (neg) value = -value;
  return value;
}

long parse_number_token(char **cursor, char *ctx)
{
  char *tok = next_token(cursor);
  if (!tok) fail(ctx);
  return parse_number_from_string(tok, ctx);
}

void ensure_label_storage()
{
  int i;
  if (!labels) {
    label_count = ins_count;
    labels = malloc(sizeof(char *) * label_count);
    if (!labels) fail("out of memory");
    i = 0;
    while (i < label_count) { labels[i] = 0; ++i; }
  }
  if (label_count < ins_count) {
    char **new_labels = malloc(sizeof(char *) * ins_count);
    if (!new_labels) fail("out of memory");
    i = 0;
    while (i < ins_count) {
      if (i < label_count) new_labels[i] = labels[i];
      else new_labels[i] = 0;
      ++i;
    }
    free(labels);
    labels = new_labels;
    label_count = ins_count;
  }
}

char *make_numeric_label(int index)
{
  int digits = 1;
  int n = index;
  int len;
  char *buf;
  if (n < 0) n = 0;
  if (n == 0) digits = 1;
  else {
    digits = 0;
    while (n > 0) { ++digits; n = n / 10; }
  }
  len = digits + 1;
  buf = malloc(len + 1);
  if (!buf) fail("out of memory");
  buf[0] = 'L';
  buf[len] = 0;
  n = index;
  if (n < 0) n = 0;
  if (n == 0) {
    buf[1] = '0';
    buf[2] = 0;
    return buf;
  }
  while (digits > 0) {
    buf[digits] = (char)('0' + (n % 10));
    n = n / 10;
    --digits;
  }
  return buf;
}

char *make_label_with_suffix(char *base, int idx)
{
  char *suffix = make_numeric_label(idx);
  int base_len = str_len(base);
  int suffix_len = str_len(suffix);
  int total = base_len + suffix_len + 2;
  char *buf = malloc(total);
  int i = 0;
  int j;
  if (!buf) fail("out of memory");
  while (i < base_len) {
    buf[i] = base[i];
    ++i;
  }
  buf[i] = '_';
  ++i;
  j = 0;
  while (j <= suffix_len) {
    buf[i + j] = suffix[j];
    ++j;
  }
  free(suffix);
  return buf;
}

void ensure_labels_ready()
{
  int i;
  ensure_label_storage();
  i = 0;
  while (i < label_count) {
    if (!labels[i]) labels[i] = make_numeric_label(i);
    ++i;
  }
}

void dedupe_labels()
{
  int i = 0;
  while (i < label_count) {
    int j = i + 1;
    while (j < label_count) {
      if (str_eq(labels[i], labels[j])) {
        char *new_name = make_label_with_suffix(labels[j], j);
        free(labels[j]);
        labels[j] = new_name;
      }
      ++j;
    }
    ++i;
  }
}

void init_bss_layout()
{
  long offset = 0;
  if (!bss_slot_offsets) {
    bss_slot_offsets = (long *)malloc(sizeof(long) * BSS_SLOT_COUNT);
    if (!bss_slot_offsets) fail("out of memory");
  }
  vm_stack_size_bytes = (long)stack_slots * WORD_SIZE;
  offset = align_to(offset, 16);
  bss_slot_offsets[BSS_VM_STACK] = offset;
  offset = offset + vm_stack_size_bytes;
  if (bss_size > 0) {
    offset = align_to(offset, 16);
    program_bss_offset = offset;
    offset = offset + bss_size;
  }
  else program_bss_offset = -1;
  total_bss_size = offset;
}

long get_bss_slot_offset(int slot)
{
  if (slot < 0 || slot >= BSS_SLOT_COUNT) return -1;
  return bss_slot_offsets[slot];
}

void init_external_symbol_state()
{
  int i = 0;
  if (!external_symbol_used) {
    external_symbol_used = (char *)malloc(SYM_COUNT);
    if (!external_symbol_used) fail("out of memory");
  }
  if (!external_stub_offsets) {
    external_stub_offsets = (long *)malloc(sizeof(long) * SYM_COUNT);
    if (!external_stub_offsets) fail("out of memory");
  }
  if (!external_got_offsets) {
    external_got_offsets = (long *)malloc(sizeof(long) * SYM_COUNT);
    if (!external_got_offsets) fail("out of memory");
  }
  if (!external_dynsym_index) {
    external_dynsym_index = (int *)malloc(sizeof(int) * SYM_COUNT);
    if (!external_dynsym_index) fail("out of memory");
  }
  if (!got_relocations) {
    got_relocations = (long *)malloc(sizeof(long) * 3);
    if (!got_relocations) fail("out of memory");
  }
  while (i < SYM_COUNT) {
    external_symbol_used[i] = 0;
    external_stub_offsets[i] = -1;
    external_got_offsets[i] = -1;
    external_dynsym_index[i] = -1;
    ++i;
  }
  reloc_list_init(got_relocations);
}

void init_label_offsets()
{
  int i = 0;
  if (label_count <= 0) {
    label_offsets = 0;
    return;
  }
  label_offsets = malloc(sizeof(long) * label_count);
  if (!label_offsets) fail("out of memory");
  while (i < label_count) {
    label_offsets[i] = -1;
    ++i;
  }
}

void record_label_offset(int idx, long offset)
{
  if (idx < 0 || idx >= label_count) fail("label offset out of range");
  label_offsets[idx] = offset;
}

long get_label_offset(int idx)
{
  if (!label_offsets) return -1;
  if (idx < 0 || idx >= label_count) return -1;
  return label_offsets[idx];
}

void init_special_label_offsets()
{
  int i = 0;
  if (!special_label_offsets) {
    special_label_offsets = (long *)malloc(sizeof(long) * SPECIAL_LABEL_COUNT);
    if (!special_label_offsets) fail("out of memory");
  }
  while (i < SPECIAL_LABEL_COUNT) {
    special_label_offsets[i] = -1;
    ++i;
  }
}

void record_special_label_offset(int which, long offset)
{
  if (which < 0 || which >= SPECIAL_LABEL_COUNT) fail("special label index out of range");
  special_label_offsets[which] = offset;
}

long get_special_label_offset(int which)
{
  if (which < 0 || which >= SPECIAL_LABEL_COUNT) return -1;
  return special_label_offsets[which];
}

void apply_local_fixups(long *buf, long *list)
{
  int i = 0;
  int count = (int)list[FIXUP_LIST_COUNT_INDEX];
  int *items = (int *)list[FIXUP_LIST_ITEMS_INDEX];
  while (i < count) {
    int base = i * FIXUP_ENTRY_STRIDE;
    int type = items[base + 1];
    long target_offset = -1;
    if (type == FIX_LABEL) target_offset = get_label_offset(items[base + 2]);
    else if (type == FIX_SPECIAL) target_offset = get_special_label_offset(items[base + 2]);
    else if (type == FIX_GOT) {
      if (items[base + 2] < 0 || items[base + 2] >= SYM_COUNT) fail("invalid external symbol fixup");
      target_offset = external_stub_offsets[items[base + 2]];
    }
    if (target_offset >= 0) {
      long value = (target_offset + items[base + 3]) - (items[base] + 4);
      int j = 0;
      while (j < 4) {
        char *data = (char *)buf[BUFFER_DATA_INDEX];
        data[items[base] + j] = (char)((value >> (j * 8)) & 255);
        ++j;
      }
      items[base + 1] = -1;
    }
    ++i;
  }
}

void apply_remaining_fixups(long *buf, long *list, long text_base, long data_base, long bss_base)
{
  int i = 0;
  int count = (int)list[FIXUP_LIST_COUNT_INDEX];
  int *items = (int *)list[FIXUP_LIST_ITEMS_INDEX];
  char *data = (char *)buf[BUFFER_DATA_INDEX];
  while (i < count) {
    int base = i * FIXUP_ENTRY_STRIDE;
    int type = items[base + 1];
    long target_addr = -1;
    if (type != -1) {
      if (type == FIX_DATA) target_addr = data_base + items[base + 2];
      else if (type == FIX_BSS) {
        long slot_offset = get_bss_slot_offset(items[base + 2]);
        if (slot_offset < 0) fail("unknown BSS slot");
        target_addr = bss_base + slot_offset;
      }
      else if (type == FIX_GOT_ENTRY) {
        if (items[base + 2] < 0 || items[base + 2] >= SYM_COUNT) fail("invalid GOT entry fixup");
        if (external_got_offsets[items[base + 2]] < 0) fail("missing GOT entry");
        target_addr = data_base + external_got_offsets[items[base + 2]];
      }
      else fail("unresolved fixup kind");
      if (target_addr < 0) fail("unresolved target address");
      {
        long patch_addr = text_base + items[base];
        long value = (target_addr + items[base + 3]) - (patch_addr + 4);
        int j = 0;
        while (j < 4) {
          data[items[base] + j] = (char)((value >> (j * 8)) & 255);
          ++j;
        }
      }
      items[base + 1] = -1;
    }
    ++i;
  }
}

void ensure_all_fixups_resolved(long *list)
{
  int i = 0;
  int count = (int)list[FIXUP_LIST_COUNT_INDEX];
  int *items = (int *)list[FIXUP_LIST_ITEMS_INDEX];
  while (i < count) {
    if (items[i * FIXUP_ENTRY_STRIDE + 1] != -1) fail("unresolved fixup remains");
    ++i;
  }
}

char *read_file(char *path, int *out_size)
{
  int fd = open(path, 0);
  int cap = 4096;
  char *buf;
  char *tmp;
  int size = 0;
  int space;
  int rd;
  int done;
  if (fd < 0) fail("could not open input file");
  buf = malloc(cap);
  if (!buf) fail("out of memory");
  done = 0;
  while (!done) {
    space = cap - size;
    if (space == 0) {
      tmp = malloc(cap * 2);
      if (!tmp) fail("out of memory");
      copy_bytes(tmp, buf, cap);
      free(buf);
      buf = tmp;
      cap = cap * 2;
      space = cap - size;
    }
    rd = read(fd, buf + size, space);
    if (rd < 0) fail("read error");
    if (rd == 0) done = 1;
    if (rd != 0) size = size + rd;
  }
  close(fd);
  tmp = malloc(size + 1);
  if (!tmp) fail("out of memory");
  copy_bytes(tmp, buf, size);
  tmp[size] = 0;
  free(buf);
  *out_size = size;
  return tmp;
}

void ensure_instruction_storage(int count)
{
  int i;
  ins_op = malloc(sizeof(int) * count);
  ins_kind = malloc(sizeof(int) * count);
  ins_value = malloc(sizeof(long) * count);
  ins_seen = malloc(count);
  if (!ins_op || !ins_kind || !ins_value || !ins_seen) fail("out of memory");
  i = 0;
  while (i < count) {
    ins_op[i] = 0;
    ins_kind[i] = 0;
    ins_value[i] = 0;
    ins_seen[i] = 0;
    ++i;
  }
}

void ensure_data_storage(int count)
{
  int i = 0;
  data_bytes = malloc(count);
  data_seen = malloc(count);
  if (!data_bytes || !data_seen) fail("out of memory");
  while (i < count) {
    data_bytes[i] = 0;
    data_seen[i] = 0;
    ++i;
  }
}

void parse_line(char *line)
{
  char *cursor;
  char *keyword;
  int idx;
  int value;
  int i;
  char *name_tok;
  if (line[0] != ';') return;
  cursor = line + 1;
  keyword = next_token(&cursor);
  if (!keyword) return;
  if (str_eq(keyword, "STACK_SLOTS")) {
    stack_slots = (int)parse_number_token(&cursor, "STACK_SLOTS");
    return;
  }
  if (str_eq(keyword, "BSS_SIZE")) {
    bss_size = (int)parse_number_token(&cursor, "BSS_SIZE");
    return;
  }
  if (str_eq(keyword, "ENTRY_INDEX")) {
    entry_index = (int)parse_number_token(&cursor, "ENTRY_INDEX");
    return;
  }
  if (str_eq(keyword, "LABEL_COUNT")) {
    i = 0;
    label_count = (int)parse_number_token(&cursor, "LABEL_COUNT");
    if (label_count <= 0) fail("invalid label count");
    labels = malloc(sizeof(char *) * label_count);
    if (!labels) fail("out of memory");
    while (i < label_count) { labels[i] = 0; ++i; }
    return;
  }
  if (str_eq(keyword, "LABEL")) {
    idx = (int)parse_number_token(&cursor, "LABEL index");
    if (!labels) fail("LABEL before LABEL_COUNT");
    if (idx < 0 || idx >= label_count) fail("label index out of range");
    name_tok = next_token(&cursor);
    if (!name_tok) fail("LABEL name missing");
    if (labels[idx]) free(labels[idx]);
    labels[idx] = dup_string(name_tok);
    return;
  }
  if (str_eq(keyword, "INS_COUNT")) {
    ins_count = (int)parse_number_token(&cursor, "INS_COUNT");
    if (ins_count <= 0) fail("invalid instruction count");
    ensure_instruction_storage(ins_count);
    return;
  }
  if (str_eq(keyword, "INS")) {
    if (!ins_op) fail("INS before INS_COUNT");
    idx = (int)parse_number_token(&cursor, "INS index");
    if (idx < 0 || idx >= ins_count) fail("instruction index out of range");
    ins_op[idx] = (int)parse_number_token(&cursor, "INS op");
    ins_kind[idx] = (int)parse_number_token(&cursor, "INS kind");
    ins_value[idx] = parse_number_token(&cursor, "INS value");
    ins_seen[idx] = 1;
    return;
  }
  if (str_eq(keyword, "DATA_SIZE")) {
    data_size = (int)parse_number_token(&cursor, "DATA_SIZE");
    if (data_size < 0) fail("invalid data size");
    if (data_size > 0) ensure_data_storage(data_size);
    return;
  }
  if (str_eq(keyword, "DATA")) {
    if (data_size <= 0) fail("DATA before DATA_SIZE");
    idx = (int)parse_number_token(&cursor, "DATA index");
    if (idx < 0 || idx >= data_size) fail("data index out of range");
    value = (int)parse_number_token(&cursor, "DATA value");
    if (value < 0 || value > 255) fail("DATA byte out of range");
    data_bytes[idx] = (char)value;
    data_seen[idx] = 1;
    return;
  }
}

void parse_metadata(char *text, int size)
{
  int pos = 0;
  int in_meta = 0;
  int found_end = 0;
  int done = 0;
  while (!done && pos <= size) {
    char *line = text + pos;
    while (pos < size && text[pos] != '\n') ++pos;
    text[pos] = 0;
    if (!in_meta) {
      if (starts_with(line, ";SCC-META-BEGIN")) in_meta = 1;
    }
    else {
      if (starts_with(line, ";SCC-META-END")) {
        found_end = 1;
        done = 1;
      }
      else parse_line(line);
    }
    if (!done) ++pos;
  }
  if (!in_meta || !found_end) fail("metadata block missing");
}

void validate_program()
{
  int i;
  if (ins_count <= 0 || !ins_op) fail("missing instructions");
  if (entry_index < 0 || entry_index >= ins_count) fail("invalid entry index");
  if (stack_slots <= 0) fail("invalid stack slots");
  i = 0;
  while (i < ins_count) {
    if (!ins_seen[i]) fail("missing INS entry");
    ++i;
  }
  if (data_size > 0) {
    if (!data_bytes) fail("missing data bytes");
    i = 0;
    while (i < data_size) {
      if (!data_seen[i]) fail("missing DATA entry");
      ++i;
    }
  }
  ensure_labels_ready();
  dedupe_labels();
}

void out_flush()
{
  int wrote;
  if (!asm_len) return;
  wrote = write(asm_fd, asm_buf, asm_len);
  if (wrote != asm_len) fail("write error");
  asm_len = 0;
}

void out_char(int c)
{
  asm_buf[asm_len++] = (char)c;
  if (asm_len >= ASM_BUF_SIZE) out_flush();
}

void out_str(char *s)
{
  int i = 0;
  while (s && s[i]) {
    out_char(s[i]);
    ++i;
  }
}

void out_uint_recursive(long v)
{
  if (v >= 10) out_uint_recursive(v / 10);
  out_char('0' + (int)(v % 10));
}

void out_int(long v)
{
  if (v == 0) {
    out_char('0');
    return;
  }
  if (v < 0) {
    out_char('-');
    v = -v;
  }
  out_uint_recursive(v);
}

void out_hex_byte(int v)
{
  out_char(hex_digits[(v >> 4) & 15]);
  out_char(hex_digits[v & 15]);
}

char *printf_reg(int idx)
{
  if (idx == 0) return "rdi";
  if (idx == 1) return "rsi";
  if (idx == 2) return "rdx";
  if (idx == 3) return "rcx";
  if (idx == 4) return "r8";
  return "r9";
}

int call_reg_code(int idx)
{
  if (idx == 0) return REG_RDI;
  if (idx == 1) return REG_RSI;
  if (idx == 2) return REG_RDX;
  if (idx == 3) return REG_RCX;
  if (idx == 4) return REG_R8;
  return REG_R9;
}

int call_arg_count(int idx)
{
  if (idx + 1 >= ins_count) return 0;
  if (ins_op[idx + 1] != ADJ) return 0;
  return (int)ins_value[idx + 1];
}

void emit_data_section()
{
  int idx = 0;
  int chunk;
  int value;
  int j;
  out_str(".section .data\n");
  out_str(".balign 8\n");
  out_str("data_area:\n");
  if (data_size <= 0) {
    out_str("    .byte 0\n\n");
    return;
  }
  while (idx < data_size) {
    chunk = 16;
    if (data_size - idx < chunk) chunk = data_size - idx;
    out_str("    .byte ");
    j = 0;
    while (j < chunk) {
      if (j) out_str(", ");
      out_str("0x");
      value = (int)data_bytes[idx + j];
      if (value < 0) value = value + 256;
      out_hex_byte(value);
      ++j;
    }
    out_char('\n');
    idx = idx + chunk;
  }
  out_char('\n');
}

void emit_bss_section()
{
  out_str(".section .bss\n");
  out_str(".balign 16\n");
  out_str("vm_stack:\n");
  out_str("    .zero ");
  out_int((long)stack_slots * WORD_SIZE);
  out_str("\n\n");
}

void emit_main_stub()
{
  out_str(".section .text\n");
  out_str("main:\n");
  out_str("    push rbp\n");
  out_str("    mov rbp, rsp\n");
  out_str("    sub rsp, 8\n");
  out_str("    mov rdi, [rbp + 8]\n");
  out_str("    lea rsi, [rbp + 16]\n");
  out_str("    lea r12, [rip + vm_stack]\n");
  out_str("    add r12, ");
  out_int((long)stack_slots * WORD_SIZE);
  out_char('\n');
  out_str("    mov r13, r12\n");
  out_str("    sub r12, 8\n");
  out_str("    mov [r12], rdi\n");
  out_str("    sub r12, 8\n");
  out_str("    mov [r12], rsi\n");
  out_str("    lea rax, [rip + __scc_program_exit]\n");
  out_str("    sub r12, 8\n");
  out_str("    mov [r12], rax\n");
  out_str("    xor rax, rax\n");
  out_str("    jmp ");
  out_str(labels[entry_index]);
  out_str("\n\n");
}

void emit_program_exit()
{
  out_str("__scc_program_exit:\n");
  out_str("    mov edi, eax\n");
  out_str("    add rsp, 8\n");
  out_str("    pop rbp\n");
  out_str("    call exit\n");
}

void emit_note_stack()
{
  out_str(".section .note.GNU-stack,\"\",@progbits\n");
  out_str("    .byte 0\n\n");
}

void vm_push_reg(long *buf, int reg)
{
  emit_sub_reg_imm(buf, REG_R12, WORD_SIZE);
  emit_mov_mem_reg(buf, REG_R12, 0, reg);
}

void vm_pop_reg(long *buf, int reg)
{
  emit_mov_reg_mem(buf, reg, REG_R12, 0);
  emit_add_reg_imm(buf, REG_R12, WORD_SIZE);
}

void ensure_got_entry(int sym)
{
  long offset;
  if (sym < 0 || sym >= SYM_COUNT) fail("invalid external symbol index");
  if (external_got_offsets[sym] >= 0) return;
  offset = data_buffer[BUFFER_LEN_INDEX];
  buffer_append_zeros(data_buffer, WORD_SIZE);
  external_got_offsets[sym] = offset;
  reloc_list_add(got_relocations, offset, sym, R_X86_64_GLOB_DAT, 0);
}

void emit_external_stubs_binary(long *buf, long *fixups)
{
  int i = 0;
  while (i < SYM_COUNT) {
    if (external_symbol_used[i]) {
      ensure_got_entry(i);
      external_stub_offsets[i] = buf[BUFFER_LEN_INDEX];
      emit_jmp_rip_relative_mem(buf, fixups, FIX_GOT_ENTRY, i, 0);
    }
    ++i;
  }
}

long elf_hash(char *name)
{
  long h = 0;
  long g;
  char *p = name;
  if (!p) return 0;
  while (*p) {
    int c = *p;
    if (c < 0) c = c + 256;
    h = (h << 4) + c;
    g = h & 0xF0000000;
    if (g) h = h ^ (g >> 24);
    h = h & ~g;
    ++p;
  }
  return h;
}

void append_dynsym_entry(long *buf, int name_offset, int info, int other, int shndx, long value, long size)
{
  buffer_append_u32(buf, name_offset);
  buffer_append_byte(buf, info);
  buffer_append_byte(buf, other);
  buffer_append_u16(buf, shndx);
  buffer_append_u64(buf, value);
  buffer_append_u64(buf, size);
}

void build_sysv_hash(long *hash, char **names, int entry_count)
{
  int nbucket = 1;
  int i;
  int *buckets;
  int *chains;
  if (entry_count < 1) entry_count = 1;
  while (nbucket < entry_count) nbucket = nbucket << 1;
  if (nbucket < 1) nbucket = 1;
  buckets = (int *)malloc(sizeof(int) * nbucket);
  chains = (int *)malloc(sizeof(int) * entry_count);
  if (!buckets || !chains) fail("out of memory");
  i = 0;
  while (i < nbucket) { buckets[i] = 0; ++i; }
  i = 0;
  while (i < entry_count) { chains[i] = 0; ++i; }
  i = 1;
  while (i < entry_count) {
    long h = elf_hash(names ? names[i] : 0);
    int bucket = (int)(h % nbucket);
    if (buckets[bucket] == 0) buckets[bucket] = i;
    else {
      int cur = buckets[bucket];
      while (chains[cur] != 0) cur = chains[cur];
      chains[cur] = i;
    }
    ++i;
  }
  buffer_append_u32(hash, nbucket);
  buffer_append_u32(hash, entry_count);
  i = 0;
  while (i < nbucket) { buffer_append_u32(hash, buckets[i]); ++i; }
  i = 0;
  while (i < entry_count) { buffer_append_u32(hash, chains[i]); ++i; }
  free(buckets);
  free(chains);
}

void append_dynamic_entry(long *buf, long tag, long value)
{
  buffer_append_u64(buf, tag);
  buffer_append_u64(buf, value);
}

void write_program_header(long *buf, long offset, int type, int flags, long file_offset, long vaddr, long filesz, long memsz, long align)
{
  buffer_write_u32(buf, offset, type);
  buffer_write_u32(buf, offset + 4, flags);
  buffer_write_u64(buf, offset + 8, file_offset);
  buffer_write_u64(buf, offset + 16, vaddr);
  buffer_write_u64(buf, offset + 24, vaddr);
  buffer_write_u64(buf, offset + 32, filesz);
  buffer_write_u64(buf, offset + 40, memsz);
  buffer_write_u64(buf, offset + 48, align);
}

void build_relocation_section(long *buf, long data_base)
{
  int i = 0;
  int count = (int)got_relocations[RELOC_LIST_COUNT_INDEX];
  long *items = (long *)got_relocations[RELOC_LIST_ITEMS_INDEX];
  buffer_init(buf);
  while (i < count) {
    int base = i * RELOC_ENTRY_STRIDE;
    long offset = items[base];
    int symbol = (int)items[base + 1];
    int type = (int)items[base + 2];
    long addend = items[base + 3];
    int sym_index;
    long info;
    if (symbol < 0 || symbol >= SYM_COUNT) fail("relocation symbol out of range");
    sym_index = external_dynsym_index[symbol];
    if (sym_index < 0) fail("missing dynsym index");
    info = (((long)sym_index) << 32) | (long)type;
    buffer_append_u64(buf, data_base + offset);
    buffer_append_u64(buf, info);
    buffer_append_u64(buf, addend);
    ++i;
  }
}

void init_machine_code_builder()
{
  if (!text_buffer) {
    text_buffer = (long *)malloc(sizeof(long) * 3);
    if (!text_buffer) fail("out of memory");
  }
  if (!data_buffer) {
    data_buffer = (long *)malloc(sizeof(long) * 3);
    if (!data_buffer) fail("out of memory");
  }
  if (!text_fixups) {
    text_fixups = (long *)malloc(sizeof(long) * 3);
    if (!text_fixups) fail("out of memory");
  }
  buffer_init(text_buffer);
  buffer_init(data_buffer);
  fixup_list_init(text_fixups);
  init_bss_layout();
  init_external_symbol_state();
  if (data_size > 0) buffer_append_bytes(data_buffer, data_bytes, data_size);
  init_label_offsets();
  init_special_label_offsets();
}

void emit_main_stub_binary(long *buf, long *fixups)
{
  emit_push_reg(buf, REG_RBP);
  emit_mov_reg_reg(buf, REG_RBP, REG_RSP);
  emit_sub_reg_imm(buf, REG_RSP, WORD_SIZE);
  emit_mov_reg_mem(buf, REG_RDI, REG_RBP, WORD_SIZE);
  emit_lea_reg_base_disp(buf, REG_RSI, REG_RBP, 2 * WORD_SIZE);
  emit_lea_rip_relative(buf, fixups, REG_R12, FIX_BSS, BSS_VM_STACK, 0);
  emit_add_reg_imm(buf, REG_R12, (long)stack_slots * WORD_SIZE);
  emit_mov_reg_reg(buf, REG_R13, REG_R12);
  vm_push_reg(buf, REG_RDI);
  vm_push_reg(buf, REG_RSI);
  emit_lea_rip_relative(buf, fixups, REG_RAX, FIX_SPECIAL, SPECIAL_LABEL_EXIT, 0);
  vm_push_reg(buf, REG_RAX);
  emit_binop_rr(buf, 0x33, REG_RAX, REG_RAX);
  emit_jmp_rel32(buf, fixups, FIX_LABEL, entry_index, 0);
}

void emit_program_exit_binary(long *buf, long *fixups)
{
  record_special_label_offset(SPECIAL_LABEL_EXIT, buf[BUFFER_LEN_INDEX]);
  emit_mov_reg_reg(buf, REG_RDI, REG_RAX);
  emit_add_reg_imm(buf, REG_RSP, WORD_SIZE);
  emit_pop_reg(buf, REG_RBP);
  emit_call_rel32(buf, fixups, FIX_GOT, SYM_EXIT, 0);
}

void emit_printf_call_binary(long *buf, long *fixups, int idx)
{
  int args = call_arg_count(idx);
  int j = 0;
  if (args <= 0) fail("printf metadata missing");
  emit_lea_reg_base_disp(buf, REG_R10, REG_R12, (long)args * WORD_SIZE);
  while (j < args && j < 6) {
    int reg = call_reg_code(j);
    long disp = -((long)(j + 1) * WORD_SIZE);
    emit_mov_reg_mem(buf, reg, REG_R10, (int)disp);
    ++j;
  }
  emit_binop_rr(buf, 0x33, REG_RAX, REG_RAX);
  emit_call_rel32(buf, fixups, FIX_GOT, SYM_PRINTF, 0);
}

void emit_instruction_binary(long *buf, long *fixups, int idx)
{
  int op = ins_op[idx];
  long value = ins_value[idx];
  int kind = ins_kind[idx];
  if (op == IMM) {
    if (kind == IMM_KIND_DATA) {
      emit_lea_rip_relative(buf, fixups, REG_RAX, FIX_DATA, (int)value, 0);
    }
    else if (kind == IMM_KIND_LABEL) {
      if (value < 0 || value >= label_count) fail("label immediate out of range");
      emit_lea_rip_relative(buf, fixups, REG_RAX, FIX_LABEL, (int)value, 0);
    }
    else {
      emit_mov_reg_imm64(buf, REG_RAX, value);
    }
  }
  else if (op == LEA) {
    emit_lea_reg_base_disp(buf, REG_RAX, REG_R13, (int)(value * WORD_SIZE));
  }
  else if (op == LI) {
    emit_mov_reg_mem(buf, REG_RAX, REG_RAX, 0);
  }
  else if (op == LC) {
    emit_movsx_eax_mem8(buf, REG_RAX, 0);
  }
  else if (op == SI) {
    vm_pop_reg(buf, REG_R10);
    emit_mov_mem_reg(buf, REG_R10, 0, REG_RAX);
  }
  else if (op == SC) {
    vm_pop_reg(buf, REG_R10);
    emit_mov_mem8_reg8(buf, REG_R10, 0, REG_RAX);
  }
  else if (op == PSH) {
    vm_push_reg(buf, REG_RAX);
  }
  else if (op == JMP) {
    if (value < 0 || value >= label_count) fail("JMP target out of range");
    emit_jmp_rel32(buf, fixups, FIX_LABEL, (int)value, 0);
  }
  else if (op == JSR) {
    if (value < 0 || value >= label_count) fail("JSR target out of range");
    if (idx + 1 >= ins_count) fail("JSR missing fallthrough");
    emit_lea_rip_relative(buf, fixups, REG_RAX, FIX_LABEL, idx + 1, 0);
    vm_push_reg(buf, REG_RAX);
    emit_jmp_rel32(buf, fixups, FIX_LABEL, (int)value, 0);
  }
  else if (op == BZ || op == BNZ) {
    if (value < 0 || value >= label_count) fail("branch target out of range");
    if (idx + 1 >= ins_count) fail("branch missing fallthrough");
    emit_test_rr(buf, REG_RAX, REG_RAX);
    if (op == BZ) emit_jcc(buf, fixups, 0x85, FIX_LABEL, idx + 1, 0);
    else emit_jcc(buf, fixups, 0x84, FIX_LABEL, idx + 1, 0);
    emit_jmp_rel32(buf, fixups, FIX_LABEL, (int)value, 0);
  }
  else if (op == ENT) {
    vm_push_reg(buf, REG_R13);
    emit_mov_reg_reg(buf, REG_R13, REG_R12);
    if (value) emit_sub_reg_imm(buf, REG_R12, value * WORD_SIZE);
  }
  else if (op == ADJ) {
    if (value) emit_add_reg_imm(buf, REG_R12, value * WORD_SIZE);
  }
  else if (op == LEV) {
    emit_mov_reg_reg(buf, REG_R12, REG_R13);
    emit_mov_reg_mem(buf, REG_R13, REG_R12, 0);
    emit_add_reg_imm(buf, REG_R12, WORD_SIZE);
    emit_mov_reg_mem(buf, REG_R11, REG_R12, 0);
    emit_add_reg_imm(buf, REG_R12, WORD_SIZE);
    emit_jmp_reg(buf, REG_R11);
  }
  else if (op == OR || op == XOR || op == AND || op == ADD || op == SUB || op == MUL) {
    vm_pop_reg(buf, REG_R10);
    if (op == OR) emit_binop_rr(buf, 0x0B, REG_RAX, REG_R10);
    else if (op == XOR) emit_binop_rr(buf, 0x33, REG_RAX, REG_R10);
    else if (op == AND) emit_binop_rr(buf, 0x23, REG_RAX, REG_R10);
    else if (op == ADD) emit_binop_rr(buf, 0x03, REG_RAX, REG_R10);
    else if (op == SUB) {
      emit_binop_rr(buf, 0x2B, REG_R10, REG_RAX);
      emit_mov_reg_reg(buf, REG_RAX, REG_R10);
    }
    else emit_imul_rr(buf, REG_RAX, REG_R10);
  }
  else if (op == EQ || op == NE || op == LT || op == GT || op == LE || op == GE) {
    int setcc_code;
    vm_pop_reg(buf, REG_R10);
    emit_cmp_rr(buf, REG_R10, REG_RAX);
    if (op == EQ) setcc_code = 0x94;
    else if (op == NE) setcc_code = 0x95;
    else if (op == LT) setcc_code = 0x9C;
    else if (op == GT) setcc_code = 0x9F;
    else if (op == LE) setcc_code = 0x9E;
    else setcc_code = 0x9D;
    emit_setcc(buf, setcc_code);
    emit_movzx_rax_al(buf);
  }
  else if (op == SHL || op == SHR) {
    emit_mov_reg_reg(buf, REG_RCX, REG_RAX);
    vm_pop_reg(buf, REG_RAX);
    if (op == SHL) emit_shift_cl(buf, 4, REG_RAX);
    else emit_shift_cl(buf, 7, REG_RAX);
  }
  else if (op == DIV || op == MOD) {
    vm_pop_reg(buf, REG_R10);
    emit_mov_reg_reg(buf, REG_RCX, REG_RAX);
    emit_mov_reg_reg(buf, REG_RAX, REG_R10);
    emit_cqo(buf);
    emit_idiv_reg(buf, REG_RCX);
    if (op == MOD) emit_mov_reg_reg(buf, REG_RAX, REG_RDX);
  }
  else if (op == OPEN) {
    int args = call_arg_count(idx);
    if (args < 2) args = 2;
    if (args > 3) fail("open expects <=3 args");
    if (args == 3) {
      emit_mov_reg_mem(buf, REG_RDX, REG_R12, 0);
      emit_mov_reg_mem(buf, REG_RSI, REG_R12, WORD_SIZE);
      emit_mov_reg_mem(buf, REG_RDI, REG_R12, 2 * WORD_SIZE);
    }
    else {
      emit_mov_reg_mem(buf, REG_RSI, REG_R12, 0);
      emit_mov_reg_mem(buf, REG_RDI, REG_R12, WORD_SIZE);
      emit_binop_rr(buf, 0x33, REG_RDX, REG_RDX);
    }
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_OPEN, 0);
  }
  else if (op == READ || op == WRIT) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 2 * WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RSI, REG_R12, WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RDX, REG_R12, 0);
    if (op == READ) emit_call_rel32(buf, fixups, FIX_GOT, SYM_READ, 0);
    else emit_call_rel32(buf, fixups, FIX_GOT, SYM_WRITE, 0);
  }
  else if (op == CLOS) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_CLOSE, 0);
  }
  else if (op == PRTF) {
    emit_printf_call_binary(buf, fixups, idx);
  }
  else if (op == MALC) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_MALLOC, 0);
  }
  else if (op == FREE) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_FREE, 0);
    emit_binop_rr(buf, 0x33, REG_RAX, REG_RAX);
  }
  else if (op == MSET || op == MCMP) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 2 * WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RSI, REG_R12, WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RDX, REG_R12, 0);
    if (op == MSET) emit_call_rel32(buf, fixups, FIX_GOT, SYM_MEMSET, 0);
    else emit_call_rel32(buf, fixups, FIX_GOT, SYM_MEMCMP, 0);
  }
  else if (op == EXIT) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_EXIT, 0);
  }
  else if (op == FORKSYS) {
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_FORK, 0);
  }
  else if (op == EXECVP) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RSI, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_EXECVP, 0);
  }
  else if (op == WAITPID) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 2 * WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RSI, REG_R12, WORD_SIZE);
    emit_mov_reg_mem(buf, REG_RDX, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_WAITPID, 0);
  }
  else if (op == UNLINKSYS) {
    emit_mov_reg_mem(buf, REG_RDI, REG_R12, 0);
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_UNLINK, 0);
  }
  else if (op == GETPID) {
    emit_call_rel32(buf, fixups, FIX_GOT, SYM_GETPID, 0);
  }
  else {
    fail("unknown opcode");
  }
}

void generate_machine_code()
{
  int i = 0;
  init_machine_code_builder();
  emit_main_stub_binary(text_buffer, text_fixups);
  while (i < ins_count) {
    record_label_offset(i, text_buffer[BUFFER_LEN_INDEX]);
    emit_instruction_binary(text_buffer, text_fixups, i);
    ++i;
  }
  emit_program_exit_binary(text_buffer, text_fixups);
  emit_external_stubs_binary(text_buffer, text_fixups);
  apply_local_fixups(text_buffer, text_fixups);
}

void write_elf_file(char *output_path)
{
  long *dynsym = (long *)malloc(sizeof(long) * 3);
  long *dynstr = (long *)malloc(sizeof(long) * 3);
  long *hash = (long *)malloc(sizeof(long) * 3);
  long *rela = (long *)malloc(sizeof(long) * 3);
  long *dynamic = (long *)malloc(sizeof(long) * 3);
  long *interp = (long *)malloc(sizeof(long) * 3);
  long *elf = (long *)malloc(sizeof(long) * 3);
  char **dynsym_names;
  int i = 0;
  int libc_needed_offset;
  int dynsym_entries;
  long rela_size;
  int got_present = 0;
  long got_start_offset = -1;
  int dynamic_entry_count;
  long dynamic_size;
  long image_base = 0x400000;
  int phnum = 4;
  long ehdr_size = 64;
  long phent_size = 56;
  long header_size = ehdr_size + (long)phnum * phent_size;
  long text_section_offset;
  long cursor;
  long text_size = text_buffer[BUFFER_LEN_INDEX];
  long interp_offset = -1;
  long hash_offset = -1;
  long dynsym_offset = -1;
  long dynstr_offset = -1;
  long rela_offset = -1;
  long ro_end;
  long data_segment_offset;
  long data_section_offset;
  long dynamic_offset;
  long data_initialized_size;
  long bss_gap;
  long data_filesz;
  long data_memsz;
  long text_segment_filesz;
  long text_segment_memsz;
  long text_section_vaddr;
  long interp_vaddr = 0;
  long hash_vaddr = 0;
  long dynsym_vaddr = 0;
  long dynstr_vaddr = 0;
  long rela_vaddr = 0;
  long data_section_vaddr;
  long dynamic_vaddr;
  long data_segment_vaddr;
  long bss_base_vaddr;
  long entry_point;
  long got_vaddr = 0;
  int fd;
  int written;
  char *elf_bytes;
  long elf_len;
  int open_flags = 577;
  int open_mode = 493;

  if (!dynsym || !dynstr || !hash || !rela || !dynamic || !interp || !elf) fail("out of memory");

  buffer_init(dynsym);
  buffer_init(dynstr);
  buffer_init(hash);
  buffer_init(rela);
  buffer_init(dynamic);
  buffer_init(interp);
  buffer_init(elf);

  buffer_append_byte(dynstr, 0);
  buffer_append_zeros(dynsym, 24);
  libc_needed_offset = buffer_append_cstring(dynstr, "libc.so.6");

  dynsym_names = (char **)malloc(sizeof(char *) * (SYM_COUNT + 2));
  if (!dynsym_names) fail("out of memory");
  dynsym_names[0] = 0;

  while (i < SYM_COUNT) {
    if (external_symbol_used[i]) {
      char *name = get_external_symbol_name(i);
      int info = (1 << 4) | 2;
      external_dynsym_index[i] = (int)(dynsym[BUFFER_LEN_INDEX] / 24);
      dynsym_names[external_dynsym_index[i]] = name;
      append_dynsym_entry(dynsym, buffer_append_cstring(dynstr, name), info, 0, 0, 0, 0);
    }
    ++i;
  }

  dynsym_entries = (int)(dynsym[BUFFER_LEN_INDEX] / 24);
  build_sysv_hash(hash, dynsym_names, dynsym_entries);
  free(dynsym_names);

  rela_size = (long)got_relocations[RELOC_LIST_COUNT_INDEX] * 24;
  i = 0;
  while (i < SYM_COUNT) {
    if (external_got_offsets[i] >= 0) {
      if (got_start_offset < 0 || external_got_offsets[i] < got_start_offset) got_start_offset = external_got_offsets[i];
      got_present = 1;
    }
    ++i;
  }

  dynamic_entry_count = 6;
  if (rela_size > 0) dynamic_entry_count = dynamic_entry_count + 3;
  if (got_present) dynamic_entry_count = dynamic_entry_count + 1;
  dynamic_entry_count = dynamic_entry_count + 1;
  dynamic_size = (long)dynamic_entry_count * 16;

  buffer_append_cstring(interp, interp_path);

  text_section_offset = align_to(header_size, 16);
  cursor = text_section_offset + text_size;

  if (interp[BUFFER_LEN_INDEX] > 0) {
    cursor = align_to(cursor, 16);
    interp_offset = cursor;
    cursor = cursor + interp[BUFFER_LEN_INDEX];
  }
  if (hash[BUFFER_LEN_INDEX] > 0) {
    cursor = align_to(cursor, 16);
    hash_offset = cursor;
    cursor = cursor + hash[BUFFER_LEN_INDEX];
  }
  if (dynsym[BUFFER_LEN_INDEX] > 0) {
    cursor = align_to(cursor, 16);
    dynsym_offset = cursor;
    cursor = cursor + dynsym[BUFFER_LEN_INDEX];
  }
  if (dynstr[BUFFER_LEN_INDEX] > 0) {
    cursor = align_to(cursor, 16);
    dynstr_offset = cursor;
    cursor = cursor + dynstr[BUFFER_LEN_INDEX];
  }
  if (rela_size > 0) {
    cursor = align_to(cursor, 16);
    rela_offset = cursor;
    cursor = cursor + rela_size;
  }

  ro_end = cursor;
  data_segment_offset = align_to(ro_end, 0x1000);
  data_section_offset = align_to(data_segment_offset, 16);
  cursor = data_section_offset + data_buffer[BUFFER_LEN_INDEX];
  dynamic_offset = align_to(cursor, 16);
  cursor = dynamic_offset + dynamic_size;
  data_initialized_size = cursor - data_segment_offset;
  bss_gap = align_to(data_initialized_size, 16) - data_initialized_size;
  data_filesz = data_initialized_size;
  data_memsz = data_initialized_size + bss_gap + total_bss_size;

  text_segment_filesz = ro_end;
  text_segment_memsz = text_segment_filesz;

  text_section_vaddr = image_base + text_section_offset;
  if (interp_offset >= 0) interp_vaddr = image_base + interp_offset;
  if (hash_offset >= 0) hash_vaddr = image_base + hash_offset;
  if (dynsym_offset >= 0) dynsym_vaddr = image_base + dynsym_offset;
  if (dynstr_offset >= 0) dynstr_vaddr = image_base + dynstr_offset;
  if (rela_offset >= 0) rela_vaddr = image_base + rela_offset;
  data_section_vaddr = image_base + data_section_offset;
  dynamic_vaddr = image_base + dynamic_offset;
  data_segment_vaddr = image_base + data_segment_offset;
  bss_base_vaddr = data_segment_vaddr + data_initialized_size + bss_gap;
  if (got_present && got_start_offset >= 0) got_vaddr = data_section_vaddr + got_start_offset;

  apply_remaining_fixups(text_buffer, text_fixups, text_section_vaddr, data_section_vaddr, bss_base_vaddr);
  ensure_all_fixups_resolved(text_fixups);

  if (rela_size > 0) build_relocation_section(rela, data_section_vaddr);
  if (rela_size > 0 && rela[BUFFER_LEN_INDEX] != rela_size) fail("rela size mismatch");

  buffer_init(dynamic);
  append_dynamic_entry(dynamic, 1, libc_needed_offset);
  append_dynamic_entry(dynamic, 5, dynstr_vaddr);
  append_dynamic_entry(dynamic, 6, dynsym_vaddr);
  append_dynamic_entry(dynamic, 10, dynstr[BUFFER_LEN_INDEX]);
  append_dynamic_entry(dynamic, 11, 24);
  append_dynamic_entry(dynamic, 4, hash_vaddr);
  if (rela_size > 0) {
    append_dynamic_entry(dynamic, 7, rela_vaddr);
    append_dynamic_entry(dynamic, 8, rela_size);
    append_dynamic_entry(dynamic, 9, 24);
  }
  if (got_present && got_vaddr) append_dynamic_entry(dynamic, 3, got_vaddr);
  append_dynamic_entry(dynamic, 0, 0);
  if (dynamic[BUFFER_LEN_INDEX] != dynamic_size) fail("dynamic size mismatch");

  if (interp_offset < 0 || hash_offset < 0 || dynsym_offset < 0 || dynstr_offset < 0) fail("missing ELF sections");

  entry_point = text_section_vaddr;

  buffer_append_zeros(elf, header_size);
  buffer_pad_to(elf, text_section_offset);
  buffer_append_buffer(elf, text_buffer);
  if (interp_offset >= 0) {
    buffer_pad_to(elf, interp_offset);
    buffer_append_buffer(elf, interp);
  }
  if (hash_offset >= 0) {
    buffer_pad_to(elf, hash_offset);
    buffer_append_buffer(elf, hash);
  }
  if (dynsym_offset >= 0) {
    buffer_pad_to(elf, dynsym_offset);
    buffer_append_buffer(elf, dynsym);
  }
  if (dynstr_offset >= 0) {
    buffer_pad_to(elf, dynstr_offset);
    buffer_append_buffer(elf, dynstr);
  }
  if (rela_offset >= 0 && rela[BUFFER_LEN_INDEX] > 0) {
    buffer_pad_to(elf, rela_offset);
    buffer_append_buffer(elf, rela);
  }
  buffer_pad_to(elf, data_segment_offset);
  buffer_pad_to(elf, data_section_offset);
  buffer_append_buffer(elf, data_buffer);
  buffer_pad_to(elf, dynamic_offset);
  buffer_append_buffer(elf, dynamic);
  buffer_pad_to(elf, data_segment_offset + data_filesz);
  elf_bytes = (char *)elf[BUFFER_DATA_INDEX];
  elf_len = elf[BUFFER_LEN_INDEX];

  elf_bytes[0] = 0x7F;
  elf_bytes[1] = 'E';
  elf_bytes[2] = 'L';
  elf_bytes[3] = 'F';
  elf_bytes[4] = 2;
  elf_bytes[5] = 1;
  elf_bytes[6] = 1;
  elf_bytes[7] = 0;
  i = 8;
  while (i < 16) {
    elf_bytes[i] = 0;
    i = i + 1;
  }
  buffer_write_u16(elf, 16, 2);
  buffer_write_u16(elf, 18, 62);
  buffer_write_u32(elf, 20, 1);
  buffer_write_u64(elf, 24, entry_point);
  buffer_write_u64(elf, 32, ehdr_size);
  buffer_write_u64(elf, 40, 0);
  buffer_write_u32(elf, 48, 0);
  buffer_write_u16(elf, 52, (int)ehdr_size);
  buffer_write_u16(elf, 54, (int)phent_size);
  buffer_write_u16(elf, 56, phnum);
  buffer_write_u16(elf, 58, 0);
  buffer_write_u16(elf, 60, 0);
  buffer_write_u16(elf, 62, 0);

  write_program_header(elf, ehdr_size + phent_size * 0, 1, 5, 0, image_base, text_segment_filesz, text_segment_memsz, 0x1000);
  write_program_header(elf, ehdr_size + phent_size * 1, 1, 6, data_segment_offset, data_segment_vaddr, data_filesz, data_memsz, 0x1000);
  write_program_header(elf, ehdr_size + phent_size * 2, 2, 6, dynamic_offset, dynamic_vaddr, dynamic_size, dynamic_size, 8);
  write_program_header(elf, ehdr_size + phent_size * 3, 3, 4, interp_offset, interp_vaddr, interp[BUFFER_LEN_INDEX], interp[BUFFER_LEN_INDEX], 1);

  fd = open(output_path, open_flags, open_mode);
  if (fd < 0) fail("could not open output file");
  written = 0;
  while (written < elf_len) {
    int chunk = write(fd, elf_bytes + written, (int)(elf_len - written));
    if (chunk <= 0) {
      close(fd);
      fail("write error");
    }
    written = written + chunk;
  }
  close(fd);
}

void emit_load_immediate(int idx)
{
  long value = ins_value[idx];
  int kind = ins_kind[idx];
  if (kind == IMM_KIND_DATA) {
    out_str("    lea rax, [rip + data_area");
    if (value) {
      out_str(" + ");
      out_int(value);
    }
    out_str("]\n");
    return;
  }
  if (kind == IMM_KIND_LABEL) {
    if (value < 0 || value >= label_count) fail("label immediate out of range");
    out_str("    lea rax, [rip + ");
    out_str(labels[(int)value]);
    out_str("]\n");
    return;
  }
  out_str("    mov rax, ");
  out_int(value);
  out_char('\n');
}

void emit_printf_call(int idx)
{
  int args = call_arg_count(idx);
  int j = 0;
  if (args <= 0) fail("printf metadata missing");
  out_str("    lea r10, [r12 + ");
  out_int((long)args * WORD_SIZE);
  out_str("]\n");
  while (j < args && j < 6) {
    out_str("    mov ");
    out_str(printf_reg(j));
    out_str(", [r10 - ");
    out_int((long)(j + 1) * WORD_SIZE);
    out_str("]\n");
    ++j;
  }
  out_str("    xor eax, eax\n");
  out_str("    call printf\n");
}

void emit_instruction(int idx)
{
  int op = ins_op[idx];
  long value = ins_value[idx];
  out_str(labels[idx]);
  out_str(":\n");
  if (op == IMM) {
    emit_load_immediate(idx);
  }
  else if (op == LEA) {
    out_str("    lea rax, [r13");
    if (value >= 0) out_char('+');
    out_int(value * WORD_SIZE);
    out_str("]\n");
  }
  else if (op == LI) {
    out_str("    mov rax, [rax]\n");
  }
  else if (op == LC) {
    out_str("    movsx eax, byte ptr [rax]\n");
  }
  else if (op == SI) {
    out_str("    mov r10, [r12]\n    add r12, 8\n    mov [r10], rax\n");
  }
  else if (op == SC) {
    out_str("    mov r10, [r12]\n    add r12, 8\n    mov byte ptr [r10], al\n");
  }
  else if (op == PSH) {
    out_str("    sub r12, 8\n    mov [r12], rax\n");
  }
  else if (op == JMP) {
    if (value < 0 || value >= label_count) fail("JMP target out of range");
    out_str("    jmp ");
    out_str(labels[(int)value]);
    out_char('\n');
  }
  else if (op == JSR) {
    if (value < 0 || value >= label_count) fail("JSR target out of range");
    if (idx + 1 >= ins_count) fail("JSR missing fallthrough");
    out_str("    lea rax, [rip + ");
    out_str(labels[idx + 1]);
    out_str("]\n");
    out_str("    sub r12, 8\n    mov [r12], rax\n");
    out_str("    jmp ");
    out_str(labels[(int)value]);
    out_char('\n');
  }
  else if (op == BZ || op == BNZ) {
    if (value < 0 || value >= label_count) fail("branch target out of range");
    if (idx + 1 >= ins_count) fail("branch missing fallthrough");
    out_str("    test rax, rax\n");
    if (op == BZ) {
      out_str("    jne ");
      out_str(labels[idx + 1]);
      out_char('\n');
      out_str("    jmp ");
      out_str(labels[(int)value]);
      out_char('\n');
    }
    else {
      out_str("    je ");
      out_str(labels[idx + 1]);
      out_char('\n');
      out_str("    jmp ");
      out_str(labels[(int)value]);
      out_char('\n');
    }
  }
  else if (op == ENT) {
    out_str("    sub r12, 8\n    mov [r12], r13\n    mov r13, r12\n");
    if (value) {
      out_str("    sub r12, ");
      out_int(value * WORD_SIZE);
      out_char('\n');
    }
  }
  else if (op == ADJ) {
    if (value) {
      out_str("    add r12, ");
      out_int(value * WORD_SIZE);
      out_char('\n');
    }
  }
  else if (op == LEV) {
    out_str("    mov r12, r13\n    mov r13, [r12]\n    add r12, 8\n    mov r11, [r12]\n    add r12, 8\n    jmp r11\n");
  }
  else if (op == OR || op == XOR || op == AND || op == ADD || op == SUB || op == MUL) {
    out_str("    mov r10, [r12]\n    add r12, 8\n");
    if (op == OR) out_str("    or rax, r10\n");
    else if (op == XOR) out_str("    xor rax, r10\n");
    else if (op == AND) out_str("    and rax, r10\n");
    else if (op == ADD) out_str("    add rax, r10\n");
    else if (op == SUB) out_str("    sub r10, rax\n    mov rax, r10\n");
    else out_str("    imul rax, r10\n");
  }
  else if (op == EQ || op == NE || op == LT || op == GT || op == LE || op == GE) {
    out_str("    mov r10, [r12]\n    add r12, 8\n    cmp r10, rax\n");
    if (op == EQ) out_str("    sete al\n");
    else if (op == NE) out_str("    setne al\n");
    else if (op == LT) out_str("    setl al\n");
    else if (op == GT) out_str("    setg al\n");
    else if (op == LE) out_str("    setle al\n");
    else out_str("    setge al\n");
    out_str("    movzx rax, al\n");
  }
  else if (op == SHL || op == SHR) {
    out_str("    mov rcx, rax\n    mov rax, [r12]\n    add r12, 8\n");
    if (op == SHL) out_str("    shl rax, cl\n");
    else out_str("    sar rax, cl\n");
  }
  else if (op == DIV || op == MOD) {
    out_str("    mov r10, [r12]\n    add r12, 8\n    mov rcx, rax\n    mov rax, r10\n    cqo\n    idiv rcx\n");
    if (op == MOD) out_str("    mov rax, rdx\n");
  }
  else if (op == OPEN) {
    int args = call_arg_count(idx);
    if (args < 2) args = 2;
    if (args > 3) fail("open expects <=3 args");
    if (args == 3) {
      out_str("    mov rdx, [r12]\n    mov rsi, [r12 + 8]\n    mov rdi, [r12 + 16]\n");
    } else {
      out_str("    mov rsi, [r12]\n    mov rdi, [r12 + 8]\n    xor edx, edx\n");
    }
    out_str("    call open\n");
  }
  else if (op == READ || op == WRIT) {
    out_str("    mov rdi, [r12 + 16]\n    mov rsi, [r12 + 8]\n    mov rdx, [r12]\n");
    if (op == READ) out_str("    call read\n");
    else out_str("    call write\n");
  }
  else if (op == CLOS) {
    out_str("    mov rdi, [r12]\n    call close\n");
  }
  else if (op == PRTF) {
    emit_printf_call(idx);
  }
  else if (op == MALC) {
    out_str("    mov rdi, [r12]\n    call malloc\n");
  }
  else if (op == FREE) {
    out_str("    mov rdi, [r12]\n    call free\n    xor rax, rax\n");
  }
  else if (op == MSET || op == MCMP) {
    out_str("    mov rdi, [r12 + 16]\n    mov rsi, [r12 + 8]\n    mov rdx, [r12]\n");
    if (op == MSET) out_str("    call memset\n");
    else out_str("    call memcmp\n");
  }
  else if (op == EXIT) {
    out_str("    mov rdi, [r12]\n    call exit\n");
  }
  else if (op == FORKSYS) {
    out_str("    call fork\n");
  }
  else if (op == EXECVP) {
    out_str("    mov rdi, [r12 + 8]\n    mov rsi, [r12]\n    call execvp\n");
  }
  else if (op == WAITPID) {
    out_str("    mov rdi, [r12 + 16]\n    mov rsi, [r12 + 8]\n    mov rdx, [r12]\n    call waitpid\n");
  }
  else if (op == UNLINKSYS) {
    out_str("    mov rdi, [r12]\n    call unlink\n");
  }
  else if (op == GETPID) {
    out_str("    call getpid\n");
  }
  else {
    fail("unknown opcode");
  }
  out_char('\n');
}

void emit_program()
{
  int i = 0;
  out_str("# generated by sccas\n");
  out_str(".intel_syntax noprefix\n\n");
  out_str(".extern printf\n.extern malloc\n.extern free\n.extern memset\n.extern memcmp\n");
  out_str(".extern open\n.extern read\n.extern write\n.extern close\n.extern exit\n");
  out_str(".extern fork\n.extern execvp\n.extern waitpid\n.extern unlink\n.extern getpid\n\n");
  out_str(".globl main\n\n");
  emit_data_section();
  emit_bss_section();
  emit_main_stub();
  while (i < ins_count) {
    emit_instruction(i);
    ++i;
  }
  emit_program_exit();
  out_char('\n');
  emit_note_stack();
  out_flush();
}

void append_decimal(char *buf, int *pos, int value)
{
  if (value >= 10) append_decimal(buf, pos, value / 10);
  buf[*pos] = (char)('0' + (value % 10));
  *pos = *pos + 1;
}

void build_temp_path(char *buf)
{
  char *prefix = "/tmp/sccas_";
  int i = 0;
  int pid = getpid();
  int pos = 0;
  while (prefix[i]) { buf[pos++] = prefix[i]; ++i; }
  if (pid == 0) buf[pos++] = '0';
  else append_decimal(buf, &pos, pid);
  buf[pos++] = '_';
  buf[pos++] = 'a';
  buf[pos++] = '.';
  buf[pos++] = 's';
  buf[pos] = 0;
}

void usage()
{
  printf("usage: sccas input.s -o output\n");
  exit(1);
}

int main(int argc, char **argv)
{
  char *input_path = 0;
  char *output_path = "a.out";
  char *file_content;
  int file_size = 0;
  int i = 1;
  hex_digits = "0123456789abcdef";
  interp_path = "/lib64/ld-linux-x86-64.so.2";
  while (i < argc) {
    if (argv[i][0] == '-' && argv[i][1] == 'o') {
      if (i + 1 >= argc) usage();
      output_path = argv[i + 1];
      i = i + 2;
    }
    else if (!input_path) {
      input_path = argv[i];
      ++i;
    }
    else usage();
  }
  if (!input_path) usage();
  file_content = read_file(input_path, &file_size);
  parse_metadata(file_content, file_size);
  validate_program();
  generate_machine_code();
  write_elf_file(output_path);
  free(file_content);
  return 0;
}

