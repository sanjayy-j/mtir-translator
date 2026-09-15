# LlvmAsCheck.cmake -- assemble the generated LLVM IR with the real llvm-as.
#
# This is the only check that can honestly claim the emitted IR is valid
# LLVM; the structural validator in tests/LLVMBackendTests.cpp is a faster,
# more readable approximation of it.
#
# Invoked by CTest with -DMTIRC, -DLLVM_AS, -DROOT and -DOUTDIR.

set(inputs
  "${ROOT}/docs/examples/abs.cir")

foreach(input IN LISTS inputs)
  get_filename_component(stem "${input}" NAME_WE)
  set(ll "${OUTDIR}/${stem}.generated.ll")
  set(bc "${OUTDIR}/${stem}.generated.bc")

  execute_process(
    COMMAND "${MTIRC}" --emit=ll "${input}" -o "${ll}"
    RESULT_VARIABLE emitStatus
    ERROR_VARIABLE emitErrors)
  if(NOT emitStatus EQUAL 0)
    message(FATAL_ERROR "mtirc --emit=ll ${input} failed:\n${emitErrors}")
  endif()

  execute_process(
    COMMAND "${LLVM_AS}" "${ll}" -o "${bc}"
    RESULT_VARIABLE asStatus
    ERROR_VARIABLE asErrors)
  if(NOT asStatus EQUAL 0)
    file(READ "${ll}" generated)
    message(FATAL_ERROR
      "llvm-as rejected the IR generated from ${input}:\n${asErrors}\n"
      "--- generated ---\n${generated}")
  endif()

  message(STATUS "llvm-as accepted the IR generated from ${input}")
endforeach()
