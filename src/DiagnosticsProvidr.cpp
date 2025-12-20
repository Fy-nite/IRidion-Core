#include "DiagnosticsProvider.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace ObjectIR {

namespace {

struct Ansi {
    const char* reset = "\x1b[0m";
    const char* bold = "\x1b[1m";
    const char* dim = "\x1b[2m";
    const char* red = "\x1b[31m";
    const char* green = "\x1b[32m";
    const char* yellow = "\x1b[33m";
    const char* blue = "\x1b[34m";
    const char* magenta = "\x1b[35m";
    const char* cyan = "\x1b[36m";
    const char* gray = "\x1b[90m";
};

bool EnableVirtualTerminalIfPossible() {
#if defined(_WIN32)
    static bool attempted = false;
    static bool enabled = false;
    if (attempted) return enabled;
    attempted = true;

    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return false;

    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return false;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(h, mode)) return false;
    enabled = true;
    return true;
#else
    return true;
#endif
}

std::string LevelName(Diagnostics::Level level) {
    switch (level) {
        case Diagnostics::Level::Info: return "INFO";
        case Diagnostics::Level::Warning: return "WARN";
        case Diagnostics::Level::Error: return "ERROR";
        case Diagnostics::Level::Debug: return "DEBUG";
        default: return "LOG";
    }
}

const char* LevelColor(Diagnostics::Level level, const Ansi& a) {
    switch (level) {
        case Diagnostics::Level::Info: return a.cyan;
        case Diagnostics::Level::Warning: return a.yellow;
        case Diagnostics::Level::Error: return a.red;
        case Diagnostics::Level::Debug: return a.magenta;
        default: return a.gray;
    }
}

std::string ValueToPrettyString(const Value& v, const Ansi& a, bool useColor) {
    std::ostringstream oss;
    auto color = [&](const char* c) { if (useColor) oss << c; };

    if (v.IsNull()) {
        color(a.gray);
        oss << "null";
        color(a.reset);
        return oss.str();
    }
    if (v.IsBool()) {
        color(a.yellow);
        oss << (v.AsBool() ? "true" : "false");
        color(a.reset);
        return oss.str();
    }
    if (v.IsInt32()) {
        color(a.green);
        oss << "int32 " << v.AsInt32();
        color(a.reset);
        return oss.str();
    }
    if (v.IsInt64()) {
        color(a.green);
        oss << "int64 " << v.AsInt64();
        color(a.reset);
        return oss.str();
    }
    if (v.IsFloat32()) {
        color(a.green);
        oss << "float32 " << v.AsFloat32();
        color(a.reset);
        return oss.str();
    }
    if (v.IsFloat64()) {
        color(a.green);
        oss << "float64 " << v.AsFloat64();
        color(a.reset);
        return oss.str();
    }
    if (v.IsString()) {
        color(a.blue);
        oss << "string \"";
        color(a.reset);
        // Keep strings readable (truncate long lines)
        std::string s = v.AsString();
        if (s.size() > 160) {
            s = s.substr(0, 157) + "...";
        }
        oss << s;
        color(a.blue);
        oss << "\"";
        color(a.reset);
        return oss.str();
    }
    if (v.IsObject()) {
        ObjectRef obj = v.AsObject();
        std::string className = "<object>";
        if (obj && obj->GetClass()) {
            className = obj->GetClass()->GetName();
        }
        color(a.magenta);
        oss << className;
        color(a.reset);
        oss << " @";
        color(a.gray);
        oss << reinterpret_cast<uintptr_t>(obj.get());
        color(a.reset);
        return oss.str();
    }

    oss << "<value>";
    return oss.str();
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
        case OpCode::NewObj: return "newobj";
        case OpCode::Call: return "call";
        case OpCode::CallVirt: return "callvirt";
        case OpCode::CastClass: return "castclass";
        case OpCode::IsInst: return "isinst";
        case OpCode::NewArr: return "newarr";
        case OpCode::LdElem: return "ldelem";
        case OpCode::StElem: return "stelem";
        case OpCode::LdLen: return "ldlen";
        case OpCode::Throw: return "throw";
        case OpCode::While: return "while";
        case OpCode::If: return "if";
        default: return std::to_string(static_cast<int>(op));
    }
}

std::string FrameToString(const ExecutionContext* ctx) {
    if (!ctx) return "<null frame>";
    const auto m = ctx->GetMethod();
    std::string methodName = m ? m->GetName() : "<unknown>";

    std::string owner = "<static>";
    if (auto th = ctx->GetThis()) {
        if (auto cls = th->GetClass()) {
            owner = cls->GetName();
        }
    }
    std::string out = owner + "." + methodName + "()";
    if (ctx->HasLastInstruction()) {
        out += " [ip=" + std::to_string(ctx->GetLastIp()) + " op=" + OpCodeToString(ctx->GetLastOpCode()) + "]";
    }
    return out;
}

} // namespace

void Diagnostics::Emit(Level level,
                       const VirtualMachine* vm,
                       const std::string& header,
                       const std::string& contents,
                       const std::vector<Value>* lastStack) {
    const Ansi a;
    const bool useColor = EnableVirtualTerminalIfPossible();

    const char* lvlColor = LevelColor(level, a);
    const std::string lvlName = LevelName(level);

    auto out = [&](const std::string& s) { std::cerr << s; };

    // Title line
    {
        std::ostringstream oss;
        if (useColor) oss << a.bold << lvlColor;
        oss << "\n== " << lvlName << " ==";
        if (useColor) oss << a.reset;
        if (!header.empty()) {
            oss << " ";
            if (useColor) oss << a.bold;
            oss << header;
            if (useColor) oss << a.reset;
        }
        oss << "\n";
        out(oss.str());
    }

    if (!contents.empty()) {
        std::ostringstream oss;
        if (useColor) oss << a.gray;
        oss << contents;
        if (useColor) oss << a.reset;
        oss << "\n";
        out(oss.str());
    }

    // Call stack (C#-style, most recent first)
    if (vm) {
        const auto frames = vm->GetCallStackSnapshot();
        if (!frames.empty()) {
            std::ostringstream oss;
            if (useColor) oss << a.bold << a.cyan;
            oss << "\nCall Stack (most recent call first):\n";
            if (useColor) oss << a.reset;

            for (size_t i = 0; i < frames.size(); ++i) {
                if (useColor) oss << a.gray;
                oss << "  #" << i << " ";
                if (useColor) oss << a.reset;
                if (useColor) oss << a.bold;
                oss << FrameToString(frames[i]);
                if (useColor) oss << a.reset;
                oss << "\n";
            }
            out(oss.str());
        }
    }

    // Eval stack snapshot (top N)
    std::vector<Value> snapshot;
    if (lastStack) {
        snapshot = *lastStack;
    } else if (vm && vm->GetCurrentContext()) {
        snapshot = vm->GetCurrentContext()->GetStackSnapshot(16);
    }

    if (!snapshot.empty()) {
        std::ostringstream oss;
        if (useColor) oss << a.bold << a.cyan;
        oss << "\nEval Stack (top first):\n";
        if (useColor) oss << a.reset;
        for (size_t i = 0; i < snapshot.size(); ++i) {
            if (useColor) oss << a.gray;
            oss << "  [" << i << "] ";
            if (useColor) oss << a.reset;
            oss << ValueToPrettyString(snapshot[i], a, useColor) << "\n";
        }
        out(oss.str());
    }
}

void Diagnostics::Info(std::string header, std::vector<Value> lastStackContents, std::string contents) {
    Emit(Level::Info, nullptr, header, contents, &lastStackContents);
}

void Diagnostics::Warning(std::string header, std::vector<Value> lastStackContents, std::string contents) {
    Emit(Level::Warning, nullptr, header, contents, &lastStackContents);
}

void Diagnostics::Error(std::string header, std::vector<Value> lastStackContents, std::string contents) {
    Emit(Level::Error, nullptr, header, contents, &lastStackContents);
}

void Diagnostics::Debug(std::string header, std::vector<Value> lastStackContents, std::string contents) {
    Emit(Level::Debug, nullptr, header, contents, &lastStackContents);
}

void Diagnostics::Info(const VirtualMachine* vm, const std::string& header, const std::string& contents) {
    Emit(Level::Info, vm, header, contents, nullptr);
}

void Diagnostics::Warning(const VirtualMachine* vm, const std::string& header, const std::string& contents) {
    Emit(Level::Warning, vm, header, contents, nullptr);
}

void Diagnostics::Error(const VirtualMachine* vm, const std::string& header, const std::string& contents) {
    Emit(Level::Error, vm, header, contents, nullptr);
}

void Diagnostics::Debug(const VirtualMachine* vm, const std::string& header, const std::string& contents) {
    Emit(Level::Debug, vm, header, contents, nullptr);
}

} // namespace ObjectIR