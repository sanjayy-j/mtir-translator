"""CIR -> textual stack bytecode."""
from ...backend.wasm.reg2stack import lower_function
from ...cir.ir import *

def emit(mod):
    lines=[]
    for f in mod.functions:
        lines.append(f'{f.name}:')
        seqs=lower_function(f)
        for label,seq in seqs.items():
            if label!='entry': lines.append(f'{label}:')
            for x in seq: lines.append('        '+str(x))
    return '\n'.join(lines)+'\n'
