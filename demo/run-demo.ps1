<#
.SYNOPSIS
    Review demo for the Multi-Target IR Translator.

.DESCRIPTION
    Drives the real mtirc executable over the real repository examples.  It
    fakes nothing: every line of output below the banners is what the compiler
    actually printed, and any step that fails stops the run with a non-zero
    exit code.

    Stages that need an external tool (llvm-as, wat2wasm, wasmtime) are
    reported as such rather than skipped silently -- see -Stage status.

.PARAMETER Stage
    all      every stage, in pipeline order              (default)
    front    lexer, parser/AST, semantic analysis
    cir      MiniLang -> CIR, and the CIR round-trip
    verify   the CIR verifier, on a well-formed and a malformed module
    opt      unoptimised vs optimised CIR, and identical behaviour
    llvm     CIR -> LLVM IR
    wasm     CIR -> WebAssembly text
    stack    CIR -> stack bytecode, then executed on the reference VM
    tests    the project's own test suite via ctest
    status   what is executable here and what needs a tool that is absent

.PARAMETER Verify
    Also diff generated output against demo/expected/.  Off by default: the
    demo is about showing the compiler work, not about golden files, and
    ctest already owns the golden checks.

.PARAMETER Pause
    Wait for a keypress between sections.  Useful when presenting.

.EXAMPLE
    .\demo\run-demo.ps1
    .\demo\run-demo.ps1 -Stage cir
    .\demo\run-demo.ps1 -Stage all -Pause
#>
[CmdletBinding()]
param(
    [ValidateSet('all','front','cir','verify','opt','llvm','wasm','stack','tests','status')]
    [string]$Stage = 'all',

    [switch]$Verify,
    [switch]$Pause
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Locate the project and the executable
# ---------------------------------------------------------------------------
$DemoDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root    = Split-Path -Parent $DemoDir
$OutDir  = Join-Path $DemoDir 'out'

# Every layout CMake might have produced, in the order we prefer them.
$Candidates = @(
    (Join-Path $Root 'build\mtirc.exe'),
    (Join-Path $Root 'build\Release\mtirc.exe'),
    (Join-Path $Root 'build\Debug\mtirc.exe'),
    (Join-Path $Root 'build\bin\mtirc.exe'),
    (Join-Path $Root 'build\bin\Release\mtirc.exe')
)

$Mtirc = $null
foreach ($c in $Candidates) {
    if (Test-Path $c) { $Mtirc = $c; break }
}

$BuildHelp = @'
mtirc was not found.  Build it first:

    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    cmake --build build

  The "NMake Makefiles" generator is used deliberately.  The Visual Studio
  generators need a registered VS instance, and on this machine vswhere
  reports none: the 2022 Build Tools are installed without the C++ compiler
  component, and the 2019 Build Tools are not registered.  vcvars64.bat puts
  the real MSVC 14.29 toolchain on PATH, and NMake Makefiles uses whatever is
  on PATH, which sidesteps the discovery problem entirely.

  If you later install the "Desktop development with C++" workload for the
  2022 Build Tools, this also works and needs no vcvars shell:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
'@

if (-not $Mtirc) {
    Write-Host ''
    Write-Host $BuildHelp -ForegroundColor Yellow
    Write-Host ''
    exit 2
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---------------------------------------------------------------------------
# Presentation helpers
# ---------------------------------------------------------------------------
$script:StepsRun    = 0
$script:StepsFailed = 0

function Write-Section {
    param([string]$Number, [string]$Title, [string]$Point)
    Write-Host ''
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
    Write-Host ("  $Number  $Title") -ForegroundColor Cyan
    if ($Point) { Write-Host ("       $Point") -ForegroundColor DarkGray }
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
}

function Write-Note {
    param([string]$Text)
    Write-Host "  $Text" -ForegroundColor DarkGray
}

function Wait-IfPausing {
    if ($Pause) {
        Write-Host ''
        Write-Host '  [press any key]' -ForegroundColor DarkGray
        $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
    }
}

<#
Run mtirc and show exactly what it printed.

ExpectExit lets a step assert a non-zero status on purpose -- a trap exits 4
and a rejected module exits 1, and both are correct behaviour being
demonstrated, not failures.
#>
function Invoke-Mtirc {
    param(
        [string[]]$MtircArgs,
        [int]$ExpectExit = 0,
        [string]$SaveAs,
        [int]$Head = 0,
        [switch]$Quiet
    )

    $display = $MtircArgs | ForEach-Object {
        if ($_.StartsWith($Root)) { $_.Substring($Root.Length).TrimStart('\') } else { $_ }
    }
    $shown = ($display -join ' ')
    Write-Host ''
    Write-Host "  $ mtirc $shown" -ForegroundColor Green

    $stdoutFile = Join-Path $OutDir '.stdout.tmp'
    $stderrFile = Join-Path $OutDir '.stderr.tmp'

    $proc = Start-Process -FilePath $Mtirc -ArgumentList $MtircArgs `
        -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile

    $out = ''
    if (Test-Path $stdoutFile) { $out = Get-Content $stdoutFile -Raw }
    $err = ''
    if (Test-Path $stderrFile) { $err = Get-Content $stderrFile -Raw }

    $script:StepsRun++

    if ($SaveAs) {
        $target = Join-Path $OutDir $SaveAs
        $noBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($target, $out, $noBom)
    }

    if (-not $Quiet -and $out) {
        $lines = $out -split "`r?`n"
        if ($Head -gt 0 -and $lines.Count -gt $Head) {
            $lines[0..($Head - 1)] | ForEach-Object { Write-Host "  $_" }
            Write-Host ("  ... ({0} more lines; full output in demo\out\{1})" -f ($lines.Count - $Head), $SaveAs) -ForegroundColor DarkGray
        } else {
            $lines | ForEach-Object { Write-Host "  $_" }
        }
    }

    if ($err) {
        ($err -split "`r?`n") | ForEach-Object {
            if ($_) { Write-Host "  $_" -ForegroundColor Yellow }
        }
    }

    if ($proc.ExitCode -ne $ExpectExit) {
        Write-Host ("  STEP FAILED: expected exit {0}, got {1}" -f $ExpectExit, $proc.ExitCode) -ForegroundColor Red
        $script:StepsFailed++
        return $false
    }

    if ($ExpectExit -ne 0) {
        Write-Host ("  (exit {0} -- expected: this is the behaviour being demonstrated)" -f $proc.ExitCode) -ForegroundColor DarkGray
    }
    return $true
}

function Compare-Expected {
    param([string]$Generated, [string]$ExpectedName)
    if (-not $Verify) { return }

    $expectedPath = Join-Path (Join-Path $DemoDir 'expected') $ExpectedName
    if (-not (Test-Path $expectedPath)) {
        Write-Host "  -Verify: no reference file $ExpectedName" -ForegroundColor Yellow
        return
    }
    $a = (Get-Content (Join-Path $OutDir $Generated) -Raw) -replace "`r`n", "`n"
    $b = (Get-Content $expectedPath -Raw) -replace "`r`n", "`n"
    if ($a -eq $b) {
        Write-Host "  -Verify: matches demo\expected\$ExpectedName" -ForegroundColor Green
    } else {
        Write-Host "  -Verify: DIFFERS from demo\expected\$ExpectedName" -ForegroundColor Red
        $script:StepsFailed++
    }
}

# ---------------------------------------------------------------------------
# Inputs -- real repository files, not demo-only copies
# ---------------------------------------------------------------------------
$AbsMini    = Join-Path $Root 'docs\examples\abs.mini'
$AbsCir     = Join-Path $Root 'docs\examples\abs.cir'
$ArithMini  = Join-Path $Root 'tests\corpus\valid\arith.mini'
$ShiftMini  = Join-Path $Root 'tests\corpus\boundary\shift_edge.mini'
$DivMini    = Join-Path $Root 'tests\corpus\boundary\div_edge.mini'
$BrokenCir  = Join-Path $DemoDir 'examples\broken.cir'

# ---------------------------------------------------------------------------
# Stages
# ---------------------------------------------------------------------------
function Stage-Front {
    Write-Section '1' 'Front end: MiniLang -> tokens -> AST' `
        'The C++ lexer and parser.  Semantic analysis runs next and is what types the AST.'
    Write-Note "source: docs\examples\abs.mini"
    Write-Host ''
    Get-Content $AbsMini | ForEach-Object { Write-Host "  | $_" -ForegroundColor White }

    [void](Invoke-Mtirc -MtircArgs @('--emit=tokens', $AbsMini) -SaveAs 'abs.tokens.txt' -Head 8)
    [void](Invoke-Mtirc -MtircArgs @('--emit=ast', $AbsMini) -SaveAs 'abs.ast.txt' -Head 14)
    Wait-IfPausing
}

function Stage-Cir {
    Write-Section '2' 'Middle end: MiniLang -> CIR' `
        'Typed, register-based, three-address, explicit CFG -- and deliberately not SSA.'
    [void](Invoke-Mtirc -MtircArgs @('--emit=cir', $AbsMini) -SaveAs 'abs.cir')
    Compare-Expected -Generated 'abs.cir' -ExpectedName 'abs.cir'

    Write-Note ''
    Write-Note 'The .cir text is the contract between the middle end and every back end.'
    Write-Note 'It round-trips: printing a parsed module reproduces the input exactly.'
    [void](Invoke-Mtirc -MtircArgs @('--emit=cir', $AbsCir) -SaveAs 'abs.roundtrip.cir' -Quiet)

    $a = (Get-Content (Join-Path $OutDir 'abs.roundtrip.cir') -Raw) -replace "`r`n", "`n"
    $b = (Get-Content $AbsCir -Raw) -replace "`r`n", "`n"
    $script:StepsRun++
    if ($a -eq $b) {
        Write-Host '  round-trip: output is byte-identical to docs\examples\abs.cir' -ForegroundColor Green
    } else {
        Write-Host '  round-trip: DIFFERS from docs\examples\abs.cir' -ForegroundColor Red
        $script:StepsFailed++
    }
    Wait-IfPausing
}

function Stage-Verify {
    Write-Section '3' 'CIR verifier: eight well-formedness rules' `
        'Objective O1 -- malformed IR is detected and named, not passed to a back end.'
    Write-Note 'A well-formed module verifies silently and exits 0:'
    [void](Invoke-Mtirc -MtircArgs @('--verify', '--emit=cir', $AbsCir) -Quiet)
    Write-Host '  (no diagnostics)' -ForegroundColor DarkGray

    Write-Note ''
    Write-Note 'demo\examples\broken.cir branches to a block that does not exist.'
    Write-Note 'Rule 3: every branch target names a block in the same function.'
    [void](Invoke-Mtirc -MtircArgs @('--verify', '--emit=cir', $BrokenCir) -ExpectExit 1)
    Wait-IfPausing
}

function Stage-Opt {
    Write-Section '4' 'Optimiser: constant folding, copy propagation, DCE' `
        'Run to a fixed point over CIR -- once, for all three targets.'
    Write-Note 'source: tests\corpus\valid\arith.mini'
    Write-Host ''
    Get-Content $ArithMini | ForEach-Object { Write-Host "  | $_" -ForegroundColor White }

    [void](Invoke-Mtirc -MtircArgs @('--emit=cir', '--opt=0', $ArithMini) -SaveAs 'arith.O0.cir' -Quiet)
    [void](Invoke-Mtirc -MtircArgs @('--emit=cir', '--opt=1', $ArithMini) -SaveAs 'arith.O1.cir' -Quiet)

    $o0 = @(Get-Content (Join-Path $OutDir 'arith.O0.cir') | Where-Object { $_ -match '^\s\s\S' })
    $o1 = @(Get-Content (Join-Path $OutDir 'arith.O1.cir') | Where-Object { $_ -match '^\s\s\S' })

    Write-Host ''
    Write-Host ("  unoptimised: {0} instructions       optimised: {1} instructions" -f $o0.Count, $o1.Count) -ForegroundColor Cyan
    Write-Host ''
    Write-Host '  --- entry block, --opt=0 ---' -ForegroundColor DarkGray
    Get-Content (Join-Path $OutDir 'arith.O0.cir') | Select-Object -First 18 | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
    Write-Host '  --- entry block, --opt=1 ---' -ForegroundColor DarkGray
    Get-Content (Join-Path $OutDir 'arith.O1.cir') | Select-Object -First 18 | ForEach-Object { Write-Host "  $_" }

    Write-Note ''
    Write-Note 'The point is not that it shrank -- it is that behaviour is unchanged.'
    Write-Note 'Both are executed on the reference VM and must print the same thing:'
    [void](Invoke-Mtirc -MtircArgs @('--run', '--opt=0', $ArithMini))
    [void](Invoke-Mtirc -MtircArgs @('--run', '--opt=1', $ArithMini))
    Wait-IfPausing
}

function Stage-Llvm {
    Write-Section '5' 'Back end 1: CIR -> LLVM IR' `
        'Register machine to register machine.  LLVM branches to any label, so this is close to a printer over the CFG.'
    [void](Invoke-Mtirc -MtircArgs @('--emit=ll', $AbsCir) -SaveAs 'abs.ll')
    Compare-Expected -Generated 'abs.ll' -ExpectedName 'abs.ll'

    Write-Note ''
    Write-Note 'Guards: CIR fixes semantics the targets disagree about (docs\divergence.md).'
    Write-Note 'A signed division needs an explicit trap guard in LLVM, which has none:'
    [void](Invoke-Mtirc -MtircArgs @('--emit=ll', (Join-Path $Root 'tests\cir\div_edge.cir')) -SaveAs 'div_edge.ll' -Head 16)

    Write-Note ''
    Write-Note 'NOT verified here: llvm-as is not installed on this machine, so this IR'
    Write-Note 'has not been assembled locally.  CI runs llvm-as over it; see STATUS.md.'
    Wait-IfPausing
}

function Stage-Wasm {
    Write-Section '6' 'Back end 2: CIR -> WebAssembly text' `
        'Register machine to stack machine, and an arbitrary CFG into structured regions.'
    [void](Invoke-Mtirc -MtircArgs @('--emit=wat', $AbsCir) -SaveAs 'abs.wat')
    Compare-Expected -Generated 'abs.wat' -ExpectedName 'abs.wat'

    Write-Note ''
    Write-Note 'Two things had to happen that LLVM never needed:'
    Write-Note '  1. register-to-stack lowering -- operands are pushed, not addressed'
    Write-Note '  2. CFG structuring -- WebAssembly has no branch to an arbitrary label,'
    Write-Note '     so the CFG is re-expressed as a br_table dispatch tower'
    Write-Note ''
    Write-Note 'NOT verified here: wat2wasm is not installed, so this module has not been'
    Write-Note 'assembled locally.  CI runs wat2wasm and wasm-validate; see STATUS.md.'
    Wait-IfPausing
}

function Stage-Stack {
    Write-Section '7' 'Back end 3: CIR -> stack bytecode -> executed' `
        'The one target that runs here, so the semantics are observed rather than asserted.'
    [void](Invoke-Mtirc -MtircArgs @('--emit=sbc', $AbsCir) -SaveAs 'abs.sbc.txt')
    Compare-Expected -Generated 'abs.sbc.txt' -ExpectedName 'abs.sbc.txt'

    Write-Note ''
    Write-Note 'Labels stay flat and branches are absolute addresses -- no structuring pass.'
    Write-Note 'Now run the whole program, source to result, on the C++ reference VM:'
    [void](Invoke-Mtirc -MtircArgs @('--run', $AbsMini))
    Write-Note 'abs(-7) = 7.'

    Write-Note ''
    Write-Note 'Because the VM runs, docs\divergence.md is executable. Shift counts are'
    Write-Note 'taken modulo the operand width, so 1 << 32 is 1, not 0:'
    [void](Invoke-Mtirc -MtircArgs @('--run', $ShiftMini))

    Write-Note ''
    Write-Note 'And INT_MIN / -1 traps rather than wrapping. A trap exits 4:'
    [void](Invoke-Mtirc -MtircArgs @('--run', $DivMini) -ExpectExit 4)
    Wait-IfPausing
}

function Stage-Tests {
    Write-Section '8' 'Tests' 'The project suite, run live. Nothing here is a canned number.'

    $testsExe = Join-Path $Root 'build\mtir_tests.exe'
    if (Test-Path $testsExe) {
        Write-Host ''
        Write-Host '  $ build\mtir_tests.exe' -ForegroundColor Green
        $script:StepsRun++
        $unit = & $testsExe 2>&1
        $unitExit = $LASTEXITCODE
        $unit | Select-Object -Last 3 | ForEach-Object { Write-Host "  $_" }
        if ($unitExit -ne 0) {
            Write-Host '  STEP FAILED: unit tests reported failures' -ForegroundColor Red
            $script:StepsFailed++
        }
    } else {
        Write-Host '  build\mtir_tests.exe not found; build it to run the unit suite.' -ForegroundColor Yellow
    }

    Write-Note ''
    Write-Note 'ctest additionally runs the golden-file checks and executes the corpus:'
    if (Get-Command ctest -ErrorAction SilentlyContinue) {
        Write-Host ''
        Write-Host '  $ ctest --test-dir build' -ForegroundColor Green
        $script:StepsRun++
        $ct = & ctest --test-dir (Join-Path $Root 'build') 2>&1
        $ctExit = $LASTEXITCODE
        $ct | Select-Object -Last 10 | ForEach-Object { Write-Host "  $_" }
        if ($ctExit -ne 0) {
            Write-Host '  STEP FAILED: ctest reported failures' -ForegroundColor Red
            $script:StepsFailed++
        }
    } else {
        Write-Host '  ctest is not on PATH in this shell.' -ForegroundColor Yellow
        Write-Note 'Open a Developer Command Prompt, or add CMake to PATH, then:'
        Write-Note '  ctest --test-dir build --output-on-failure'
    }
    Wait-IfPausing
}

function Stage-Status {
    Write-Section '0' 'What can and cannot be executed on this machine' `
        'Stated up front so nothing in the demo is mistaken for more than it is.'

    $tools = @(
        @{ Name = 'llvm-as';       For = 'assemble generated LLVM IR' },
        @{ Name = 'lli';           For = 'execute generated LLVM IR' },
        @{ Name = 'wat2wasm';      For = 'assemble generated WebAssembly' },
        @{ Name = 'wasm-validate'; For = 'validate generated WebAssembly' },
        @{ Name = 'wasmtime';      For = 'execute generated WebAssembly' }
    )

    Write-Host ''
    Write-Host '  Executable here (no external tool needed):' -ForegroundColor Green
    Write-Host '    front end, semantic analysis, CIR, CFG + verifier, optimiser,'
    Write-Host '    all three back ends (text emission), and the stack VM (execution).'
    Write-Host ''
    Write-Host '  External tools:' -ForegroundColor Cyan
    foreach ($t in $tools) {
        $found = Get-Command $t.Name -ErrorAction SilentlyContinue
        if ($found) {
            Write-Host ("    {0,-14} present  -- {1}" -f $t.Name, $t.For) -ForegroundColor Green
        } else {
            Write-Host ("    {0,-14} ABSENT   -- {1}" -f $t.Name, $t.For) -ForegroundColor Yellow
        }
    }
    Write-Host ''
    Write-Host '  So: LLVM IR and WebAssembly are generated and inspected here, never' -ForegroundColor DarkGray
    Write-Host '  assembled or executed. CI does that. See demo\presentation\STATUS.md.' -ForegroundColor DarkGray
    Wait-IfPausing
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '  Multi-Target Intermediate Representation Translator' -ForegroundColor White
Write-Host '  Project A30 . Team 5 . BCSE307 Compiler Design' -ForegroundColor DarkGray
Write-Host ''
Write-Note "executable: $Mtirc"
Write-Note "outputs:    demo\out\"

switch ($Stage) {
    'front'  { Stage-Front }
    'cir'    { Stage-Cir }
    'verify' { Stage-Verify }
    'opt'    { Stage-Opt }
    'llvm'   { Stage-Llvm }
    'wasm'   { Stage-Wasm }
    'stack'  { Stage-Stack }
    'tests'  { Stage-Tests }
    'status' { Stage-Status }
    'all'    {
        Stage-Status
        Stage-Front
        Stage-Cir
        Stage-Verify
        Stage-Opt
        Stage-Llvm
        Stage-Wasm
        Stage-Stack
        Stage-Tests
    }
}

Remove-Item (Join-Path $OutDir '.stdout.tmp') -ErrorAction SilentlyContinue
Remove-Item (Join-Path $OutDir '.stderr.tmp') -ErrorAction SilentlyContinue

Write-Host ''
Write-Host ('=' * 78) -ForegroundColor DarkCyan
if ($script:StepsFailed -eq 0) {
    Write-Host ("  {0} steps run, all as expected.  Generated files: demo\out\" -f $script:StepsRun) -ForegroundColor Green
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
    Write-Host ''
    exit 0
} else {
    Write-Host ("  {0} steps run, {1} FAILED." -f $script:StepsRun, $script:StepsFailed) -ForegroundColor Red
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
    Write-Host ''
    exit 1
}
