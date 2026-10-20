"""CIR -> valid WebAssembly Text (WAT) emitter.
Uses a small program-counter dispatcher, so arbitrary reducible CIR CFGs remain valid structured Wasm."""
from __future__ import annotations
from ...cir.ir import *

def emit(mod):
    lines=['(module']
    lines += ['  (import "env" "print_int" (func $print_int (param i32)))',
              '  (import "env" "print_float" (func $print_float (param f64)))',
              '  (memory (export "memory") 2)']
    for g in mod.globals:
        if g.ty==Ty.I32: lines.append(f'  (global ${g.name} (mut i32) (i32.const {getattr(g.init,"value",0)}))')
        elif g.ty==Ty.I64: lines.append(f'  (global ${g.name} (mut i64) (i64.const {getattr(g.init,"value",0)}))')
        elif g.ty==Ty.F64: lines.append(f'  (global ${g.name} (mut f64) (f64.const {getattr(g.init,"value",0)}))')
    for f in mod.functions: lines += _fn(f)
    lines.append(')'); return '\n'.join(lines)+'\n'

def wt(t): return {Ty.I1:'i32',Ty.I32:'i32',Ty.I64:'i64',Ty.F64:'f64',Ty.PTR:'i32',Ty.VOID:'i32'}[t]
def op(ins):
    m={'add':'add','sub':'sub','mul':'mul','sdiv':'div_s','udiv':'div_u','srem':'rem_s','urem':'rem_u','and':'and','or':'or','xor':'xor','shl':'shl','ashr':'shr_s','lshr':'shr_u'}
    if ins.op in m: return f'{wt(ins.ty)}.{m[ins.op]}'
    if ins.op in ('fadd','fsub','fmul','fdiv'): return f'f64.{ins.op[1:]}'
    if ins.op=='neg': return None
    if ins.op=='not': return f'{wt(ins.ty)}.xor'
    if ins.op.startswith('icmp.'):
        c=ins.op[5:]; return 'i32.'+{'eq':'eq','ne':'ne','slt':'lt_s','sle':'le_s','sgt':'gt_s','sge':'ge_s','ult':'lt_u','ule':'le_u','ugt':'gt_u','uge':'ge_u'}[c]
    if ins.op.startswith('fcmp.'):
        c=ins.op[5:]; return 'f64.'+{'oeq':'eq','one':'ne','olt':'lt','ole':'le','ogt':'gt','oge':'ge'}[c]
    return None

def val(v):
    if isinstance(v,Reg): return f'(local.get ${v.name})'
    if isinstance(v,ConstInt): return f'({wt(v.ty)}.const {v.value})'
    if isinstance(v,ConstFloat): return f'(f64.const {v.value})'
    if isinstance(v,GlobalRef): return f'(global.get ${v.name})'

def _fn(f):
    params=' '.join(f'(param ${p.name} {wt(p.ty)})' for p in f.params); result='' if f.ret_ty==Ty.VOID else f' (result {wt(f.ret_ty)})'
    regs={r.name:r.ty for b in f.blocks for i in b.instrs if (r:=i.dest) is not None}
    locals_=''.join(f' (local ${n} {wt(t)})' for n,t in regs.items())+' (local $pc i32)'
    # alloca addresses are deterministic per function; array slots reserve enough bytes.
    addr={}; next_addr=0
    for b in f.blocks:
        for i in b.instrs:
            if i.op=='alloca': addr[i.dest.name]=next_addr; next_addr += (getattr(i.args[0],'value',1) or 1)*8
    lines=[f'  (func ${f.name} {params}{result}{locals_}', '    (local.set $pc (i32.const 0))', '    (block $done', '      (loop $dispatch']
    for bi,b in enumerate(f.blocks):
        lines.append(f'        (if (i32.eq (local.get $pc) (i32.const {bi})) (then')
        for i in b.instrs[:-1] if b.instrs else []: lines += ['          '+x for x in _instr(i,addr)]
        if b.instrs:
            t=b.instrs[-1]
            if t.op=='br': lines.append(f'          (local.set $pc (i32.const {f.blocks.index(f.block(t.labels[0]))}))'); lines.append('          (br $dispatch)')
            elif t.op=='br.cond':
                a=val(t.args[0]); ti=f.blocks.index(f.block(t.labels[0])); fi=f.blocks.index(f.block(t.labels[1])); lines.append(f'          (if {a} (then (local.set $pc (i32.const {ti}))) (else (local.set $pc (i32.const {fi}))))'); lines.append('          (br $dispatch)')
            elif t.op=='ret':
                if t.args: lines.append(f'          {val(t.args[0])}')
                lines.append('          (return)')
        lines.append('        ))')
    lines += ['        (br $done)','      )','    )','  )']
    return lines

def _instr(i,addr):
    out=[]
    if i.op=='alloca': out.append(f'(local.set ${i.dest.name} (i32.const {addr[i.dest.name]}))'); return out
    if i.op=='load':
        ty=wt(i.ty)
        if isinstance(i.args[0],GlobalRef): out.append(f'(local.set ${i.dest.name} (global.get ${i.args[0].name}))')
        else:
            load='f64.load' if ty=='f64' else ('i64.load' if ty=='i64' else 'i32.load'); out.append(f'(local.set ${i.dest.name} ({load} {val(i.args[0])}))')
        return out
    if i.op=='store':
        if isinstance(i.args[1],GlobalRef): out.append(f'(global.set ${i.args[1].name} {val(i.args[0])})')
        else:
            ty=wt(i.ty); st='f64.store' if ty=='f64' else ('i64.store' if ty=='i64' else 'i32.store'); out.append(f'({st} {val(i.args[1])} {val(i.args[0])})')
        return out
    if i.op=='gep': out.append(f'(local.set ${i.dest.name} (i32.add {val(i.args[0])} (i32.mul {val(i.args[1])} (i32.const 8))))'); return out
    if i.op=='call':
        s=f'(call ${i.callee}'+(' '+' '.join(val(a) for a in i.args) if i.args else '')+')'; out.append(f'(local.set ${i.dest.name} {s})' if i.dest else s); return out
    if i.op=='print.i32': out.append(f'(call $print_int {val(i.args[0])})'); return out
    if i.op=='print.f64': out.append(f'(call $print_float {val(i.args[0])})'); return out
    if i.op=='trap': out.append('(unreachable)'); return out
    if i.op=='neg': out.append(f'(local.set ${i.dest.name} ({"f64.neg" if i.ty==Ty.F64 else wt(i.ty)+".sub"} {"(f64.const 0)" if i.ty==Ty.F64 else "(i32.const 0)"} {val(i.args[0])}))'); return out
    if i.op=='not': out.append(f'(local.set ${i.dest.name} ({wt(i.ty)}.xor {val(i.args[0])} ({wt(i.ty)}.const -1)))'); return out
    o=op(i)
    if o: out.append(f'(local.set ${i.dest.name} ({o} {val(i.args[0])} {val(i.args[1])}))')
    return out
