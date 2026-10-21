"""Random MiniLang program generator.

Module M8b.  Owner: Member 4.

``generate(seed)`` returns a random, well-typed, terminating MiniLang program
over the whole language except casts (there is no cast syntax): int, long,
float, bool, scalar globals, fixed-size local arrays, if/else, while, for,
break, continue, calls and bounded recursion.  The same seed always yields the
same program.

Why every program terminates
  * loops count a fresh variable from 0 to a small constant, and the body
    never assigns that variable;
  * a function only calls functions defined before it, so the call graph is
    a DAG; the one recursive helper is called with a small argument and
    recurses on n - 1 down to 0.

Why every program is well defined
  * every variable and every array element is initialised before it is read
    (reading uninitialised memory differs per target: LLVM and Wasm read stale
    stack memory, the interpreter reads 0);
  * division, out-of-range indices and INT_MIN / -1 are *allowed*: they trap
    the same way everywhere (docs/divergence.md rows 1, 2, 7), and the harness
    compares traps too.
"""
from __future__ import annotations

import random

INT_OPS = ["+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"]
FLOAT_OPS = ["+", "-", "*", "/"]
CMP_OPS = ["==", "!=", "<", "<=", ">", ">="]
SCALARS = ["int", "long", "float", "bool"]


class _Gen:
    def __init__(self, seed: int) -> None:
        self.r = random.Random(seed)
        self.n = 0
        self.funcs = []            # (name, ret type, [param types])
        self.globals = {}          # name -> type
        self.lines = []

    # -- helpers -------------------------------------------------------------
    def fresh(self, base: str) -> str:
        self.n += 1
        return f"{base}{self.n}"

    def chance(self, p: float) -> bool:
        return self.r.random() < p

    def emit(self, indent: int, text: str) -> None:
        self.lines.append("    " * indent + text)

    # -- expressions -----------------------------------------------------------
    def literal(self, ty: str) -> str:
        r = self.r
        if ty == "int":
            return str(r.choice([0, 1, 2, 3, 7, 31, 32, 33, 100, 65535, 2147483647,
                                 r.randint(-1000, 1000)]))
        if ty == "long":
            return str(r.choice([0, 1, 5, 4294967296, 9000000000, r.randint(-10**6, 10**6)]))
        if ty == "float":
            return r.choice(["0.0", "1.5", "2.25", "1.0e3", "0.1", "3.0", "1.0e-3"])
        return r.choice(["true", "false"])

    def var(self, scope, ty):
        names = [n for n, t in scope.items() if t == ty]
        return self.r.choice(names) if names else None

    def expr(self, scope, ty: str, depth: int) -> str:
        r = self.r
        leaf = depth <= 0 or self.chance(0.3)
        if leaf:
            v = self.var(scope, ty)
            return v if v and self.chance(0.7) else self.literal(ty)
        if ty == "bool":
            k = r.randint(0, 3)
            if k == 0:
                t = r.choice(["int", "long", "float"])
                return f"({self.expr(scope, t, depth - 1)} {r.choice(CMP_OPS)} {self.expr(scope, t, depth - 1)})"
            if k == 1:
                return f"({self.expr(scope, 'bool', depth - 1)} {r.choice(['&&', '||'])} {self.expr(scope, 'bool', depth - 1)})"
            if k == 2:
                return f"!{self.expr(scope, 'bool', depth - 1)}"
            return f"({self.expr(scope, 'bool', depth - 1)} {r.choice(['&', '|', '^'])} {self.expr(scope, 'bool', depth - 1)})"
        calls = [f for f in self.funcs if f[1] == ty]
        if calls and self.chance(0.15):
            name, _, params = r.choice(calls)
            return f"{name}({', '.join(self.expr(scope, p, depth - 1) for p in params)})"
        arrays = [n for n, t in scope.items() if t == f"{ty}[]"]
        if arrays and self.chance(0.2):
            name = r.choice(arrays)
            return f"{name}[{self.index(scope, name, depth - 1)}]"
        if self.chance(0.15):
            op = r.choice(["-", "~"] if ty in ("int", "long") else ["-"])
            return f"{op}{self.expr(scope, ty, depth - 1)}"
        ops = FLOAT_OPS if ty == "float" else INT_OPS
        op = r.choice(ops)
        lhs = self.expr(scope, ty, depth - 1)
        rhs = self.expr(scope, ty, depth - 1)
        if op in ("<<", ">>"):
            rhs = self.expr(scope, "int", depth - 1)
            if ty == "long" and lhs.lstrip("-").isdigit():
                op = "+"     # a bare literal is an int, so it would shift as i32
            return f"({lhs} {op} {rhs})"
        if op in ("/", "%") and ty != "float" and self.chance(0.8):
            rhs = f"({rhs} | 1)"            # mostly avoid the division trap
        if ty == "long" and self.chance(0.3):
            rhs = self.expr(scope, "int", depth - 1)    # int -> long widening
        if ty == "float" and self.chance(0.2):
            rhs = self.expr(scope, "int", depth - 1)    # int -> float widening
        return f"({lhs} {op} {rhs})"

    def index(self, scope, name, depth):
        size = self.sizes[name]
        loopvars = [v for v in self.loopvars if v in scope]
        if loopvars and self.chance(0.6):
            return f"{self.r.choice(loopvars)} % {size}"
        if self.chance(0.9):
            return str(self.r.randrange(size))
        return self.expr(scope, "int", depth)          # may trap: row 7

    # -- statements ------------------------------------------------------------
    def block(self, scope, ind, depth, in_loop, ret_ty, n_stmts):
        scope = dict(scope)
        for _ in range(n_stmts):
            self.stmt(scope, ind, depth, in_loop, ret_ty)
        return scope

    def stmt(self, scope, ind, depth, in_loop, ret_ty):
        r = self.r
        k = r.random()
        assignable = [n for n, t in scope.items()
                      if t in SCALARS and n not in self.readonly]
        if k < 0.25 or not assignable:
            ty = r.choice(SCALARS + ["int"])
            name = self.fresh("v")
            self.emit(ind, f"let {name}: {ty} = {self.expr(scope, ty, 3)};")
            scope[name] = ty
        elif k < 0.40:
            name = r.choice(assignable)
            self.emit(ind, f"{name} = {self.expr(scope, scope[name], 3)};")
        elif k < 0.50:
            ty = r.choice(["int", "float", "long", "bool"])
            size = r.randint(1, 6)
            name = self.fresh("a")
            self.sizes[name] = size
            self.emit(ind, f"let {name}: {ty}[{size}];")
            i = self.fresh("i")
            self.emit(ind, f"for (let {i}: int = 0; {i} < {size}; {i} = {i} + 1) {{")
            self.emit(ind + 1, f"{name}[{i}] = {self.expr(dict(scope, **{i: 'int'}), ty, 2)};")
            self.emit(ind, "}")
            scope[name] = f"{ty}[]"
        elif k < 0.58:
            arrays = [n for n, t in scope.items() if t.endswith("[]")]
            if arrays:
                name = r.choice(arrays)
                self.emit(ind, f"{name}[{self.index(scope, name, 2)}] = "
                               f"{self.expr(scope, scope[name][:-2], 2)};")
        elif k < 0.70:
            ty = r.choice(["int", "int", "float"])
            fn = "print_int" if ty == "int" else "print_float"
            self.emit(ind, f"{fn}({self.expr(scope, ty, 3)});")
        elif k < 0.80 and depth > 0:
            self.emit(ind, f"if ({self.expr(scope, 'bool', 2)}) {{")
            self.block(scope, ind + 1, depth - 1, in_loop, ret_ty, r.randint(1, 3))
            if self.chance(0.5):
                self.emit(ind, "} else {")
                self.block(scope, ind + 1, depth - 1, in_loop, ret_ty, r.randint(1, 3))
            self.emit(ind, "}")
        elif k < 0.90 and depth > 0:
            self.loop(scope, ind, depth, ret_ty)
        elif in_loop and k < 0.94:
            self.emit(ind, f"if ({self.expr(scope, 'bool', 2)}) {{ {r.choice(['break', 'continue'])}; }}")
        elif ret_ty and k < 0.96 and depth < 2:
            self.emit(ind, f"if ({self.expr(scope, 'bool', 2)}) {{ return {self.expr(scope, ret_ty, 2)}; }}")
        else:
            g = [n for n, t in self.globals.items() if t in ("int", "long", "float")]
            if g:
                name = r.choice(g)
                self.emit(ind, f"{name} = {self.expr(scope, self.globals[name], 2)};")

    def loop(self, scope, ind, depth, ret_ty):
        i = self.fresh("i")
        bound = self.r.randint(0, 5)
        self.readonly.add(i)
        self.loopvars.append(i)
        inner = dict(scope, **{i: "int"})
        if self.chance(0.5):
            self.emit(ind, f"for (let {i}: int = 0; {i} < {bound}; {i} = {i} + 1) {{")
            self.block(inner, ind + 1, depth - 1, True, ret_ty, self.r.randint(1, 3))
            self.emit(ind, "}")
        else:
            # A while loop increments first, so 'continue' cannot skip it.
            self.emit(ind, f"let {i}: int = 0;")
            self.emit(ind, f"while ({i} < {bound}) {{")
            self.emit(ind + 1, f"{i} = {i} + 1;")
            self.block(inner, ind + 1, depth - 1, True, ret_ty, self.r.randint(1, 3))
            self.emit(ind, "}")
        self.loopvars.pop()

    # -- program ---------------------------------------------------------------
    def function(self, name, ret_ty, params):
        self.readonly, self.loopvars, self.sizes = set(), [], {}
        sig = ", ".join(f"{p}: {t}" for p, t in params)
        self.emit(0, f"fn {name}({sig}) -> {ret_ty} {{")
        scope = dict(self.globals)
        scope.update(params)
        scope = self.block(scope, 1, 2, False, ret_ty, self.r.randint(2, 6))
        self.emit(1, f"return {self.expr(scope, ret_ty, 3)};")
        self.emit(0, "}")
        self.emit(0, "")

    def program(self) -> str:
        r = self.r
        self.emit(0, "// generated by tests/harness/gen_random.py")
        for _ in range(r.randint(0, 3)):
            ty = r.choice(SCALARS)
            name = self.fresh("g")
            self.globals[name] = ty
            self.emit(0, f"global {name}: {ty} = {self.literal(ty)};")
        self.emit(0, "")

        if self.chance(0.4):        # bounded recursion
            self.emit(0, "fn rec(n: int, acc: int) -> int {")
            self.emit(1, "if (n <= 0) { return acc; }")
            self.emit(1, f"return rec(n - 1, acc {r.choice(['+', '*', '^', '-'])} n);")
            self.emit(0, "}")
            self.emit(0, "")
            self.funcs.append(("rec", "int", ["int", "int"]))

        for _ in range(r.randint(0, 3)):
            ret = r.choice(SCALARS)
            params = [(self.fresh("p"), r.choice(SCALARS)) for _ in range(r.randint(0, 3))]
            name = self.fresh("f")
            self.function(name, ret, params)
            self.funcs.append((name, ret, [t for _, t in params]))

        self.function("main", "int", [])
        return "\n".join(self.lines) + "\n"


def generate(seed: int) -> str:
    """Emit a random, terminating MiniLang program for the fuzzing layer."""
    return _bound_recursion(_Gen(seed).program())


def _bound_recursion(src: str) -> str:
    """Make every external call to rec() pass a small, non-negative count."""
    out = []
    for line in src.splitlines():
        if "rec(" in line and "fn rec" not in line and "return rec(n - 1" not in line:
            line = line.replace("rec(", "rec(7 & ")
        out.append(line)
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    import sys
    print(generate(int(sys.argv[1]) if len(sys.argv) > 1 else 0), end="")
