"""Diagnostic formatting and error codes.

Module M2.  Owner: Member 2.
Status: PLANNED -- Week 4.
"""
class Diagnostic:
    """file:line:col: severity: message  -- one shared format for every phase."""

    def __init__(self, code, message, line, col, filename="<input>"):
        self.code, self.message = code, message
        self.line, self.col, self.filename = line, col, filename

    def __str__(self):
        return f"{self.filename}:{self.line}:{self.col}: error[{self.code}]: {self.message}"
