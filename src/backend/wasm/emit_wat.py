"""CIR -> WebAssembly text format.

Module M6c.  Owner: Member 4.
Status: PLANNED -- Week 7-8.
"""
def emit(module):
    """Emit .wat for a CIR module.

    Uses reg2stack.lower_block for the instruction sequence and
    structurer.structure for control flow.  Globals and arrays are laid out in
    linear memory behind a shadow stack pointer, because WebAssembly locals
    have no address.  Every output must be accepted by wat2wasm.
    """
    raise NotImplementedError(
        "WebAssembly back end is scheduled for Weeks 7-8 (M6c, Member 4)")
