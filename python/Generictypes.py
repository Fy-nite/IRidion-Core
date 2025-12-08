"""
Type definitions and value representations for ObjectIR runtime
"""

from enum import Enum
from dataclasses import dataclass
from typing import Any


class ValueType(Enum):
    """Supported value types in ObjectIR"""
    INT32 = "System.Int32"
    INT64 = "System.Int64"
    FLOAT = "System.Float"
    DOUBLE = "System.Double"
    STRING = "System.String"
    BOOL = "System.Boolean"
    VOID = "System.Void"
    OBJECT = "System.Object"


@dataclass
class Value:
    """Represents a value on the stack"""
    data: Any
    type_: ValueType

    def __repr__(self):
        return f"Value({self.data}, {self.type_.name})"
