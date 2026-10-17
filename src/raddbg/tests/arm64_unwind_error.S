// Deliberately unreadable frame. The runner stops at the nop and kills this
// target; it must never try to return through the damaged stack.
.text
.globl _unwind_leaf
.p2align 2
_unwind_leaf:
  mov x29, #0x1000
  mov sp, x29
  nop
Lhang:
  b Lhang
Lend:
.section __LD,__compact_unwind,regular,debug
.p2align 3
.quad _unwind_leaf
.long Lend - _unwind_leaf
.long 0x04000000
.quad 0, 0
