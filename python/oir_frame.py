"""
Execution frame management for ObjectIR runtime
"""

from typing import Dict, List, Optional
from oir_types import Value, ValueType


class ExecutionFrame:
    """Represents a method execution frame with local variables and stack"""
    
    def __init__(self, method_name: str):
        self.method_name = method_name
        self.stack: List[Value] = []
        self.locals: Dict[str, Value] = {}
        self.return_value: Optional[Value] = None
        self.pc = 0  # Program counter
    
    def push(self, value: Value):
        """Push value onto stack"""
        self.stack.append(value)
    
    def pop(self) -> Value:
        """Pop value from stack"""
        if not self.stack:
            raise RuntimeError(f"Stack underflow in {self.method_name}")
        return self.stack.pop()
    
    def peek(self) -> Value:
        """Peek at top of stack without removing"""
        if not self.stack:
            raise RuntimeError(f"Stack underflow in {self.method_name}")
        return self.stack[-1]
    
    def set_local(self, name: str, value: Value):
        """Set local variable"""
        self.locals[name] = value
    
    def get_local(self, name: str) -> Value:
        """Get local variable"""
        if name not in self.locals:
            raise RuntimeError(f"Undefined local variable: {name}")
        return self.locals[name]
