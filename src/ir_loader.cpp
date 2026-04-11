#include "ir_loader.hpp"
#include "fob_loader.hpp"
#include "instruction_executor.hpp"
#include "stdlib.hpp"
#include "ir_text_parser.hpp"
#include "objectir_type_names.hpp"
#include <fstream>
#include <iostream>
#include <codecvt>
#include <algorithm>
#include <locale>

namespace ObjectIR {

namespace {

std::string NormalizeToUtf8(const std::string& content) {
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        // UTF-8 BOM
        return content.substr(3);
    }

    if (content.size() >= 2 &&
        static_cast<unsigned char>(content[0]) == 0xFF &&
        static_cast<unsigned char>(content[1]) == 0xFE) {
        // UTF-16 LE BOM
        std::u16string u16;
        u16.reserve((content.size() - 2) / 2);
        for (size_t i = 2; i + 1 < content.size(); i += 2) {
            const auto lo = static_cast<unsigned char>(content[i]);
            const auto hi = static_cast<unsigned char>(content[i + 1]);
            const char16_t ch = static_cast<char16_t>(lo | (hi << 8));
            u16.push_back(ch);
        }
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
        return conv.to_bytes(u16);
    }

    if (content.size() >= 2 &&
        static_cast<unsigned char>(content[0]) == 0xFE &&
        static_cast<unsigned char>(content[1]) == 0xFF) {
        // UTF-16 BE BOM
        std::u16string u16;
        u16.reserve((content.size() - 2) / 2);
        for (size_t i = 2; i + 1 < content.size(); i += 2) {
            const auto hi = static_cast<unsigned char>(content[i]);
            const auto lo = static_cast<unsigned char>(content[i + 1]);
            const char16_t ch = static_cast<char16_t>(lo | (hi << 8));
            u16.push_back(ch);
        }
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
        return conv.to_bytes(u16);
    }

    return content;
}

} // namespace

std::shared_ptr<VirtualMachine> IRLoader::LoadFromFile(const std::string& filePath) {
    // Auto-detect format
    if (IsFOBFormat(filePath)) {
        auto result = FOBLoader::LoadFromFile(filePath);
        return result.vm;
    } else {
        // Prefer textual ObjectIR for .ir, JSON for .json, otherwise best-effort auto-detect.
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open IR file: " + filePath);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string content = NormalizeToUtf8(buffer.str());

        // Extension-based hinting prevents the text parser from accidentally accepting JSON.
        std::string lowerPath = filePath;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        if (lowerPath.size() >= 5 && lowerPath.rfind(".json") == (lowerPath.size() - 5)) {
            return LoadFromString(content);
        }
        if (lowerPath.size() >= 3 && lowerPath.rfind(".ir") == (lowerPath.size() - 3)) {
            return LoadFromText(content);
        }

        // Try text IR first
        try {
            return LoadFromText(content);
        } catch (const std::exception& textErr) {
            std::cerr << "[IRLoader] Text parse failed, falling back to JSON: " << textErr.what() << std::endl;
        }

        // Fallback: assume JSON
        return LoadFromString(content);
    }
}

std::shared_ptr<VirtualMachine> IRLoader::LoadFromString(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        return ParseModule(j);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }
}

std::shared_ptr<VirtualMachine> IRLoader::LoadFromText(const std::string& irText) {
    auto moduleJson = IRTextParser::ParseToJson(irText);
    return ParseModule(moduleJson);
}

std::shared_ptr<VirtualMachine> IRLoader::LoadFromFOBData(const std::vector<uint8_t>& data) {
    auto result = FOBLoader::LoadFromData(data);
    return result.vm;
}

bool IRLoader::IsFOBFormat(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    char magic[3];
    file.read(magic, 3);
    file.close();

    return (magic[0] == 'F' && magic[1] == 'O' && magic[2] == 'B');
}

std::shared_ptr<VirtualMachine> IRLoader::ParseModule(const json& moduleJson) {
    auto vm = std::make_shared<VirtualMachine>();

    // Register standard library types and methods
    RegisterStandardLibrary(vm);

    // Load types
    if (moduleJson.contains("types")) {
        LoadTypes(vm, moduleJson["types"]);
    }

    return vm;
}

void IRLoader::LoadTypes(std::shared_ptr<VirtualMachine> vm, const json& typesArray) {
    for (const auto& typeJson : typesArray) {
        LoadTypeDefinition(vm, typeJson);
    }
}

void IRLoader::LoadTypeDefinition(std::shared_ptr<VirtualMachine> vm, const json& typeJson) {
    std::string kind = typeJson["kind"];
    // Convert to lowercase for case-insensitive comparison
    std::transform(kind.begin(), kind.end(), kind.begin(), ::tolower);
    

    if (kind == "class") {
        LoadClass(vm, typeJson);
    } else if (kind == "interface") {
        LoadInterface(vm, typeJson);
    } else if (kind == "struct") {
        LoadStruct(vm, typeJson);
    }
}

ClassRef IRLoader::LoadClass(std::shared_ptr<VirtualMachine> vm, const json& classJson) {
    std::string name = classJson["name"];
    std::string ns = classJson.value("namespace", "");

    std::string fullName = GetFQTypeName(name, ns);
    auto classRef = std::make_shared<Class>(name);  // Pass simple name to constructor
    classRef->SetNamespace(ns);  // Set namespace separately

    // Load base class if present
    if (classJson.contains("base")) {
        std::string baseName = classJson["base"];
        // TODO: Resolve base class
    }

    // Load interfaces if present
    if (classJson.contains("interfaces")) {
        // TODO: Load interfaces
    }

    // Load fields
    if (classJson.contains("fields")) {
        LoadFields(classRef, classJson["fields"], vm);
    }

    // Load methods
    if (classJson.contains("methods")) {
        LoadMethods(classRef, classJson["methods"], vm);
    }

    vm->RegisterClass(classRef);
    return classRef;
}

void IRLoader::LoadInterface(std::shared_ptr<VirtualMachine> vm, const json& interfaceJson) {
    // TODO: Implement interface loading
}

void IRLoader::LoadStruct(std::shared_ptr<VirtualMachine> vm, const json& structJson) {
    // TODO: Implement struct loading
}

void IRLoader::LoadFields(ClassRef classRef, const json& fieldsArray, std::shared_ptr<VirtualMachine> vm) {
    for (const auto& fieldJson : fieldsArray) {
        std::string name = fieldJson["name"];
        std::string typeStr = fieldJson["type"];
        TypeReference typeRef = ParseTypeReference(vm, typeStr);

        auto field = std::make_shared<Field>(name, typeRef);
        classRef->AddField(field);
    }
}

void IRLoader::LoadMethods(ClassRef classRef, const json& methodsArray, std::shared_ptr<VirtualMachine> vm) {
    for (const auto& methodJson : methodsArray) {
        std::string name = methodJson["name"];
        std::string returnTypeStr = methodJson["returnType"];
        TypeReference returnType = ParseTypeReference(vm, returnTypeStr);

        bool isStatic = methodJson.value("isStatic", false);
        auto method = std::make_shared<Method>(name, returnType, isStatic);

        if (methodJson.contains("parameters")) {
            for (const auto& paramJson : methodJson["parameters"]) {
                std::string paramName = paramJson["name"];
                std::string paramTypeStr = paramJson["type"];
                TypeReference paramType = ParseTypeReference(vm, paramTypeStr);
                method->AddParameter(paramName, paramType);
            }
        }

        // Load locals
        // Accept both lower-case and PascalCase JSON produced by different emitters
        const json* localsArray = nullptr;
        if (methodJson.contains("localVariables")) {
            localsArray = &methodJson["localVariables"];
        } else if (methodJson.contains("LocalVariables")) {
            localsArray = &methodJson["LocalVariables"];
        }

        if (localsArray) {
            for (const auto& localJson : *localsArray) {
                std::string localName = localJson["name"];
                std::string localTypeStr = localJson["type"];
                TypeReference localType = ParseTypeReference(vm, localTypeStr);
                method->AddLocal(localName, localType);
                std::cerr << "  [" << name << "] Added local: " << localName << " (" << localType.ToString() << ")" << std::endl;
            }
            // std::cerr << "  [" << name << "] Total locals: " << method->GetLocals().size() << std::endl;
        }

        // Load label map for branch resolution
        if (methodJson.contains("labelMap") && methodJson["labelMap"].is_object()) {
            std::unordered_map<std::string, size_t> labelMap;
            const auto& labelMapJson = methodJson["labelMap"];
            for (const auto& [labelName, targetIndex] : labelMapJson.items()) {
                labelMap[labelName] = targetIndex.get<size_t>();
                // std::cerr << "  [" << name << "] Added label: " << labelName << " -> instruction " << targetIndex.get<size_t>() << std::endl;
            }
            method->SetLabelMap(labelMap);
        }

        // Load method body/instructions
        if (methodJson.contains("instructions") && methodJson["instructions"].is_array()) {
            std::vector<Instruction> instructions;
            int instrCount = 0;
            for (const auto& instrJson : methodJson["instructions"]) {
                try {
                    // std::cerr << "  [" << name << "] Parsing instruction " << instrCount << ": "
                    //           << instrJson.dump() << std::endl;
                    Instruction instr = InstructionExecutor::ParseJsonInstruction(instrJson);
                    instructions.push_back(instr);
                    // std::cerr << "  [" << name << "] ✓ Instruction " << instrCount << " parsed successfully" << std::endl;
                } catch (const std::exception& e) {
                    // If instruction parsing fails, log but continue
                    // The method will have partial instructions rather than failing completely
                    std::cerr << "ERROR [" << name << "] Failed to parse instruction " << instrCount
                              << ": " << e.what() << std::endl;
                    std::cerr << "  Instruction JSON: " << instrJson.dump() << std::endl;
                    std::cerr << "  Instruction type: " << instrJson.type_name() << std::endl;
                }
                instrCount++;
            }
            if (!instructions.empty()) {
                // std::cerr << "[" << name << "] Setting " << instructions.size() << " instructions on method" << std::endl;
                // Debug: Print a summary of parsed instruction operands
                for (size_t i = 0; i < instructions.size(); ++i) {
                    const auto& instr = instructions[i];
                    // std::cerr << "  [" << name << "] Parsed instr " << i << ": op=" << static_cast<int>(instr.opCode);
                    // if (!instr.identifier.empty()) std::cerr << " id='" << instr.identifier << "'";
                    // if (!instr.operandString.empty()) std::cerr << " operand='" << instr.operandString << "'";
                    // if (instr.fieldTarget.has_value()) std::cerr << " field='" << instr.fieldTarget->name << "'";
                    // if (instr.callTarget.has_value()) std::cerr << " call='" << instr.callTarget->name << "'";
                    // std::cerr << std::endl;
                }
                method->SetInstructions(std::move(instructions));
            }
        }

        classRef->AddMethod(method);
    }
}

TypeReference IRLoader::ParseTypeReference(std::shared_ptr<VirtualMachine> vm, const std::string& typeStr) {
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

    // User-defined types: best-effort lookup.
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

std::string IRLoader::GetFQTypeName(const std::string& name, const std::string& ns) {
    return ns.empty() ? name : ns + "." + name;
}

} // namespace ObjectIR
