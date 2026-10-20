# RunCorpusCheck.cmake -- run each corpus program on the reference stack VM
# and compare what it printed with what the source says it should print.
#
# Every figure below is derived from the MiniLang or CIR source, not from a
# previous run of this compiler, so the check cannot be satisfied by a
# consistent mistake.
#
# The expectations are held in one variable per case rather than in a packed
# list: CMake treats ";" as a list separator, and shift_edge's expected output
# is four lines, so any scheme that packs newlines into a list element comes
# apart the moment a case prints more than once.
#
# Invoked by CTest with -DMTIRC and -DROOT.

set(cases arith control_flow nesting shift_edge recursion div_edge)

set(expect_arith        "55\n")
set(status_arith        0)

set(expect_control_flow "16\n")
set(status_control_flow 0)

set(expect_nesting      "3\n")
set(status_nesting      0)

set(expect_shift_edge   "1\n-2147483648\n1\n2\n")
set(status_shift_edge   0)

set(expect_recursion    "3628800\n1\n")
set(status_recursion    0)

# div_edge prints 7 / 2 and then traps on INT_MIN / -1, which is exit 4.
set(expect_div_edge     "3\n")
set(status_div_edge     4)

foreach(name IN LISTS cases)
  execute_process(
    COMMAND "${MTIRC}" --run "${ROOT}/tests/cir/${name}.cir"
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors
    RESULT_VARIABLE status)

  string(REPLACE "\r\n" "\n" actual "${actual}")

  if(NOT status EQUAL ${status_${name}})
    message(FATAL_ERROR
      "mtirc --run ${name}.cir exited ${status}, expected ${status_${name}}\n"
      "stderr:\n${errors}")
  endif()
  if(NOT actual STREQUAL "${expect_${name}}")
    message(FATAL_ERROR
      "mtirc --run ${name}.cir printed:\n${actual}\nexpected:\n${expect_${name}}")
  endif()

  message(STATUS "stack VM ran ${name}.cir as expected")
endforeach()
