"""Small MiniLang programs shared by the back-end tests.

Each one targets a bug a back end has had, or a divergence.md row.
"""

CORPUS = ["valid/arith", "valid/control_flow", "valid/recursion",
          "boundary/nesting", "boundary/shift_edge", "boundary/div_edge"]

SOURCES = {
    "recursion_with_locals": """
        fn f(n: int) -> int {
            let x: int = n;
            if (n > 0) { let y: int = f(n - 1); }
            return x;
        }
        fn main() -> int { print_int(f(5)); return 0; }""",
    "srem_int_min_traps": """
        fn r(a: int, b: int) -> int { return a % b; }
        fn main() -> int { print_int(r(7, 3)); print_int(r(-2147483648, -1)); return 0; }""",
    "conversions": """
        fn main() -> int {
            let i: int = -7; let l: long = i; let f: float = i;
            print_float(f / 2.0);
            if (l < 0) { print_int(1); }
            return 0;
        }""",
    "bounds_trap": """
        fn main() -> int { let xs: int[4]; let i: int = 4; xs[0] = 1; print_int(xs[0]); xs[i] = 2; return 0; }""",
    "long_arithmetic": """
        fn main() -> int {
            let a: long = 4294967296; let b: long = a * 3 - 1;
            if (b > a) { print_int(2); }
            let n: long = -a; if (n < 0) { print_int(-1); }
            return 0;
        }""",
    "literals_typed_by_value": """
        fn main() -> int {
            let a: long = 4294967296 + 1; let b: long = -9000000000;
            let c: long = 0 / (4294967296 | 1);
            if (a == 4294967297) { print_int(1); }
            if (b < -8999999999) { print_int(2); }
            if (c == 0) { print_int(3); }
            let m: int = -2147483648; print_int(m);
            return 0;
        }""",
    "globals_and_bools": """
        global g: int = 5;
        global flag: bool = true;
        fn bump() { g = g + 1; }
        fn main() -> int { bump(); bump(); if (!flag) { print_int(0); } print_int(g); return g; }""",
}

