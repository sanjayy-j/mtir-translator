"""CIR -- Common Intermediate Representation: core data structures.

Module M3a (data half).  Owner: Member 3.
Status: COMPLETE for the instruction shape; the builder that produces these
        from an AST is Week 5.

Design notes (see docs/cir-spec.md for the full specification):

  * Register-based three-address code, not stack-based.  Lowering registers to
    a stack is a linear pass; the reverse requires reconstructing stack heights.
  * Typed on every instruction, so no back end ever infers a type.
  * NOT in SSA form.  Mutable locals become alloca/load/store; LLVM's mem2reg
    recovers SSA downstream.  See docs/cir-spec.md section 6.
  * Each basic block ends in exactly one terminator (br, br.cond or ret).
    The verifier (M3b) enforces this.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Union


class Ty(Enum):
    I1 = "i1"
    I32 = "i32"
    I64 = "i64"
    F64 = "f64"
    PTR = "ptr"
    VOID = "void"

    def __str__(self) -> str:
        return self.value


# --------------------------------------------------------------------------
# Values
# --------------------------------------------------------------------------
@dataclass(frozen=True)
class Reg:
    """A virtual register.  Unbounded in number; named %0, %t1, %x, ..."""
    name: str
    ty: Ty

    def __str__(self) -> str:
        return f"%{self.name}"


@dataclass(frozen=True)
class ConstInt:
    value: int
    ty: Ty = Ty.I32

    def __str__(self) -> str:
        return str(self.value)


@dataclass(frozen=True)
class ConstFloat:
    value: float
    ty: Ty = Ty.F64

    def __str__(self) -> str:
        return repr(self.value)


@dataclass(frozen=True)
class GlobalRef:
    name: str
    ty: Ty = Ty.PTR

    def __str__(self) -> str:
        return f"@{self.name}"


Value = Union[Reg, ConstInt, ConstFloat, GlobalRef]


# --------------------------------------------------------------------------
# Instructions
# --------------------------------------------------------------------------
ARITH_OPS = {"add", "sub", "mul", "sdiv", "udiv", "srem", "urem",
             "fadd", "fsub", "fmul", "fdiv", "neg"}
BIT_OPS = {"and", "or", "xor", "not", "shl", "ashr", "lshr"}
CMP_OPS = {f"icmp.{c}" for c in
           ("eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge")} | \
          {f"fcmp.{c}" for c in ("oeq", "one", "olt", "ole", "ogt", "oge")}
CONV_OPS = {"sext", "zext", "trunc", "sitofp", "fptosi"}
MEM_OPS = {"alloca", "load", "store", "gep"}
CALL_OPS = {"call"}
INTRINSICS = {"print.i32", "print.f64", "trap"}
TERMINATORS = {"br", "br.cond", "ret"}

ALL_OPS = (ARITH_OPS | BIT_OPS | CMP_OPS | CONV_OPS | MEM_OPS
           | CALL_OPS | INTRINSICS | TERMINATORS)


@dataclass
class Instr:
    """One CIR instruction.

    dest is None for instructions that produce no value (store, br, ret void,
    print.*).  `ty` is the type of the *result* for value-producing
    instructions, and the type of the operands for stores and comparisons.
    """
    op: str
    ty: Ty = Ty.VOID
    dest: Optional[Reg] = None
    args: List[Value] = field(default_factory=list)
    labels: List[str] = field(default_factory=list)   # branch targets
    callee: Optional[str] = None                      # for 'call'

    def is_terminator(self) -> bool:
        return self.op in TERMINATORS


@dataclass
class BasicBlock:
    label: str
    instrs: List[Instr] = field(default_factory=list)

    @property
    def terminator(self) -> Optional[Instr]:
        return self.instrs[-1] if self.instrs and self.instrs[-1].is_terminator() else None

    def successors(self) -> List[str]:
        term = self.terminator
        return list(term.labels) if term else []


@dataclass
class Param:
    name: str
    ty: Ty


@dataclass
class Function:
    name: str
    params: List[Param] = field(default_factory=list)
    ret_ty: Ty = Ty.VOID
    blocks: List[BasicBlock] = field(default_factory=list)

    def block(self, label: str) -> Optional[BasicBlock]:
        return next((b for b in self.blocks if b.label == label), None)

    @property
    def entry(self) -> Optional[BasicBlock]:
        return self.blocks[0] if self.blocks else None

    def cfg(self) -> dict[str, List[str]]:
        """Adjacency list of the control-flow graph, keyed by block label."""
        return {b.label: b.successors() for b in self.blocks}


@dataclass
class Global:
    name: str
    ty: Ty
    init: Optional[Value] = None
    array_len: Optional[int] = None


@dataclass
class Module:
    name: str = "module"
    globals: List[Global] = field(default_factory=list)
    functions: List[Function] = field(default_factory=list)

    def function(self, name: str) -> Optional[Function]:
        return next((f for f in self.functions if f.name == name), None)
