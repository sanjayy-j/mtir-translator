;; Hand-written WebAssembly text for docs/examples/abs.cir
;; Validated with:  wat2wasm docs/examples/abs.wat -o /tmp/abs.wasm
;;
;; The same CFG cannot be emitted directly: WebAssembly has no branch to a
;; label of our choosing, so the two-successor branch is re-expressed as a
;; structured if/else whose result is left on the operand stack.  That
;; transformation is module M6b.

(module
  (func $abs (param $x i32) (result i32)
    local.get $x
    i32.const 0
    i32.lt_s
    if (result i32)
      i32.const 0
      local.get $x
      i32.sub
    else
      local.get $x
    end)
  (export "abs" (func $abs)))
