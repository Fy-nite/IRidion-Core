import sys, os, inspect, importlib, types, builtins, pkgutil, importlib.util, importlib.machinery, importlib.abc, importlib.metadata


def dynamic_import(module_name: str) -> types.ModuleType:
    """
    Dynamically imports a module by name.

    Args:
        module_name (str): The name of the module to import.

    Returns:
        types.ModuleType: The imported module.
    """
    try:
        module = importlib.import_module(module_name)
        return module
    except ImportError as e:
        print(f"Error importing module {module_name}: {e}")
        raise