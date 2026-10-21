"""CIR structural/type verifier."""
from .ir import *

def verify(mod: Module):
    errors=[]
    fns={f.name:f for f in mod.functions}
    def err(f,msg): errors.append(f'{f.name}: {msg}')
    for f in mod.functions:
        labels={b.label for b in f.blocks}
        if not f.blocks: err(f,'has no blocks'); continue
        preds={b.label:[] for b in f.blocks}
        defs={p.name:p.ty for p in f.params}
        for b in f.blocks:
            if not b.instrs: err(f,f'block {b.label} is empty'); continue
            terms=[i for i in b.instrs if i.is_terminator()]
            if len(terms)!=1 or not b.instrs[-1].is_terminator(): err(f,f'block {b.label} must end in exactly one terminator')
            for s in b.successors():
                if s not in labels: err(f,f'unknown branch target {s}')
                else: preds[s].append(b.label)
            for i in b.instrs:
                for a in i.args:
                    if isinstance(a,Reg) and a.name not in defs: err(f,f'use before definition %{a.name} in {b.label}')
                if i.dest: defs[i.dest.name]=i.dest.ty
                if i.op in CMP_OPS and i.dest and i.dest.ty!=Ty.I1: err(f,f'comparison result must be i1')
                if i.op=='br.cond' and i.args and getattr(i.args[0],'ty',None)!=Ty.I1: err(f,'conditional branch requires i1')
                if i.op=='ret':
                    if f.ret_ty==Ty.VOID and i.args: err(f,'void function returns a value')
                    if f.ret_ty!=Ty.VOID and (not i.args or i.args[0].ty!=f.ret_ty): err(f,'return type mismatch')
        if preds[f.entry.label]: err(f,'entry block has predecessors')
        if f.ret_ty!=Ty.VOID:
            for b in f.blocks:
                if not b.successors() and (not b.terminator or b.terminator.op!='ret'): err(f,f'non-void path {b.label} does not return')
    if errors: raise ValueError('\n'.join(errors))
    return True
