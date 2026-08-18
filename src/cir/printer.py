"""CIR textual printer.

Module M3c (print half).  Owner: Member 3.
Status: COMPLETE.  The matching parser (text -> Module) is Week 6; together
        they must satisfy the round-trip property  parse(print(m)) == m,
        which is what makes .cir a real interchange format between the middle
        end and every back end.

Output is deterministic: no dict iteration order leaks into the text, so the
golden-file tests are stable.
"""

from __future__ import annotations

from .ir import (
    BasicBlock, ConstFloat, ConstInt, Function, Global, GlobalRef, Instr,
    Module, Reg, Ty, Value,
)


def fmt_value(v: Value) -> str:
    return str(v)


def fmt_instr(ins: Instr) -> str:
    args = ", ".join(fmt_value(a) for a in ins.args)

    if ins.op == "br":
        return f"br {ins.labels[0]}"
    if ins.op == "br.cond":
        return f"br {args} ? {ins.labels[0]} : {ins.labels[1]}"
    if ins.op == "ret":
        return "ret void" if not ins.args else f"ret {ins.ty} {args}"
    if ins.op == "store":
        return f"store {ins.ty} {args}"
    if ins.op == "call":
        call = f"call {ins.ty} @{ins.callee}({args})"
        return f"{ins.dest} = {call}" if ins.dest else call
    if ins.op == "alloca":
        n = f", {fmt_value(ins.args[0])}" if ins.args else ""
        return f"{ins.dest} = alloca {ins.ty}{n}"
    if ins.op in ("print.i32", "print.f64"):
        return f"{ins.op} {args}"
    if ins.op == "trap":
        return "trap"

    body = f"{ins.op} {ins.ty} {args}" if args else f"{ins.op} {ins.ty}"
    return f"{ins.dest} = {body}" if ins.dest else body


def fmt_block(blk: BasicBlock) -> str:
    lines = [f"{blk.label}:"]
    lines += [f"  {fmt_instr(i)}" for i in blk.instrs]
    return "\n".join(lines)


def fmt_function(fn: Function) -> str:
    params = ", ".join(f"{p.ty} %{p.name}" for p in fn.params)
    head = f"func @{fn.name}({params}) -> {fn.ret_ty} {{"
    body = "\n".join(fmt_block(b) for b in fn.blocks)
    return f"{head}\n{body}\n}}"


def fmt_global(g: Global) -> str:
    ty = f"{g.ty}[{g.array_len}]" if g.array_len is not None else str(g.ty)
    init = f" = {fmt_value(g.init)}" if g.init is not None else ""
    return f"global @{g.name} : {ty}{init}"


def print_module(mod: Module) -> str:
    parts = [fmt_global(g) for g in mod.globals]
    if parts:
        parts.append("")
    parts += [fmt_function(f) for f in mod.functions]
    return "\n".join(parts) + "\n"
