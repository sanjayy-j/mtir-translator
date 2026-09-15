"""CIR -> textual LLVM IR.

Module M5.  Owner: Member 3.
Status: IMPLEMENTED for every CIR opcode in docs/cir-spec.md section 3.

Why textual IR and not the LLVM C API
-------------------------------------
The artefact under review is the translation, not the bindings.  Text is
diffable, is what the report's figures show, and is checked by ``llvm-as``
in CI -- which is a real verifier, not a smoke test.  It also keeps the
project dependency-free, matching the choice already made in requirements.txt.

The shape of the pass
---------------------
CIR's CFG maps one-to-one onto LLVM's: both allow a branch to any label in
the function, so no restructuring is needed and this back end is close to a
printer.  That is the contrast the report draws against the WebAssembly back
end (M6b), which has to rebuild the control flow as nested regions.

What stops it being *only* a printer is guards.py.  Four CIR operations mean
something LLVM leaves undefined, so this emitter splits the current block and
branches to a trap block instead of emitting the bare LLVM instruction.  A
block split is why the emitter works on a flat line buffer with its own label
counter rather than emitting one LLVM block per CIR block.
"""

from __future__ import annotations

from typing import Dict, List, Optional

from ...cir.ir import (ConstFloat, ConstInt, Function, GlobalRef, INT_WIDTH,
                       Instr, Module, Reg, Ty, Value, is_cmp, wrap_int)
from . import guards
from .typemap import llvm_ty

# CIR opcode -> LLVM binary instruction, for the cases that are a rename.
_BINOP = {
    "add": "add", "sub": "sub", "mul": "mul",
    "sdiv": "sdiv", "udiv": "udiv", "srem": "srem", "urem": "urem",
    "fadd": "fadd", "fsub": "fsub", "fmul": "fmul", "fdiv": "fdiv",
    "and": "and", "or": "or", "xor": "xor",
    "shl": "shl", "ashr": "ashr", "lshr": "lshr",
}
_CONV = {"sext": "sext", "zext": "zext", "trunc": "trunc",
         "sitofp": "sitofp", "fptosi": "fptosi"}
_DIV_OPS = ("sdiv", "udiv", "srem", "urem")
_SHIFT_OPS = ("shl", "ashr", "lshr")

_FMT = {"print.i32": ("@.fmt.i32", "i32"), "print.f64": ("@.fmt.f64", "double")}


class EmitError(Exception):
    """CIR this back end cannot lower.  Always a bug in the input, not here."""


def fmt_float(value: float) -> str:
    """An LLVM double literal.

    LLVM's lexer only treats a number as floating point once it has seen a
    '.', so an exponent form such as ``1e-05`` would lex as the integer 1.
    Infinities and NaN have no decimal spelling at all and use the raw 64-bit
    hexadecimal form.
    """
    if value != value or value in (float("inf"), float("-inf")):
        import struct
        bits = struct.unpack("<Q", struct.pack("<d", value))[0]
        return f"0x{bits:016X}"
    text = repr(float(value))
    if "." not in text:
        text = text.replace("e", ".0e") if "e" in text else text + ".0"
    return text


class Emitter:
    """Emits one function into a flat line buffer.

    Guards split basic blocks, so the buffer holds already-formatted LLVM
    lines and label lines rather than a list of blocks.  ``tmp`` and ``label``
    hand out names under the ``g.`` prefix, which the builder never generates
    -- its registers are ``%tN`` or a source name with an ``.addr`` suffix.
    """

    def __init__(self, sigs: Dict[str, tuple], needs: set) -> None:
        self.sigs = sigs
        self.needs = needs
        self.out: List[str] = []
        self.n = 0
        self.extents: Dict[str, int] = {}

    # -- output ------------------------------------------------------------
    def line(self, text: str) -> None:
        self.out.append(f"  {text}")

    def label_line(self, name: str) -> None:
        self.out.append(f"{name}:")

    def tmp(self) -> str:
        self.n += 1
        return f"%g.{self.n}"

    def label(self, base: str) -> str:
        self.n += 1
        return f"{base}.{self.n}"

    def trap_branch(self, cond: str, kind: str) -> None:
        """Branch to a fresh trap block when ``cond`` holds, and carry on.

        The continuation label is opened immediately, so the caller can keep
        emitting into what is now a new LLVM basic block.
        """
        self.needs.add("trap")
        trap_l = self.label(f"trap.{kind}")
        cont_l = self.label(f"cont.{kind}")
        self.line(f"br i1 {cond}, label %{trap_l}, label %{cont_l}")
        self.label_line(trap_l)
        self.line("call void @llvm.trap()")
        self.line("unreachable")
        self.label_line(cont_l)

    # -- values ------------------------------------------------------------
    def val(self, value: Value, ty: Ty) -> str:
        if isinstance(value, Reg):
            return f"%{value.name}"
        if isinstance(value, GlobalRef):
            return f"@{value.name}"
        if isinstance(value, ConstFloat):
            return fmt_float(value.value)
        if isinstance(value, ConstInt):
            if ty is Ty.I1:
                return "true" if value.value & 1 else "false"
            if ty is Ty.F64:
                return fmt_float(float(value.value))
            return str(wrap_int(value.value, ty) if ty in INT_WIDTH
                       else value.value)
        raise EmitError(f"cannot emit operand {value!r}")

    def ty_of(self, value: Value, default: Ty) -> Ty:
        if isinstance(value, GlobalRef):
            return Ty.PTR
        return getattr(value, "ty", default)

    # -- functions ---------------------------------------------------------
    def function(self, fn: Function) -> str:
        self.out = []
        self.n = 0
        self.extents = {
            i.dest.name: i.args[0].value
            for b in fn.blocks for i in b.instrs
            if i.op == "alloca" and i.dest is not None and i.args
            and isinstance(i.args[0], ConstInt)
        }

        params = ", ".join(f"{llvm_ty(p.ty)} %{p.name}" for p in fn.params)
        head = f"define {llvm_ty(fn.ret_ty)} @{fn.name}({params}) {{"

        for block in fn.blocks:
            self.label_line(block.label)
            for instr in block.instrs:
                self.instr(instr)
        return "\n".join([head] + self.out + ["}"])

    # -- instructions ------------------------------------------------------
    def instr(self, ins: Instr) -> None:
        op = ins.op
        ty = ins.ty
        lty = llvm_ty(ty)
        dest = f"%{ins.dest.name}" if ins.dest is not None else None
        args = [self.val(a, ty) for a in ins.args]

        if op == "br":
            self.line(f"br label %{ins.labels[0]}")
            return
        if op == "br.cond":
            self.line(f"br i1 {args[0]}, label %{ins.labels[0]}, "
                      f"label %{ins.labels[1]}")
            return
        if op == "ret":
            self.line("ret void" if not ins.args else f"ret {lty} {args[0]}")
            return
        if op == "trap":
            # Not a CIR terminator, so no 'unreachable' here: the block's own
            # terminator still follows.  llvm.trap is noreturn, so control
            # never actually reaches it.
            self.needs.add("trap")
            self.line("call void @llvm.trap()")
            return

        if op in _FMT:
            self.needs.add("printf")
            symbol, arg_ty = _FMT[op]
            self.line(f"call i32 (ptr, ...) @printf(ptr {symbol}, "
                      f"{arg_ty} {args[0]})")
            return

        if op == "call":
            self.call(ins, dest, lty)
            return

        if op == "alloca":
            count = f", i32 {args[0]}" if ins.args else ""
            self.line(f"{dest} = alloca {lty}{count}")
            return
        if op == "load":
            self.line(f"{dest} = load {lty}, ptr {args[0]}")
            return
        if op == "store":
            self.line(f"store {lty} {args[0]}, ptr {args[1]}")
            return
        if op == "gep":
            base = ins.args[0]
            extent = self.extents.get(base.name) if isinstance(base, Reg) else None
            index_text = self.val(ins.args[1], Ty.I32)
            if extent is not None:
                guards.guard_bounds(self, extent, ins.args[1], index_text)
            self.line(f"{dest} = getelementptr {lty}, ptr {args[0]}, "
                      f"i32 {index_text}")
            return

        if is_cmp(op):
            kind, _, pred = op.partition(".")
            # CIR prints the *operand* type on a comparison; the result is i1.
            self.line(f"{dest} = {kind} {pred} {lty} {args[0]}, {args[1]}")
            return

        if op in _CONV:
            src = llvm_ty(self.ty_of(ins.args[0], Ty.I32))
            if op == "fptosi":
                guards.guard_fptosi(self, ty, args[0])
            self.line(f"{dest} = {_CONV[op]} {src} {args[0]} to {lty}")
            return

        if op == "neg":
            if ty is Ty.F64:
                self.line(f"{dest} = fneg double {args[0]}")
            else:
                # LLVM has no integer negate; CIR's 'neg' becomes 0 - x, which
                # is what the builder emits directly for integers anyway.
                self.line(f"{dest} = sub {lty} 0, {args[0]}")
            return
        if op == "not":
            ones = "true" if ty is Ty.I1 else "-1"
            self.line(f"{dest} = xor {lty} {args[0]}, {ones}")
            return

        if op in _BINOP:
            rhs = args[1]
            if op in _DIV_OPS:
                guards.guard_div(self, op, ty, args[0], ins.args[1], rhs)
            elif op in _SHIFT_OPS:
                rhs = guards.mask_shift_count(self, ty, ins.args[1], rhs)
            # Deliberately no 'nsw'/'nuw': CIR defines signed overflow as
            # wraparound (docs/divergence.md row 4), and those flags would
            # make it poison instead.
            self.line(f"{dest} = {_BINOP[op]} {lty} {args[0]}, {rhs}")
            return

        raise EmitError(f"no LLVM lowering for CIR opcode {op!r}")

    def call(self, ins: Instr, dest: Optional[str], lty: str) -> None:
        sig = self.sigs.get(ins.callee)
        param_tys = sig[1] if sig else [self.ty_of(a, Ty.I32) for a in ins.args]
        if len(param_tys) != len(ins.args):
            raise EmitError(
                f"@{ins.callee} takes {len(param_tys)} argument(s), "
                f"{len(ins.args)} given")
        args = ", ".join(f"{llvm_ty(t)} {self.val(a, t)}"
                         for a, t in zip(ins.args, param_tys))
        call = f"call {lty} @{ins.callee}({args})"
        self.line(call if dest is None else f"{dest} = {call}")


# --------------------------------------------------------------------------
# Module
# --------------------------------------------------------------------------
_RUNTIME = {
    "printf": [
        'declare i32 @printf(ptr, ...)',
        '@.fmt.i32 = private unnamed_addr constant [4 x i8] c"%d\\0A\\00"',
        '@.fmt.f64 = private unnamed_addr constant [4 x i8] c"%f\\0A\\00"',
    ],
    "trap": ['declare void @llvm.trap()'],
}


def emit(module: Module) -> str:
    """Emit textual LLVM IR for a CIR module.

    The runtime declarations at the end are emitted only when something used
    them, so a module that neither prints nor traps produces no unused
    declarations and the golden files stay small.
    """
    sigs = {f.name: (f.ret_ty, [p.ty for p in f.params]) for f in module.functions}
    needs: set = set()
    emitter = Emitter(sigs, needs)

    bodies = [emitter.function(fn) for fn in module.functions]

    # Shared verbatim with the C++ back end in src/backend/llvm/EmitLL.cpp so
    # that docs/examples/abs.gen.ll is a golden file for both.
    parts = [f"; CIR module {module.name!r} lowered by the MTIR LLVM back end "
             f"(M5)"]
    for g in module.globals:
        gty = llvm_ty(g.ty)
        init = emitter.val(g.init, g.ty) if g.init is not None else "zeroinitializer"
        if g.array_len is not None:
            parts.append(f"@{g.name} = global [{g.array_len} x {gty}] "
                         f"zeroinitializer")
        else:
            parts.append(f"@{g.name} = global {gty} {init}")
    if module.globals:
        parts.append("")

    parts += bodies

    runtime = [line for key in ("printf", "trap") if key in needs
               for line in _RUNTIME[key]]
    if runtime:
        parts.append("")
        parts += runtime

    return "\n".join(parts) + "\n"
