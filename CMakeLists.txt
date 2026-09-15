# Multi-Target Intermediate Representation Translator -- C++17 build.
#
# One static library per subsystem, so the dependency graph is enforced at
# link time rather than by convention.  The invariant that matters:
#
#     mtir_cir depends on nothing but mtir_support.
#
# No AST, no semantic analysis, no back end.  That is what lets a back end be
# built and tested against checked-in .cir files with no front end involved --
# the property docs/cir-spec.md section 1 claims for the textual format.
#
#     mtir_support  ->  (nothing)
#     mtir_cir      ->  mtir_support
#     mtir_opt      ->  mtir_cir
#     mtir_llvm     ->  mtir_cir
#     mtirc         ->  all of the above
#     mtir_tests    ->  all of the above
#
# CMake 3.16 is the version Ubuntu 20.04 ships, which is the oldest machine
# the team is likely to build on.

cmake_minimum_required(VERSION 3.16)
project(mtir-translator LANGUAGES CXX VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Warnings
#
# Deliberately NOT warnings-as-errors by default: a team member on a different
# compiler version should not be blocked by a warning the author never saw.
# CI turns it on with -DMTIR_WERROR=ON.
# ---------------------------------------------------------------------------
# The base set has been verified clean with MSVC 19.29 at /W4 /permissive-.
# MTIR_STRICT_WARNINGS adds the conversion and style warnings, which have NOT
# yet been run against GCC or Clang -- turning them on by default would block
# a teammate on a warning nobody has looked at.  Enable them once someone has
# built on GCC and fixed what they report.
option(MTIR_WERROR "Treat compiler warnings as errors" OFF)
option(MTIR_STRICT_WARNINGS "Enable conversion and style warnings (unverified on GCC/Clang)" OFF)

add_library(mtir_warnings INTERFACE)
if(MSVC)
  target_compile_options(mtir_warnings INTERFACE /W4 /permissive-)
  if(MTIR_WERROR)
    target_compile_options(mtir_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(mtir_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wunused -Woverloaded-virtual)
  if(MTIR_STRICT_WARNINGS)
    target_compile_options(mtir_warnings INTERFACE
      -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-align)
  endif()
  if(MTIR_WERROR)
    target_compile_options(mtir_warnings INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
add_library(mtir_support
  src/support/Diagnostic.cpp)
target_include_directories(mtir_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mtir_support PRIVATE mtir_warnings)

add_library(mtir_cir
  src/cir/Type.cpp
  src/cir/Arith.cpp
  src/cir/Value.cpp
  src/cir/Opcode.cpp
  src/cir/Instruction.cpp
  src/cir/Function.cpp
  src/cir/Module.cpp
  src/cir/CFG.cpp
  src/cir/Verifier.cpp
  src/cir/Printer.cpp
  src/cir/Parser.cpp)
target_link_libraries(mtir_cir PUBLIC mtir_support PRIVATE mtir_warnings)

add_library(mtir_opt
  src/opt/ConstantFolding.cpp
  src/opt/CopyPropagation.cpp
  src/opt/DeadCodeElimination.cpp
  src/opt/PassManager.cpp)
target_link_libraries(mtir_opt PUBLIC mtir_cir PRIVATE mtir_warnings)

add_library(mtir_llvm
  src/backend/llvm/TypeMap.cpp
  src/backend/llvm/Guards.cpp
  src/backend/llvm/EmitLL.cpp)
target_link_libraries(mtir_llvm PUBLIC mtir_cir PRIVATE mtir_warnings)

# A convenience target for anything that wants the whole middle end.
add_library(mtir_core INTERFACE)
target_link_libraries(mtir_core INTERFACE mtir_cir mtir_opt mtir_llvm)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(mtirc tools/mtirc/main.cpp)
target_link_libraries(mtirc PRIVATE mtir_core mtir_warnings)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)

if(BUILD_TESTING)
  add_executable(mtir_tests
    tests/mtir_test.cpp
    tests/CIRCoreTests.cpp
    tests/CIRTextTests.cpp
    tests/CFGTests.cpp
    tests/OptimizerTests.cpp
    tests/LLVMBackendTests.cpp
    tests/CorpusTests.cpp)
  target_include_directories(mtir_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(mtir_tests PRIVATE mtir_core mtir_warnings)
  # Golden files live in the repository, so the tests need to know where that
  # is regardless of the working directory the runner uses.
  target_compile_definitions(mtir_tests
    PRIVATE MTIR_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

  add_test(NAME unit COMMAND mtir_tests)

  # -- golden-file checks through the CLI ----------------------------------
  add_test(NAME golden_cir_roundtrip
    COMMAND ${CMAKE_COMMAND}
      -DMTIRC=$<TARGET_FILE:mtirc>
      -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -DSTAGE=cir
      -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/abs.cir
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GoldenCheck.cmake)

  add_test(NAME golden_llvm
    COMMAND ${CMAKE_COMMAND}
      -DMTIRC=$<TARGET_FILE:mtirc>
      -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -DSTAGE=ll
      -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/abs.gen.ll
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GoldenCheck.cmake)

  # -- the real assembler, when it is available ----------------------------
  #
  # This is the authority on whether the emitted IR is valid.  When llvm-as is
  # not installed the test is registered as skipped rather than silently
  # passing, so a run on a machine without LLVM cannot be mistaken for a run
  # that validated anything.
  find_program(MTIR_LLVM_AS NAMES llvm-as)
  if(MTIR_LLVM_AS)
    add_test(NAME llvm_as_accepts_generated_ir
      COMMAND ${CMAKE_COMMAND}
        -DMTIRC=$<TARGET_FILE:mtirc>
        -DLLVM_AS=${MTIR_LLVM_AS}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LlvmAsCheck.cmake)
  else()
    message(STATUS "llvm-as not found: the LLVM validation test will be skipped")
    add_test(NAME llvm_as_accepts_generated_ir
      COMMAND ${CMAKE_COMMAND} -E echo
        "SKIPPED: llvm-as is not installed; the generated IR was not validated")
    set_tests_properties(llvm_as_accepts_generated_ir PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIPPED")
  endif()
endif()

message(STATUS "mtir-translator ${PROJECT_VERSION}, C++${CMAKE_CXX_STANDARD}, build type ${CMAKE_BUILD_TYPE}")
