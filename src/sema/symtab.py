"""Scope-chained symbol table.

Module M2.  Owner: Member 2.

declare() rejects a duplicate in the *current* scope only, so shadowing an
outer name is legal (docs/minilang-spec.md section 5); resolve() walks
outward from the innermost scope.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Optional


@dataclass
class Symbol:
    """A declared name: variable, parameter, global or function."""
    name: str
    kind: str              # 'var' | 'param' | 'global' | 'fn'
    ty: object             # a sema type (see typecheck.py) or a FnDecl for 'fn'
    node: object = None


class Scope:
    """One lexical scope; scopes form a chain to the enclosing scope."""

    def __init__(self, parent: Optional["Scope"] = None) -> None:
        self.parent = parent
        self.names: Dict[str, Symbol] = {}


class SymbolTable:
    """Push/pop scopes, declare names, resolve references."""

    def __init__(self) -> None:
        self.current = Scope()

    def push(self) -> None:
        self.current = Scope(self.current)

    def pop(self) -> None:
        assert self.current.parent is not None, "cannot pop the global scope"
        self.current = self.current.parent

    def declare(self, sym: Symbol) -> bool:
        """Add ``sym`` to the current scope.  False if it is already there."""
        if sym.name in self.current.names:
            return False
        self.current.names[sym.name] = sym
        return True

    def resolve(self, name: str) -> Optional[Symbol]:
        scope = self.current
        while scope is not None:
            if name in scope.names:
                return scope.names[name]
            scope = scope.parent
        return None
