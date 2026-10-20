"""Practical MiniLang semantic/type checker used before CIR construction."""
from __future__ import annotations
from ..frontend import ast_nodes as A

T={'int':'int','long':'long','float':'float','bool':'bool','void':'void'}
class TypeError_(Exception): pass

def check(program):
    funcs={d.name:d for d in program.decls if isinstance(d,A.FnDecl)}
    globals_={d.name:d.type_node for d in program.decls if isinstance(d,A.GlobalDecl)}
    if len(funcs)+len(globals_) != len([d for d in program.decls if isinstance(d,(A.FnDecl,A.GlobalDecl))]): raise TypeError_('duplicate declaration')
    for f in funcs.values():
        env=dict(globals_); env.update({p.name:p.type_node for p in f.params})
        _block(f.body,env,funcs,f.ret_type,0)
    for g in [d for d in program.decls if isinstance(d,A.GlobalDecl)]:
        if g.init: _expr(g.init,globals_,funcs)
    return program

def _type(t): return t.name

def _block(b,env,funcs,ret,loops):
    local=dict(env)
    for s in b.stmts: _stmt(s,local,funcs,ret,loops)

def _stmt(s,env,funcs,ret,loops):
    if isinstance(s,A.Let):
        if s.name in env: raise TypeError_(f'duplicate local {s.name}')
        env[s.name]=s.type_node
        if s.init and s.type_node.array_len is None: _compat(_expr(s.init,env,funcs),s.type_node.name)
    elif isinstance(s,A.Assign):
        lt=_lvalue(s.target,env,funcs); rt=_expr(s.value,env,funcs); _compat(rt,lt)
    elif isinstance(s,A.ExprStmt): _expr(s.expr,env,funcs)
    elif isinstance(s,A.Return):
        if ret.name=='void':
            if s.value: raise TypeError_('void function cannot return a value')
        elif s.value is None or not _compat(_expr(s.value,env,funcs),ret.name): raise TypeError_('return type mismatch')
    elif isinstance(s,A.If):
        if _expr(s.cond,env,funcs)!='bool': raise TypeError_('if condition must be bool')
        _block(s.then_blk,env,funcs,ret,loops)
        if s.else_blk: _block(s.else_blk,env,funcs,ret,loops) if isinstance(s.else_blk,A.Block) else _stmt(s.else_blk,env,funcs,ret,loops)
    elif isinstance(s,A.While):
        if _expr(s.cond,env,funcs)!='bool': raise TypeError_('while condition must be bool')
        _block(s.body,env,funcs,ret,loops+1)
    elif isinstance(s,A.For):
        e=dict(env)
        if s.init: _stmt(s.init,e,funcs,ret,loops+1)
        if s.cond and _expr(s.cond,e,funcs)!='bool': raise TypeError_('for condition must be bool')
        if s.step: _stmt(s.step,e,funcs,ret,loops+1)
        _block(s.body,e,funcs,ret,loops+1)
    elif isinstance(s,(A.Break,A.Continue)) and loops<=0: raise TypeError_('loop control outside loop')

def _lvalue(e,env,funcs):
    if isinstance(e,A.VarRef):
        if e.name not in env: raise TypeError_(f'unknown name {e.name}')
        t=env[e.name]; return t.name
    if isinstance(e,A.Index):
        bt=_expr(e.base,env,funcs)
        if isinstance(e.base,A.VarRef): bt=env[e.base.name]
        if not isinstance(bt,A.TypeNode) or bt.array_len is None: raise TypeError_('indexing non-array')
        if _expr(e.index,env,funcs)!='int': raise TypeError_('array index must be int')
        return bt.name
    raise TypeError_('not assignable')

def _expr(e,env,funcs):
    if isinstance(e,A.IntLit): e.ty='int'; return 'int'
    if isinstance(e,A.FloatLit): e.ty='float'; return 'float'
    if isinstance(e,A.BoolLit): e.ty='bool'; return 'bool'
    if isinstance(e,A.VarRef):
        if e.name not in env: raise TypeError_(f'unknown name {e.name}')
        t=env[e.name]; e.ty=t.name; return t.name
    if isinstance(e,A.Index):
        bt=_expr(e.base,env,funcs); raw=env[e.base.name] if isinstance(e.base,A.VarRef) else None
        if raw is None or raw.array_len is None: raise TypeError_('indexing non-array')
        if _expr(e.index,env,funcs)!='int': raise TypeError_('array index must be int')
        e.ty=raw.name; return raw.name
    if isinstance(e,A.Unary):
        t=_expr(e.operand,env,funcs)
        if e.op=='!' and t!='bool' or e.op=='-' and t not in ('int','long','float') or e.op=='~' and t not in ('int','long'): raise TypeError_('invalid unary operator')
        e.ty='bool' if e.op=='!' else t; return e.ty
    if isinstance(e,A.Binary):
        a,b=_expr(e.lhs,env,funcs),_expr(e.rhs,env,funcs)
        if e.op in ('&&','||'): typ='bool' if a==b=='bool' else None
        elif e.op in ('==','!=','<','<=','>','>='): typ='bool' if a==b else None
        else: typ=a if a==b and a in ('int','long','float','bool') else None
        if typ is None: raise TypeError_(f'type mismatch for {e.op}')
        e.ty=typ; return typ
    if isinstance(e,A.Call):
        if e.callee in ('print_int','print_float'):
            want='int' if e.callee=='print_int' else 'float'
            if len(e.args)!=1 or _expr(e.args[0],env,funcs)!=want: raise TypeError_('builtin argument mismatch')
            e.ty='void'; return 'void'
        if e.callee not in funcs: raise TypeError_(f'unknown function {e.callee}')
        f=funcs[e.callee]
        if len(e.args)!=len(f.params): raise TypeError_('wrong argument count')
        for x,p in zip(e.args,f.params): _compat(_expr(x,env,funcs),p.type_node.name)
        e.ty=f.ret_type.name; return e.ty
    raise TypeError_(f'unsupported expression {type(e).__name__}')

def _compat(actual,expected):
    return actual==expected or (actual=='int' and expected in ('long','float'))
