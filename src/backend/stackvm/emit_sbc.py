"""CIR -> flat stack bytecode.

Module M7.  Owner: Member 4.
Status: PLANNED -- Week 9.
"""
def emit(module):
    """Reuses reg2stack, but keeps labels flat with absolute jump offsets --
    no structuring pass.  The difference in emitted instruction count against
    the WebAssembly back end isolates the cost of structuring."""
    raise NotImplementedError(
        "stack bytecode back end is scheduled for Week 9 (M7, Member 4)")
