# RunCorpusCheck.cmake -- run each corpus program on the reference stack VM
# and compare what it printed with what the source says it should print.
#
# Every figure below is derived from the MiniLang or CIR source, not from a
# previous run of this compiler, so the check cannot be satisfied by a
# consistent mistake.
#
# Invoked by CTest with -DMTIRC and -DROOT.

# name, expected stdout, expected exit status
set(cases
  "arith|55|0"
  "control_flow|16|0"
  "nesting|3|0"
  "shift_edge|1;-2147483648;1;2|0"
  "recursion|3628800;1|0"
  # div_edge prints 7 / 2 and then traps on INT_MIN / -1, which is exit 4.
  "div_edge|3|4")

foreach(case IN LISTS cases)
  string(REPLACE "|" ";" parts "${case}")
  list(GET parts 0 name)
  list(GET parts 1 expectedLines)
  list(GET parts 2 expectedStatus)

  execute_process(
    COMMAND "${MTIRC}" --run "${ROOT}/tests/cir/${name}.cir"
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors
    RESULT_VARIABLE status)

  string(REPLACE ";" "\n" expected "${expectedLines}")
  string(APPEND expected "\n")
  string(REPLACE "\r\n" "\n" actual "${actual}")

  if(NOT status EQUAL expectedStatus)
    message(FATAL_ERROR
      "mtirc --run ${name}.cir exited ${status}, expected ${expectedStatus}\n"
      "stderr:\n${errors}")
  endif()
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
      "mtirc --run ${name}.cir printed:\n${actual}\nexpected:\n${expected}")
  endif()

  message(STATUS "stack VM ran ${name}.cir as expected")
endforeach()
