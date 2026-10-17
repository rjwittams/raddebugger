.text
.globl _red_zone_leaf
.p2align 2
_red_zone_leaf:
.cfi_startproc
  mov x0, #1234
  stp x0, x0, [sp, #-16]
  stp x0, x0, [sp, #-32]
  stp x0, x0, [sp, #-48]
  stp x0, x0, [sp, #-64]
  stp x0, x0, [sp, #-80]
  stp x0, x0, [sp, #-96]
  stp x0, x0, [sp, #-112]
  stp x0, x0, [sp, #-128]
  nop
  ldur x0, [sp, #-8]
  ret
.cfi_endproc
