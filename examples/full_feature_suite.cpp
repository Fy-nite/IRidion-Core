#include "../include/objectir_runtime.hpp"
#include "../include/ir_text_parser.hpp"
#include <iostream>
#include <string>

using namespace ObjectIR;

// A comprehensive ObjectIR example demonstrating all major runtime features:
// - Arithmetic operations (add, sub, mul, div)
// - Branching (br, brtrue, brfalse)
// - Comparisons (beq, bne, bgt, blt, bge, ble)
// - Function calls and returns
// - Field access and manipulation
// - Type reflection and introspection
// - Module serialization/deserialization

const std::string IR_CODE = R"(
module FullFeatureSuite version 1.0.0

// Utility class for mathematical operations and tests
class MathTest {
    private field testsPassed: int32
    private field testsFailed: int32
    
    constructor() {
        ldarg this
        ldc.i4 0
        stfld MathTest.testsPassed
        
        ldarg this
        ldc.i4 0
        stfld MathTest.testsFailed
        ret
    }
    
    // Test arithmetic: Add
    method TestAdd() -> int32 {
        local a: int32
        local b: int32
        local result: int32
        
        ldc.i4 10
        stloc a
        
        ldc.i4 5
        stloc b
        
        ldloc a
        ldloc b
        add
        stloc result
        
        // result should be 15, increment passed
        ldloc result
        ldc.i4 15
        beq.s pass_add
        
        // Test failed
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_add:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldloc result
        ret
    }
    
    // Test arithmetic: Subtract
    method TestSubtract() -> int32 {
        local a: int32
        local b: int32
        local result: int32
        
        ldc.i4 20
        stloc a
        
        ldc.i4 8
        stloc b
        
        ldloc a
        ldloc b
        sub
        stloc result
        
        // result should be 12
        ldloc result
        ldc.i4 12
        beq.s pass_sub
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_sub:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldloc result
        ret
    }
    
    // Test arithmetic: Multiply
    method TestMultiply() -> int32 {
        local a: int32
        local b: int32
        local result: int32
        
        ldc.i4 7
        stloc a
        
        ldc.i4 6
        stloc b
        
        ldloc a
        ldloc b
        mul
        stloc result
        
        // result should be 42
        ldloc result
        ldc.i4 42
        beq.s pass_mul
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_mul:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldloc result
        ret
    }
    
    // Test arithmetic: Divide
    method TestDivide() -> int32 {
        local a: int32
        local b: int32
        local result: int32
        
        ldc.i4 100
        stloc a
        
        ldc.i4 5
        stloc b
        
        ldloc a
        ldloc b
        div
        stloc result
        
        // result should be 20
        ldloc result
        ldc.i4 20
        beq.s pass_div
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_div:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldloc result
        ret
    }
    
    // Test branching: Unconditional branch
    method TestUnconditionalBranch() -> int32 {
        local value: int32
        
        ldc.i4 999
        stloc value
        
        br skip_block
        
        // This should be skipped
        ldc.i4 888
        stloc value
        
        skip_block:
        // value should still be 999
        ldloc value
        ldc.i4 999
        beq.s pass_br
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_br:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldloc value
        ret
    }
    
    // Test comparison: Equal
    method TestEqual() -> int32 {
        local a: int32
        local b: int32
        
        ldc.i4 42
        stloc a
        
        ldc.i4 42
        stloc b
        
        ldloc a
        ldloc b
        beq.s pass_eq
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_eq:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test comparison: Not Equal
    method TestNotEqual() -> int32 {
        local a: int32
        local b: int32
        
        ldc.i4 10
        stloc a
        
        ldc.i4 20
        stloc b
        
        ldloc a
        ldloc b
        bne.s pass_ne
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_ne:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test comparison: Greater Than
    method TestGreaterThan() -> int32 {
        local a: int32
        local b: int32
        
        ldc.i4 50
        stloc a
        
        ldc.i4 30
        stloc b
        
        ldloc a
        ldloc b
        bgt.s pass_gt
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_gt:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test comparison: Less Than
    method TestLessThan() -> int32 {
        local a: int32
        local b: int32
        
        ldc.i4 15
        stloc a
        
        ldc.i4 25
        stloc b
        
        ldloc a
        ldloc b
        blt.s pass_lt
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_lt:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test comparison: Greater Than or Equal
    method TestGreaterEqual() -> int32 {
        local a: int32
        local b: int32
        
        ldc.i4 50
        stloc a
        
        ldc.i4 50
        stloc b
        
        ldloc a
        ldloc b
        bge.s pass_ge
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_ge:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test comparison: Less Than or Equal
    method TestLessEqual() -> int32 {
        local a: int32
        local b: int32
        
        ldc.i4 30
        stloc a
        
        ldc.i4 50
        stloc b
        
        ldloc a
        ldloc b
        ble.s pass_le
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_le:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test conditional branching: True condition
    method TestConditionalTrue() -> int32 {
        local cond: int32
        local result: int32
        
        ldc.i4 1
        stloc cond
        
        ldloc cond
        brtrue.s true_branch
        
        ldc.i4 0
        stloc result
        br end_cond
        
        true_branch:
        ldc.i4 1
        stloc result
        
        end_cond:
        ldloc result
        ldc.i4 1
        beq.s pass_cond_true
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_cond_true:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 1
        ret
    }
    
    // Test conditional branching: False condition
    method TestConditionalFalse() -> int32 {
        local cond: int32
        local result: int32
        
        ldc.i4 0
        stloc cond
        
        ldloc cond
        brfalse.s false_branch
        
        ldc.i4 1
        stloc result
        br end_cond2
        
        false_branch:
        ldc.i4 0
        stloc result
        
        end_cond2:
        ldloc result
        ldc.i4 0
        beq.s pass_cond_false
        
        ldarg this
        ldfld MathTest.testsFailed
        ldc.i4 1
        add
        stfld MathTest.testsFailed
        ldc.i4 0
        ret
        
        pass_cond_false:
        ldarg this
        ldfld MathTest.testsPassed
        ldc.i4 1
        add
        stfld MathTest.testsPassed
        ldc.i4 0
        ret
    }
    
    // Get total tests passed
    method GetPassedCount() -> int32 {
        ldarg this
        ldfld MathTest.testsPassed
        ret
    }
    
    // Get total tests failed
    method GetFailedCount() -> int32 {
        ldarg this
        ldfld MathTest.testsFailed
        ret
    }
}
)";

int main()
{
    std::cout << "=== ObjectIR Full Feature Suite Test ===\n\n";

    try {
        // Parse the IR code to a virtual machine
        std::cout << "Parsing ObjectIR code...\n";
        auto vm = IRTextParser::ParseToVirtualMachine(IR_CODE);
        std::cout << "✓ Parsing successful\n\n";

        // Get the MathTest class
        auto mathTestClass = vm->GetClass("MathTest");
        if (!mathTestClass) {
            std::cerr << "✗ Could not find MathTest class\n";
            return 1;
        }

        // Create an instance
        std::cout << "Creating MathTest instance...\n";
        auto testInstance = vm->CreateObject(mathTestClass);
        std::cout << "✓ Instance created\n\n";

        // Run all tests
        std::cout << "Running tests:\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        std::vector<std::string> testNames = {
            "TestAdd",
            "TestSubtract",
            "TestMultiply",
            "TestDivide",
            "TestUnconditionalBranch",
            "TestEqual",
            "TestNotEqual",
            "TestGreaterThan",
            "TestLessThan",
            "TestGreaterEqual",
            "TestLessEqual",
            "TestConditionalTrue",
            "TestConditionalFalse"
        };

        for (const auto& testName : testNames) {
            try {
                auto result = vm->InvokeMethod(testInstance, testName, {});
                std::cout << "  ✓ " << testName << " (result: " << result.AsInt32() << ")\n";
            } catch (const std::exception& e) {
                std::cout << "  ✗ " << testName << " - Error: " << e.what() << "\n";
            }
        }

        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

        // Get test results
        auto passedCount = vm->InvokeMethod(testInstance, "GetPassedCount", {});
        auto failedCount = vm->InvokeMethod(testInstance, "GetFailedCount", {});

        std::cout << "Test Results Summary:\n";
        std::cout << "  Passed: " << passedCount.AsInt32() << "\n";
        std::cout << "  Failed: " << failedCount.AsInt32() << "\n";
        std::cout << "  Total:  " << (passedCount.AsInt32() + failedCount.AsInt32()) << "\n\n";

        if (failedCount.AsInt32() == 0) {
            std::cout << "✓ All tests passed!\n";
        } else {
            std::cout << "✗ Some tests failed.\n";
            return 1;
        }

        // Test reflection/serialization
        std::cout << "\nTesting Module Reflection:\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        // Export metadata for introspection
        auto metadata = vm->ExportMetadata(false);
        std::cout << "  Class Name: " << mathTestClass->GetName() << "\n";
        std::cout << "  Method Count: " << mathTestClass->GetAllMethods().size() << "\n";
        std::cout << "  Field Count: " << mathTestClass->GetAllFields().size() << "\n";

        std::cout << "\n  Methods:\n";
        for (const auto& method : mathTestClass->GetAllMethods()) {
            std::cout << "    - " << method->GetName() << "\n";
        }

        std::cout << "\n  Fields:\n";
        for (const auto& field : mathTestClass->GetAllFields()) {
            std::cout << "    - " << field->GetName() << "\n";
        }

        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        std::cout << "\n✓ Full Feature Suite completed successfully!\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n✗ Error: " << e.what() << "\n";
        return 1;
    }
}
