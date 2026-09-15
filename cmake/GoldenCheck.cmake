# GoldenCheck.cmake -- run mtirc over docs/examples/abs.cir and compare the
# output with a checked-in golden file.
#
# Invoked by CTest with -DMTIRC, -DROOT, -DSTAGE and -DGOLDEN.
#
# Line endings are normalised before comparing: the repository stores LF, but
# a Windows checkout may hand the tool CRLF, and that difference says nothing
# about whether the compiler is correct.

execute_process(
  COMMAND "${MTIRC}" "--emit=${STAGE}" "${ROOT}/docs/examples/abs.cir"
  OUTPUT_VARIABLE actual
  ERROR_VARIABLE errors
  RESULT_VARIABLE status)

if(NOT status EQUAL 0)
  message(FATAL_ERROR "mtirc --emit=${STAGE} failed (${status}):\n${errors}")
endif()

file(READ "${GOLDEN}" expected)

string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")

if(NOT actual STREQUAL expected)
  message(FATAL_ERROR
    "output of 'mtirc --emit=${STAGE}' does not match ${GOLDEN}\n"
    "--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

message(STATUS "golden file ${GOLDEN} matches")
