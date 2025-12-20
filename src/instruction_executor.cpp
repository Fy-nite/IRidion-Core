#include "instruction_executor.hpp"
#include "objectir_type_names.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace ObjectIR {

namespace {

TypeReference ParseTypeReference(VirtualMachine* vm, const std::string& typeStr) {
    const auto normalized = TypeNames::NormalizeTypeName(typeStr);

    if (normalized == "int32") return TypeReference::Int32();
    if (normalized == "int64") return TypeReference::Int64();
    if (normalized == "float32") return TypeReference::Float32();
    if (normalized == "float64") return TypeReference::Float64();
    if (normalized == "bool") return TypeReference::Bool();
    if (normalized == "string") return TypeReference::String();
    if (normalized == "void") return TypeReference::Void();
    if (normalized == "uint8") return TypeReference::UInt8();
    if (normalized == "object") return TypeReference::Object();

    if (vm) {
        try {
            if (vm->HasClass(normalized)) {
                return TypeReference::Object(vm->GetClass(normalized));
            }
        } catch (...) {
            // Ignore and fall back.
        }
    }

    return TypeReference::Object();
}

struct BreakSignal : public std::exception {
    const char* what() const noexcept override { return "break"; }
};

struct ContinueSignal : public std::exception {
    const char* what() const noexcept override { return "continue"; }
};

std::string ToLowerInvariant(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

Instruction::ConditionData ParseConditionNode(const json& node);
std::vector<Instruction> ParseInstructionArray(const json& node);

bool EqualsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return ToLowerInvariant(lhs) == ToLowerInvariant(rhs);
}

Value CreateConstantValue(const Instruction& instr) {
    if (instr.constantIsNull) {
        return Value();
    }

    if (!instr.constantType.empty()) {
        auto typeLower = ToLowerInvariant(instr.constantType);

        if (typeLower == "system.string" || typeLower == "string") {
            return Value(instr.constantRawValue);
        }

        if (typeLower == "system.boolean" || typeLower == "bool" || typeLower == "boolean") {
            bool boolValue = instr.constantBool;
            if (instr.constantRawValue.empty()) {
                return Value(boolValue);
            }
            auto valueLower = ToLowerInvariant(instr.constantRawValue);
            if (valueLower == "true" || valueLower == "1") {
                boolValue = true;
            } else if (valueLower == "false" || valueLower == "0") {
                boolValue = false;
            }
            return Value(boolValue);
        }

        if (typeLower == "system.int32" || typeLower == "int32" || typeLower == "int") {
            return Value(static_cast<int32_t>(std::stoi(instr.constantRawValue)));
        }

        if (typeLower == "system.int64" || typeLower == "int64" || typeLower == "long") {
            return Value(static_cast<int64_t>(std::stoll(instr.constantRawValue)));
        }

        if (typeLower == "system.single" || typeLower == "single" || typeLower == "float" || typeLower == "float32") {
            return Value(static_cast<float>(std::stof(instr.constantRawValue)));
        }

        if (typeLower == "system.double" || typeLower == "double" || typeLower == "float64") {
            return Value(static_cast<double>(std::stod(instr.constantRawValue)));
        }
    }

    if (instr.constantBool) {
        return Value(instr.constantBool);
    }

    return Value(instr.constantRawValue);
}

std::string ValueToString(const Value& value) {
    if (value.IsNull()) {
        return "null";
    }
    if (value.IsString()) {
        return value.AsString();
    }
    if (value.IsInt32()) {
        return std::to_string(value.AsInt32());
    }
    if (value.IsInt64()) {
        return std::to_string(value.AsInt64());
    }
    if (value.IsFloat32()) {
        return std::to_string(value.AsFloat32());
    }
    if (value.IsFloat64()) {
        return std::to_string(value.AsFloat64());
    }
    if (value.IsBool()) {
        return value.AsBool() ? "true" : "false";
    }
    if (value.IsObject()) {
        return "<object>";
    }
    return "";
}

Instruction::ConditionData ParseConditionNode(const json& node) {
    Instruction::ConditionData data;

    if (!node.is_object()) {
        throw std::runtime_error("Condition node must be an object");
    }

    auto kindIt = node.find("kind");
    if (kindIt == node.end() || !kindIt->is_string()) {
        throw std::runtime_error("Condition kind missing");
    }

    auto kindStr = kindIt->get<std::string>();
    if (kindStr == "stack") {
        data.kind = ConditionKind::Stack;
    } else if (kindStr == "binary") {
        data.kind = ConditionKind::Binary;
    } else if (kindStr == "expression") {
        data.kind = ConditionKind::Expression;
    } else {
        throw std::runtime_error("Unsupported condition kind: " + kindStr);
    }

    if (data.kind == ConditionKind::Binary) {
        auto opIt = node.find("operation");
        if (opIt == node.end() || !opIt->is_string()) {
            throw std::runtime_error("Binary condition missing operation");
        }
        data.comparisonOp = InstructionExecutor::ParseOpCode(opIt->get<std::string>());
    }

    if (data.kind == ConditionKind::Expression) {
        auto exprIt = node.find("expression");
        if (exprIt != node.end()) {
            data.expressionInstructions.push_back(InstructionExecutor::ParseJsonInstruction(*exprIt));
        }
    }

    return data;
}

std::vector<Instruction> ParseInstructionArray(const json& node) {
    std::vector<Instruction> result;
    if (!node.is_array()) {
        return result;
    }

    result.reserve(node.size());
    for (const auto& element : node) {
        result.push_back(InstructionExecutor::ParseJsonInstruction(element));
    }
    return result;
}

} // namespace

OpCode InstructionExecutor::ParseOpCode(const std::string& opStr) {
    const auto op = ToLowerInvariant(opStr);

    if (op == "nop") return OpCode::Nop;
    if (op == "dup") return OpCode::Dup;
    if (op == "pop") return OpCode::Pop;
    
    if (op == "ldarg") return OpCode::LdArg;
    if (op == "ldloc") return OpCode::LdLoc;
    if (op == "ldfld") return OpCode::LdFld;
    if (op == "ldcon" || op == "ldc") return OpCode::LdCon;
    if (op == "ldstr") return OpCode::LdStr;
    if (op == "ldi4" || op == "ldi32" || op == "ldc.i4") return OpCode::LdI4;
    if (op == "ldi8" || op == "ldi64" || op == "ldc.i8") return OpCode::LdI8;
    if (op == "ldr4" || op == "ldc.r4") return OpCode::LdR4;
    if (op == "ldr8" || op == "ldc.r8") return OpCode::LdR8;
    if (op == "ldtrue") return OpCode::LdTrue;
    if (op == "ldfalse") return OpCode::LdFalse;
    if (op == "ldnull") return OpCode::LdNull;
    
    if (op == "stloc") return OpCode::StLoc;
    if (op == "stfld") return OpCode::StFld;
    if (op == "starg") return OpCode::StArg;
    
    if (op == "add") return OpCode::Add;
    if (op == "sub") return OpCode::Sub;
    if (op == "mul") return OpCode::Mul;
    if (op == "div") return OpCode::Div;
    if (op == "rem") return OpCode::Rem;
    if (op == "neg") return OpCode::Neg;
    
    if (op == "ceq") return OpCode::Ceq;
    if (op == "cne") return OpCode::Cne;
    if (op == "clt") return OpCode::Clt;
    if (op == "cle") return OpCode::Cle;
    if (op == "cgt") return OpCode::Cgt;
    if (op == "cge") return OpCode::Cge;
    
    if (op == "ret") return OpCode::Ret;
    if (op == "br") return OpCode::Br;
    if (op == "brtrue") return OpCode::BrTrue;
    if (op == "brfalse") return OpCode::BrFalse;
    if (op == "beq" || op == "beq.s") return OpCode::Beq;
    if (op == "bne" || op == "bne.un" || op == "bne.s") return OpCode::Bne;
    if (op == "bgt" || op == "bgt.s" || op == "bgt.un") return OpCode::Bgt;
    if (op == "blt" || op == "blt.s" || op == "blt.un") return OpCode::Blt;
    if (op == "bge" || op == "bge.s" || op == "bge.un") return OpCode::Bge;
    if (op == "ble" || op == "ble.s" || op == "ble.un") return OpCode::Ble;
    
    if (op == "if") return OpCode::If;
    
    if (op == "newobj") return OpCode::NewObj;
    if (op == "call") return OpCode::Call;
    if (op == "callvirt") return OpCode::CallVirt;
    if (op == "castclass") return OpCode::CastClass;
    if (op == "isinst") return OpCode::IsInst;
    
    if (op == "newarr") return OpCode::NewArr;
    if (op == "ldelem") return OpCode::LdElem;
    if (op == "stelem") return OpCode::StElem;
    if (op == "ldlen") return OpCode::LdLen;
    
    if (op == "break") return OpCode::Break;
    if (op == "continue") return OpCode::Continue;
    if (op == "throw") return OpCode::Throw;
    if (op == "while") return OpCode::While;
    
    throw std::runtime_error("Unknown opcode: " + opStr);
}

Instruction InstructionExecutor::ParseJsonInstruction(const json& instrJson) {
    Instruction instr;

    std::string opCodeStr = instrJson.value("opCode", "");
    instr.opCode = ParseOpCode(opCodeStr);

    if (!instrJson.contains("operand") || instrJson["operand"].is_null()) {
        return instr;
    }

    const auto& operand = instrJson["operand"];

    switch (instr.opCode) {
        case OpCode::LdArg:
        case OpCode::StArg:
            if (operand.contains("argumentName")) {
                instr.identifier = operand.value("argumentName", "");
            }
            break;

        case OpCode::LdLoc:
        case OpCode::StLoc:
            if (operand.contains("localName")) {
                instr.identifier = operand.value("localName", "");
            }
            break;

        case OpCode::LdFld:
        case OpCode::StFld:
            if (operand.contains("field")) {
                const auto& fieldJson = operand["field"];
                FieldTarget ft;
                
                // Handle both string (from IR text parser) and object (from JSON) formats
                if (fieldJson.is_string()) {
                    // Simple string format from IR text parser: "ClassName.fieldName"
                    std::string fieldStr = fieldJson.get<std::string>();
                    size_t dotPos = fieldStr.find_last_of('.');
                    if (dotPos != std::string::npos) {
                        ft.declaringType = fieldStr.substr(0, dotPos);
                        ft.name = fieldStr.substr(dotPos + 1);
                    } else {
                        ft.name = fieldStr;
                    }
                } else {
                    // Object format: {"declaringType": "...", "name": "...", "type": "..."}
                    ft.declaringType = fieldJson.value("declaringType", "");
                    ft.name = fieldJson.value("name", "");
                    ft.type = fieldJson.value("type", "");
                }
                
                instr.fieldTarget = std::move(ft);
                // Also populate operandString with the field name as a lightweight fallback
                instr.operandString = instr.fieldTarget->name;
                std::cerr << "[ParseJsonInstruction] op=" << opCodeStr << " field present: yes, name='" << instr.fieldTarget->name << "'" << std::endl;
            } else {
                std::cerr << "[ParseJsonInstruction] op=" << opCodeStr << " field present: no" << std::endl;
            }
            
            break;

        case OpCode::LdCon:
        case OpCode::LdStr:
            instr.hasConstant = true;
            if (operand.contains("type")) {
                instr.constantType = operand.value("type", "");
            }
            if (operand.contains("value")) {
                const auto& valueNode = operand["value"];
                if (valueNode.is_string()) {
                    instr.constantRawValue = valueNode.get<std::string>();
                } else if (valueNode.is_number_integer()) {
                    instr.constantRawValue = std::to_string(valueNode.get<int64_t>());
                } else if (valueNode.is_number_float()) {
                    instr.constantRawValue = std::to_string(valueNode.get<double>());
                } else if (valueNode.is_boolean()) {
                    instr.constantBool = valueNode.get<bool>();
                    instr.constantRawValue = instr.constantBool ? "true" : "false";
                } else if (valueNode.is_null()) {
                    instr.constantIsNull = true;
                }
            } else {
                instr.constantIsNull = true;
            }
            break;

        case OpCode::Call:
        case OpCode::CallVirt:
            if (operand.contains("method")) {
                const auto& methodJson = operand["method"];
                CallTarget target;
                target.declaringType = methodJson.value("declaringType", "");
                target.name = methodJson.value("name", "");
                target.returnType = TypeNames::NormalizeTypeName(methodJson.value("returnType", "void"));
                if (methodJson.contains("parameterTypes") && methodJson["parameterTypes"].is_array()) {
                    target.hasParameterTypes = true;
                    for (const auto& param : methodJson["parameterTypes"]) {
                        target.parameterTypes.push_back(TypeNames::NormalizeTypeName(param.get<std::string>()));
                    }
                }
                instr.callTarget = std::move(target);
            }
            break;

        case OpCode::NewObj:
            if (operand.contains("type")) {
                instr.operandString = operand.value("type", "");
            }
            break;

        case OpCode::NewArr:
        case OpCode::CastClass:
        case OpCode::IsInst:
            if (operand.is_object()) {
                if (operand.contains("type")) {
                    instr.operandString = operand.value("type", "");
                } else if (operand.contains("arguments") && operand["arguments"].is_array() && !operand["arguments"].empty()) {
                    // Generic fallback used by the IR text parser for unknown instructions.
                    instr.operandString = operand["arguments"][0].get<std::string>();
                }
            } else if (operand.is_string()) {
                instr.operandString = operand.get<std::string>();
            }
            break;

        case OpCode::Br:
        case OpCode::BrTrue:
        case OpCode::BrFalse:
        case OpCode::Beq:
        case OpCode::Bne:
        case OpCode::Bgt:
        case OpCode::Blt:
        case OpCode::Bge:
        case OpCode::Ble: {
            if (operand.is_object()) {
                if (operand.contains("target")) {
                    const auto& targetNode = operand["target"];
                    if (targetNode.is_number_integer()) {
                        instr.operandInt = targetNode.get<int32_t>();
                        instr.hasOperandInt = true;
                    } else if (targetNode.is_string()) {
                        instr.operandString = targetNode.get<std::string>();
                        instr.hasOperandInt = false;
                    } else {
                        instr.operandInt = 0;
                        instr.hasOperandInt = true;
                    }
                } else if (operand.contains("offset")) {
                    instr.operandInt = operand.value("offset", 0);
                    instr.hasOperandInt = true;
                }
            } else if (operand.is_number_integer()) {
                instr.operandInt = operand.get<int32_t>();
                instr.hasOperandInt = true;
            } else if (operand.is_string()) {
                instr.operandString = operand.get<std::string>();
            }
            break;
        }

        case OpCode::While: {
            if (!operand.is_object()) {
                throw std::runtime_error("While instruction operand must be object");
            }
            Instruction::WhileData data;
            if (operand.contains("condition")) {
                data.condition = ParseConditionNode(operand.at("condition"));
            }
            if (operand.contains("body")) {
                data.body = ParseInstructionArray(operand.at("body"));
            }
            instr.whileData = std::move(data);
            break;
        }

        case OpCode::If: {
            if (!operand.is_object()) {
                throw std::runtime_error("If instruction operand must be object");
            }
            Instruction::IfData data;
            if (operand.contains("thenBlock")) {
                data.thenBlock = ParseInstructionArray(operand.at("thenBlock"));
            }
            if (operand.contains("elseBlock")) {
                data.elseBlock = ParseInstructionArray(operand.at("elseBlock"));
            }
            instr.ifData = std::move(data);
            break;
        }

        default:
            if (operand.is_string()) {
                instr.operandString = operand.get<std::string>();
            } else if (operand.is_number_integer()) {
                instr.operandInt = operand.get<int32_t>();
                instr.hasOperandInt = true;
            } else if (operand.is_number_float()) {
                instr.operandDouble = operand.get<double>();
                instr.hasOperandInt = true;
            }
            break;
    }

    return instr;
}

void InstructionExecutor::Execute(
    const Instruction& instr,
    ExecutionContext* context,
    VirtualMachine* vm
) {
    if (!context) {
        throw std::runtime_error("Execution context is null");
    }
    
    switch (instr.opCode) {
        case OpCode::Nop:
            // No operation
            break;
            
        case OpCode::Dup: {
            auto val = context->PeekStack();
            context->PushStack(val);
            break;
        }
        
        case OpCode::Pop: {
            (void)context->PopStack();
            break;
        }

        case OpCode::LdArg: {
            context->PushStack(context->GetArgument(instr.identifier));
            break;
        }

        case OpCode::StArg: {
            auto value = context->PopStack();
            context->SetArgument(instr.identifier, value);
            break;
        }

        case OpCode::LdLoc: {
            // std::cerr << "[" << context->GetMethod()->GetName() << "] Executing LdLoc '" << instr.identifier << "'" << std::endl;
            context->PushStack(context->GetLocal(instr.identifier));
            break;
        }

        case OpCode::LdFld: {
            const std::string fieldName = instr.fieldTarget.has_value() ? instr.fieldTarget->name : instr.operandString;
            std::cerr << "[" << context->GetMethod()->GetName() << "] LdFld operand present: " << (instr.fieldTarget.has_value() ? "yes" : "no") << " name='" << fieldName << "'" << std::endl;
            if (fieldName.empty()) {
                throw std::runtime_error("LdFld instruction missing field operand");
            }
            ObjectRef instance;
            try {
                auto instanceValue = context->PopStack();
                if (instanceValue.IsObject()) {
                    instance = instanceValue.AsObject();
                } else {
                    instance = context->GetThis();
                }
            } catch (...) {
                instance = context->GetThis();
            }
            if (!instance) {
                throw std::runtime_error("LdFld requires an object instance on the stack or a valid 'this' in the context");
            }
            Value val = instance->GetField(fieldName);
            context->PushStack(val);
            break;
        }

        case OpCode::StLoc: {
            auto value = context->PopStack();
            // std::cerr << "[" << context->GetMethod()->GetName() << "] Executing StLoc '" << instr.identifier << "'" << std::endl;
            context->SetLocal(instr.identifier, value);
            break;
        }

        case OpCode::StFld: {
            const std::string fieldName = instr.fieldTarget.has_value() ? instr.fieldTarget->name : instr.operandString;
            std::cerr << "[" << context->GetMethod()->GetName() << "] StFld operand present: " << (instr.fieldTarget.has_value() ? "yes" : "no") << " name='" << fieldName << "'" << std::endl;
            if (fieldName.empty()) {
                throw std::runtime_error("StFld instruction missing field operand");
            }
            auto value = context->PopStack();
            ObjectRef instance;
            try {
                auto instanceValue = context->PopStack();
                if (instanceValue.IsObject()) {
                    instance = instanceValue.AsObject();
                } else {
                    // Fallback to implicit 'this' if an instance was not provided
                    instance = context->GetThis();
                }
            } catch (...) {
                // If stack underflow while popping instance, fallback to 'this'
                instance = context->GetThis();
            }
            if (!instance) {
                throw std::runtime_error("StFld requires an object instance on the stack or a valid 'this' in the context");
            }
            instance->SetField(fieldName, value);
            break;
        }

        case OpCode::LdCon:
        case OpCode::LdStr: {
            context->PushStack(CreateConstantValue(instr));
            break;
        }
        
        case OpCode::LdI4:
            context->PushStack(Value(instr.operandInt));
            break;
            
        case OpCode::LdI8:
            context->PushStack(Value(static_cast<int64_t>(instr.operandInt)));
            break;
            
        case OpCode::LdR4:
            context->PushStack(Value(static_cast<float>(instr.operandDouble)));
            break;
            
        case OpCode::LdR8:
            context->PushStack(Value(instr.operandDouble));
            break;
            
        case OpCode::LdTrue:
            context->PushStack(Value(true));
            break;
            
        case OpCode::LdFalse:
            context->PushStack(Value(false));
            break;
            
        case OpCode::LdNull:
            context->PushStack(Value());
            break;
            
        case OpCode::Add:
            ExecuteAdd(context);
            break;
            
        case OpCode::Sub:
            ExecuteSub(context);
            break;
            
        case OpCode::Mul:
            ExecuteMul(context);
            break;
            
        case OpCode::Div:
            ExecuteDiv(context);
            break;
            
        case OpCode::Rem:
            ExecuteRem(context);
            break;
            
        case OpCode::Neg:
            ExecuteNeg(context);
            break;
        
        case OpCode::Ceq:
            ExecuteCeq(context);
            break;
            
        case OpCode::Cne:
            ExecuteCne(context);
            break;
            
        case OpCode::Clt:
            ExecuteClt(context);
            break;
            
        case OpCode::Cle:
            ExecuteCle(context);
            break;
            
        case OpCode::Cgt:
            ExecuteCgt(context);
            break;
            
        case OpCode::Cge:
            ExecuteCge(context);
            break;
        
        case OpCode::Ret:
            // Return handled at higher level
            break;

        case OpCode::Br:
        case OpCode::BrTrue:
        case OpCode::BrFalse:
        case OpCode::Beq:
        case OpCode::Bne:
        case OpCode::Bgt:
        case OpCode::Blt:
        case OpCode::Bge:
        case OpCode::Ble:
            throw std::runtime_error("Branch opcodes must be handled by the instruction dispatcher");

        case OpCode::NewObj: {
            if (instr.operandString.empty()) {
                throw std::runtime_error("NewObj instruction missing type operand");
            }
            ObjectRef obj = vm->CreateObject(instr.operandString);
            context->PushStack(Value(obj));
            break;
        }

        case OpCode::NewArr: {
            const std::string typeName = !instr.operandString.empty() ? instr.operandString : instr.identifier;
            if (typeName.empty()) {
                throw std::runtime_error("NewArr instruction missing element type operand");
            }
            const auto lengthValue = context->PopStack();
            const int32_t length = static_cast<int32_t>(ValueToInt64(lengthValue));
            if (length < 0) {
                throw std::runtime_error("NewArr length must be non-negative");
            }
            const auto elementType = ParseTypeReference(vm, typeName);
            ObjectRef arrayObject = vm->CreateArray(elementType, length);
            context->PushStack(Value(arrayObject));
            break;
        }

        case OpCode::LdLen: {
            const auto arrayValue = context->PopStack();
            if (!arrayValue.IsObject()) {
                throw std::runtime_error("LdLen requires an array object on the stack");
            }
            auto arrayObj = std::dynamic_pointer_cast<Array>(arrayValue.AsObject());
            if (!arrayObj) {
                throw std::runtime_error("LdLen requires an Array instance");
            }
            context->PushStack(Value(arrayObj->GetArrayLength()));
            break;
        }

        case OpCode::LdElem: {
            const auto indexValue = context->PopStack();
            const auto arrayValue = context->PopStack();
            if (!arrayValue.IsObject()) {
                throw std::runtime_error("LdElem requires an array object on the stack");
            }
            auto arrayObj = std::dynamic_pointer_cast<Array>(arrayValue.AsObject());
            if (!arrayObj) {
                throw std::runtime_error("LdElem requires an Array instance");
            }
            const int32_t index = static_cast<int32_t>(ValueToInt64(indexValue));
            context->PushStack(arrayObj->GetElement(index));
            break;
        }

        case OpCode::StElem: {
            const auto valueToStore = context->PopStack();
            const auto indexValue = context->PopStack();
            const auto arrayValue = context->PopStack();
            if (!arrayValue.IsObject()) {
                throw std::runtime_error("StElem requires an array object on the stack");
            }
            auto arrayObj = std::dynamic_pointer_cast<Array>(arrayValue.AsObject());
            if (!arrayObj) {
                throw std::runtime_error("StElem requires an Array instance");
            }
            const int32_t index = static_cast<int32_t>(ValueToInt64(indexValue));
            arrayObj->SetElement(index, valueToStore);
            break;
        }

        case OpCode::CastClass:
        case OpCode::IsInst: {
            const std::string typeName = !instr.operandString.empty() ? instr.operandString : instr.identifier;
            if (typeName.empty()) {
                throw std::runtime_error("CastClass/IsInst instruction missing type operand");
            }
            const auto normalized = TypeNames::NormalizeTypeName(typeName);
            const auto value = context->PopStack();

            auto matchesPrimitive = [&]() -> bool {
                if (normalized == "object") return true;
                if (normalized == "string") return value.IsString();
                if (normalized == "bool") return value.IsBool();
                if (normalized == "int32") return value.IsInt32();
                if (normalized == "int64") return value.IsInt64();
                if (normalized == "float32") return value.IsFloat32();
                if (normalized == "float64") return value.IsFloat64();
                if (normalized == "uint8") return value.IsInt32();
                return false;
            };

            auto matchesClass = [&]() -> bool {
                if (normalized == "object") return true;
                if (!value.IsObject()) return false;
                if (!vm) return false;
                try {
                    auto cls = vm->GetClass(normalized);
                    auto obj = value.AsObject();
                    return obj && obj->IsInstanceOf(cls);
                } catch (...) {
                    return false;
                }
            };

            const bool ok = value.IsNull() || matchesPrimitive() || matchesClass();

            if (instr.opCode == OpCode::IsInst) {
                context->PushStack(ok ? value : Value());
                break;
            }

            // CastClass
            if (!ok) {
                throw std::runtime_error("Invalid cast to '" + typeName + "'");
            }
            context->PushStack(value);
            break;
        }

        case OpCode::Call:
        case OpCode::CallVirt: {
            if (!instr.callTarget.has_value()) {
                throw std::runtime_error("Call instruction missing target metadata");
            }

            const auto& target = instr.callTarget.value();
            std::vector<Value> callArgs;
            callArgs.reserve(target.parameterTypes.size());
            for (size_t i = 0; i < target.parameterTypes.size(); ++i) {
                callArgs.push_back(context->PopStack());
            }
            std::reverse(callArgs.begin(), callArgs.end());

            auto isVoidReturn = target.returnType.empty() || target.returnType == "void" || target.returnType == "System.Void";

            if (target.declaringType == "System.Console" && target.name == "WriteLine") {
                if (callArgs.empty()) {
                    vm->WriteOutput("\n");
                } else {
                    for (size_t i = 0; i < callArgs.size(); ++i) {
                        if (i > 0) {
                            vm->WriteOutput(" ");
                        }
                        // If the runtime is asked to print a null value, do not show the
                        // literal "null" string; instead emit an empty string so that
                        // Console.WriteLine(null) will behave like WriteLine("") in
                        // typical .NET loggers (printing an empty line).
                        if (callArgs[i].IsNull()) {
                            vm->WriteOutput("");
                        } else {
                            vm->WriteOutput(ValueToString(callArgs[i]));
                        }
                    }
                    vm->WriteOutput("\n");
                }
                break;
            }

            Value result;
            if (instr.opCode == OpCode::CallVirt) {
                auto instanceValue = context->PopStack();
                if (!instanceValue.IsObject()) {
                    throw std::runtime_error("CallVirt requires object instance on stack");
                }
                auto instance = instanceValue.AsObject();
                result = vm->InvokeMethod(instance, target, callArgs);
            } else {
                auto classRef = vm->GetClass(target.declaringType);
                result = vm->InvokeStaticMethod(classRef, target, callArgs);
            }

            if (!isVoidReturn) {
                context->PushStack(result);
            }

            break;
        }
        
        case OpCode::Break:
            throw BreakSignal();

        case OpCode::Continue:
            throw ContinueSignal();

        case OpCode::While: {
            if (!instr.whileData.has_value()) {
                throw std::runtime_error("While instruction missing metadata");
            }

            const auto& whileData = instr.whileData.value();

            while (EvaluateCondition(whileData.condition, context, vm)) {
                try {
                    for (const auto& bodyInstr : whileData.body) {
                        Execute(bodyInstr, context, vm);
                    }
                } catch (const ContinueSignal&) {
                    continue;
                } catch (const BreakSignal&) {
                    break;
                }
            }
            break;
        }

        case OpCode::If: {
            if (!instr.ifData.has_value()) {
                throw std::runtime_error("If instruction missing metadata");
            }

            const auto& ifData = instr.ifData.value();

            // Pop the condition value from the stack
            Value conditionValue = context->PopStack();
            bool condition = ValueToBool(conditionValue);

            if (condition) {
                // Execute then block
                for (const auto& thenInstr : ifData.thenBlock) {
                    Execute(thenInstr, context, vm);
                }
            } else if (!ifData.elseBlock.empty()) {
                // Execute else block if present
                for (const auto& elseInstr : ifData.elseBlock) {
                    Execute(elseInstr, context, vm);
                }
            }
            break;
        }

        case OpCode::Throw:
            throw std::runtime_error("Instruction not yet implemented: throw");
        
        default:
            throw std::runtime_error("Unknown instruction opcode");
    }
}

Value InstructionExecutor::ExecuteInstructions(
    const std::vector<Instruction>& instructions,
    ObjectRef thisPtr,
    const std::vector<Value>& args,
    ExecutionContext* context,
    VirtualMachine* vm,
    const std::unordered_map<std::string, size_t>& labelMap
) {
    context->SetThis(thisPtr);
    context->SetArguments(args);

    auto resolveTarget = [&](const Instruction& instr) -> size_t {
        int target = -1;
        if (instr.hasOperandInt) {
            target = instr.operandInt;
        } else if (!instr.operandString.empty()) {
            // First try to look up as a label name
            auto labelIt = labelMap.find(instr.operandString);
            if (labelIt != labelMap.end()) {
                return labelIt->second;
            }
            // Otherwise try to parse as an integer
            try {
                target = std::stoi(instr.operandString);
            } catch (...) {
                throw std::runtime_error("Branch target not found: " + instr.operandString);
            }
        }

        if (target < 0 || static_cast<size_t>(target) >= instructions.size()) {
            throw std::runtime_error("Branch target out of range");
        }
        return static_cast<size_t>(target);
    };

    auto compareBranch = [&](OpCode cmp, const Value& left, const Value& right) -> bool {
        switch (cmp) {
            case OpCode::Beq:
                if (left.IsString() && right.IsString()) return left.AsString() == right.AsString();
                if (left.IsBool() && right.IsBool()) return left.AsBool() == right.AsBool();
                if ((left.IsInt32() || left.IsInt64()) && (right.IsInt32() || right.IsInt64())) return ValueToInt64(left) == ValueToInt64(right);
                return ValueToDouble(left) == ValueToDouble(right);
            case OpCode::Bne:
                if (left.IsString() && right.IsString()) return left.AsString() != right.AsString();
                if (left.IsBool() && right.IsBool()) return left.AsBool() != right.AsBool();
                if ((left.IsInt32() || left.IsInt64()) && (right.IsInt32() || right.IsInt64())) return ValueToInt64(left) != ValueToInt64(right);
                return ValueToDouble(left) != ValueToDouble(right);
            case OpCode::Bgt:
                if ((left.IsInt32() || left.IsInt64()) && (right.IsInt32() || right.IsInt64())) return ValueToInt64(left) > ValueToInt64(right);
                return ValueToDouble(left) > ValueToDouble(right);
            case OpCode::Blt:
                if ((left.IsInt32() || left.IsInt64()) && (right.IsInt32() || right.IsInt64())) return ValueToInt64(left) < ValueToInt64(right);
                return ValueToDouble(left) < ValueToDouble(right);
            case OpCode::Bge:
                if ((left.IsInt32() || left.IsInt64()) && (right.IsInt32() || right.IsInt64())) return ValueToInt64(left) >= ValueToInt64(right);
                return ValueToDouble(left) >= ValueToDouble(right);
            case OpCode::Ble:
                if ((left.IsInt32() || left.IsInt64()) && (right.IsInt32() || right.IsInt64())) return ValueToInt64(left) <= ValueToInt64(right);
                return ValueToDouble(left) <= ValueToDouble(right);
            default:
                return false;
        }
    };

    size_t ip = 0;
    while (ip < instructions.size()) {
        const auto& instr = instructions[ip];
        if (context) {
            context->SetLastInstruction(ip, instr.opCode);
        }
        // std::cerr << "[" << (context->GetMethod() ? context->GetMethod()->GetName() : std::string("<static>"))
        //           << "] Executing instruction " << ip << ": op=" << static_cast<int>(instr.opCode)
        //           << ", id='" << instr.identifier << "', operand='" << instr.operandString << "'" << std::endl;

        try {
            if (instr.opCode == OpCode::Ret) {
                try {
                    return context->PopStack();
                } catch (...) {
                    return Value();
                }
            }

            switch (instr.opCode) {
                case OpCode::Br: {
                    ip = resolveTarget(instr);
                    continue;
                }
                case OpCode::BrTrue: {
                    bool cond = ValueToBool(context->PopStack());
                    if (cond) {
                        ip = resolveTarget(instr);
                        continue;
                    }
                    ++ip;
                    continue;
                }
                case OpCode::BrFalse: {
                    bool cond = ValueToBool(context->PopStack());
                    if (!cond) {
                        ip = resolveTarget(instr);
                        continue;
                    }
                    ++ip;
                    continue;
                }
                case OpCode::Beq:
                case OpCode::Bne:
                case OpCode::Bgt:
                case OpCode::Blt:
                case OpCode::Bge:
                case OpCode::Ble: {
                    auto right = context->PopStack();
                    auto left = context->PopStack();
                    bool cond = compareBranch(instr.opCode, left, right);
                    if (cond) {
                        ip = resolveTarget(instr);
                        continue;
                    }
                    ++ip;
                    continue;
                }
                default:
                    break;
            }

            // Special handling for while loops with binary conditions
            if (instr.opCode == OpCode::While && instr.whileData.has_value()) {
            const auto& whileData = instr.whileData.value();
            if (whileData.condition.kind == ConditionKind::Binary) {
                std::vector<Instruction> setupInstrs;
                int setupIdx = static_cast<int>(ip) - 1;

                while (setupIdx >= 0) {
                    const auto& prevInstr = instructions[setupIdx];
                    if (prevInstr.opCode == OpCode::LdLoc ||
                        prevInstr.opCode == OpCode::LdCon ||
                        prevInstr.opCode == OpCode::LdI4 ||
                        prevInstr.opCode == OpCode::LdI8 ||
                        prevInstr.opCode == OpCode::LdR4 ||
                        prevInstr.opCode == OpCode::LdR8 ||
                        prevInstr.opCode == OpCode::LdTrue ||
                        prevInstr.opCode == OpCode::LdFalse ||
                        prevInstr.opCode == OpCode::LdNull) {
                        setupInstrs.insert(setupInstrs.begin(), prevInstr);
                        setupIdx--;
                    } else {
                        break;
                    }
                }

                while (true) {
                    for (const auto& setupInstr : setupInstrs) {
                        Execute(setupInstr, context, vm);
                    }

                    bool cond_result;
                    if (whileData.condition.comparisonOp == OpCode::Nop) {
                        throw std::runtime_error("Binary condition missing comparison operation");
                    }

                    auto right = context->PopStack();
                    auto left = context->PopStack();
                    context->PushStack(left);
                    context->PushStack(right);

                    switch (whileData.condition.comparisonOp) {
                        case OpCode::Ceq: ExecuteCeq(context); break;
                        case OpCode::Cne: ExecuteCne(context); break;
                        case OpCode::Clt: ExecuteClt(context); break;
                        case OpCode::Cle: ExecuteCle(context); break;
                        case OpCode::Cgt: ExecuteCgt(context); break;
                        case OpCode::Cge: ExecuteCge(context); break;
                        default:
                            throw std::runtime_error("Unsupported comparison opcode in binary condition");
                    }

                    auto result = context->PopStack();
                    cond_result = ValueToBool(result);

                    if (!cond_result) {
                        break;
                    }

                    try {
                        for (const auto& bodyInstr : whileData.body) {
                            Execute(bodyInstr, context, vm);
                        }
                    } catch (const ContinueSignal&) {
                        continue;
                    } catch (const BreakSignal&) {
                        break;
                    }
                }
                ++ip;
                continue;
            }
        }

            Execute(instr, context, vm);
            ++ip;
        } catch (const std::exception& ex) {
            const std::string methodName = (context && context->GetMethod()) ? context->GetMethod()->GetName() : std::string("<unknown>");
            throw std::runtime_error(
                "VM error in method '" + methodName + "' at ip=" + std::to_string(ip) +
                " op=" + std::to_string(static_cast<int>(instr.opCode)) +
                " id='" + instr.identifier + "' operand='" + instr.operandString + "': " + ex.what()
            );
        }
    }

    try {
        return context->PopStack();
    } catch (...) {
        return Value();
    }
}

double InstructionExecutor::ValueToDouble(const Value& v) {
    if (v.IsInt32()) return static_cast<double>(v.AsInt32());
    if (v.IsInt64()) return static_cast<double>(v.AsInt64());
    if (v.IsFloat32()) return static_cast<double>(v.AsFloat32());
    if (v.IsFloat64()) return v.AsFloat64();
    throw std::runtime_error("Cannot convert value to double");
}

int64_t InstructionExecutor::ValueToInt64(const Value& v) {
    if (v.IsInt32()) return static_cast<int64_t>(v.AsInt32());
    if (v.IsInt64()) return v.AsInt64();
    if (v.IsFloat32()) return static_cast<int64_t>(v.AsFloat32());
    if (v.IsFloat64()) return static_cast<int64_t>(v.AsFloat64());
    throw std::runtime_error("Cannot convert value to int64");
}

void InstructionExecutor::ExecuteAdd(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    if (a.IsString() || b.IsString()) {
        // String concatenation
        std::string result = a.AsString() + b.AsString();
        context->PushStack(Value(result));
    } else if (a.IsInt32() && b.IsInt32()) {
        context->PushStack(Value(a.AsInt32() + b.AsInt32()));
    } else if (a.IsInt64() || b.IsInt64()) {
        context->PushStack(Value(ValueToInt64(a) + ValueToInt64(b)));
    } else {
        context->PushStack(Value(ValueToDouble(a) + ValueToDouble(b)));
    }
}

void InstructionExecutor::ExecuteSub(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    if (a.IsInt32() && b.IsInt32()) {
        context->PushStack(Value(a.AsInt32() - b.AsInt32()));
    } else if (a.IsInt64() || b.IsInt64()) {
        context->PushStack(Value(ValueToInt64(a) - ValueToInt64(b)));
    } else {
        context->PushStack(Value(ValueToDouble(a) - ValueToDouble(b)));
    }
}

void InstructionExecutor::ExecuteMul(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    if (a.IsInt32() && b.IsInt32()) {
        context->PushStack(Value(a.AsInt32() * b.AsInt32()));
    } else if (a.IsInt64() || b.IsInt64()) {
        context->PushStack(Value(ValueToInt64(a) * ValueToInt64(b)));
    } else {
        context->PushStack(Value(ValueToDouble(a) * ValueToDouble(b)));
    }
}

void InstructionExecutor::ExecuteDiv(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    if (b.IsInt32() && b.AsInt32() == 0) {
        throw std::runtime_error("Division by zero");
    }
    if (b.IsInt64() && b.AsInt64() == 0) {
        throw std::runtime_error("Division by zero");
    }
    
    if (a.IsInt32() && b.IsInt32()) {
        context->PushStack(Value(a.AsInt32() / b.AsInt32()));
    } else if (a.IsInt64() || b.IsInt64()) {
        context->PushStack(Value(ValueToInt64(a) / ValueToInt64(b)));
    } else {
        context->PushStack(Value(ValueToDouble(a) / ValueToDouble(b)));
    }
}

void InstructionExecutor::ExecuteRem(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    if (a.IsInt32() && b.IsInt32()) {
        context->PushStack(Value(a.AsInt32() % b.AsInt32()));
    } else if (a.IsInt64() || b.IsInt64()) {
        context->PushStack(Value(ValueToInt64(a) % ValueToInt64(b)));
    } else {
        throw std::runtime_error("Modulo operation not supported for floating point");
    }
}

void InstructionExecutor::ExecuteNeg(ExecutionContext* context) {
    auto a = context->PopStack();
    
    if (a.IsInt32()) {
        context->PushStack(Value(-a.AsInt32()));
    } else if (a.IsInt64()) {
        context->PushStack(Value(-a.AsInt64()));
    } else if (a.IsFloat32()) {
        context->PushStack(Value(-a.AsFloat32()));
    } else if (a.IsFloat64()) {
        context->PushStack(Value(-a.AsFloat64()));
    }
}

void InstructionExecutor::ExecuteCeq(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    bool result = false;
    if (a.IsInt32() && b.IsInt32()) {
        result = a.AsInt32() == b.AsInt32();
    } else if ((a.IsInt32() || a.IsInt64()) && (b.IsInt32() || b.IsInt64())) {
        result = ValueToInt64(a) == ValueToInt64(b);
    } else if (a.IsString() && b.IsString()) {
        result = a.AsString() == b.AsString();
    } else if (a.IsBool() && b.IsBool()) {
        result = a.AsBool() == b.AsBool();
    } else {
        result = ValueToDouble(a) == ValueToDouble(b);
    }
    
    context->PushStack(Value(result));
}

void InstructionExecutor::ExecuteCne(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    bool result = false;
    if (a.IsInt32() && b.IsInt32()) {
        result = a.AsInt32() != b.AsInt32();
    } else if ((a.IsInt32() || a.IsInt64()) && (b.IsInt32() || b.IsInt64())) {
        result = ValueToInt64(a) != ValueToInt64(b);
    } else if (a.IsString() && b.IsString()) {
        result = a.AsString() != b.AsString();
    } else if (a.IsBool() && b.IsBool()) {
        result = a.AsBool() != b.AsBool();
    } else {
        result = ValueToDouble(a) != ValueToDouble(b);
    }
    
    context->PushStack(Value(result));
}

void InstructionExecutor::ExecuteClt(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    bool result = false;
    if (a.IsInt32() && b.IsInt32()) {
        result = a.AsInt32() < b.AsInt32();
    } else if ((a.IsInt32() || a.IsInt64()) && (b.IsInt32() || b.IsInt64())) {
        result = ValueToInt64(a) < ValueToInt64(b);
    } else {
        result = ValueToDouble(a) < ValueToDouble(b);
    }
    
    context->PushStack(Value(result));
}

void InstructionExecutor::ExecuteCle(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    bool result = false;
    if (a.IsInt32() && b.IsInt32()) {
        result = a.AsInt32() <= b.AsInt32();
    } else if ((a.IsInt32() || a.IsInt64()) && (b.IsInt32() || b.IsInt64())) {
        result = ValueToInt64(a) <= ValueToInt64(b);
    } else {
        result = ValueToDouble(a) <= ValueToDouble(b);
    }
    
    context->PushStack(Value(result));
}

void InstructionExecutor::ExecuteCgt(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    bool result = false;
    if (a.IsInt32() && b.IsInt32()) {
        result = a.AsInt32() > b.AsInt32();
    } else if ((a.IsInt32() || a.IsInt64()) && (b.IsInt32() || b.IsInt64())) {
        result = ValueToInt64(a) > ValueToInt64(b);
    } else {
        result = ValueToDouble(a) > ValueToDouble(b);
    }
    
    context->PushStack(Value(result));
}

void InstructionExecutor::ExecuteCge(ExecutionContext* context) {
    auto b = context->PopStack();
    auto a = context->PopStack();
    
    bool result = false;
    if (a.IsInt32() && b.IsInt32()) {
        result = a.AsInt32() >= b.AsInt32();
    } else if ((a.IsInt32() || a.IsInt64()) && (b.IsInt32() || b.IsInt64())) {
        result = ValueToInt64(a) >= ValueToInt64(b);
    } else {
        result = ValueToDouble(a) >= ValueToDouble(b);
    }
    
    context->PushStack(Value(result));
}

bool InstructionExecutor::EvaluateCondition(
    const Instruction::ConditionData& condition,
    ExecutionContext* context,
    VirtualMachine* vm
) {
    for (const auto& setupInstr : condition.setupInstructions) {
        Execute(setupInstr, context, vm);
    }

    switch (condition.kind) {
        case ConditionKind::Stack: {
            auto value = context->PopStack();
            return ValueToBool(value);
        }

        case ConditionKind::Binary: {
            if (condition.comparisonOp == OpCode::Nop) {
                throw std::runtime_error("Binary condition missing comparison operation");
            }

            auto right = context->PopStack();
            auto left = context->PopStack();

            context->PushStack(left);
            context->PushStack(right);

            switch (condition.comparisonOp) {
                case OpCode::Ceq: ExecuteCeq(context); break;
                case OpCode::Cne: ExecuteCne(context); break;
                case OpCode::Clt: ExecuteClt(context); break;
                case OpCode::Cle: ExecuteCle(context); break;
                case OpCode::Cgt: ExecuteCgt(context); break;
                case OpCode::Cge: ExecuteCge(context); break;
                default:
                    throw std::runtime_error("Unsupported comparison opcode in binary condition");
            }

            auto result = context->PopStack();
            return ValueToBool(result);
        }

        case ConditionKind::Expression: {
            for (const auto& exprInstr : condition.expressionInstructions) {
                Execute(exprInstr, context, vm);
            }
            auto result = context->PopStack();
            return ValueToBool(result);
        }

        case ConditionKind::None:
        default:
            throw std::runtime_error("Condition kind not supported");
    }
}

bool InstructionExecutor::ValueToBool(const Value& value) {
    if (value.IsBool()) {
        return value.AsBool();
    }
    if (value.IsNull()) {
        return false;
    }
    if (value.IsInt32()) {
        return value.AsInt32() != 0;
    }
    if (value.IsInt64()) {
        return value.AsInt64() != 0;
    }
    if (value.IsFloat32()) {
        return value.AsFloat32() != 0.0f;
    }
    if (value.IsFloat64()) {
        return value.AsFloat64() != 0.0;
    }
    if (value.IsString()) {
        return !value.AsString().empty();
    }
    if (value.IsObject()) {
        return true;
    }
    return false;
}

} // namespace ObjectIR
