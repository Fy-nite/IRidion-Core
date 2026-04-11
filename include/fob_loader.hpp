#pragma once

#include "objectir_runtime.hpp"
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>

namespace ObjectIR
{

// ============================================================================
// FOB/IR v3 Loader
// Binary format produced/consumed by ObjectIR.Core's FobIrCompiler (C#).
//
// File layout  (24-byte header + three sections):
//   [Header]
//     6 bytes  – ASCII magic "FOB/IR"
//     2 bytes  – uint16 format version (= 3)
//     4 bytes  – uint32 includesOffset   (absolute file position)
//     4 bytes  – uint32 stringDataOffset (absolute file position)
//     4 bytes  – uint32 payloadOffset    (absolute file position)
//     4 bytes  – uint32 payloadLength    (byte count of payload)
//
//   [Includes]   @ includesOffset
//     4 bytes  – uint32 count
//     count×4  – uint32 offsets into StringData blob
//
//   [StringData] @ stringDataOffset
//     4 bytes  – uint32 total byte length of packed string pool
//     N bytes  – null-terminated packed UTF-8 strings
//
//   [Payload]    @ payloadOffset
//     payloadLength bytes – compact binary module bytecode (see payload spec)
// ============================================================================

/// Loads ObjectIR modules from the FOB/IR v3 binary format.
class OBJECTIR_API FOBLoader
{
public:
    // ── Public result type ────────────────────────────────────────────────────
    struct FOBLoadResult {
        std::shared_ptr<VirtualMachine> vm;
        uint16_t entryTypeIndex   = 0;
        uint16_t entryMethodIndex = 0;
        std::vector<std::string>              classNames;
        std::vector<std::vector<std::string>> methodNames; ///< [classIdx][methodIdx]
    };

    static FOBLoadResult LoadFromFile(const std::string &filePath);
    static FOBLoadResult LoadFromData(const std::vector<uint8_t> &data);

    // ── Format constants ──────────────────────────────────────────────────────
    static constexpr uint16_t FORMAT_VERSION = 3;
    static constexpr const char MAGIC[7]     = "FOB/IR"; ///< 6 usable bytes
    static constexpr size_t    MAGIC_SIZE    = 6;
    static constexpr size_t    HEADER_SIZE   = 24;       ///< magic(6)+ver(2)+4×uint32(16)
    static constexpr uint32_t  NULL_IDX      = 0xFFFFFFFFu; ///< "no value" sentinel

    // ── Type / access / flag byte constants ───────────────────────────────────
    static constexpr uint8_t  KIND_CLASS      = 0;
    static constexpr uint8_t  KIND_INTERFACE  = 1;
    static constexpr uint8_t  KIND_STRUCT     = 2;
    static constexpr uint8_t  KIND_ENUM       = 3;

    static constexpr uint8_t  ACCESS_PUBLIC    = 0;
    static constexpr uint8_t  ACCESS_PRIVATE   = 1;
    static constexpr uint8_t  ACCESS_PROTECTED = 2;
    static constexpr uint8_t  ACCESS_INTERNAL  = 3;

    static constexpr uint16_t TYPE_ABSTRACT = 0x01;
    static constexpr uint16_t TYPE_SEALED   = 0x02;

    static constexpr uint8_t  FIELD_STATIC   = 0x01;
    static constexpr uint8_t  FIELD_READONLY = 0x02;

    static constexpr uint8_t  METHOD_STATIC      = 0x01;
    static constexpr uint8_t  METHOD_VIRTUAL     = 0x02;
    static constexpr uint8_t  METHOD_ABSTRACT    = 0x04;
    static constexpr uint8_t  METHOD_OVERRIDE    = 0x08;
    static constexpr uint8_t  METHOD_CONSTRUCTOR = 0x10;

    static constexpr uint8_t  COND_STACK      = 0;
    static constexpr uint8_t  COND_BINARY     = 1;
    static constexpr uint8_t  COND_EXPRESSION = 2;

    // ── Serialised opcode values (match ObjectIR.Core.IR.OpCode enum) ─────────
    enum class SerializedOpCode : uint8_t {
        Ldarg = 0, Ldloc, Ldfld, Ldsfld, Ldelem, Ldlen, Ldnull,
        LdcI4, LdcI8, LdcR4, LdcR8, Ldstr,
        Starg, Stloc, Stfld, Stsfld, Stelem,
        Add, Sub, Mul, Div, Rem, Neg, And, Or, Xor, Not, Shl, Shr,
        Ceq, Cgt, Clt,
        Br, Brtrue, Brfalse, Beq, Bne, Bgt, Blt, Ret,
        Call, Callvirt, Calli, Newobj,
        Newarr, Castclass, Isinst, Box, Unbox,
        Dup, Pop,
        ConvI4, ConvI8, ConvR4, ConvR8, ConvU4, ConvU8,
        If, While, For, Switch, Try, Break, Continue, Throw
    };

    // ── Decoded intermediate structures ───────────────────────────────────────
    struct DecodedField {
        uint32_t nameIndex     = NULL_IDX;
        uint32_t typeNameIndex = NULL_IDX;
        uint8_t  access        = ACCESS_PUBLIC;
        uint8_t  flags         = 0;
    };

    struct DecodedParam {
        uint32_t nameIndex     = NULL_IDX;
        uint32_t typeNameIndex = NULL_IDX;
    };

    struct DecodedLocal {
        uint32_t nameIndex     = NULL_IDX;
        uint32_t typeNameIndex = NULL_IDX;
    };

    struct DecodedInstruction;  // forward-declared for recursive structures

    struct DecodedCondition {
        uint8_t  kind  = COND_STACK;
        uint8_t  compOp = 0;  ///< comparison op when kind == COND_BINARY
        std::vector<DecodedInstruction> setupExprs; ///< for COND_EXPRESSION
    };

    struct DecodedInstruction {
        SerializedOpCode opcode = SerializedOpCode::Ret;
        // Scalar operands
        int32_t  i32  = 0;
        int64_t  i64  = 0;
        float    f32  = 0.f;
        double   f64  = 0.0;
        // Index operands (into payload string pool)
        uint32_t idx0 = NULL_IDX;
        uint32_t idx1 = NULL_IDX;
        uint32_t idx2 = NULL_IDX;
        std::vector<uint32_t> extraIndices; ///< call parameter type indices
        // Structured control flow
        DecodedCondition               condition;
        std::vector<DecodedInstruction> thenBlock;
        std::vector<DecodedInstruction> elseBlock;
        std::vector<DecodedInstruction> bodyBlock; ///< while body
    };

    struct DecodedMethod {
        uint32_t nameIndex        = NULL_IDX;
        uint32_t returnTypeIndex  = NULL_IDX;
        uint8_t  access           = ACCESS_PUBLIC;
        uint8_t  flags            = 0;
        std::vector<DecodedParam>        parameters;
        std::vector<DecodedLocal>        locals;
        std::vector<DecodedInstruction>  instructions;
    };

    struct DecodedType {
        uint8_t  kind           = KIND_CLASS;
        uint8_t  access         = ACCESS_PUBLIC;
        uint16_t typeFlags      = 0;
        uint32_t nameIndex      = NULL_IDX;
        uint32_t namespaceIndex = NULL_IDX;
        uint32_t baseTypeIndex  = NULL_IDX;
        std::vector<uint32_t>      interfaceIndices;
        std::vector<DecodedField>  fields;
        std::vector<DecodedMethod> methods;
    };

    struct DecodedModule {
        uint32_t moduleNameIndex = 0;
        uint16_t versionMajor = 1, versionMinor = 0, versionPatch = 0;
        uint32_t entryPoint   = NULL_IDX;
        std::vector<std::string>  strings;   ///< payload-local string pool
        std::vector<DecodedType>   types;
        std::vector<DecodedMethod> functions; ///< module-level (not in a type)
    };

private:
    // ── File-level parsing ────────────────────────────────────────────────────
    struct FobFileInfo {
        uint16_t             version = FORMAT_VERSION;
        std::vector<std::string> includes;   ///< external type dependencies
        std::vector<uint8_t>     payload;    ///< raw binary bytecode
    };

    static FobFileInfo   ParseFobFile(const uint8_t* data, size_t size);

    // ── Payload parsing ───────────────────────────────────────────────────────
    static DecodedModule  ParsePayload(const std::vector<uint8_t>& payload);
    static DecodedType    ParseType(std::istream& s);
    static DecodedMethod  ParseMethod(std::istream& s);
    static std::vector<DecodedInstruction> ParseInstructionBlock(std::istream& s);
    static DecodedInstruction ParseInstruction(std::istream& s);
    static DecodedCondition   ParseCondition(std::istream& s);

    // ── VM construction ───────────────────────────────────────────────────────
    static FOBLoadResult BuildVM(const DecodedModule& mod,
                                 const std::vector<std::string>& includes);

    static TypeReference ResolveType(const std::string& typeName,
                                     std::shared_ptr<VirtualMachine> vm);
    static Instruction   LowerInstruction(const DecodedInstruction& di,
                                          const std::vector<std::string>& pool);
    static Instruction::ConditionData LowerCondition(const DecodedCondition& dc,
                                                     const std::vector<std::string>& pool);

    // ── Primitive read helpers ────────────────────────────────────────────────
    static uint8_t  ReadU8 (std::istream& s);
    static uint16_t ReadU16(std::istream& s);
    static uint32_t ReadU32(std::istream& s);
    static int32_t  ReadI32(std::istream& s);
    static int64_t  ReadI64(std::istream& s);
    static float    ReadF32(std::istream& s);
    static double   ReadF64(std::istream& s);
    static std::string ReadLengthPrefixedString(std::istream& s);
};

} // namespace ObjectIR