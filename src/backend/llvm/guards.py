"""Trap guards realising the CIR semantics on LLVM.

Module M5.  Owner: Member 3.
Status: PLANNED -- Week 7.
"""
"""LLVM leaves undefined what CIR defines.  Each function here emits the
guard code that closes one row of docs/divergence.md.

  sdiv/srem  : zero divisor and INT_MIN / -1 are UB in LLVM -> explicit
               compare + branch to the trap block
  shl/ashr/lshr : count >= width yields poison -> emit 'and i32 %c, 31'
  fptosi     : out-of-range or NaN is UB -> range and NaN check first
  array index: explicit bounds compare

TODO(Member 3, Week 7).
"""


def guard_sdiv(*args, **kwargs):
    raise NotImplementedError("division guards are scheduled for Week 7 (M5)")


def mask_shift_count(*args, **kwargs):
    raise NotImplementedError("shift masking is scheduled for Week 7 (M5)")
