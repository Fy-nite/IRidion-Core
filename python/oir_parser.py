"""
ObjectIR text format parser
"""

import re
from typing import List, Dict, Any


class ObjectIRParser:
    """Parses ObjectIR text format files"""
    
    def __init__(self):
        self.modules: Dict[str, Dict[str, Any]] = {}
        self.current_module: str = None
        self.classes: Dict[str, Dict[str, Any]] = {}
        self.methods: Dict[str, List[str]] = {}  # method_name -> instructions
    
    def parse_file(self, filepath: str) -> None:
        """Parse an ObjectIR text file"""
        with open(filepath, 'r') as f:
            content = f.read()
        self.parse(content)
    
    def parse(self, content: str) -> None:
        """Parse ObjectIR text format"""
        lines = content.strip().split('\n')
        i = 0
        
        while i < len(lines):
            line = lines[i].strip()
            i += 1
            
            # Skip empty lines and comments
            if not line or line.startswith('//'):
                continue
            
            # Module declaration
            if line.startswith('module '):
                self.current_module = line.replace('module ', '').strip()
                self.modules[self.current_module] = {'classes': {}}
                continue
            
            # Class declaration
            if line.startswith('class '):
                match = re.match(r'class\s+(\w+)\s*\{', line)
                if match:
                    class_name = match.group(1)
                    self.classes[class_name] = {'methods': {}}
                    if self.current_module:
                        self.modules[self.current_module]['classes'][class_name] = {'methods': {}}
                continue
            
            # Method declaration
            if line.startswith('method '):
                match = re.match(r'method\s+(\w+)\s*\(\s*\)\s*->\s*(\w+(?:\.\w+)*)\s*\{', line)
                if match:
                    method_name = match.group(1)
                    return_type = match.group(2)
                    self.methods[method_name] = []
                    
                    # Collect method instructions until closing brace
                    i = self._parse_method_body(lines, i, method_name)
    
    def _parse_method_body(self, lines: List[str], start_idx: int, method_name: str) -> int:
        """Parse method body and collect instructions"""
        instructions = []
        brace_count = 1
        
        for i in range(start_idx, len(lines)):
            line = lines[i].strip()
            
            if not line or line.startswith('//'):
                continue
            
            # Count braces
            brace_count += line.count('{') - line.count('}')
            
            if brace_count == 0:
                self.methods[method_name] = instructions
                return i + 1
            
            # Parse instruction
            if brace_count > 0 and line and not line.startswith('}'):
                instructions.append(line)
        
        return len(lines)
