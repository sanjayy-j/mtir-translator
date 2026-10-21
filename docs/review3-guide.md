# Review 3 guide: Multi-Target IR Translator

**BCSE307 Compiler Design · Team 5 · Project A30**
Krishita Gupta (M1) · Pragati Manot (M2) · Sanjay J (M3) · Sparsh Khandelwal (M4)

This page explains what the project is, how it works, how to run the demo on
Windows command by command (with the output you should see and what it
means), which code files each of us should know, and how the tests prove that
the project works. Read it once from top to bottom before the review, and run
sections 4 and 5 on your own laptop at least once.

---

## 1. What the project is, in one paragraph

We designed a small programming language, **MiniLang**, and one intermediate
representation, **CIR** (Common IR). A MiniLang program is compiled once into
CIR, and CIR is then translated into **three different targets**: **LLVM IR**,
**WebAssembly** (text format, `.wat`), and our own **stack bytecode** (`.sbc`)
with a small virtual machine. A fourth component, the **CIR reference
interpreter**, runs CIR directly and acts as the *oracle*: every target must
produce exactly the same output, return value and traps as the interpreter.

**Why this is hard (and the point of the project).** The targets disagree
about edge cases. Examples: in LLVM, dividing by zero or shifting by 33 bits is
*undefined behaviour* (the program may do anything), but WebAssembly *traps* on
division by zero and *masks* the shift count. If we translated naively, the
same program would behave differently on each target. So **CIR defines one
meaning for each of these cases**, and every back end adds whatever guard code
is needed to make its target obey that meaning. The full list is the
divergence table in `docs/divergence.md` (rows 1 to 9).

## 2. The flow (the pipeline)

```
 abs.mini
    │  lexer        src/frontend/lexer.py      text → tokens
    │  parser       src/frontend/parser.py     tokens → AST (syntax tree)
    │  CIR builder  src/cir/builder.py         AST → CIR
    │  type checker src/sema/typecheck.py      checks types, reports E001–E012
    │  verifier     src/cir/verifier.py        checks the CIR is well formed
    │  optimiser    src/opt/ (only with --opt=1)  constant folding, copy
    │                                             propagation, dead-code removal
    ├──► LLVM IR (.ll)          src/backend/llvm/      run with clang
    ├──► WebAssembly (.wat)     src/backend/wasm/      run with wasmtime
    ├──► stack bytecode (.sbc)  src/backend/stackvm/   run with our VM
    └──► CIR interpreter        src/cir/interp.py      THE ORACLE
```

All the code lives in `src/`. The command-line tool is `src/driver.py`; every
demo command below goes through it.

### What each stage produces, in plain words

| Stage | Input → output | What to say about it |
|---|---|---|
| Lexer | characters → tokens (`KW_FN`, `IDENT 'abs'`, …) with line:column | Rejects bad characters and unterminated comments. |
| Parser | tokens → AST | Recursive descent; operator precedence; reports syntax errors with position. |
| CIR builder | AST → CIR | Three-address code with an explicit control-flow graph (basic blocks ending in a branch or return). Local variables are `alloca` slots with `load`/`store`. |
| Type checker | AST → typed AST | 12 error codes, E001 to E012 (spec §7). Implicit widening int→long and int→float only. |
| Verifier | CIR → OK / error | Every block ends in a terminator, every register is defined, types match. |
| Optimiser | CIR → smaller CIR | Never folds an operation that would trap, so it can't remove a trap. |
| Back ends | CIR → `.ll` / `.wat` / `.sbc` | Each one emits the guards from the divergence table. |
| Interpreter | CIR → (return value, output, trapped?) | Defines what "correct" means. |

### The semantics every target implements (the divergence rows)

| # | Case | CIR says | LLVM needs | WebAssembly needs |
|---|---|---|---|---|
| 1 | division by zero | trap | explicit check + trap (it's UB) | traps natively |
| 2 | `INT_MIN / -1` | trap | explicit check + trap (UB) | traps natively |
| 3 | shift by ≥ width | count taken mod width (`1 << 32` is `1`) | mask the count (otherwise poison) | masks natively |
| 4 | signed overflow | wraps around | no `nsw`/`nuw` flags | wraps natively |
| 5 | booleans | always 0 or 1 | native `i1` | stored as `i32`, normalised |
| 6 | float→int out of range / NaN | trap | range + NaN checks | traps natively |
| 7 | array index out of range | trap | bounds check | bounds check (memory only traps per 64 KB page) |
| 8 | locals and arrays in memory | addressable | `alloca` | a shadow stack in linear memory |
| 9 | evaluation order | left to right | source order | fixed push order |

Two decisions we made in the spec (`docs/minilang-spec.md`), in case the
professor asks:

- **Decision 0001:** an array declaration takes no initialiser:
  `let xs: int[8] = 0;` is error E004. `tests/corpus/valid/arrays.mini` is
  the example of this and is rejected *on purpose*.
- **Integer literals are typed by value** (decided 21 Oct): a literal is
  `int` if it fits, otherwise `long`. `let x: int = 4294967296;` is error
  E005 instead of silently becoming 0.

## 3. One-time setup on Windows

You need **Python 3.10 or newer** (3.12 recommended) and **Git**.

1. Install Python from python.org. On the first installer screen, tick
   **"Add python.exe to PATH"**.
2. Open **Command Prompt** (cmd) or **PowerShell** and run:

```
git clone https://github.com/sanjayy-j/mtir-translator.git
cd mtir-translator
py -3.12 -m venv venv
```

3. Activate the virtual environment. You must do this **every time you open
   a new window**:

   - Command Prompt: `venv\Scripts\activate.bat`
   - PowerShell: `venv\Scripts\Activate.ps1`

   If PowerShell says running scripts is disabled, run this once and try
   again: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`

   When it works, the prompt starts with `(venv)`.

4. Install the test tools (pytest, and wasmtime for running WebAssembly):

```
pip install -r requirements.txt
```

5. If you already have the repo, just update it instead:

```
git pull origin main
```

**About LLVM on Windows.** *Producing* LLVM IR (`--emit=ll`) needs nothing
extra and works everywhere. *Running* it needs `clang`, which most Windows
laptops don't have. Without clang, the tests that run LLVM are **skipped, not
failed**, and the harness reports `llvm` as skipped. Don't install clang just
for the demo: our LLVM runner was written and tested on macOS and has not been
tested on Windows. The four-way run *with* LLVM is shown from Krishita's Mac.

**All paths below use forward slashes** (`tests/corpus/valid/arith.mini`).
Python accepts them on Windows in both cmd and PowerShell.

**Checking the exit status** after a command (used in steps 4.6 and 4.7):

- Command Prompt: `echo %ERRORLEVEL%`
- PowerShell: `echo $LASTEXITCODE`

The compiler exits with **0** on success, **1** on a compile error, and **4**
when the program traps.

> **Note:** the `demo/` folder (with `run-demo.ps1`) is the demo for the old
> C++ version and needs a Visual Studio C++ build. **We don't use it.** Use
> the commands in this guide.

## 4. The demo, step by step (about 10 minutes)

Before the review: open a terminal in the repo folder, activate the venv, and
open `docs/examples/abs.mini` in an editor. The running example is:

```
fn abs(x: int) -> int {
    if (x < 0) { return -x; }
    return x;
}

fn main() -> int {
    print_int(abs(-7));
    return 0;
}
```

### 4.1 Run a program

```
python -m src.driver --run docs/examples/abs.mini
```

Expected output:

```
7
0
```

**What it means:** `7` is what the program printed (`print_int(abs(-7))`).
The last line, `0`, is the value `main` returned. `--run` uses the CIR
interpreter, our oracle.

### 4.2 Front end: tokens and syntax tree

```
python -m src.driver --emit=tokens docs/examples/abs.mini
```

Expected (first lines):

```
   4:1    KW_FN        'fn'
   4:4    IDENT        'abs'
   4:7    LPAREN       '('
   4:8    IDENT        'x'
   4:9    COLON        ':'
   4:11   KW_INT       'int'
```

**What it means:** the lexer turned the text into tokens, each with its
line:column. Comments and spaces are gone.

```
python -m src.driver --emit=ast docs/examples/abs.mini
```

Expected (start):

```
Program
  FnDecl abs(x: int) -> int
    Block
      If
        Binary <
          VarRef x
          IntLit 0
        Block
          Return
            Unary -
              VarRef x
      Return
        VarRef x
```

**What it means:** the parser built the tree that reflects the program's
structure: an `if` whose condition is `x < 0`, a `return -x` inside it, and
a `return x` after it.

### 4.3 The IR: CIR

```
python -m src.driver --emit=cir docs/examples/abs.mini
```

Expected:

```
func @abs(i32 %x) -> i32 {
entry:
  %t0 = icmp.slt i32 %x, 0
  br %t0 ? if.then.0 : if.end.0
if.then.0:
  %t1 = sub i32 0, %x
  ret i32 %t1
if.end.0:
  ret i32 %x
}
func @main() -> i32 {
entry:
  %t0 = call i32 @abs(-7)
  print.i32 %t0
  ret i32 0
}
```

**What it means:** this is our IR. It's typed (`i32`), register based (`%t0`,
`%t1`), and has an explicit control-flow graph: three basic blocks
(`entry`, `if.then.0`, `if.end.0`), each ending in a branch (`br`) or a return
(`ret`). `-x` became `sub i32 0, %x`. This is the single thing all three back
ends start from.

### 4.4 The three targets from the same CIR

**LLVM IR:**

```
python -m src.driver --emit=ll docs/examples/abs.mini
```

Expected (the `abs` function):

```
define i32 @abs(i32 %x) {
entry:
  %t0 = icmp slt i32 %x, 0
  br i1 %t0, label %if.then.0, label %if.end.0
if.then.0:
  %t1 = sub i32 0, %x
  ret i32 %t1
if.end.0:
  ret i32 %x
}
```

**What it means:** it's almost one-to-one with CIR, because CIR was designed
to be close to LLVM's model. `print.i32` becomes a call to C's `printf`.

**WebAssembly:**

```
python -m src.driver --emit=wat docs/examples/abs.mini
```

Expected (the `abs` function):

```
  (func $abs (param $x i32) (result i32) (local $t0 i32) (local $t1 i32) (local $.fp i32) (local $.pc i32)
    (local.set $t0 (i32.lt_s (local.get $x) (i32.const 0)))
    (if (local.get $t0)
      (then
        (local.set $t1 (i32.sub (i32.const 0) (local.get $x)))
        (return (local.get $t1))
      )
      (else
        (return (local.get $x))
      )
    )
    (unreachable)
  )
```

**What it means:** WebAssembly has **no `goto`**, only nested
`block`/`loop`/`if`. Our *structurer* (`src/backend/wasm/structurer.py`)
rebuilds the CFG as nested structure; here the two branches became one
`(if … (then …) (else …))`. The `memory` and `$.sp` lines at the top of the
module are the shadow stack where local variables and arrays live
(divergence row 8).

**Stack bytecode:**

```
python -m src.driver --emit=sbc docs/examples/abs.mini
```

Expected:

```
abs:
        .params x
        LOADL   x
        PUSHI   0
        LT_S
        JZ      L_if.end.0
L_if.then.0:
        PUSHI   0
        LOADL   x
        SUB
        RET
L_if.end.0:
        LOADL   x
        RET
main:
        .params
        PUSHI   -7
        CALL    abs 1
        PRINTI
        PUSHI   0
        RET
```

**What it means:** a stack machine has no registers. Every operation takes its
operands from a stack (`PUSHI 0`, `LOADL x`, then `LT_S` pops both and pushes
the result). The register-to-stack lowering is `src/backend/wasm/reg2stack.py`,
and `src/backend/stackvm/vm.py` runs this code.

### 4.5 The optimiser

```
python -m src.driver --emit=cir tests/corpus/valid/arith.mini
python -m src.driver --opt=1 --emit=cir tests/corpus/valid/arith.mini
```

The source has `let a: int = 2 + 3 * 4;` and three similar lines. Without
optimisation (first command), you see the arithmetic done at run time:

```
  %t0 = mul i32 3, 4
  %t1 = add i32 2, %t0
  store i32 %t1, %a.addr
```

With `--opt=1` (second command) it's computed at compile time:

```
  store i32 14, %a.addr
  store i32 20, %b.addr
  store i32 2, %c.addr
  store i32 19, %d.addr
```

**What it means:** constant folding plus dead-code elimination took the
function from 27 lines to 20. Running either version prints `55`. The folder
**never** folds something that would trap (like `x / 0`), because that would
delete a trap the program must have.

### 4.6 Edge cases: where the targets disagree (the core of the project)

**Division traps:**

```
python -m src.driver --run tests/corpus/boundary/div_edge.mini
```

then check the exit status (`echo %ERRORLEVEL%` or `echo $LASTEXITCODE`).

Expected:

```
3
trap
```

and exit status **4**.

**What it means:** the program prints `div(7, 2)` = `3`, then calls
`div(-2147483648, -1)`. That's `INT_MIN / -1`, which CIR defines as a **trap**
(row 2). Output printed before the trap (`3`) is kept, and exit status 4 means
"the program trapped". (`trap` is printed on the error stream, so in some
terminals it appears before the `3`.)

Now show **how LLVM is made to trap** even though LLVM itself leaves this
undefined:

```
python -m src.driver --emit=ll tests/corpus/boundary/div_edge.mini
```

Expected (the `div` function):

```
define i32 @div(i32 %a, i32 %b) {
entry:
  %g.1 = icmp eq i32 %b, 0
  %g.2 = icmp eq i32 %a, -2147483648
  %g.3 = icmp eq i32 %b, -1
  %g.4 = and i1 %g.2, %g.3
  %g.5 = or i1 %g.1, %g.4
  br i1 %g.5, label %trap.div.6, label %cont.div.7
trap.div.6:
  call i32 @fflush(ptr null)
  call void @llvm.trap()
  unreachable
cont.div.7:
  %t0 = sdiv i32 %a, %b
  ret i32 %t0
}
```

**What it means:** the back end inserted a guard (rows 1 and 2): if `b == 0`,
or `a == INT_MIN` and `b == -1`, jump to a block that traps. Only otherwise
does it run the real `sdiv`. `fflush` makes sure the `3` printed earlier isn't
lost. In the `.wat` (`--emit=wat`) there is no such guard, because
WebAssembly's `i32.div_s` traps on its own.

**Shifts:**

```
python -m src.driver --run tests/corpus/boundary/shift_edge.mini
```

Expected:

```
1
-2147483648
1
2
0
```

**What it means:** the program prints `1 << 0`, `1 << 31`, `1 << 32` and
`1 << 33`. CIR takes the shift count mod 32 (row 3), so `1 << 32` is `1 << 0`
= `1` and `1 << 33` is `1 << 1` = `2`. In C or raw LLVM, `1 << 32` is
undefined.

### 4.7 Errors the compiler catches

```
python -m src.driver --run tests/corpus/invalid/missing_semicolon.mini
python -m src.driver --run tests/corpus/invalid/bad_char.mini
python -m src.driver --run tests/corpus/valid/arrays.mini
```

Expected:

```
tests/corpus/invalid/missing_semicolon.mini:4:5: syntax error: expected ';', found 'return'
tests/corpus/invalid/bad_char.mini:3:20: lexical error: unexpected character '$'
tests/corpus/valid/arrays.mini: error: 5:5: error[E004]: an array declaration cannot take an initialiser (minilang-spec.md section 5)
```

each with exit status **1**.

**What it means:** errors come from three different stages (the lexer, the
parser and the semantic checks), each with a file, line and column. The third
is decision 0001: `arrays.mini` parses fine but is rejected by design.

### 4.8 The proof: all four targets agree

This is the most important command of the demo:

```
python -m tests.harness.differential
```

Expected (on the Mac, with clang):

```
REJECT  tests/corpus/valid/arrays.mini: BuildError: 5:5: error[E004]: …

7/7 programs agree on interp, llvm, wasm, vm (100.0%)
1 program(s) rejected by the front end (not counted)
```

Then the big one, which adds 500 randomly generated programs:

```
python -m tests.harness.differential --random 500
python -m tests.harness.differential --random 500 --opt 1
```

Expected:

```
507/507 programs agree on interp, llvm, wasm, vm (100.0%)
1 program(s) rejected by the front end (not counted)
```

Each run takes a minute or two.

**What it means:** every program was compiled to all targets and run on all
four: the interpreter, LLVM (compiled with clang), WebAssembly (run in
wasmtime) and our VM. The output, return value and traps matched on all 507.
With `--opt 1`, each program is also compared with its *unoptimised* version,
which proves the optimiser didn't change what any program does.

On Windows without clang, the other three targets are compared and LLVM is
reported as skipped:

```
7/7 programs agree on interp, wasm, vm (100.0%)
1 program(s) rejected by the front end (not counted)
skipped (tool not installed): llvm
```

To show what a random program looks like:

```
python -m tests.harness.gen_random 42
```

That prints a generated MiniLang program with functions, loops, arrays,
recursion and mixed types. The generator guarantees the program is well typed
and terminates.

### 4.9 The test suite

```
python -m pytest -q
```

Expected: **582 passed** on the Mac (about 40 seconds). On Windows without
clang: **575 passed, 7 skipped**. The 7 are the tests that need clang, and
none fail.

## 5. How the tests work and what they prove

### The argument, in one paragraph

We have **four independent implementations** of the same meaning: the
interpreter computes it directly, and three back ends translate it into three
very different machines (register-based LLVM, structured stack-based
WebAssembly, and our bytecode VM). If any back end had a bug in translation, a
guard, or the edge-case semantics, its output would differ from the others on
some program. They agree on the whole corpus, every divergence-row boundary
program, and **500 random programs, at both optimisation levels**. Every bug
the random testing ever found got its own regression test.

### Test files (582 tests, `tests/`)

| File | Tests | What it checks |
|---|---|---|
| `test_lexer.py` | 11 | tokens, positions, lexical errors |
| `test_parser.py` | 33 | grammar, precedence, syntax errors |
| `test_typecheck.py` | 40 | each error code E001–E012 is reported; valid programs are accepted; literal typing |
| `test_cir_builder.py` | 54 | AST → CIR; the builder's output for `abs.mini` matches the hand-drawn `docs/examples/abs.cir` |
| `test_cir_printer.py`, `test_cir_parser.py` | 11 + 41 | CIR text prints and parses back **exactly** (round trip) |
| `test_cir_cfg.py` | 26 | control-flow graph and verifier checks |
| `test_interp.py` | 26 | the oracle itself: wrapping, traps, conversions, `%f` floats |
| `test_opt.py` | 70 | each optimisation, and that nothing that would trap gets folded |
| `test_llvm_backend.py` | 66 | the guards for rows 1, 2, 3, 6, 7 are emitted; the IR is valid (checked with clang) |
| `test_wasm_backend.py` | 32 | the module validates and runs in wasmtime; shadow stack; recursion |
| `test_structurer.py` | 15 | CFG → nested `block`/`loop`/`if` |
| `test_reg2stack.py` | 23 | register → stack lowering and its peephole |
| `test_stackvm.py` | 16 | the bytecode emitter and the VM agree |
| `test_driver.py` | 14 | the command line: flags, exit codes 0/1/4 |
| `test_differential.py` | 104 | the four-way comparison: corpus + 40 random seeds at O0 and O1, plus pinned regression seeds |

### How a test is written (examples to show)

Tests are plain `pytest` functions: build something, then `assert` what must
be true. Three styles:

**1. Unit test, one stage** (`tests/test_cir_builder.py`):

```python
def test_negating_a_literal_folds_rather_than_emitting_a_subtraction(mini_module):
    fn = mini_module("fn f() -> int { return -2147483648; }").function("f")
    assert ops(fn) == ["ret"]
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(-2147483648, Ty.I32)
```

**2. Table of error cases** (`tests/test_typecheck.py`): one row per error
code, a small program that must trigger it, and a check that the right code
comes out:

```python
@pytest.mark.parametrize("code,body,extra", [
    ("E001", "x = 1;", ""),
    ("E004", "let xs: int[8] = 0;", ""),        # decision 0001
    ("E005", "let x: int = 4294967296;", ""),
    ...
])
def test_diagnostics(code, body, extra):
    with pytest.raises(TypeError_) as e:
        chk(body, extra)
    assert e.value.code == code
```

**3. Differential test** (`tests/test_differential.py`): run one program on
all four targets and require them to agree:

```python
@pytest.mark.parametrize("seed", range(40))
@pytest.mark.parametrize("opt", [0, 1])
def test_random(seed, opt):
    check(run_all(source=generate(seed), name=f"seed{seed}", opt=opt))
```

### The test harness (`tests/harness/`)

- `runners.py`: one function per target. Each returns the same triple
  `(return value, printed output, trapped?)`.
- `differential.py`: runs a program on all targets and compares the triples.
  It allows two documented differences: LLVM only sees the process exit
  status, so return values are compared **mod 256**; and NaN may print as
  `nan` or `-nan`.
- `gen_random.py`: the random program generator. It's deterministic (seed 42
  always gives the same program), the programs are always well typed and
  always terminate, and it never reads a variable before it's assigned (that
  is undefined by our spec).

### Bugs the random testing found (good to mention)

The differential testing caught real bugs that the unit tests missed. All are
fixed and each has a regression test:

- LLVM lost the output printed before a trap (fixed with `fflush`).
- LLVM emitted invalid IR for `bool` arrays (`alloca i1, i32 true`).
- The interpreter and constant folder didn't wrap constants (`4294967296` as
  an `int`).
- A folded `true` became -1 instead of 1.
- The builder rejected `global g: int = -5;`.
- The sign of NaN differs between x86 and ARM (handled in the harness).

### Continuous integration

Every push to GitHub runs 5 CI jobs (`.github/workflows/ci.yml`): the C++
build and tests, the Python tests, validation of the hand-written example
files, and the 500-program differential at `--opt 0` and `--opt 1`. All are
green on `main`. You can show them on the repo's **Actions** tab.

## 6. Which code files to know for Review 3

Everyone should be able to explain **the pipeline (section 2)**, **the
divergence table**, and **the differential argument (section 5)**. Then each
of us should know our own files well enough to walk through them:

| Member | Files to know | Be ready to explain |
|---|---|---|
| **M1 Krishita** (front end, spec, CLI) | `src/frontend/lexer.py`, `src/frontend/parser.py`, `src/frontend/ast_nodes.py`, `src/driver.py`, `docs/minilang-spec.md` | How the lexer and the recursive-descent parser work; precedence; the order of the driver's stages and its exit codes; the spec decisions (0001, literal typing). |
| **M2 Pragati** (semantics) | `src/sema/typecheck.py`, `src/sema/symtab.py`, `src/sema/diagnostics.py`, `src/cir/verifier.py`, `src/cir/interp.py` | The 12 error codes; scopes and shadowing; widening; why the interpreter is the oracle and how it does traps and wrapping. |
| **M3 Sanjay** (CIR core, optimiser, LLVM) | `src/cir/ir.py`, `src/cir/builder.py`, `src/cir/printer.py`, `src/cir/parser.py`, `src/opt/constfold.py`, `src/opt/copyprop.py`, `src/opt/dce.py`, `src/backend/llvm/emit_ll.py`, `src/backend/llvm/guards.py` | The CIR design; AST→CIR lowering; why the folder never folds a trap; the LLVM guard for each divergence row. |
| **M4 Sparsh** (stack targets, testing) | `src/backend/wasm/emit_wat.py`, `src/backend/wasm/structurer.py`, `src/backend/wasm/reg2stack.py`, `src/backend/stackvm/emit_sbc.py`, `src/backend/stackvm/vm.py`, `tests/harness/` | Turning a CFG into nested blocks; the shadow stack; register-to-stack lowering; how the differential harness and the generator work. |

Good further reading: `docs/divergence.md` (the table, plus a log of every
cross-target bug found), `docs/cir-spec.md` (the IR), and `status.md`
(current state).

## 7. Questions the professor may ask

**Why have an IR at all, instead of going straight to each target?**
So semantics are decided once. Without CIR, each back end would answer "what
does `x / 0` mean?" on its own, and they'd disagree. With CIR there's one
answer, and each back end only has to *realise* it.

**How do you know the targets are correct?**
They're compared with an independent oracle (the interpreter) and with each
other on 507 programs, including every divergence row and 500 random
programs, at both optimisation levels. They agree 100%.

**What happens on division by zero?**
CIR says trap. WebAssembly traps natively; LLVM needs our explicit guard
(section 4.6); the interpreter and VM check for it. The exit status is 4.

**Why doesn't WebAssembly just use the CFG?**
It has no `goto`. Control flow must be nested `block`/`loop`/`if`, so the
structurer rebuilds it. For a CFG that can't be nested (irreducible), the
emitter falls back to a dispatch loop.

**Where do local variables live in WebAssembly?**
Wasm locals have no address, so anything that needs memory (arrays, and
locals) goes on a *shadow stack* in linear memory. Each call gets its own
frame, so recursion works.

**What doesn't it do?**
Global arrays aren't lowered (only scalar globals), and there's no cast syntax
in MiniLang. Reading a variable before assigning it is undefined by our spec.
`status.md` lists these.
