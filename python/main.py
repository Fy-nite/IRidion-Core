import os, sys
from Dynamics import dynamic_import
from core import ObjectIRTextRuntime
# from Utilities import helper_function
# from Config import Config_Loader

class Main():
    def __init__(self):
        print("Main class initialized.")
        self.runtime = ObjectIRTextRuntime()
    
    def InitVM(self):
        """Initialize and return the ObjectIR Text Runtime"""
        return self.runtime
    
    def run_file(self, filepath: str):
        """Run an ObjectIR text file"""
        print(f"Loading ObjectIR file: {filepath}")
        self.runtime.parse_file(filepath)
        
        # Execute Main method if it exists
        if 'Main' in self.runtime.methods:
            print("Executing Main method...")
            result = self.runtime.execute_method('Main')
            print(f"Execution completed.")
            return result
        else:
            print("No Main method found in module.")
            return None

if __name__ == "__main__":
    main_instance = Main()
    vm = main_instance.InitVM()
    # yeahhhhh, got the vm now.
    
    # Run the hello world example
    try:
        main_instance.run_file("helloworld.oir")
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
