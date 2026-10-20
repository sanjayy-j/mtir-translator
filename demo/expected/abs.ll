; CIR module 'abs' lowered by the MTIR LLVM back end (M5)
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
