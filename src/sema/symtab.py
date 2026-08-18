"""Scope-chained symbol table.

Module M2.  Owner: Member 2.
Status: PLANNED -- Week 3-4.
"""
class Symbol:
    """A declared name: variable, parameter, global or function."""


class Scope:
    """One lexical scope; scopes form a chain to the enclosing scope."""


class SymbolTable:
    """Push/pop scopes, declare names, resolve references.

    TODO(Member 2, Week 3): chained hash-table scopes; declare() must reject a
    duplicate in the *current* scope only; resolve() walks outward.
    """
