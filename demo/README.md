# Review demo

A 5–10 minute live demonstration of the Multi-Target IR Translator, driven by
the real `mtirc` executable over the real repository examples.

Nothing here is scripted output. Every line the runner prints below a banner
is what the compiler actually printed, and any step that does not behave as
expected turns the run red and exits non-zero.

## Build, then run

```powershell
# once, in a shell where the MSVC toolchain is on PATH
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# then, any time
.\demo\run-demo.ps1
```

If `mtirc` is missing, the runner says so and prints the build commands rather
than failing obscurely.

### Why "NMake Makefiles"

The Visual Studio generators need a registered VS instance, and `vswhere`
reports none on this machine: the 2022 Build Tools are installed *without*
the C++ compiler component, and the 2019 Build Tools are not registered.
`vcvars64.bat` puts the real MSVC 14.29 toolchain on `PATH`, and NMake
Makefiles uses whatever is on `PATH`, which sidesteps discovery entirely.

Installing the "Desktop development with C++" workload for the 2022 Build
Tools would also work, and would not need a vcvars shell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Stages

```powershell
.\demo\run-demo.ps1                 # everything, in pipeline order
.\demo\run-demo.ps1 -Stage status   # what can and cannot run on this machine
.\demo\run-demo.ps1 -Stage front    # lexer, parser/AST
.\demo\run-demo.ps1 -Stage cir      # MiniLang -> CIR, and the round-trip
.\demo\run-demo.ps1 -Stage verify   # the eight well-formedness rules
.\demo\run-demo.ps1 -Stage opt      # unoptimised vs optimised, same behaviour
.\demo\run-demo.ps1 -Stage llvm     # CIR -> LLVM IR
.\demo\run-demo.ps1 -Stage wasm     # CIR -> WebAssembly text
.\demo\run-demo.ps1 -Stage stack    # CIR -> bytecode -> executed on the VM
.\demo\run-demo.ps1 -Stage tests    # the project's own suite, live
```

Two flags:

- `-Pause` waits for a keypress between sections. Use it when presenting.
- `-Verify` additionally diffs generated output against `demo/expected/`.

`demo/run-demo.bat` forwards to the same script for `cmd.exe`.

## What is in here

```
demo/
  README.md                    this file
  run-demo.ps1                 the runner
  run-demo.bat                 cmd.exe wrapper
  examples/
    README.md                  which repository files the demo uses, and why
    broken.cir                 malformed module, for the verifier section
  expected/                    reference output, regenerate with the commands below
  presentation/
    DEMO-SCRIPT.md             what to say and run, minute by minute
    ARCHITECTURE.md            the design, for a faculty reader
    VIVA.md                    twenty questions with answers
    STATUS.md                  component-by-component, what is verified and how
  out/                         generated on each run; gitignored
```

## Regenerating demo/expected/

These are convenience references for `-Verify`, not the project's golden
files — `docs/examples/abs.cir` and `docs/examples/abs.gen.ll` are the
goldens, and `ctest` is what enforces them. Regenerate after any intentional
change to the compiler's output:

```powershell
.\build\mtirc.exe --emit=cir docs\examples\abs.mini -o demo\expected\abs.cir
.\build\mtirc.exe --emit=ll  docs\examples\abs.cir  -o demo\expected\abs.ll
.\build\mtirc.exe --emit=wat docs\examples\abs.cir  -o demo\expected\abs.wat
.\build\mtirc.exe --emit=sbc docs\examples\abs.cir  -o demo\expected\abs.sbc.txt
```

## Honesty about this machine

`llvm-as`, `lli`, `wat2wasm`, `wasm-validate` and `wasmtime` are **not
installed here**. The LLVM and WebAssembly back ends therefore emit text that
is inspected but never assembled or executed locally; CI installs both
toolchains and runs them. The stack bytecode target needs nothing external,
so it is the one that genuinely executes during the demo.

`-Stage status` prints this live rather than asserting it, so if you run the
demo on a machine that does have the tools, it will say so.
