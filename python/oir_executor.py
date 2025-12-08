"""
Instruction execution engine for ObjectIR runtime
"""

import re
import sys
from typing import Optional
from oir_types import Value, ValueType
from oir_frame import ExecutionFrame


class InstructionExecutor:
    """Executes ObjectIR instructions"""
    
    def __init__(self):
        self.console_output = []
    
    def execute_instruction(self, frame: ExecutionFrame, instruction: str) -> None:
        """Execute a single instruction"""
        parts = instruction.split()
        if not parts:
            return
        
        opcode = parts[0]
        
        # Load instructions
        if opcode == 'ldstr':
            self._ldstr(frame, instruction)
        elif opcode == 'ldc.i4':
            self._ldc_i4(frame, parts)
        elif opcode == 'ldc.i8':
            self._ldc_i8(frame, parts)
        elif opcode == 'ldc.r8':
            self._ldc_r8(frame, parts)
        elif opcode == 'ldnull':
            self._ldnull(frame)
        elif opcode == 'ldc.b.0':
            self._ldc_bool(frame, False)
        elif opcode == 'ldc.b.1':
            self._ldc_bool(frame, True)
        elif opcode == 'ldloc':
            self._ldloc(frame, parts)
        
        # Store instructions
        elif opcode == 'stloc':
            self._stloc(frame, parts)
        
        # Arithmetic
        elif opcode == 'add':
            self._add(frame)
        elif opcode == 'sub':
            self._sub(frame)
        elif opcode == 'mul':
            self._mul(frame)
        elif opcode == 'div':
            self._div(frame)
        elif opcode == 'rem':
            self._rem(frame)
        
        # Comparison
        elif opcode == 'ceq':
            self._ceq(frame)
        elif opcode == 'cgt':
            self._cgt(frame)
        elif opcode == 'clt':
            self._clt(frame)
        elif opcode == 'cge':
            self._cge(frame)
        elif opcode == 'cle':
            self._cle(frame)
        
        # Stack manipulation
        elif opcode == 'dup':
            self._dup(frame)
        elif opcode == 'pop':
            self._pop(frame)
        
        # Built-in method calls
        elif opcode == 'call':
            self._call(frame, instruction)
        
        # Return
        elif opcode == 'ret':
            self._ret(frame)
        
        else:
            print(f"Warning: Unknown opcode '{opcode}'")
    
    # Load instruction implementations
    def _ldstr(self, frame: ExecutionFrame, instruction: str) -> None:
        """Load string constant"""
        match = re.search(r'ldstr\s+"([^"]*)"', instruction)
        if match:
            value = Value(match.group(1), ValueType.STRING)
            frame.push(value)
    
    def _ldc_i4(self, frame: ExecutionFrame, parts: list) -> None:
        """Load 32-bit integer constant"""
        value_str = ' '.join(parts[1:])
        value = Value(int(value_str), ValueType.INT32)
        frame.push(value)
    
    def _ldc_i8(self, frame: ExecutionFrame, parts: list) -> None:
        """Load 64-bit integer constant"""
        value_str = ' '.join(parts[1:])
        value = Value(int(value_str), ValueType.INT64)
        frame.push(value)
    
    def _ldc_r8(self, frame: ExecutionFrame, parts: list) -> None:
        """Load double constant"""
        value_str = ' '.join(parts[1:])
        value = Value(float(value_str), ValueType.DOUBLE)
        frame.push(value)
    
    def _ldnull(self, frame: ExecutionFrame) -> None:
        """Load null reference"""
        value = Value(None, ValueType.OBJECT)
        frame.push(value)
    
    def _ldc_bool(self, frame: ExecutionFrame, bool_val: bool) -> None:
        """Load boolean constant"""
        value = Value(bool_val, ValueType.BOOL)
        frame.push(value)
    
    def _ldloc(self, frame: ExecutionFrame, parts: list) -> None:
        """Load local variable"""
        var_name = parts[1]
        value = frame.get_local(var_name)
        frame.push(value)
    
    # Store instruction implementations
    def _stloc(self, frame: ExecutionFrame, parts: list) -> None:
        """Store to local variable"""
        var_name = parts[1]
        value = frame.pop()
        frame.set_local(var_name, value)
    
    # Arithmetic implementations
    def _add(self, frame: ExecutionFrame) -> None:
        """Add two values"""
        b = frame.pop()
        a = frame.pop()
        result = a.data + b.data
        frame.push(Value(result, a.type_))
    
    def _sub(self, frame: ExecutionFrame) -> None:
        """Subtract two values"""
        b = frame.pop()
        a = frame.pop()
        result = a.data - b.data
        frame.push(Value(result, a.type_))
    
    def _mul(self, frame: ExecutionFrame) -> None:
        """Multiply two values"""
        b = frame.pop()
        a = frame.pop()
        result = a.data * b.data
        frame.push(Value(result, a.type_))
    
    def _div(self, frame: ExecutionFrame) -> None:
        """Divide two values"""
        b = frame.pop()
        a = frame.pop()
        result = a.data // b.data if a.type_ in [ValueType.INT32, ValueType.INT64] else a.data / b.data
        frame.push(Value(result, a.type_))
    
    def _rem(self, frame: ExecutionFrame) -> None:
        """Remainder operation"""
        b = frame.pop()
        a = frame.pop()
        result = a.data % b.data
        frame.push(Value(result, a.type_))
    
    # Comparison implementations
    def _ceq(self, frame: ExecutionFrame) -> None:
        """Compare equal"""
        b = frame.pop()
        a = frame.pop()
        result = a.data == b.data
        frame.push(Value(result, ValueType.BOOL))
    
    def _cgt(self, frame: ExecutionFrame) -> None:
        """Compare greater than"""
        b = frame.pop()
        a = frame.pop()
        result = a.data > b.data
        frame.push(Value(result, ValueType.BOOL))
    
    def _clt(self, frame: ExecutionFrame) -> None:
        """Compare less than"""
        b = frame.pop()
        a = frame.pop()
        result = a.data < b.data
        frame.push(Value(result, ValueType.BOOL))
    
    def _cge(self, frame: ExecutionFrame) -> None:
        """Compare greater than or equal"""
        b = frame.pop()
        a = frame.pop()
        result = a.data >= b.data
        frame.push(Value(result, ValueType.BOOL))
    
    def _cle(self, frame: ExecutionFrame) -> None:
        """Compare less than or equal"""
        b = frame.pop()
        a = frame.pop()
        result = a.data <= b.data
        frame.push(Value(result, ValueType.BOOL))
    
    # Stack manipulation implementations
    def _dup(self, frame: ExecutionFrame) -> None:
        """Duplicate top of stack"""
        value = frame.peek()
        frame.push(value)
    
    def _pop(self, frame: ExecutionFrame) -> None:
        """Pop value from stack"""
        frame.pop()
    
    # Call implementations
    def _call(self, frame: ExecutionFrame, instruction: str) -> None:
        """Handle built-in method calls"""
        if 'System.Console.WriteLine' in instruction:
            if frame.stack:
                value = frame.pop()
                output = str(value.data)
                self.console_output.append(output)
                print(output)
        elif 'System.Console.Write' in instruction:
            if frame.stack:
                value = frame.pop()
                output = str(value.data)
                self.console_output.append(output)
                sys.stdout.write(output)
                sys.stdout.flush()
    
    # Return implementation
    def _ret(self, frame: ExecutionFrame) -> None:
        """Return from method"""
        if frame.stack:
            frame.return_value = frame.pop()
    
    def get_output(self) -> str:
        """Get captured console output"""
        return '\n'.join(self.console_output)
