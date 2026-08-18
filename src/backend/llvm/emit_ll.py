"""CIR -> textual LLVM IR.

Module M5.  Owner: Member 3.
Status: PLANNED -- Week 6-7.
"""
def emit(module):
    """Emit textual LLVM IR for a CIR module.

    The CFG maps one-to-one, so this back end is close to a printer plus the
    guard code from guards.py.  Every output must be accepted by llvm-as.
    """
    raise NotImplementedError(
        "LLVM back end is scheduled for Weeks 6-7 (M5, Member 3)")
