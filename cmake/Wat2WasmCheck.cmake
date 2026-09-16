# Wat2WasmCheck.cmake -- assemble and validate the generated WebAssembly with
# the real wat2wasm and wasm-validate.
#
# This is the only check that can honestly claim the emitted .wat is valid
# WebAssembly.  tests/WasmTests.cpp checks structural properties of the text
# -- balanced s-expressions, one case block per CIR block, the shadow stack
# prologue -- which is a useful but much weaker statement: a module can be
# perfectly well-formed text and still fail validation on a type or a branch
# depth.  Nothing in the C++ test suite is a substitute for this.
#
# Invoked by CTest with -DMTIRC, -DWAT2WASM, -DWASM_VALIDATE, -DROOT and
# -DOUTDIR.

set(inputs
  "${ROOT}/docs/examples/abs.cir"
  "${ROOT}/tests/cir/arith.cir"
  "${ROOT}/tests/cir/control_flow.cir"
  "${ROOT}/tests/cir/recursion.cir"
  "${ROOT}/tests/cir/nesting.cir"
  "${ROOT}/tests/cir/div_edge.cir"
  "${ROOT}/tests/cir/shift_edge.cir")

foreach(input IN LISTS inputs)
  get_filename_component(stem "${input}" NAME_WE)
  set(wat "${OUTDIR}/${stem}.generated.wat")
  set(wasm "${OUTDIR}/${stem}.generated.wasm")

  execute_process(
    COMMAND "${MTIRC}" --emit=wat "${input}" -o "${wat}"
    RESULT_VARIABLE emitStatus
    ERROR_VARIABLE emitErrors)
  if(NOT emitStatus EQUAL 0)
    message(FATAL_ERROR "mtirc --emit=wat ${input} failed:\n${emitErrors}")
  endif()

  execute_process(
    COMMAND "${WAT2WASM}" "${wat}" -o "${wasm}"
    RESULT_VARIABLE asStatus
    ERROR_VARIABLE asErrors)
  if(NOT asStatus EQUAL 0)
    file(READ "${wat}" generated)
    message(FATAL_ERROR
      "wat2wasm rejected the WebAssembly generated from ${input}:\n${asErrors}\n"
      "--- generated ---\n${generated}")
  endif()

  if(WASM_VALIDATE)
    execute_process(
      COMMAND "${WASM_VALIDATE}" "${wasm}"
      RESULT_VARIABLE validateStatus
      ERROR_VARIABLE validateErrors)
    if(NOT validateStatus EQUAL 0)
      file(READ "${wat}" generated)
      message(FATAL_ERROR
        "wasm-validate rejected the module generated from ${input}:\n"
        "${validateErrors}\n--- generated ---\n${generated}")
    endif()
  endif()

  message(STATUS "wat2wasm accepted the WebAssembly generated from ${input}")
endforeach()
