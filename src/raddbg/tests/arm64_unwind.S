// Mach-O compact unwind describes the body, not the partially executed frame.
.text
.globl _unwind_leaf
.p2align 2
_unwind_leaf:
#if !defined(FRAME_RECORD) || defined(DWARF_ROWS)
.cfi_startproc
#endif
#ifdef FRAME_RECORD
  stp x29, x30, [sp, #-16]!
#ifdef DWARF_ROWS
  .cfi_def_cfa_offset 16
  .cfi_offset 29, -16
  .cfi_offset 30, -8
#endif
  mov x29, sp
#ifdef DWARF_ROWS
  .cfi_def_cfa_register 29
#endif
  sub sp, sp, #32
  stp x20, x19, [sp, #16]
#ifdef DWARF_ROWS
  .cfi_offset 20, -32
  .cfi_offset 19, -24
#endif
  mov x19, #99
#ifdef BRANCH_BODY
  b Lunwind_body
  sub sp, sp, #16 // Unreachable: a linear scan must not treat this as executed.
Lunwind_body:
  mov x0, #0
#endif
  nop
  ldp x20, x19, [sp, #16]
#ifdef DWARF_ROWS
  .cfi_restore 20
  .cfi_restore 19
#endif
  add sp, sp, #32
  ldp x29, x30, [sp], #16
#ifdef DWARF_ROWS
  .cfi_def_cfa 31, 0
  .cfi_restore 29
  .cfi_restore 30
#endif
  ret
#else
  sub sp, sp, #16
  .cfi_def_cfa_offset 16
  nop
  nop
  nop
  nop
  add sp, sp, #16
#ifdef DWARF_ROWS
  .cfi_def_cfa_offset 0
#endif
  ret
#endif
#if !defined(FRAME_RECORD) || defined(DWARF_ROWS)
.cfi_endproc
#endif
Lunwind_leaf_end:
#if defined(FRAME_RECORD) && !defined(DWARF_ROWS)
// Encode the established frame explicitly; boundary expectations are in the
// IPC test, and must not be inferred from this one body-only row.
.section __LD,__compact_unwind,regular,debug
.p2align 3
.quad _unwind_leaf
.long Lunwind_leaf_end - _unwind_leaf
.long 0x04000001 // FP frame with saved x19/x20.
.quad 0, 0
#endif
