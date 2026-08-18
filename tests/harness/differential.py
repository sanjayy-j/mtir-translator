"""Four-way differential test harness.

Module M8b.  Owner: Member 4.
Status: PLANNED -- Week 9.
"""
"""Run one program through every execution path and require agreement.

  1. compile to CIR, verify
  2. execute in the CIR reference interpreter          -> oracle
  3. emit .ll,  run under lli
  4. emit .wat, assemble with wat2wasm, run under wasmtime and node
  5. emit .sbc, run under the reference VM
  6. compare (return value, stdout, trapped) four ways

Objective O4: >= 98% four-way agreement over the 100-program corpus, with
every remaining disagreement documented in docs/divergence.md.
"""


def run_all(path):
    raise NotImplementedError(
        "differential harness is scheduled for Week 9 (M8b, Member 4)")
