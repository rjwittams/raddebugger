// Source-step fixture: explicit line rows avoid compiler-dependent scheduling.
#ifdef __APPLE__
#define SYMBOL(x) _##x
#else
#define SYMBOL(x) x
#endif
#ifndef SPECIAL_LINE
#define SPECIAL_LINE 0
#endif
.file 1 __FILE__
.text
.globl SYMBOL(main)
SYMBOL(main):
.cfi_startproc
#if defined(__aarch64__) || defined(__arm64__)
  stp x29, x30, [sp, #-16]!
  .cfi_def_cfa_offset 16
  .cfi_offset 29, -16
  .cfi_offset 30, -8
  mov x29, sp
  .loc 1 10 0
  nop
  .loc 1 SPECIAL_LINE 0
  nop
  .loc 1 0xfeefee 0
  nop
  .loc 1 0xf00f00 0
  nop
  .loc 1 20 0
  b Lhidden_branch
  .loc 1 21 0
  nop
Lhidden_branch:
  .loc 1 SPECIAL_LINE 0
  nop
#ifdef HIDDEN_LOOP
  b Lhidden_branch
#endif
  .loc 1 20 0
  nop
  .loc 1 30 0
#ifdef LEAF_SAME_LINE
  adr x10, SYMBOL(leaf)
  blr x10
#else
  bl SYMBOL(leaf)
#endif
  .loc 1 SPECIAL_LINE 0
  nop
  .loc 1 50 0
  nop
  .loc 1 SPECIAL_LINE 0
#ifdef HIDDEN_EXCEPTION
  mov x9, #0
  ldr x0, [x9]
#else
  brk #0
#endif
  .loc 1 60 0
  mov w0, #0
  ldp x29, x30, [sp], #16
  ret
#else
  pushq %rbp
  .cfi_def_cfa_offset 16
  .cfi_offset 6, -16
  movq %rsp, %rbp
  .loc 1 10 0
  nop
  .loc 1 SPECIAL_LINE 0
  nop
  .loc 1 0xfeefee 0
  nop
  .loc 1 0xf00f00 0
  nop
  .loc 1 20 0
  jmp Lhidden_branch
  .loc 1 21 0
  nop
Lhidden_branch:
  .loc 1 SPECIAL_LINE 0
  nop
#ifdef HIDDEN_LOOP
  jmp Lhidden_branch
#endif
  .loc 1 20 0
  nop
  .loc 1 30 0
#ifdef LEAF_SAME_LINE
  leaq SYMBOL(leaf)(%rip), %rax
  call *%rax
#else
  call SYMBOL(leaf)
#endif
  .loc 1 SPECIAL_LINE 0
  nop
  .loc 1 50 0
  nop
  .loc 1 SPECIAL_LINE 0
#ifdef HIDDEN_EXCEPTION
  movq 0, %rax
#else
  int3
#endif
  .loc 1 60 0
  xorl %eax, %eax
  popq %rbp
  ret
#endif
.cfi_endproc
.globl SYMBOL(leaf)
SYMBOL(leaf):
.cfi_startproc
#ifdef LEAF_SAME_LINE
  .loc 1 30 0
#else
  .loc 1 SPECIAL_LINE 0
#endif
  nop
  .loc 1 40 0
  nop
  ret
.cfi_endproc
Lfixture_end:
// Assemblers emit line rows but do not consistently emit a compilation unit.
#ifdef __APPLE__
.section __DWARF,__debug_abbrev,regular,debug
#else
.section .debug_abbrev,"",@progbits
#endif
  .byte 1, 0x11, 1 // compile_unit, children
  .byte 0x03, 0x08 // name, string
  .byte 0x1b, 0x08 // comp_dir, string
  .byte 0x11, 0x01 // low_pc, addr
  .byte 0x12, 0x07 // high_pc, data8
  .byte 0x10, 0x17 // stmt_list, sec_offset
  .byte 0, 0
  .byte 2, 0x2e, 0 // subprogram, no children
  .byte 0x03, 0x08
  .byte 0x11, 0x01
  .byte 0x12, 0x07
  .byte 0, 0, 0
#ifdef __APPLE__
.section __DWARF,__debug_info,regular,debug
#else
.section .debug_info,"",@progbits
#endif
  .long Lunit_end - Lunit_start
Lunit_start:
  .short 4
  .long 0
  .byte 8
  .byte 1
  .asciz "line_step.S"
  .asciz "."
  .quad SYMBOL(main)
  .quad Lfixture_end - SYMBOL(main)
  .long 0
  .byte 2
  .asciz "main"
  .quad SYMBOL(main)
  .quad SYMBOL(leaf) - SYMBOL(main)
  .byte 2
  .asciz "leaf"
  .quad SYMBOL(leaf)
  .quad Lfixture_end - SYMBOL(leaf)
  .byte 0
Lunit_end:
#ifndef __APPLE__
.section .note.GNU-stack,"",@progbits
#endif
