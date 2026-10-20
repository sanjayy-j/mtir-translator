"""Tiny stack VM for the textual bytecode emitted by emit_sbc."""
from __future__ import annotations

def run(bytecode, entry='main', args=()):
    funcs={}; cur=None
    for raw in bytecode.splitlines():
        s=raw.strip()
        if not s: continue
        if s.endswith(':') and not s.startswith(('L_','then','else','while','body','for','step','fbody','fend','wend','endif')):
            cur=s[:-1]; funcs[cur]=[]
        elif cur: funcs[cur].append(s)
    out=[]
    def call(name,argv):
        code=funcs[name]; labels={x[:-1]:i for i,x in enumerate(code) if x.endswith(':')}; pc=0; st=[]; locals_=dict(zip([],argv))
        while pc<len(code):
            x=code[pc]; pc+=1
            if x.endswith(':'): continue
            a=x.split(); op=a[0]; arg=a[1] if len(a)>1 else None
            if op=='PUSHI': st.append(int(arg))
            elif op=='LOADL': st.append(locals_.get(arg,0))
            elif op=='STOREL': locals_[arg]=st.pop()
            elif op in ('ADD','SUB','MUL','AND','OR','XOR','SHL','ASHR','LSHR','LT_S','LE_S','GT_S','GE_S','EQ','NE'):
                b=st.pop(); c=st.pop(); st.append({'ADD':c+b,'SUB':c-b,'MUL':c*b,'AND':c&b,'OR':c|b,'XOR':c^b,'SHL':c<<(b%32),'ASHR':c>>(b%32),'LSHR':(c&0xffffffff)>>(b%32),'LT_S':int(c<b),'LE_S':int(c<=b),'GT_S':int(c>b),'GE_S':int(c>=b),'EQ':int(c==b),'NE':int(c!=b)}[op])
            elif op=='JZ':
                if st.pop()==0: pc=labels[arg]
            elif op=='JMP': pc=labels[arg]
            elif op=='CALL': st.append(call(arg,[]))
            elif op=='RET': return st.pop() if st else 0
            elif op=='PRINTI': out.append(str(st.pop()))
            elif op=='PUSHF': st.append(float(arg))
            elif op=='PRINTF': out.append(str(st.pop()))
            elif op=='TRAP': return 0
        return st[-1] if st else 0
    return call(entry,list(args)), '\n'.join(out), False
