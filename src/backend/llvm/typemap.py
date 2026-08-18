"""CIR type -> LLVM type mapping.

Module M5.  Owner: Member 3.
Status: PLANNED -- Week 6.
"""
CIR_TO_LLVM = {
    "i1": "i1", "i32": "i32", "i64": "i64", "f64": "double",
    "ptr": "ptr", "void": "void",
}
