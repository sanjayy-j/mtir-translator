"""AST -> CIR lowering.

Module M3a.  Owner: Member 3.
Status: PLANNED -- Week 5.
"""
def build(program):
    """Walk the typed AST and emit a CIR Module.

    Plan (Week 5):
      1. One Function per FnDecl; parameters become named registers.
      2. Flatten each expression to three-address form with a fresh-register
         allocator (%t0, %t1, ...).
      3. Split statements into basic blocks; every block gets exactly one
         terminator.  if/while/for generate the label pattern documented in
         docs/cir-spec.md section 5.
      4. Mutable locals become alloca in the entry block plus load/store.
    """
    raise NotImplementedError(
        "CIR builder is scheduled for Week 5 (M3a, Member 3)")
