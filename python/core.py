"""
ObjectIR Text Runtime - Core runtime engine
"""

from typing import Any, Dict, List, Optional
from oir_parser import ObjectIRParser
from oir_frame import ExecutionFrame
from oir_executor import InstructionExecutor


class ObjectIRTextRuntime:
    """
    Basic ObjectIR Text Runtime
    Parses and executes ObjectIR text format programs
    """
    
    def __init__(self):
        self.parser = ObjectIRParser()
        self.executor = InstructionExecutor()
        self.execution_stack: List[ExecutionFrame] = []
    
    def parse_file(self, filepath: str) -> None:
        """Parse an ObjectIR text file"""
        self.parser.parse_file(filepath)
    
    def parse(self, content: str) -> None:
        """Parse ObjectIR text content"""
        self.parser.parse(content)
    
    def execute_method(self, method_name: str, *args) -> Any:
        """Execute a method by name"""
        if method_name not in self.parser.methods:
            raise RuntimeError(f"Method not found: {method_name}")
        
        frame = ExecutionFrame(method_name)
        self.execution_stack.append(frame)
        
        try:
            instructions = self.parser.methods[method_name]
            for instruction in instructions:
                self.executor.execute_instruction(frame, instruction)
            
            result = frame.return_value
        finally:
            self.execution_stack.pop()
        
        return result
    
    def get_output(self) -> str:
        """Get captured console output"""
        return self.executor.get_output()
    
    @property
    def modules(self) -> Dict:
        """Get parsed modules"""
        return self.parser.modules
    
    @property
    def classes(self) -> Dict:
        """Get parsed classes"""
        return self.parser.classes
    
    @property
    def methods(self) -> Dict:
        """Get parsed methods"""
        return self.parser.methods
