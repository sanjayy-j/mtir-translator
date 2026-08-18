; Hand-written LLVM IR for docs/examples/abs.cir
; Validated with:  llvm-as docs/examples/abs.ll -o /tmp/abs.bc
;
; Note the CFG maps one-to-one from CIR: LLVM accepts arbitrary branches, so
; no restructuring is needed on this path.

define i32 @abs(i32 %x) {
entry:
  %t0 = icmp slt i32 %x, 0
  br i1 %t0, label %then, label %exit
then:
  %t1 = sub i32 0, %x
  ret i32 %t1
exit:
  ret i32 %x
}

declare i32 @printf(ptr, ...)
@.fmt = private constant [4 x i8] c"%d\0A\00"

define i32 @main() {
  %v = call i32 @abs(i32 -7)
  call i32 (ptr, ...) @printf(ptr @.fmt, i32 %v)
  ret i32 0
}
