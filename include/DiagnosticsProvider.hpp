#pragma once

#include <string>
#include <vector>

#include "objectir_runtime.hpp"

namespace ObjectIR {

class OBJECTIR_API Diagnostics {
public:
    enum class Level { Info, Warning, Error, Debug };

    // Legacy signature (kept for compatibility with any existing callers)
    void Info(std::string header, std::vector<Value> lastStackContents, std::string contents);
    void Warning(std::string header, std::vector<Value> lastStackContents, std::string contents);
    void Error(std::string header, std::vector<Value> lastStackContents, std::string contents);
    void Debug(std::string header, std::vector<Value> lastStackContents, std::string contents);

    // Preferred overloads: pull call stack + eval stack directly from the VM.
    void Info(const VirtualMachine* vm, const std::string& header, const std::string& contents);
    void Warning(const VirtualMachine* vm, const std::string& header, const std::string& contents);
    void Error(const VirtualMachine* vm, const std::string& header, const std::string& contents);
    void Debug(const VirtualMachine* vm, const std::string& header, const std::string& contents);

private:
    void Emit(Level level, const VirtualMachine* vm, const std::string& header, const std::string& contents, const std::vector<Value>* lastStack);
};

} // namespace ObjectIR
