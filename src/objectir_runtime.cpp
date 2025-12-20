#include "objectir_runtime.hpp"
#include "instruction_executor.hpp"
#include "objectir_plugin.hpp"
#include "objectir_plugin_api.h"
#include "objectir_type_names.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

    // windows.h (via rpc.h/winuser.h) defines macros that collide with common identifiers.
    // Ensure they don't break our C++ method/parameter names.
    #if defined(interface)
        #undef interface
    #endif
    #if defined(RegisterClass)
        #undef RegisterClass
    #endif
#else
    #include <dlfcn.h>
#endif

namespace ObjectIR {

namespace {

static void* ResolvePluginSymbol(
#if defined(_WIN32)
    HMODULE lib,
#else
    void* lib,
#endif
    const char* name
) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(lib, name));
#else
    return dlsym(lib, name);
#endif
}

static std::string PluginLoaderError() {
#if defined(_WIN32)
    return "dynamic loader error";
#else
    const char* err = dlerror();
    return err ? std::string(err) : std::string();
#endif
}

std::string OpCodeToString(OpCode op) {
    switch (op) {
        case OpCode::Nop: return "nop";
        case OpCode::Dup: return "dup";
        case OpCode::Pop: return "pop";
        case OpCode::LdArg: return "ldarg";
        case OpCode::LdLoc: return "ldloc";
        case OpCode::LdFld: return "ldfld";
        case OpCode::LdCon: return "ldc";
        case OpCode::LdStr: return "ldstr";
        case OpCode::LdI4: return "ldi4";
        case OpCode::LdI8: return "ldi8";
        case OpCode::LdR4: return "ldr4";
        case OpCode::LdR8: return "ldr8";
        case OpCode::LdTrue: return "ldtrue";
        case OpCode::LdFalse: return "ldfalse";
        case OpCode::LdNull: return "ldnull";
        case OpCode::StLoc: return "stloc";
        case OpCode::StFld: return "stfld";
        case OpCode::StArg: return "starg";
        case OpCode::Add: return "add";
        case OpCode::Sub: return "sub";
        case OpCode::Mul: return "mul";
        case OpCode::Div: return "div";
        case OpCode::Rem: return "rem";
        case OpCode::Neg: return "neg";
        case OpCode::Ceq: return "ceq";
        case OpCode::Cne: return "cne";
        case OpCode::Clt: return "clt";
        case OpCode::Cle: return "cle";
        case OpCode::Cgt: return "cgt";
        case OpCode::Cge: return "cge";
        case OpCode::Ret: return "ret";
        case OpCode::Br: return "br";
        case OpCode::BrTrue: return "brtrue";
        case OpCode::BrFalse: return "brfalse";
        case OpCode::Beq: return "beq";
        case OpCode::Bne: return "bne";
        case OpCode::Bgt: return "bgt";
        case OpCode::Blt: return "blt";
        case OpCode::Bge: return "bge";
        case OpCode::Ble: return "ble";
        case OpCode::If: return "if";
        case OpCode::NewObj: return "newobj";
        case OpCode::Call: return "call";
        case OpCode::CallVirt: return "callvirt";
        case OpCode::CastClass: return "castclass";
        case OpCode::IsInst: return "isinst";
        case OpCode::NewArr: return "newarr";
        case OpCode::LdElem: return "ldelem";
        case OpCode::StElem: return "stelem";
        case OpCode::LdLen: return "ldlen";
        case OpCode::Break: return "break";
        case OpCode::Continue: return "continue";
        case OpCode::Throw: return "throw";
        case OpCode::While: return "while";
        default: return "nop";
    }
}

json SerializeInstruction(const Instruction& instr, bool includeNested);

json SerializeInstructionBlock(const std::vector<Instruction>& instructions, bool includeNested) {
    json block = json::array();
    for (const auto& instr : instructions) {
        block.push_back(SerializeInstruction(instr, includeNested));
    }
    return block;
}

json SerializeInstruction(const Instruction& instr, bool includeNested) {
    json node;
    node["opCode"] = OpCodeToString(instr.opCode);

    json operand;

    switch (instr.opCode) {
        case OpCode::LdArg:
        case OpCode::StArg:
            if (!instr.identifier.empty()) operand["argumentName"] = instr.identifier;
            break;
        case OpCode::LdLoc:
        case OpCode::StLoc:
            if (!instr.identifier.empty()) operand["localName"] = instr.identifier;
            break;
        case OpCode::LdFld:
        case OpCode::StFld:
            if (instr.fieldTarget.has_value()) {
                operand["field"] = instr.fieldTarget->name;
            } else if (!instr.identifier.empty()) {
                operand["field"] = instr.identifier;
            }
            break;
        case OpCode::LdCon:
            if (instr.constantIsNull) {
                operand["value"] = nullptr;
            } else if (!instr.constantRawValue.empty()) {
                operand["value"] = instr.constantRawValue;
            }
            if (!instr.constantType.empty()) {
                operand["type"] = instr.constantType;
            }
            break;
        case OpCode::LdI4:
        case OpCode::LdI8:
        case OpCode::LdR4:
        case OpCode::LdR8:
        case OpCode::LdStr:
            if (!instr.constantRawValue.empty()) {
                operand["value"] = instr.constantRawValue;
            } else if (!instr.operandString.empty()) {
                operand["value"] = instr.operandString;
            } else {
                operand["value"] = instr.operandInt;
            }
            break;
        case OpCode::Call:
        case OpCode::CallVirt:
            if (instr.callTarget.has_value()) {
                json method;
                method["declaringType"] = instr.callTarget->declaringType;
                method["name"] = instr.callTarget->name;
                method["returnType"] = instr.callTarget->returnType;
                method["parameterTypes"] = instr.callTarget->parameterTypes;
                operand["method"] = method;
            }
            break;
        case OpCode::NewObj:
            if (!instr.operandString.empty()) {
                operand["type"] = instr.operandString;
            }
            break;
        case OpCode::If:
            if (includeNested && instr.ifData.has_value()) {
                operand["thenBlock"] = SerializeInstructionBlock(instr.ifData->thenBlock, includeNested);
                operand["elseBlock"] = SerializeInstructionBlock(instr.ifData->elseBlock, includeNested);
            }
            break;
        case OpCode::While:
            if (includeNested && instr.whileData.has_value()) {
                json whileNode;
                if (instr.whileData->condition.kind != ConditionKind::None) {
                    json cond;
                    switch (instr.whileData->condition.kind) {
                        case ConditionKind::Stack: cond["kind"] = "stack"; break;
                        case ConditionKind::Binary: cond["kind"] = "binary"; break;
                        case ConditionKind::Expression: cond["kind"] = "expression"; break;
                        default: cond["kind"] = "none"; break;
                    }
                    if (!instr.whileData->condition.expressionInstructions.empty()) {
                        cond["expression"] = SerializeInstructionBlock(instr.whileData->condition.expressionInstructions, includeNested);
                    }
                    whileNode["condition"] = cond;
                }
                whileNode["body"] = SerializeInstructionBlock(instr.whileData->body, includeNested);
                operand = std::move(whileNode);
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
        case OpCode::Ble:
            if (instr.hasOperandInt) {
                operand["target"] = instr.operandInt;
            } else if (!instr.operandString.empty()) {
                operand["target"] = instr.operandString;
            }
            break;
        default:
            if (!instr.operandString.empty()) {
                operand["value"] = instr.operandString;
            }
            break;
    }

    if (!operand.empty()) {
        node["operand"] = operand;
    }

    return node;
}

std::string QualifiedName(const ClassRef& cls) {
    if (!cls) return {};
    if (cls->GetNamespace().empty()) return cls->GetName();
    return cls->GetNamespace() + "." + cls->GetName();
}

} // namespace

struct VirtualMachine::LoadedPlugin {
#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    PluginShutdownFn shutdown = nullptr;
    std::string path;
};

// ============================================================================
// TypeReference Implementation
// ============================================================================

TypeReference::TypeReference(const TypeReference& other)
    : _isPrimitive(other._isPrimitive), _primitiveType(other._primitiveType), 
      _classType(other._classType), _elementType(other._elementType ? std::make_shared<TypeReference>(*other._elementType) : nullptr) {}

TypeReference::TypeReference(PrimitiveType primitive)
    : _isPrimitive(true), _primitiveType(primitive) {}

TypeReference::TypeReference(ClassRef classType)
    : _isPrimitive(false), _classType(classType) {}

TypeReference TypeReference::Int32() {
    return TypeReference(PrimitiveType::Int32);
}

TypeReference TypeReference::Int64() {
    return TypeReference(PrimitiveType::Int64);
}

TypeReference TypeReference::Float32() {
    return TypeReference(PrimitiveType::Float32);
}

TypeReference TypeReference::Float64() {
    return TypeReference(PrimitiveType::Float64);
}

TypeReference TypeReference::Bool() {
    return TypeReference(PrimitiveType::Bool);
}

TypeReference TypeReference::Void() {
    return TypeReference(PrimitiveType::Void);
}

TypeReference TypeReference::String() {
    return TypeReference(PrimitiveType::String);
}

TypeReference TypeReference::UInt8() {
    return TypeReference(PrimitiveType::UInt8);
}

TypeReference TypeReference::Object(ClassRef classType) {
    return TypeReference(classType);
}

TypeReference TypeReference::Object() {
    return TypeReference(static_cast<ClassRef>(nullptr));
}

std::string TypeReference::ToString() const {
    if (_isPrimitive) {
        switch (_primitiveType) {
            case PrimitiveType::Int32: return "int32";
            case PrimitiveType::Int64: return "int64";
            case PrimitiveType::Float32: return "float";
            case PrimitiveType::Float64: return "double";
            case PrimitiveType::Bool: return "bool";
            case PrimitiveType::Void: return "void";
            case PrimitiveType::String: return "string";
            case PrimitiveType::UInt8: return "uint8";
            case PrimitiveType::Object: return "object";
            default: return "unknown";
        }
    } else {
        if (_classType) {
            return _classType->GetName();
        } else {
            return "object";
        }
    }
}

// ============================================================================
// Value Implementation
// ============================================================================

Value::Value() : _value(nullptr) {}
Value::Value(int32_t i32) : _value(i32) {}
Value::Value(int64_t i64) : _value(i64) {}
Value::Value(float f) : _value(f) {}
Value::Value(double d) : _value(d) {}
Value::Value(bool b) : _value(b) {}
Value::Value(const std::string& str) : _value(str) {}
Value::Value(ObjectRef obj) : _value(obj) {}

bool Value::IsInt32() const { return _value.index() == 1; }
bool Value::IsInt64() const { return _value.index() == 2; }
bool Value::IsFloat32() const { return _value.index() == 3; }
bool Value::IsFloat64() const { return _value.index() == 4; }
bool Value::IsBool() const { return _value.index() == 5; }
bool Value::IsString() const { return _value.index() == 6; }
bool Value::IsObject() const { return _value.index() == 7; }

int32_t Value::AsInt32() const {
    if (!IsInt32()) throw std::runtime_error("Value is not int32");
    return std::get<int32_t>(_value);
}

int64_t Value::AsInt64() const {
    if (!IsInt64()) throw std::runtime_error("Value is not int64");
    return std::get<int64_t>(_value);
}

float Value::AsFloat32() const {
    if (!IsFloat32()) throw std::runtime_error("Value is not float32");
    return std::get<float>(_value);
}

double Value::AsFloat64() const {
    if (!IsFloat64()) throw std::runtime_error("Value is not float64");
    return std::get<double>(_value);
}

bool Value::AsBool() const {
    if (!IsBool()) throw std::runtime_error("Value is not bool");
    return std::get<bool>(_value);
}

std::string Value::AsString() const {
    if (!IsString()) throw std::runtime_error("Value is not string");
    return std::get<std::string>(_value);
}

ObjectRef Value::AsObject() const {
    if (!IsObject()) throw std::runtime_error("Value is not object");
    return std::get<ObjectRef>(_value);
}

bool Value::operator==(const Value& other) const {
    if (_value.index() != other._value.index()) return false;
    
    switch (_value.index()) {
        case 0: return true; // both null
        case 1: return std::get<int32_t>(_value) == std::get<int32_t>(other._value);
        case 2: return std::get<int64_t>(_value) == std::get<int64_t>(other._value);
        case 3: return std::get<float>(_value) == std::get<float>(other._value);
        case 4: return std::get<double>(_value) == std::get<double>(other._value);
        case 5: return std::get<bool>(_value) == std::get<bool>(other._value);
        case 6: return std::get<std::string>(_value) == std::get<std::string>(other._value);
        case 7: return std::get<ObjectRef>(_value) == std::get<ObjectRef>(other._value);
        default: return false;
    }
}

// ============================================================================
// Object Implementation
// ============================================================================

void Object::SetField(const std::string& fieldName, const Value& value) {
    _fieldValues[fieldName] = value;
}

Value Object::GetField(const std::string& fieldName) const {
    auto it = _fieldValues.find(fieldName);
    if (it != _fieldValues.end()) {
        return it->second;
    }
    
    // Check base class instance
    if (_baseInstance) {
        return _baseInstance->GetField(fieldName);
    }
    
    throw std::runtime_error("Field not found: " + fieldName);
}

bool Object::IsInstanceOf(ClassRef classType) const {
    if (!_class) return false;
    
    auto current = _class;
    while (current) {
        if (current == classType) return true;
        current = current->GetBaseClass();
    }
    
    return _class->ImplementsInterface(classType);
}

// ============================================================================
// Field Implementation
// ============================================================================

// Implementation is inline in header

// ============================================================================
// Method Implementation
// ============================================================================

void Method::AddParameter(const std::string& name, const TypeReference& type) {
    _parameters.emplace_back(name, type);
}

void Method::AddLocal(const std::string& name, const TypeReference& type) {
    _locals.emplace_back(name, type);
}

void Method::SetInstructions(std::vector<Instruction> instructions) {
    _instructions = std::move(instructions);
}

// ============================================================================
// Class Implementation
// ============================================================================

Class::Class(std::string name) : _name(std::move(name)) {}

void Class::AddField(FieldRef field) {
    _fields.push_back(field);
}

FieldRef Class::GetField(const std::string& name) const {
    for (const auto& field : _fields) {
        if (field->GetName() == name) {
            return field;
        }
    }
    
    if (_baseClass) {
        return _baseClass->GetField(name);
    }
    
    return nullptr;
}

void Class::AddMethod(MethodRef method) {
    _methods.push_back(method);
}

MethodRef Class::GetMethod(const std::string& name) const {
    for (const auto& method : _methods) {
        if (method->GetName() == name) {
            return method;
        }
    }
    return nullptr;
}

std::vector<MethodRef> Class::GetMethods() const {
    return _methods;
}

MethodRef Class::LookupMethod(const std::string& name) const {
    auto method = GetMethod(name);
    if (method) return method;
    
    if (_baseClass) {
        return _baseClass->LookupMethod(name);
    }
    
    return nullptr;
}

ObjectRef Class::CreateInstance() const {
    auto obj = std::make_shared<Object>();
    obj->SetClass(std::const_pointer_cast<Class>(shared_from_this()));
    
    // Initialize all field value slots for this class and base classes
    auto current = std::const_pointer_cast<Class>(shared_from_this());
    while (current) {
        // Initialize field values from most derived to most base
        for (const auto& field : current->GetAllFields()) {
            obj->InitializeFieldSlot(field->GetName());
        }
        current = current->GetBaseClass();
    }
    
    return obj;
}

void Class::AddInterface(ClassRef interfaceType) {
    _interfaces.push_back(interfaceType);
}

bool Class::ImplementsInterface(ClassRef interfaceType) const {
    for (const auto& iface : _interfaces) {
        if (iface == interfaceType) return true;
    }
    return false;
}

// ============================================================================
// ExecutionContext Implementation
// ============================================================================

ExecutionContext::ExecutionContext(MethodRef method)
    : _method(std::move(method)) {
    if (!_method) {
        throw std::runtime_error("ExecutionContext requires a valid method reference");
    }

    const auto& locals = _method->GetLocals();
    _locals.resize(locals.size());
    for (size_t i = 0; i < locals.size(); ++i) {
        _localIndices[locals[i].first] = i;
    }

    // Debug: Print locals mapping for this execution context
    // std::cerr << "[" << _method->GetName() << "] ExecutionContext locals: count=" << locals.size() << std::endl;
    for (size_t i = 0; i < locals.size(); ++i) {
        // std::cerr << "  [" << _method->GetName() << "] local index " << i << " -> '" << locals[i].first << "' (" << locals[i].second.ToString() << ")" << std::endl;
    }

    const auto& parameters = _method->GetParameters();
    _arguments.resize(parameters.size());
    for (size_t i = 0; i < parameters.size(); ++i) {
        _parameterIndices[parameters[i].first] = i;
    }

    // Debug: Print parameters mapping for this execution context
    // std::cerr << "[" << _method->GetName() << "] ExecutionContext parameters: count=" << parameters.size() << std::endl;
    for (size_t i = 0; i < parameters.size(); ++i) {
        // std::cerr << "  [" << _method->GetName() << "] param index " << i << " -> '" << parameters[i].first << "' (" << parameters[i].second.ToString() << ")" << std::endl;
    }
}

void ExecutionContext::PushStack(const Value& value) {
    _stack.push_back(value);
}

Value ExecutionContext::PopStack() {
    if (_stack.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    Value value = _stack.back();
    _stack.pop_back();
    return value;
}

Value ExecutionContext::PeekStack() const {
    if (_stack.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    return _stack.back();
}

std::vector<Value> ExecutionContext::GetStackSnapshot(size_t maxDepth) const {
    std::vector<Value> out;
    if (_stack.empty() || maxDepth == 0) return out;
    const size_t n = _stack.size();
    const size_t take = std::min(maxDepth, n);
    out.reserve(take);
    // Return top-of-stack first (like a typical stack trace view)
    for (size_t i = 0; i < take; ++i) {
        out.push_back(_stack[n - 1 - i]);
    }
    return out;
}

void ExecutionContext::SetLocal(size_t index, const Value& value) {
    if (index >= _locals.size()) {
        _locals.resize(index + 1);
    }
    _locals[index] = value;
}

Value ExecutionContext::GetLocal(size_t index) const {
    if (index >= _locals.size()) {
        throw std::out_of_range("Local variable index out of range");
    }
    return _locals[index];
}

void ExecutionContext::SetLocal(const std::string& name, const Value& value) {
    auto it = _localIndices.find(name);
    if (it == _localIndices.end()) {
        std::cerr << "[" << _method->GetName() << "] SetLocal failed - Local variable not found: '" << name << "'" << std::endl;
        throw std::runtime_error("Local variable not found: " + name);
    }
    // std::cerr << "[" << _method->GetName() << "] SetLocal: '" << name << "' -> index " << it->second << std::endl;
    SetLocal(it->second, value);
}

Value ExecutionContext::GetLocal(const std::string& name) const {
    auto it = _localIndices.find(name);
    if (it == _localIndices.end()) {
        std::cerr << "[" << _method->GetName() << "] GetLocal failed - Local variable not found: '" << name << "'" << std::endl;
        throw std::runtime_error("Local variable not found: " + name);
    }
    // std::cerr << "[" << _method->GetName() << "] GetLocal: '" << name << "' -> index " << it->second << std::endl;
    return GetLocal(it->second);
}

void ExecutionContext::SetArguments(const std::vector<Value>& args) {
    if (args.size() != _arguments.size()) {
        _arguments.resize(args.size());
    }
    std::copy(args.begin(), args.end(), _arguments.begin());
}

Value ExecutionContext::GetArgument(size_t index) const {
    if (index >= _arguments.size()) {
        throw std::out_of_range("Argument index out of range");
    }
    return _arguments[index];
}

Value ExecutionContext::GetArgument(const std::string& name) const {
    // Special case: "this" refers to the implicit object instance
    if (name == "this") {
        return Value(_this);
    }
    
    auto it = _parameterIndices.find(name);
    if (it == _parameterIndices.end()) {
        throw std::runtime_error("Argument not found: " + name);
    }
    return GetArgument(it->second);
}

void ExecutionContext::SetArgument(const std::string& name, const Value& value) {
    auto it = _parameterIndices.find(name);
    if (it == _parameterIndices.end()) {
        throw std::runtime_error("Argument not found: " + name);
    }
    if (it->second >= _arguments.size()) {
        _arguments.resize(it->second + 1);
    }
    _arguments[it->second] = value;
}

// ============================================================================
// VirtualMachine Implementation
// ============================================================================

VirtualMachine::VirtualMachine() = default;

VirtualMachine::~VirtualMachine() {
    UnloadAllPlugins();
}

bool VirtualMachine::LoadPlugin(const std::string& path) {
    if (path.empty()) {
        throw std::runtime_error("Plugin path is empty");
    }

#if defined(_WIN32)
    HMODULE lib = LoadLibraryA(path.c_str());
    if (!lib) {
        throw std::runtime_error("Failed to load plugin library: " + path);
    }
#else
    dlerror();
    void* lib = dlopen(path.c_str(), RTLD_NOW);
    if (!lib) {
        throw std::runtime_error("Failed to load plugin library: " + path + " (" + PluginLoaderError() + ")");
    }
#endif

    // Optional: ABI compatibility handshake.
    // If the plugin exports ObjectIR_PluginGetInfo, validate the requested range before calling init.
    if (auto infoSym = ResolvePluginSymbol(lib, "ObjectIR_PluginGetInfo")) {
        using GetInfoFn = int32_t (*)(ObjectIR_PluginInfoV1* outInfo);
        auto getInfo = reinterpret_cast<GetInfoFn>(infoSym);

        ObjectIR_PluginInfoV1 info{};
        info.structSize = sizeof(ObjectIR_PluginInfoV1);

        int32_t okInfo = 0;
        try {
            okInfo = getInfo(&info);
        } catch (...) {
            okInfo = 0;
        }

        if (!okInfo) {
#if defined(_WIN32)
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
            throw std::runtime_error("Plugin ObjectIR_PluginGetInfo failed: " + path);
        }

        const uint32_t runtimeAbi = OBJECTIR_PLUGIN_ABI_VERSION_PACKED;
        if (info.abiMinPacked && runtimeAbi < info.abiMinPacked) {
#if defined(_WIN32)
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
            throw std::runtime_error("Plugin requires newer plugin ABI than runtime provides: " + path);
        }
        if (info.abiMaxPacked && runtimeAbi > info.abiMaxPacked) {
#if defined(_WIN32)
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
            throw std::runtime_error("Plugin requires older plugin ABI than runtime provides: " + path);
        }
    }

    auto initSym = ResolvePluginSymbol(lib, "ObjectIR_PluginInit");
    if (!initSym) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        throw std::runtime_error("Plugin missing required entry point ObjectIR_PluginInit: " + path);
    }

    auto init = reinterpret_cast<PluginInitFn>(initSym);
    bool ok = false;
    try {
        ok = init(this);
    } catch (const std::exception& ex) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        throw std::runtime_error(std::string("Plugin init threw: ") + ex.what());
    } catch (...) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        throw std::runtime_error("Plugin init threw unknown exception");
    }

    if (!ok) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        throw std::runtime_error("Plugin init returned false: " + path);
    }

    PluginShutdownFn shutdown = nullptr;
    if (auto shutdownSym = ResolvePluginSymbol(lib, "ObjectIR_PluginShutdown")) {
        shutdown = reinterpret_cast<PluginShutdownFn>(shutdownSym);
    }

    auto p = std::make_unique<LoadedPlugin>();
    p->handle = lib;
    p->shutdown = shutdown;
    p->path = path;
    _plugins.push_back(std::move(p));
    return true;
}

void VirtualMachine::UnloadAllPlugins() {
    for (auto it = _plugins.rbegin(); it != _plugins.rend(); ++it) {
        auto& p = *it;
        if (!p) continue;

        if (p->shutdown) {
            try {
                p->shutdown(this);
            } catch (...) {
                // Best-effort shutdown.
            }
        }

#if defined(_WIN32)
        if (p->handle) FreeLibrary(p->handle);
#else
        if (p->handle) dlclose(p->handle);
#endif
        p->handle = nullptr;
    }
    _plugins.clear();
}

std::vector<std::string> VirtualMachine::GetLoadedPluginPaths() const {
    std::vector<std::string> paths;
    paths.reserve(_plugins.size());
    for (const auto& p : _plugins) {
        if (p) paths.push_back(p->path);
    }
    return paths;
}

void VirtualMachine::RegisterClass(ClassRef classType) {
    if (!classType) return;

    const std::string& rawName = classType->GetName();
    const auto lastDot = rawName.find_last_of('.');
    const std::string simpleName = (lastDot == std::string::npos) ? rawName : rawName.substr(lastDot + 1);
    const std::string qualifiedFromFields = TypeNames::GetQualifiedClassName(classType);

    // Register multiple keys for legacy compatibility.
    // - simpleName: for code that references "Program"
    // - rawName: for code that stored fully-qualified names in Class::name
    // - qualifiedFromFields: canonical (namespace + simpleName)
    if (!simpleName.empty()) {
        _classes[simpleName] = classType;
    }
    if (!rawName.empty()) {
        _classes[rawName] = classType;
    }
    if (!qualifiedFromFields.empty()) {
        _classes[qualifiedFromFields] = classType;
    }
}
/// @brief Retrieves a class reference by its name, supporting both simple and qualified names.
/// @param name The name of the class to retrieve.
/// @return A reference to the class.
/// @throws std::runtime_error if the class is not found.
ClassRef VirtualMachine::GetClass(const std::string& name) const {
    auto it = _classes.find(name);
    if (it != _classes.end()) {
        return it->second;
    }
    
    // If not found and name contains a dot, try just the simple name
    size_t lastDot = name.find_last_of('.');
    if (lastDot != std::string::npos) {
        std::string simpleName = name.substr(lastDot + 1);
        auto simpleIt = _classes.find(simpleName);
        if (simpleIt != _classes.end()) {
            return simpleIt->second;
        }
    }
    
    throw std::runtime_error("Class not found: " + name);
}

std::vector<std::string> VirtualMachine::GetAllClassNames() const {
    std::vector<std::string> classNames;
    for (const auto& pair : _classes) {
        classNames.push_back(pair.first);
    }
    // Remove duplicates if any (due to qualified and simple names)
    std::sort(classNames.begin(), classNames.end());
    classNames.erase(std::unique(classNames.begin(), classNames.end()), classNames.end());
    return classNames;
}

bool VirtualMachine::HasClass(const std::string& name) const {
    return _classes.find(name) != _classes.end();
}

ObjectRef VirtualMachine::CreateObject(ClassRef classType) {
    return classType->CreateInstance();
}

ObjectRef VirtualMachine::CreateObject(const std::string& className) {
    return GetClass(className)->CreateInstance();
}

std::shared_ptr<Array> VirtualMachine::CreateArray(const TypeReference& elementType, int32_t length) {
    return std::make_shared<Array>(elementType, length);
}

Value VirtualMachine::InvokeMethod(ObjectRef object, const std::string& methodName, const std::vector<Value>& args) {
    if (!object || !object->GetClass()) {
        throw std::runtime_error("Cannot invoke method on null object");
    }
    
    auto method = object->GetClass()->LookupMethod(methodName);
    if (!method) {
        throw std::runtime_error("Method not found: " + methodName);
    }
    
    auto impl = method->GetNativeImpl();
    if (impl) {
        return impl(object, args, this);
    }

    if (method->HasInstructions()) {
        auto context = std::make_unique<ExecutionContext>(method);
        context->SetThis(object);
        context->SetArguments(args);
        auto* rawContext = context.get();
        PushContext(std::move(context));
        auto result = InstructionExecutor::ExecuteInstructions(method->GetInstructions(), object, args, rawContext, this, method->GetLabelMap());
        PopContext();
        // If method is declared void, ignore residual stack value and return null
        if (method->GetReturnType().IsPrimitive() && method->GetReturnType().GetPrimitiveType() == PrimitiveType::Void) {
            return Value();
        }
        return result;
    }

    throw std::runtime_error("Method has no implementation: " + methodName);
}

namespace {

std::string FormatMethodSignature(const ObjectIR::MethodRef& method) {
    if (!method) return "<null>";
    std::string sig = method->GetName();
    sig.push_back('(');
    const auto& params = method->GetParameters();
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) sig += ", ";
        sig += ObjectIR::TypeNames::CanonicalTypeName(params[i].second);
    }
    sig.push_back(')');
    sig += " -> ";
    sig += ObjectIR::TypeNames::CanonicalTypeName(method->GetReturnType());
    return sig;
}

std::vector<ObjectIR::MethodRef> CollectMethodsByName(ObjectIR::ClassRef cls, const std::string& name) {
    std::vector<ObjectIR::MethodRef> matches;
    for (auto current = cls; current; current = current->GetBaseClass()) {
        for (const auto& method : current->GetAllMethods()) {
            if (method && method->GetName() == name) {
                matches.push_back(method);
            }
        }
    }
    return matches;
}

bool TypeNameMatchesParameter(const std::string& requestedType,
                             const ObjectIR::TypeReference& parameterType) {
    const auto requestedNorm = ObjectIR::TypeNames::NormalizeTypeName(requestedType);
    const auto paramCanon = ObjectIR::TypeNames::CanonicalTypeName(parameterType);
    if (requestedNorm == paramCanon) return true;

    // Legacy fallback: allow simple-name match when request is unqualified.
    if (requestedNorm.find('.') == std::string::npos) {
        const auto dot = paramCanon.find_last_of('.');
        const auto paramSimple = (dot == std::string::npos) ? paramCanon : paramCanon.substr(dot + 1);
        return requestedNorm == paramSimple;
    }

    return false;
}

ObjectIR::MethodRef ResolveOverloadOrThrow(ObjectIR::ClassRef cls,
                                          const ObjectIR::CallTarget& target,
                                          bool requireStatic) {
    if (!cls) {
        throw std::runtime_error("Null class when resolving method: " + target.name);
    }

    const auto methods = CollectMethodsByName(cls, target.name);
    if (methods.empty()) {
        throw std::runtime_error("Method not found: " + target.name);
    }

    // If no signature was provided, only allow resolution when unambiguous.
    if (!target.hasParameterTypes) {
        std::vector<ObjectIR::MethodRef> viable;
        for (const auto& m : methods) {
            if (!m) continue;
            if (requireStatic && !m->IsStatic()) continue;
            viable.push_back(m);
        }
        if (viable.size() == 1) return viable[0];

        std::string msg = "Ambiguous overload for '" + target.name + "'. Provide parameterTypes. Candidates:";
        for (const auto& m : viable) {
            msg += "\n  - ";
            msg += FormatMethodSignature(m);
        }
        throw std::runtime_error(msg);
    }

    const auto requestedParams = ObjectIR::TypeNames::NormalizeTypeNames(target.parameterTypes);

    // Exact signature match (arity + per-parameter type name).
    std::vector<ObjectIR::MethodRef> exact;
    exact.reserve(methods.size());
    for (const auto& m : methods) {
        if (!m) continue;
        if (requireStatic && !m->IsStatic()) continue;

        const auto& params = m->GetParameters();
        if (params.size() != requestedParams.size()) continue;

        bool ok = true;
        for (size_t i = 0; i < params.size(); ++i) {
            if (!TypeNameMatchesParameter(requestedParams[i], params[i].second)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            exact.push_back(m);
        }
    }

    if (exact.size() == 1) {
        return exact[0];
    }
    if (exact.size() > 1) {
        std::string msg = "Ambiguous overload for '" + target.name + "' with provided signature. Candidates:";
        for (const auto& m : exact) {
            msg += "\n  - ";
            msg += FormatMethodSignature(m);
        }
        throw std::runtime_error(msg);
    }

    // Legacy fallback: if exactly one overload matches arity, choose it.
    std::vector<ObjectIR::MethodRef> arity;
    for (const auto& m : methods) {
        if (!m) continue;
        if (requireStatic && !m->IsStatic()) continue;
        if (m->GetParameters().size() == requestedParams.size()) {
            arity.push_back(m);
        }
    }
    if (arity.size() == 1) {
        return arity[0];
    }

    std::string msg = "No matching overload for '" + target.name + "'. Candidates:";
    for (const auto& m : methods) {
        if (!m) continue;
        if (requireStatic && !m->IsStatic()) continue;
        msg += "\n  - ";
        msg += FormatMethodSignature(m);
    }
    throw std::runtime_error(msg);
}

} // namespace

Value VirtualMachine::InvokeMethod(ObjectRef object, const CallTarget& target, const std::vector<Value>& args) {
    if (!object || !object->GetClass()) {
        throw std::runtime_error("Cannot invoke method on null object");
    }

    auto method = ResolveOverloadOrThrow(object->GetClass(), target, /*requireStatic*/false);

    auto impl = method->GetNativeImpl();
    if (impl) {
        return impl(object, args, this);
    }

    if (method->HasInstructions()) {
        auto context = std::make_unique<ExecutionContext>(method);
        context->SetThis(object);
        context->SetArguments(args);
        auto* rawContext = context.get();
        PushContext(std::move(context));
        auto result = InstructionExecutor::ExecuteInstructions(method->GetInstructions(), object, args, rawContext, this, method->GetLabelMap());
        PopContext();
        if (method->GetReturnType().IsPrimitive() && method->GetReturnType().GetPrimitiveType() == PrimitiveType::Void) {
            return Value();
        }
        return result;
    }

    throw std::runtime_error("Method has no implementation: " + target.name);
}

Value VirtualMachine::InvokeStaticMethod(ClassRef classType, const CallTarget& target, const std::vector<Value>& args) {
    auto method = ResolveOverloadOrThrow(classType, target, /*requireStatic*/true);

    auto impl = method->GetNativeImpl();
    if (impl) {
        return impl(nullptr, args, this);
    }

    if (method->HasInstructions()) {
        auto context = std::make_unique<ExecutionContext>(method);
        context->SetArguments(args);
        auto* rawContext = context.get();
        PushContext(std::move(context));
        auto result = InstructionExecutor::ExecuteInstructions(method->GetInstructions(), nullptr, args, rawContext, this, method->GetLabelMap());
        PopContext();
        if (method->GetReturnType().IsPrimitive() && method->GetReturnType().GetPrimitiveType() == PrimitiveType::Void) {
            return Value();
        }
        return result;
    }

    throw std::runtime_error("Method has no implementation: " + target.name);
}

Value VirtualMachine::InvokeStaticMethod(ClassRef classType, const std::string& methodName, const std::vector<Value>& args) {
    auto method = classType->LookupMethod(methodName);
    if (!method) {
        throw std::runtime_error("Static method not found: " + methodName);
    }
    
    auto impl = method->GetNativeImpl();
    if (impl) {
        return impl(nullptr, args, this);
    }

    if (method->HasInstructions()) {
        auto context = std::make_unique<ExecutionContext>(method);
        context->SetArguments(args);
        auto* rawContext = context.get();
        PushContext(std::move(context));
        auto result = InstructionExecutor::ExecuteInstructions(method->GetInstructions(), nullptr, args, rawContext, this, method->GetLabelMap());
        PopContext();
        // If method is declared void, ignore residual stack value and return null
        if (method->GetReturnType().IsPrimitive() && method->GetReturnType().GetPrimitiveType() == PrimitiveType::Void) {
            return Value();
        }
        return result;
    }

    throw std::runtime_error("Method has no implementation: " + methodName);
}

void VirtualMachine::PushContext(std::unique_ptr<ExecutionContext> context) {
    _contextStack.push_back(std::move(_currentContext));
    _currentContext = std::move(context);
}

std::vector<const ExecutionContext*> VirtualMachine::GetCallStackSnapshot() const {
    std::vector<const ExecutionContext*> frames;
    // Most-recent frame first (current context)
    if (_currentContext) {
        frames.push_back(_currentContext.get());
    }
    // Then previous contexts (top of stack is most-recent caller)
    for (auto it = _contextStack.rbegin(); it != _contextStack.rend(); ++it) {
        if (it->get()) {
            frames.push_back(it->get());
        }
    }
    return frames;
}

void VirtualMachine::PopContext() {
    if (!_contextStack.empty()) {
        _currentContext = std::move(_contextStack.back());
        _contextStack.pop_back();
    } else {
        _currentContext = nullptr;
    }
}

json VirtualMachine::ExportClassMetadata(const std::string& name, bool includeInstructions) const {
    auto classRef = GetClass(name);
    json type;
    type["name"] = classRef->GetName();
    type["namespace"] = classRef->GetNamespace();
    type["fullName"] = TypeNames::GetQualifiedClassName(classRef);
    type["kind"] = "class";
    type["isAbstract"] = classRef->IsAbstract();
    type["isSealed"] = classRef->IsSealed();

    json fields = json::array();
    for (const auto& field : classRef->GetAllFields()) {
        json f;
        f["name"] = field->GetName();
        f["type"] = TypeNames::CanonicalTypeName(field->GetType());
        fields.push_back(f);
    }
    type["fields"] = fields;

    json methods = json::array();
    for (const auto& method : classRef->GetAllMethods()) {
        json m;
        m["name"] = method->GetName();
        m["returnType"] = TypeNames::CanonicalTypeName(method->GetReturnType());
        m["isStatic"] = method->IsStatic();
        m["isVirtual"] = method->IsVirtual();

        json params = json::array();
        for (const auto& param : method->GetParameters()) {
            json p;
            p["name"] = param.first;
            p["type"] = TypeNames::CanonicalTypeName(param.second);
            params.push_back(p);
        }
        m["parameters"] = params;

        json locals = json::array();
        for (const auto& local : method->GetLocals()) {
            json l;
            l["name"] = local.first;
            l["type"] = local.second.ToString();
            locals.push_back(l);
        }
        if (!locals.empty()) {
            m["locals"] = locals;
        }

        if (includeInstructions && method->HasInstructions()) {
            m["instructions"] = SerializeInstructionBlock(method->GetInstructions(), true);
        }

        methods.push_back(m);
    }
    type["methods"] = methods;

    return type;
}

json VirtualMachine::ExportMetadata(bool includeInstructions) const {
    json module;
    module["types"] = json::array();

    std::unordered_set<const Class*> seen;
    for (const auto& entry : _classes) {
        const Class* rawPtr = entry.second.get();
        if (seen.find(rawPtr) != seen.end()) {
            continue;
        }
        seen.insert(rawPtr);
        module["types"].push_back(ExportClassMetadata(QualifiedName(entry.second), includeInstructions));
    }

    return module;
}

// ============================================================================
// RuntimeBuilder Implementation
// ============================================================================

RuntimeBuilder& RuntimeBuilder::Class(const std::string& name) {
    _currentClass = std::make_shared<::ObjectIR::Class>(name);
    _vm->RegisterClass(_currentClass);
    return *this;
}

RuntimeBuilder& RuntimeBuilder::Field(const std::string& name, const TypeReference& type) {
    if (!_currentClass) {
        throw std::runtime_error("No current class");
    }
    auto field = std::make_shared<::ObjectIR::Field>(name, type);
    _currentClass->AddField(field);
    return *this;
}

RuntimeBuilder& RuntimeBuilder::Method(const std::string& name, const TypeReference& returnType, bool isStatic) {
    if (!_currentClass) {
        throw std::runtime_error("No current class");
    }
    _currentMethod = std::make_shared<::ObjectIR::Method>(name, returnType, isStatic);
    return *this;
}

RuntimeBuilder& RuntimeBuilder::Parameter(const std::string& name, const TypeReference& type) {
    if (!_currentMethod) {
        throw std::runtime_error("No current method");
    }
    _currentMethod->AddParameter(name, type);
    return *this;
}

RuntimeBuilder& RuntimeBuilder::NativeImpl(NativeMethodImpl impl) {
    if (!_currentMethod) {
        throw std::runtime_error("No current method");
    }
    _currentMethod->SetNativeImpl(impl);
    return *this;
}

RuntimeBuilder& RuntimeBuilder::EndMethod() {
    if (_currentClass && _currentMethod) {
        _currentClass->AddMethod(_currentMethod);
        _currentMethod = nullptr;
    }
    return *this;
}

RuntimeBuilder& RuntimeBuilder::EndClass() {
    _currentClass = nullptr;
    return *this;
}
} // namespace ObjectIR
