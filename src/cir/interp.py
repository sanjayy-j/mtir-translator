"""Reference interpreter for CIR."""
from __future__ import annotations
from .ir import *
import math

def _wrap(v,bits):
    m=1<<bits; v%=m
    return v-m if v >= (1<<(bits-1)) else v

def run(mod, entry='main', args=()):
    funcs={f.name:f for f in mod.functions}; globs={g.name:(g.init.value if g.init else 0) for g in mod.globals}; out=[]
    def call(name,argv):
        f=funcs[name]; regs={p.name:v for p,v in zip(f.params,argv)}; mem={}; arrays={}; labels={b.label:b for b in f.blocks}; pc='entry'; trapped=False
        while True:
            b=labels[pc]
            for ins in b.instrs:
                def val(x):
                    if isinstance(x,Reg): return regs[x.name]
                    if isinstance(x,(ConstInt,ConstFloat)): return x.value
                    if isinstance(x,GlobalRef): return ('global', x.name)
                if ins.op=='alloca': regs[ins.dest.name]=('mem',id(ins)); mem[id(ins)]=[0]*(val(ins.args[0]) if ins.args else 1)
                elif ins.op=='load':
                    p=val(ins.args[0]);
                    if isinstance(p,tuple) and p[0]=='elem': regs[ins.dest.name]=mem[p[1]][p[2]]
                    elif isinstance(p,tuple) and p[0]=='mem': regs[ins.dest.name]=mem[p[1]][0]
                    elif isinstance(p,tuple) and p[0]=='global': regs[ins.dest.name]=globs[p[1]]
                elif ins.op=='store':
                    v=val(ins.args[0]); p=val(ins.args[1]);
                    if isinstance(p,tuple) and p[0]=='elem': mem[p[1]][p[2]]=v
                    elif isinstance(p,tuple) and p[0]=='mem': mem[p[1]][0]=v
                    elif isinstance(p,tuple) and p[0]=='global': globs[p[1]]=v
                elif ins.op=='gep':
                    base=val(ins.args[0]); idx=int(val(ins.args[1])); regs[ins.dest.name]=('elem',base[1],idx)
                elif ins.op=='call':
                    cv,_,ct=call(ins.callee,[val(a) for a in ins.args])
                    if ins.dest: regs[ins.dest.name]=cv
                    if ct: return 0,'\n'.join(out),True
                elif ins.op.startswith('print.'):
                    out.append(str(val(ins.args[0])))
                elif ins.op=='trap': return 0, '\n'.join(out), True
                elif ins.op=='br': pc=ins.labels[0]; break
                elif ins.op=='br.cond': pc=ins.labels[0] if val(ins.args[0]) else ins.labels[1]; break
                elif ins.op=='ret': return (val(ins.args[0]) if ins.args else 0), '\n'.join(out), False
                elif ins.op in ('add','sub','mul','sdiv','udiv','srem','urem','fadd','fsub','fmul','fdiv','and','or','xor','shl','ashr','lshr'):
                    a,bv=val(ins.args[0]),val(ins.args[1]); w=32 if ins.ty in (Ty.I1,Ty.I32) else 64
                    if ins.op in ('sdiv','udiv','srem','urem') and bv==0: return 0,'\n'.join(out),True
                    if ins.op in ('sdiv','srem') and ins.ty==Ty.I32 and a==-2147483648 and bv==-1: return 0,'\n'.join(out),True
                    if ins.op=='add': z=a+bv
                    elif ins.op=='sub': z=a-bv
                    elif ins.op=='mul': z=a*bv
                    elif ins.op in ('sdiv','udiv'): z=int(a/bv)
                    elif ins.op in ('srem','urem'): z=a-int(a/bv)*bv
                    elif ins.op=='fadd': z=a+bv
                    elif ins.op=='fsub': z=a-bv
                    elif ins.op=='fmul': z=a*bv
                    elif ins.op=='fdiv': z=a/bv
                    elif ins.op=='and': z=int(a)&int(bv)
                    elif ins.op=='or': z=int(a)|int(bv)
                    elif ins.op=='xor': z=int(a)^int(bv)
                    elif ins.op=='shl': z=int(a) << (int(bv)%w)
                    elif ins.op in ('ashr','lshr'): z=int(a) >> (int(bv)%w)
                    regs[ins.dest.name]=_wrap(z,w) if ins.ty in (Ty.I32,Ty.I64) else z
                elif ins.op=='neg':
                    z=-val(ins.args[0]); regs[ins.dest.name]=_wrap(z,32) if ins.ty==Ty.I32 else z
                elif ins.op=='not': regs[ins.dest.name]=~int(val(ins.args[0]))
                elif ins.op.startswith('icmp.'):
                    a,bv=val(ins.args[0]),val(ins.args[1]); c=ins.op[5:]; regs[ins.dest.name]=int({'eq':a==bv,'ne':a!=bv,'slt':a<bv,'sle':a<=bv,'sgt':a>bv,'sge':a>=bv,'ult':(a&0xffffffff)<(bv&0xffffffff),'ule':(a&0xffffffff)<=(bv&0xffffffff),'ugt':(a&0xffffffff)>(bv&0xffffffff),'uge':(a&0xffffffff)>=(bv&0xffffffff)}[c])
                elif ins.op.startswith('fcmp.'):
                    a,bv=val(ins.args[0]),val(ins.args[1]); c=ins.op[5:]; regs[ins.dest.name]=int({'oeq':a==bv,'one':a!=bv,'olt':a<bv,'ole':a<=bv,'ogt':a>bv,'oge':a>=bv}[c])
                else: raise ValueError(f'unsupported CIR op {ins.op}')
        
    r,o,t=call(entry,list(args)); return r,o,t
