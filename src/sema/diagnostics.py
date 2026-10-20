"""Diagnostic formatting and error codes.

Module M2.  Owner: Member 2.

The codes are the twelve in docs/minilang-spec.md section 7.
"""
CODES = {
    "E001": "undeclared identifier",
    "E002": "duplicate declaration in the same scope",
    "E003": "type mismatch in a binary operation",
    "E004": "type mismatch in an assignment",
    "E005": "narrowing conversion without an explicit cast",
    "E006": "call to an undeclared function",
    "E007": "wrong number of arguments",
    "E008": "argument type mismatch",
    "E009": "missing return on some path of a non-void function",
    "E010": "'return' with a value in a void function",
    "E011": "'break' or 'continue' outside a loop",
    "E012": "indexing a non-array, or a non-integer index",
}


class Diagnostic:
    """file:line:col: severity: message  -- one shared format for every phase."""

    def __init__(self, code, message, line, col, filename="<input>"):
        assert code in CODES, code
        self.code, self.message = code, message
        self.line, self.col, self.filename = line, col, filename

    def __str__(self):
        return f"{self.filename}:{self.line}:{self.col}: error[{self.code}]: {self.message}"
