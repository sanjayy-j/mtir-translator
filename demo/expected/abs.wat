(module
  ;; CIR module 'abs' lowered by the MTIR WebAssembly back end (M6c)
  (func $abs (param $x i32) (result i32)
    (local $__block i32)
    i32.const 0
    local.set $__block
    (block $exit
      (loop $dispatch
        (block $case2
          (block $case1
            (block $case0
              local.get $__block
              br_table $case0 $case1 $case2
            )
            ;; --- entry ---
            local.get $x
            i32.const 0
            i32.lt_s
            if (result i32)
              i32.const 1
            else
              i32.const 2
            end
            local.set $__block
            br $dispatch
          )
          ;; --- then ---
          i32.const 0
          local.get $x
          i32.sub
          return
        )
        ;; --- exit ---
        local.get $x
        return
      )
    )
    unreachable
  )
  (export "abs" (func $abs))
)
