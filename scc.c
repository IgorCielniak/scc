#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
enum { WORD_SIZE = 8 };

char *p,      // current position in source code
  *data,   // data/bss pointer
  *data_start;

long *e, *text,  // current position in emitted code and base pointer
   *id,        // currently parsed identifier
   *sym;       // symbol table (simple list of identifiers)
int tk,            // current token
  ty,            // current expression type
  loc,           // local variable offset
  line,          // current line number
  local_slot_count; // running slot index for parameters/locals
long ival;       // current token value

// tokens and classes (operators last and in precedence order)
enum { Num = 128, Fun, Sys, Glo, Loc, Id, Char, Else, Enum, If, Int, Return, Sizeof, While, Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak };
// opcodes
enum { LEA, IMM, JMP, JSR, BZ, BNZ, ENT, ADJ, LEV, LI, LC, SI, SC, PSH, OR, XOR, AND, EQ, NE, LT, GT, LE, GE, SHL, SHR, ADD, SUB, MUL, DIV, MOD, OPEN, READ, CLOS, WRIT, PRTF, MALC, FREE, MSET, MCMP, EXIT };
// types
enum { CHAR, INT, PTR };
// identifier offsets (since we can't create an ident struct)
enum { Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz };

int is_ident_char(char c)
{
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

void next()
{
  char *pp;
  while (tk = *p) {
    ++p; if (tk == '\n') ++line;
    else if (tk == '#') { while (*p != 0 && *p != '\n') ++p; }
    else if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') || tk == '_') {
      pp = p - 1;
      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
        tk = tk * 147 + *p++;
      tk = (tk << 6) + (p - pp);
      id = sym;
      while (id[Tk]) {
        if (tk == id[Hash] && !memcmp((char *)id[Name], pp, p - pp)) { tk = id[Tk]; return; }
        id = id + Idsz;
      }
      id[Name] = (long)pp;
      id[Hash] = tk;
      tk = id[Tk] = Id;
      return;
    }
    else if (tk >= '0' && tk <= '9') {
      if (ival = tk - '0') { while (*p >= '0' && *p <= '9') ival = ival * 10 + *p++ - '0'; }
      else if (*p == 'x' || *p == 'X') {
        while ((tk = *++p) && ((tk >= '0' && tk <= '9') || (tk >= 'a' && tk <= 'f') || (tk >= 'A' && tk <= 'F')))
          ival = ival * 16 + (tk & 15) + (tk >= 'A' ? 9 : 0);
      }
      else { while (*p >= '0' && *p <= '7') ival = ival * 8 + *p++ - '0'; }
      tk = Num;
      return;
    }
    else if (tk == '/') {
      if (*p == '/') { ++p; while (*p != 0 && *p != '\n') ++p; }
      else { tk = Div; return; }
    }
    else if (tk == '\'' || tk == '"') {
      pp = data;
      while (*p != 0 && *p != tk) {
        if ((ival = *p++) == '\\' && (ival = *p++) == 'n') ival = '\n';
        if (tk == '"') *data++ = ival;
      }
      ++p;
      if (tk == '"') {
        *data++ = 0;
        ival = (long)pp;
      }
      else tk = Num;
      return;
    }
    else if (tk == '=') { if (*p == '=') { ++p; tk = Eq; } else tk = Assign; return; }
    else if (tk == '+') { if (*p == '+') { ++p; tk = Inc; } else tk = Add; return; }
    else if (tk == '-') { if (*p == '-') { ++p; tk = Dec; } else tk = Sub; return; }
    else if (tk == '!') { if (*p == '=') { ++p; tk = Ne; } return; }
    else if (tk == '<') { if (*p == '=') { ++p; tk = Le; } else if (*p == '<') { ++p; tk = Shl; } else tk = Lt; return; }
    else if (tk == '>') { if (*p == '=') { ++p; tk = Ge; } else if (*p == '>') { ++p; tk = Shr; } else tk = Gt; return; }
    else if (tk == '|') { if (*p == '|') { ++p; tk = Lor; } else tk = Or; return; }
    else if (tk == '&') { if (*p == '&') { ++p; tk = Lan; } else tk = And; return; }
    else if (tk == '^') { tk = Xor; return; }
    else if (tk == '%') { tk = Mod; return; }
    else if (tk == '*') { tk = Mul; return; }
    else if (tk == '[') { tk = Brak; return; }
    else if (tk == '?') { tk = Cond; return; }
    else if (tk == '~' || tk == ';' || tk == '{' || tk == '}' || tk == '(' || tk == ')' || tk == ']' || tk == ',' || tk == ':') return;
  }
}

void expr(int lev)
{
  int t;
  long *d, aligned;

  if (!tk) { printf("%d: unexpected eof in expression\n", line); exit(-1); }
  else if (tk == Num) { *++e = IMM; *++e = ival; next(); ty = INT; }
  else if (tk == '"') {
    *++e = IMM; *++e = ival; next();
    while (tk == '"') next();
    aligned = ((long)data + WORD_SIZE - 1) & -(long)WORD_SIZE;
    data = (char *)aligned;
    ty = PTR;
  }
  else if (tk == Sizeof) {
    next(); if (tk == '(') next(); else { printf("%d: open paren expected in sizeof\n", line); exit(-1); }
    ty = INT; if (tk == Int) next(); else if (tk == Char) { next(); ty = CHAR; }
    while (tk == Mul) { next(); ty = ty + PTR; }
    if (tk == ')') next(); else { printf("%d: close paren expected in sizeof\n", line); exit(-1); }
    *++e = IMM; *++e = (ty == CHAR) ? (long)sizeof(char) : (long)WORD_SIZE;
    ty = INT;
  }
  else if (tk == Id) {
    d = id; next();
    if (tk == '(') {
      next();
      t = 0;
      while (tk != ')') { expr(Assign); *++e = PSH; ++t; if (tk == ',') next(); }
      next();
      if (d[Class] == Sys) *++e = d[Val];
      else if (d[Class] == Fun) { *++e = JSR; *++e = d[Val]; }
      else { printf("%d: bad function call\n", line); exit(-1); }
      if (t) { *++e = ADJ; *++e = t; }
      ty = d[Type];
    }
    else if (d[Class] == Num) { *++e = IMM; *++e = d[Val]; ty = INT; }
    else {
      if (d[Class] == Loc) { *++e = LEA; *++e = loc - d[Val]; }
      else if (d[Class] == Glo) { *++e = IMM; *++e = d[Val]; }
      else { printf("%d: undefined variable\n", line); exit(-1); }
      *++e = ((ty = d[Type]) == CHAR) ? LC : LI;
    }
  }
  else if (tk == '(') {
    next();
    if (tk == Int || tk == Char) {
      t = (tk == Int) ? INT : CHAR; next();
      while (tk == Mul) { next(); t = t + PTR; }
      if (tk == ')') next(); else { printf("%d: bad cast\n", line); exit(-1); }
      expr(Inc);
      ty = t;
    }
    else {
      expr(Assign);
      if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    }
  }
  else if (tk == Mul) {
    next(); expr(Inc);
    if (ty > INT) ty = ty - PTR; else { printf("%d: bad dereference\n", line); exit(-1); }
    *++e = (ty == CHAR) ? LC : LI;
  }
  else if (tk == And) {
    next(); expr(Inc);
    if (*e == LC || *e == LI) --e; else { printf("%d: bad address-of\n", line); exit(-1); }
    ty = ty + PTR;
  }
  else if (tk == '!') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = 0; *++e = EQ; ty = INT; }
  else if (tk == '~') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = -1; *++e = XOR; ty = INT; }
  else if (tk == Add) { next(); expr(Inc); ty = INT; }
  else if (tk == Sub) {
    next(); *++e = IMM;
    if (tk == Num) { *++e = -ival; next(); } else { *++e = -1; *++e = PSH; expr(Inc); *++e = MUL; }
    ty = INT;
  }
  else if (tk == Inc || tk == Dec) {
    t = tk; next(); expr(Inc);
    if (*e == LC) { *e = PSH; *++e = LC; }
    else if (*e == LI) { *e = PSH; *++e = LI; }
    else { printf("%d: bad lvalue in pre-increment\n", line); exit(-1); }
    *++e = PSH;
    *++e = IMM; *++e = (ty > PTR) ? (long)WORD_SIZE : (long)sizeof(char);
    *++e = (t == Inc) ? ADD : SUB;
    *++e = (ty == CHAR) ? SC : SI;
  }
  else { printf("%d: bad expression\n", line); exit(-1); }

  while (tk >= lev) { // "precedence climbing" or "Top Down Operator Precedence" method
    t = ty;
    if (tk == Assign) {
      next();
      if (*e == LC || *e == LI) *e = PSH; else { printf("%d: bad lvalue in assignment\n", line); exit(-1); }
      expr(Assign); *++e = ((ty = t) == CHAR) ? SC : SI;
    }
    else if (tk == Cond) {
      next();
      *++e = BZ; d = ++e;
      expr(Assign);
      if (tk == ':') next(); else { printf("%d: conditional missing colon\n", line); exit(-1); }
      *d = (long)(e + 3); *++e = JMP; d = ++e;
      expr(Cond);
      *d = (long)(e + 1);
    }
    else if (tk == Lor) { next(); *++e = BNZ; d = ++e; expr(Lan); *d = (long)(e + 1); ty = INT; }
    else if (tk == Lan) { next(); *++e = BZ;  d = ++e; expr(Or);  *d = (long)(e + 1); ty = INT; }
    else if (tk == Or)  { next(); *++e = PSH; expr(Xor); *++e = OR;  ty = INT; }
    else if (tk == Xor) { next(); *++e = PSH; expr(And); *++e = XOR; ty = INT; }
    else if (tk == And) { next(); *++e = PSH; expr(Eq);  *++e = AND; ty = INT; }
    else if (tk == Eq)  { next(); *++e = PSH; expr(Lt);  *++e = EQ;  ty = INT; }
    else if (tk == Ne)  { next(); *++e = PSH; expr(Lt);  *++e = NE;  ty = INT; }
    else if (tk == Lt)  { next(); *++e = PSH; expr(Shl); *++e = LT;  ty = INT; }
    else if (tk == Gt)  { next(); *++e = PSH; expr(Shl); *++e = GT;  ty = INT; }
    else if (tk == Le)  { next(); *++e = PSH; expr(Shl); *++e = LE;  ty = INT; }
    else if (tk == Ge)  { next(); *++e = PSH; expr(Shl); *++e = GE;  ty = INT; }
    else if (tk == Shl) { next(); *++e = PSH; expr(Add); *++e = SHL; ty = INT; }
    else if (tk == Shr) { next(); *++e = PSH; expr(Add); *++e = SHR; ty = INT; }
    else if (tk == Add) {
      next(); *++e = PSH; expr(Mul);
      if ((ty = t) > PTR) { *++e = PSH; *++e = IMM; *++e = WORD_SIZE; *++e = MUL;  }
      *++e = ADD;
    }
    else if (tk == Sub) {
      next(); *++e = PSH; expr(Mul);
      if (t > PTR && t == ty) { *++e = SUB; *++e = PSH; *++e = IMM; *++e = WORD_SIZE; *++e = DIV; ty = INT; }
      else if ((ty = t) > PTR) { *++e = PSH; *++e = IMM; *++e = WORD_SIZE; *++e = MUL; *++e = SUB; }
      else *++e = SUB;
    }
    else if (tk == Mul) { next(); *++e = PSH; expr(Inc); *++e = MUL; ty = INT; }
    else if (tk == Div) { next(); *++e = PSH; expr(Inc); *++e = DIV; ty = INT; }
    else if (tk == Mod) { next(); *++e = PSH; expr(Inc); *++e = MOD; ty = INT; }
    else if (tk == Inc || tk == Dec) {
      if (*e == LC) { *e = PSH; *++e = LC; }
      else if (*e == LI) { *e = PSH; *++e = LI; }
      else { printf("%d: bad lvalue in post-increment\n", line); exit(-1); }
      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? WORD_SIZE : (long)sizeof(char);
      *++e = (tk == Inc) ? ADD : SUB;
      *++e = (ty == CHAR) ? SC : SI;
      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? WORD_SIZE : (long)sizeof(char);
      *++e = (tk == Inc) ? SUB : ADD;
      next();
    }
    else if (tk == Brak) {
      next(); *++e = PSH; expr(Assign);
      if (tk == ']') next(); else { printf("%d: close bracket expected\n", line); exit(-1); }
      if (t > PTR) { *++e = PSH; *++e = IMM; *++e = WORD_SIZE; *++e = MUL;  }
      else if (t < PTR) { printf("%d: pointer type expected\n", line); exit(-1); }
      *++e = ADD;
      *++e = ((ty = t - PTR) == CHAR) ? LC : LI;
    }
    else { printf("%d: compiler error tk=%d\n", line, tk); exit(-1); }
  }
}

void parse_local_declaration()
{
  int base_type, decl_type, more, store_type;
  long *var_id;
  base_type = (tk == Int) ? INT : CHAR;
  next();
  more = 1;
  while (more) {
    decl_type = base_type;
    while (tk == Mul) { next(); decl_type = decl_type + PTR; }
    if (tk != Id) { printf("%d: bad local declaration\n", line); exit(-1); }
    if (id[Class] == Loc) { printf("%d: duplicate local definition\n", line); exit(-1); }
    var_id = id;
    id[HClass] = id[Class]; id[Class] = Loc;
    id[HType]  = id[Type];  id[Type] = decl_type;
    id[HVal]   = id[Val];   id[Val] = ++local_slot_count;
    next();
    if (tk == Assign) {
      store_type = decl_type;
      next();
      *++e = LEA; *++e = loc - var_id[Val];
      *++e = PSH;
      expr(Assign);
      *++e = (store_type == CHAR) ? SC : SI;
    }
    if (tk == ',') next();
    else more = 0;
  }
  if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
}

void stmt()
{
  long *a, *b;
  if (tk == Int || tk == Char) {
    parse_local_declaration();
    return;
  }
  if (tk == If) {
    next();
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;
    stmt();
    if (tk == Else) {
      *b = (long)(e + 3); *++e = JMP; b = ++e;
      next();
      stmt();
    }
    *b = (long)(e + 1);
  }
  else if (tk == While) {
    next();
    a = e + 1;
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;
    stmt();
    *++e = JMP; *++e = (long)a;
    *b = (long)(e + 1);
  }
  else if (tk == Return) {
    next();
    if (tk != ';') expr(Assign);
    *++e = LEV;
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
  else if (tk == '{') {
    next();
    while (tk != '}') stmt();
    next();
  }
  else if (tk == ';') {
    next();
}
  else {
    expr(Assign);
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
}

enum { STACK_SLOTS = 65536, ASM_BUF_SIZE = 4096 };

int asm_fd, asm_len, ins_count, total_words;
int *ins_op, *ins_has_arg, *ins_off, *word_map_arr;
long *ins_arg;

char *asm_buf, *hex_digits, **labels;

void fail(char *msg)
{
  printf("codegen error: %s\n", msg);
  exit(-1);
}

int str_len(char *s)
{
  int n = 0;
  if (!s) return 0;
  while (s[n]) ++n;
  return n;
}

char *dup_string(char *src)
{
  int len = str_len(src), i = 0;
  char *dst;
  dst = malloc(len + 1);
  if (!dst) fail("out of memory");
  while (i <= len) { dst[i] = src[i]; ++i; }
  return dst;
}

void out_flush()
{
  int wrote;
  if (asm_len) {
    wrote = write(asm_fd, asm_buf, asm_len);
    if (wrote != asm_len) {
      printf("write error\n");
      exit(-1);
    }
    asm_len = 0;
  }
}

void out_char(int c)
{
  asm_buf[asm_len++] = c;
  if (asm_len >= ASM_BUF_SIZE) out_flush();
}

void out_str(char *s)
{
  while (s && *s) { out_char(*s); ++s; }
}

void out_uint_recursive(long v)
{
  if (v >= 10) out_uint_recursive(v / 10);
  out_char('0' + (int)(v % 10));
}

void out_int(long v)
{
  if (v == 0) { out_char('0'); return; }
  if (v < 0)  { out_char('-'); v = -v; }
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

int op_has_arg(int op)
{
  if (op == LEA || op == IMM || op == JMP || op == JSR || op == BZ || op == BNZ || op == ENT || op == ADJ) return 1;
  return 0;
}

int call_arg_count(int idx)
{
  if (idx + 1 < ins_count && ins_op[idx + 1] == ADJ) return (int)ins_arg[idx + 1];
  return 0;
}

void build_instructions()
{
  int i = 0;
  total_words = (int)(e - text);
  if (total_words <= 0) fail("no code emitted");
  ins_op = malloc(sizeof(int) * total_words);
  ins_arg = malloc(sizeof(long) * total_words);
  ins_has_arg = malloc(sizeof(int) * total_words);
  ins_off = malloc(sizeof(int) * total_words);
  word_map_arr = malloc(sizeof(int) * (total_words + 2));
  if (!ins_op || !ins_arg || !ins_has_arg || !ins_off || !word_map_arr) fail("out of memory");
  while (i < total_words + 2) { word_map_arr[i] = -1; ++i; }
  ins_count = 0;
  long *pc = text + 1;
  while (pc <= e) {
    ins_op[ins_count] = (int)*pc;
    ins_off[ins_count] = (int)(pc - text);
    word_map_arr[ins_off[ins_count]] = ins_count;
    ins_has_arg[ins_count] = 0;
    ++pc;
    if (op_has_arg(ins_op[ins_count])) {
      if (pc > e) fail("truncated instruction stream");
      ins_arg[ins_count] = *pc++;
      ins_has_arg[ins_count] = 1;
    }
    else ins_arg[ins_count] = 0;
    ++ins_count;
  }
}

int pointer_to_index(long value)
{
  if (!value) return -1;
  long *addr = (long *)value;
  long offset = addr - text;
  if (offset < 0 || offset >= total_words + 2) return -1;
  return word_map_arr[offset];
}

char *make_numeric_label(int index)
{
  char *name;
  int len = 1;
  int v = index;
  if (v > 0) {
    len = 0;
    while (v > 0) { ++len; v = v / 10; }
  }
  name = malloc(len + 2);
  if (!name) fail("out of memory");
  name[0] = 'L';
  name[len + 1] = 0;
  v = index;
  if (v == 0) name[1] = '0';
  else {
    int pos = len;
    while (v > 0) {
      name[pos] = '0' + (v % 10);
      v = v / 10;
      --pos;
    }
  }
  return name;
}

char *identifier_name(long value)
{
  int len = 0, i = 0;
  if (!value) return 0;
  char *src = (char *)value;
  while (is_ident_char(src[len])) ++len;
  if (!len) return 0;
  char *name = malloc(len + 1);
  if (!name) fail("out of memory");
  while (i < len) { name[i] = src[i]; ++i; }
  name[len] = 0;
  return name;
}

void assign_function_labels()
{
  long *cur = sym;
  while (cur[Tk]) {
    if (cur[Class] == Fun && cur[Val]) {
      int idx = pointer_to_index(cur[Val]);
      if (idx >= 0) {
        char *name = identifier_name(cur[Name]);
        if (name) {
          int len = str_len(name);
          char *label = malloc(len + 4);
          if (!label) fail("out of memory");
          label[0] = 'f'; label[1] = 'n'; label[2] = '_';
          int i = 0;
          while (i < len) { label[3 + i] = name[i]; ++i; }
          label[3 + len] = 0;
          free(labels[idx]);
          labels[idx] = label;
          free(name);
        }
      }
    }
    cur = cur + Idsz;
  }
}

void emit_data_section()
{
  int size = (int)(data - data_start), idx = 0, chunk, j, byte;
  out_str("section .data\nalign 8\ndata_area:\n");
  if (!size) { out_str("    db 0\n\n"); return; }
  while (idx < size) {
    chunk = 16;
    if (size - idx < chunk) chunk = size - idx;
    out_str("    db ");
    j = 0;
    while (j < chunk) {
      byte = data_start[idx + j] & 255;
      out_str("0x");
      out_hex_byte(byte);
      if (j + 1 < chunk) out_str(", ");
      ++j;
    }
    out_char('\n');
    idx = idx + chunk;
  }
  out_char('\n');
}

void emit_bss_section()
{
  out_str("section .bss\n");
  out_str("align 16\n");
  out_str("vm_stack resq ");
  out_int(STACK_SLOTS);
  out_str("\n\n");
}

void emit_main_stub(char *entry_label)
{
  out_str("section .text\n");
  out_str("main:\n");
  out_str("    push rbp\n");
  out_str("    mov rbp, rsp\n");
  out_str("    sub rsp, 8\n");
  out_str("    lea r12, [rel vm_stack + ");
  out_int(STACK_SLOTS * WORD_SIZE);
  out_str("]\n");
  out_str("    mov r13, r12\n");
  out_str("    sub r12, 8\n");
  out_str("    mov [r12], rdi\n");
  out_str("    sub r12, 8\n");
  out_str("    mov [r12], rsi\n");
  out_str("    lea rax, [rel __scc_program_exit]\n");
  out_str("    sub r12, 8\n");
  out_str("    mov [r12], rax\n");
  out_str("    xor rax, rax\n");
  out_str("    jmp ");
  out_str(entry_label);
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
  out_str("section .note.GNU-stack noalloc noexec nowrite align=1\n");
  out_str("    db 0\n\n");
}

void emit_load_immediate(long value)
{
  if (data_start && value >= (long)data_start && value < (long)data) {
    long offset = value - (long)data_start;
    out_str("    lea rax, [rel data_area + ");
    out_int(offset);
    out_str("]\n");
    return;
  }
  int target = pointer_to_index(value);
  if (target >= 0) {
    out_str("    lea rax, [rel ");
    out_str(labels[target]);
    out_str("]\n");
    return;
  }
  out_str("    mov rax, ");
  out_int(value);
  out_char('\n');
}

void emit_printf_call(int idx)
{
  int args = call_arg_count(idx), j = 0;
  if (args <= 0 || args > 6) fail("printf supports up to 6 arguments");
  out_str("    lea r10, [r12 + ");
  out_int(args * 8);
  out_str("]\n");
  while (j < args && j < 6) {
    out_str("    mov ");
    out_str(printf_reg(j));
    out_str(", [r10 - ");
    out_int((j + 1) * 8);
    out_str("]\n");
    ++j;
  }
  out_str("    xor eax, eax\n");
  out_str("    call printf\n");
}

void emit_instruction(int idx)
{
  int op, target, args;
  op = ins_op[idx];
  out_str(labels[idx]);
  out_str(":\n");
  if (op == IMM) emit_load_immediate(ins_arg[idx]);
  else if (op == LEA) {
    out_str("    lea rax, [r13");
    if (ins_arg[idx] >= 0) out_char('+');
    out_int(ins_arg[idx] * WORD_SIZE);
    out_str("]\n");
  }
  else if (op == LI) out_str("    mov rax, [rax]\n");
  else if (op == LC) out_str("    movsx eax, byte [rax]\n");
  else if (op == SI) {
    out_str("    mov r10, [r12]\n");
    out_str("    add r12, 8\n");
    out_str("    mov [r10], rax\n");
  }
  else if (op == SC) {
    out_str("    mov r10, [r12]\n");
    out_str("    add r12, 8\n");
    out_str("    mov byte [r10], al\n");
  }
  else if (op == PSH) {
    out_str("    sub r12, 8\n");
    out_str("    mov [r12], rax\n");
  }
  else if (op == JMP) {
    target = pointer_to_index(ins_arg[idx]);
    if (target < 0) fail("invalid jump target");
    out_str("    jmp ");
    out_str(labels[target]);
    out_char('\n');
  }
  else if (op == JSR) {
    target = pointer_to_index(ins_arg[idx]);
    if (target < 0 || idx + 1 >= ins_count) fail("invalid call target");
    out_str("    lea rax, [rel ");
    out_str(labels[idx + 1]);
    out_str("]\n");
    out_str("    sub r12, 8\n");
    out_str("    mov [r12], rax\n");
    out_str("    jmp ");
    out_str(labels[target]);
    out_char('\n');
  }
  else if (op == BZ || op == BNZ) {
    target = pointer_to_index(ins_arg[idx]);
    if (target < 0 || idx + 1 >= ins_count) fail("invalid branch target");
    out_str("    test rax, rax\n");
    if (op == BZ) {
      out_str("    jne "); out_str(labels[idx + 1]); out_char('\n');
      out_str("    jmp "); out_str(labels[target]); out_char('\n');
    } else {
      out_str("    je "); out_str(labels[idx + 1]); out_char('\n');
      out_str("    jmp "); out_str(labels[target]); out_char('\n');
    }
  }
  else if (op == ENT) {
    out_str("    sub r12, 8\n");
    out_str("    mov [r12], r13\n");
    out_str("    mov r13, r12\n");
    if (ins_arg[idx]) {
      out_str("    sub r12, ");
      out_int(ins_arg[idx] * WORD_SIZE);
      out_char('\n');
    }
  }
  else if (op == ADJ && ins_arg[idx]) {
    out_str("    add r12, ");
    out_int(ins_arg[idx] * WORD_SIZE);
    out_char('\n');
  }
  else if (op == LEV) {
    out_str("    mov r12, r13\n");
    out_str("    mov r13, [r12]\n");
    out_str("    add r12, 8\n");
    out_str("    mov r11, [r12]\n");
    out_str("    add r12, 8\n");
    out_str("    jmp r11\n");
  }
  else if (op == OR || op == XOR || op == AND || op == ADD || op == SUB || op == MUL) {
    out_str("    mov r10, [r12]\n");
    out_str("    add r12, 8\n");
    if (op == OR) out_str("    or rax, r10\n");
    else if (op == XOR) out_str("    xor rax, r10\n");
    else if (op == AND) out_str("    and rax, r10\n");
    else if (op == ADD) out_str("    add rax, r10\n");
    else if (op == SUB) { out_str("    sub r10, rax\n"); out_str("    mov rax, r10\n"); }
    else out_str("    imul rax, r10\n");
  }
  else if (op == EQ || op == NE || op == LT || op == GT || op == LE || op == GE) {
    out_str("    mov r10, [r12]\n");
    out_str("    add r12, 8\n");
    out_str("    cmp r10, rax\n");
    if (op == EQ) out_str("    sete al\n");
    else if (op == NE) out_str("    setne al\n");
    else if (op == LT) out_str("    setl al\n");
    else if (op == GT) out_str("    setg al\n");
    else if (op == LE) out_str("    setle al\n");
    else out_str("    setge al\n");
    out_str("    movzx rax, al\n");
  }
  else if (op == SHL || op == SHR) {
    out_str("    mov rcx, rax\n");
    out_str("    mov rax, [r12]\n");
    out_str("    add r12, 8\n");
    if (op == SHL) out_str("    shl rax, cl\n"); else out_str("    sar rax, cl\n");
  }
  else if (op == DIV || op == MOD) {
    out_str("    mov r10, [r12]\n");
    out_str("    add r12, 8\n");
    out_str("    mov rcx, rax\n");
    out_str("    mov rax, r10\n");
    out_str("    cqo\n");
    out_str("    idiv rcx\n");
    if (op == MOD) out_str("    mov rax, rdx\n");
  }
  else if (op == OPEN) {
    args = call_arg_count(idx);
    if (args < 2) args = 2;
    if (args > 3) fail("open supports up to 3 args");
    if (args == 3) {
      out_str("    mov rdx, [r12]\n");
      out_str("    mov rsi, [r12 + 8]\n");
      out_str("    mov rdi, [r12 + 16]\n");
    } else {
      out_str("    mov rsi, [r12]\n");
      out_str("    mov rdi, [r12 + 8]\n");
      out_str("    xor edx, edx\n");
    }
    out_str("    call open\n");
  }
  else if (op == READ || op == WRIT) {
    out_str("    mov rdi, [r12 + 16]\n");
    out_str("    mov rsi, [r12 + 8]\n");
    out_str("    mov rdx, [r12]\n");
    out_str(op == READ ? "    call read\n" : "    call write\n");
  }
  else if (op == CLOS) {
    out_str("    mov rdi, [r12]\n");
    out_str("    call close\n");
  }
  else if (op == PRTF) emit_printf_call(idx);
  else if (op == MALC) { out_str("    mov rdi, [r12]\n"); out_str("    call malloc\n"); }
  else if (op == FREE) {
    out_str("    mov rdi, [r12]\n");
    out_str("    call free\n");
    out_str("    xor rax, rax\n");
  }
  else if (op == MSET || op == MCMP) {
    out_str("    mov rdi, [r12 + 16]\n");
    out_str("    mov rsi, [r12 + 8]\n");
    out_str("    mov rdx, [r12]\n");
    out_str(op == MSET ? "    call memset\n" : "    call memcmp\n");
  }
  else if (op == EXIT) {
    out_str("    mov rdi, [r12]\n");
    out_str("    call exit\n");
  }
  else fail("unknown opcode");
  out_char('\n');
}

int open_output(char *path)
{
  int fd = open(path, 577, 420);
  if (fd < 0) { printf("could not open %s\n", path); exit(-1); }
  return fd;
}

void generate_nasm(char *path, long entry_addr)
{
  int i = 0, entry_index;
  hex_digits = "0123456789abcdef";
  build_instructions();
  labels = malloc(sizeof(char *) * ins_count);
  if (!labels) fail("out of memory");
  while (i < ins_count) { labels[i] = make_numeric_label(i); ++i; }
  assign_function_labels();
  entry_index = pointer_to_index(entry_addr);
  if (entry_index < 0) fail("invalid main entry");
  asm_fd = open_output(path);
  asm_buf = malloc(ASM_BUF_SIZE);
  if (!asm_buf) fail("out of memory");
  asm_len = 0;
  out_str("; generated by scc\n");
  out_str("default rel\n\n");
  out_str("extern printf\n");
  out_str("extern malloc\n");
  out_str("extern free\n");
  out_str("extern memset\n");
  out_str("extern memcmp\n");
  out_str("extern open\n");
  out_str("extern read\n");
  out_str("extern write\n");
  out_str("extern close\n");
  out_str("extern exit\n\n");
  out_str("global main\n\n");
  emit_data_section();
  emit_bss_section();
  emit_main_stub(labels[entry_index]);
  i = 0;
  while (i < ins_count) { emit_instruction(i); ++i; }
  emit_program_exit();
  out_char('\n');
  emit_note_stack();
  out_flush();
  close(asm_fd);
  i = 0;
  while (i < ins_count) { free(labels[i]); ++i; }
  free(labels);
  free(ins_op);
  free(ins_arg);
  free(ins_has_arg);
  free(ins_off);
  free(word_map_arr);
  free(asm_buf);
}

int main(int argc, char **argv)
{
  int fd, bt, ty, poolsz, i;
  long *idmain;
  char *out_path = "out.s";

  --argc; ++argv;
  while (argc > 0 && (*argv)[0] == '-') {
    if ((*argv)[1] == 'S' && (*argv)[2] == 0) {
      --argc; ++argv;
      if (argc < 1) { printf("usage: scc [-S output] file\n"); return -1; }
      out_path = *argv;
    }
    else { printf("usage: scc [-S output] file\n"); return -1; }
    --argc; ++argv;
  }
  if (argc < 1) { printf("usage: scc [-S output] file\n"); return -1; }
  if ((fd = open(*argv, 0)) < 0) { printf("could not open(%s)\n", *argv); return -1; }

  poolsz = 256*1024; // arbitrary size
  if (!(sym = malloc(poolsz))) { printf("could not malloc(%d) symbol area\n", poolsz); return -1; }
  if (!(text = malloc(poolsz))) { printf("could not malloc(%d) text area\n", poolsz); return -1; }
  if (!(data = malloc(poolsz))) { printf("could not malloc(%d) data area\n", poolsz); return -1; }

  memset(sym,  0, poolsz);
  memset(text, 0, poolsz);
  memset(data, 0, poolsz);
  e = text;
  data_start = data;
  p = "char else enum if int return sizeof while open read close write printf malloc free memset memcmp exit long void main";
  i = Char; while (i <= While) { next(); id[Tk] = i++; } // add keywords to symbol table
  i = OPEN; while (i <= EXIT) { next(); id[Class] = Sys; id[Type] = INT; id[Val] = i++; } // add library to symbol table
  next(); id[Tk] = Int;  // handle long type
  next(); id[Tk] = Char; // handle void type
  next(); idmain = id; // keep track of main

  if (!(p = malloc(poolsz))) { printf("could not malloc(%d) source area\n", poolsz); return -1; }
  if ((i = read(fd, p, poolsz-1)) <= 0) { printf("read() returned %d\n", i); return -1; }
  p[i] = 0;
  close(fd);

  // parse declarations
  line = 1;
  next();
  while (tk) {
    bt = INT; // basetype
    if (tk == Int) next();
    else if (tk == Char) { next(); bt = CHAR; }
    else if (tk == Enum) {
      next();
      if (tk != '{') next();
      if (tk == '{') {
        next();
        i = 0;
        while (tk != '}') {
          if (tk != Id) { printf("%d: bad enum identifier %d\n", line, tk); return -1; }
          next();
          if (tk == Assign) {
            next();
            if (tk != Num) { printf("%d: bad enum initializer\n", line); return -1; }
            i = ival;
            next();
          }
          id[Class] = Num; id[Type] = INT; id[Val] = i++;
          if (tk == ',') next();
        }
        next();
      }
    }
    while (tk != ';' && tk != '}') {
      ty = bt;
      while (tk == Mul) { next(); ty = ty + PTR; }
      if (tk != Id) { printf("%d: bad global declaration\n", line); return -1; }
      if (id[Class]) { printf("%d: duplicate global definition\n", line); return -1; }
      next();
      id[Type] = ty;
      if (tk == '(') { // function
        long *ent_slot;
        id[Class] = Fun;
        id[Val] = (long)(e + 1);
        next(); local_slot_count = 0;
        while (tk != ')') {
          ty = INT;
          if (tk == Int) next();
          else if (tk == Char) { next(); ty = CHAR; }
          while (tk == Mul) { next(); ty = ty + PTR; }
          if (tk != Id) { printf("%d: bad parameter declaration\n", line); return -1; }
          if (id[Class] == Loc) { printf("%d: duplicate parameter definition\n", line); return -1; }
          id[HClass] = id[Class]; id[Class] = Loc;
          id[HType]  = id[Type];  id[Type] = ty;
          id[HVal]   = id[Val];   id[Val] = local_slot_count++;
          next();
          if (tk == ',') next();
        }
        next();
        if (tk != '{') { printf("%d: bad function definition\n", line); return -1; }
        loc = ++local_slot_count;
        next();
        *++e = ENT;
        ent_slot = ++e;
        *ent_slot = 0;
        while (tk != '}') stmt();
        *ent_slot = local_slot_count - loc;
        *++e = LEV;
        id = sym; // unwind symbol table locals
        while (id[Tk]) {
          if (id[Class] == Loc) {
            id[Class] = id[HClass];
            id[Type] = id[HType];
            id[Val] = id[HVal];
          }
          id = id + Idsz;
        }
      }
      else {
        id[Class] = Glo;
        id[Val] = (long)data;
        data = data + WORD_SIZE;
      }
      if (tk == ',') next();
    }
    next();
  }

  if (!idmain[Val]) { printf("main() not defined\n"); return -1; }
  generate_nasm(out_path, idmain[Val]);
  return 0;
}