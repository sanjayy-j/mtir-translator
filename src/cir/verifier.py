"""CIR well-formedness verifier.

Module M3b.  Owner: Member 2.
Status: PLANNED -- Week 5.
"""
CHECKS = [
    "every basic block ends in exactly one terminator",
    "no instruction follows a terminator",
    "every branch target names an existing block",
    "every register is defined before it is used",
    "the entry block has no predecessors",
    "operand types match the opcode signature",
    "the result type of an icmp/fcmp is i1",
    "a function whose return type is not void ends every path in 'ret <ty>'",
]


def verify(module):
    """Return a list of diagnostics; empty means the module is well formed.

    TODO(Member 2, Week 5): implement the eight checks above.  Objective O1
    requires at least 8 distinct classes of malformed IR to be detected.
    """
    raise NotImplementedError(
        "CIR verifier is scheduled for Week 5 (M3b, Member 2)")
