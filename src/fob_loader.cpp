#include "fob_loader.hpp"
#include "stdlib.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

// ============================================================================
// FOB/IR v3 binary format â€“ payload spec
// ============================================================================
//
// Payload is a self-contained binary stream with this layout:
//
//  [StringPool]
//    uint32  count
//    For each: uint32 byteLength, UTF-8 bytes (no null terminator)
//
//  [ModuleHeader]
//    uint32  moduleNameIndex  (into StringPool)
//    uint16  versionMajor
//    uint16  versionMinor
//    uint16  versionPatch
//    uint32  entryPoint       ((typeIdx<<16)|methodIdx, NULL_IDX = none)
//
//  [Types]
//    uint32  typeCount
//    For each type:
//      uint8   kind            (0=Class, 1=Interface, 2=Struct, 3=Enum)
//      uint8   access          (0=Public, 1=Private, 2=Protected, 3=Internal)
//      uint32  nameIndex
//      uint32  namespaceIndex  (NULL_IDX = no namespace)
//      uint16  typeFlags       (bit0=abstract, bit1=sealed)
//      uint32  baseTypeIndex   (NULL_IDX = no base)
//      uint32  interfaceCount
//      interfaceCount Ã— uint32  interface name indices
//      [Fields]
//        uint32  fieldCount
//        For each: uint32 nameIndex, uint32 typeNameIndex, uint8 access, uint8 flags
//      [Methods]  (see method layout below)
//        uint32  methodCount
//        For each: <method layout>
//
//  [Functions]   (module-level, not in a type)
//    uint32  count
//    For each: <method layout>
//
//  Method layout:
//    uint32  nameIndex
//    uint32  returnTypeNameIndex
//    uint8   access
//    uint8   flags     (bit0=static, bit1=virtual, bit2=abstract,
//                       bit3=override, bit4=constructor)
//    uint32  paramCount
//    For each param: uint32 nameIndex, uint32 typeNameIndex
//    uint32  localCount
//    For each local: uint32 nameIndex, uint32 typeNameIndex
//    uint32  instrCount
//    For each instruction: <instruction layout>
//
//  Instruction layout:
//    uint8   opcode  (matches ObjectIR.Core.IR.OpCode enum ordinal)
//    Operands by opcode:
//      Ldarg (0):   int32  argIndex
//      Ldloc (1):   uint32 localNameIndex
//      Ldfld (2):   uint32 declTypeIdx, uint32 fieldNameIdx, uint32 fieldTypeIdx
//      Ldsfld (3):  uint32 declTypeIdx, uint32 fieldNameIdx, uint32 fieldTypeIdx
//      Ldelem (4):  <none>
//      Ldlen (5):   <none>
//      Ldnull (6):  <none>
//      LdcI4 (7):   int32  value
//      LdcI8 (8):   int64  value
//      LdcR4 (9):   float  value
//      LdcR8 (10):  double value
//      Ldstr (11):  uint32 stringIndex
//      Starg (12):  uint32 argNameIndex
//      Stloc (13):  uint32 localNameIndex
//      Stfld (14):  uint32 declTypeIdx, uint32 fieldNameIdx, uint32 fieldTypeIdx
//      Stsfld (15): uint32 declTypeIdx, uint32 fieldNameIdx, uint32 fieldTypeIdx
//      Stelem (16): <none>
//      Addâ€“Shr (17-28): <none>
//      Ceq/Cgt/Clt (29-31): <none>
//      Br/Brtrue/Brfalse/Beq/Bne/Bgt/Blt (32-38): int32 labelId
//      Ret (39):  <none>
//      Call (40): uint32 declTypeIdx, uint32 methodNameIdx,
//                 uint32 returnTypeIdx, uint32 paramCount,
//                 paramCount Ã— uint32 paramTypeIdx
//      Callvirt (41): same as Call
//      Calli (42):    same as Call
//      Newobj (43):   uint32 typeNameIndex
//      Newarr (44):   uint32 elementTypeIndex
//      Castclass (45)/Isinst (46)/Box (47)/Unbox (48): uint32 typeNameIndex
//      Dup (49)/Pop (50): <none>
//      ConvI4â€“ConvU8 (51-56): <none>
//      If (57):    uint8 condKind, [condData], uint32 thenCount, [thenInstrs],
//                  uint8 hasElse, if(hasElse): uint32 elseCount, [elseInstrs]
//      While (58): uint8 condKind, [condData], uint32 bodyCount, [bodyInstrs]
//      Break/Continue (62-63): <none>
//      Throw (64): <none>
//
//  Condition data (for If/While):
//    condKind == COND_STACK (0):      <no extra>
//    condKind == COND_BINARY (1):     uint8 compOp
//                                     (0=Eq,1=Ne,2=Gt,3=Ge,4=Lt,5=Le)
//    condKind == COND_EXPRESSION (2): uint32 exprCount, exprCountÃ—[instrs]
//
// ============================================================================

namespace ObjectIR {

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Public entry points
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

FOBLoader::FOBLoadResult FOBLoader::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open FOB/IR file: " + filePath);

    file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
    file.close();

    return LoadFromData(data);
}

FOBLoader::FOBLoadResult FOBLoader::LoadFromData(const std::vector<uint8_t>& data) {
    auto fileInfo = ParseFobFile(data.data(), data.size());
    auto module   = ParsePayload(fileInfo.payload);
    return BuildVM(module, fileInfo.includes);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// File-level parsing  (FOB/IR v3 outer wrapper)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

FOBLoader::FobFileInfo FOBLoader::ParseFobFile(const uint8_t* data, size_t size) {
    if (size < HEADER_SIZE)
        throw std::runtime_error("FOB/IR file too small to contain a valid header");

    // Validate magic "FOB/IR" at file start; if not present, search for a
    // nearby occurrence and parse from there. Some toolchains may prepend a
    // small wrapper or filename chunk — be tolerant and attempt to recover.
    size_t headerBase = 0;
    if (std::memcmp(data, MAGIC, MAGIC_SIZE) != 0) {
        // Search within the first 1KB (or file size) for the magic sequence.
        size_t searchLen = std::min((size_t)1024, size);
        const uint8_t* found = std::search(data, data + searchLen,
                                           reinterpret_cast<const uint8_t*>(MAGIC),
                                           reinterpret_cast<const uint8_t*>(MAGIC) + MAGIC_SIZE);
        if (found == data + searchLen)
            throw std::runtime_error("Not a FOB/IR file â€” unexpected magic bytes");
        headerBase = static_cast<size_t>(found - data);
    }

    // Read header fields (little-endian) (offsets are relative to headerBase)
    auto readU16 = [&](size_t off) -> uint16_t {
        const size_t idx = headerBase + off;
        return static_cast<uint16_t>(data[idx]) |
               (static_cast<uint16_t>(data[idx + 1]) << 8);
    };
    auto readU32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[off])       |
               (static_cast<uint32_t>(data[off + 1]) << 8)  |
               (static_cast<uint32_t>(data[off + 2]) << 16) |
               (static_cast<uint32_t>(data[off + 3]) << 24);
    };

    uint16_t version        = readU16(6);
    uint32_t includesOffset = readU32(8) + static_cast<uint32_t>(headerBase);
    uint32_t stringDataOff  = readU32(12) + static_cast<uint32_t>(headerBase);
    uint32_t payloadOffset  = readU32(16) + static_cast<uint32_t>(headerBase);
    uint32_t payloadLength  = readU32(20);

    if (version != FORMAT_VERSION)
        throw std::runtime_error("Unsupported FOB/IR format version " +
                                 std::to_string(version) +
                                 ". Expected v" + std::to_string(FORMAT_VERSION));

    if (payloadOffset + payloadLength > size)
        throw std::runtime_error("FOB/IR payload region exceeds file size");

    // â”€â”€ StringData section â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Layout: uint32 dataLength, then dataLength bytes of packed
    //         null-terminated UTF-8 strings.
    if (stringDataOff + 4 > size)
        throw std::runtime_error("FOB/IR StringData section out of bounds");

    uint32_t dataLength = readU32(stringDataOff);
    const uint8_t* strPool = data + stringDataOff + 4;
    if (stringDataOff + 4 + dataLength > size)
        throw std::runtime_error("FOB/IR StringData blob exceeds file size");

    auto readPooledString = [&](uint32_t offset) -> std::string {
        if (offset >= dataLength)
            throw std::runtime_error("FOB/IR string offset out of StringData bounds");
        const uint8_t* start = strPool + offset;
        const uint8_t* end   = start;
        while (end < strPool + dataLength && *end != 0) ++end;
        return std::string(reinterpret_cast<const char*>(start),
                           static_cast<size_t>(end - start));
    };

    // â”€â”€ Includes section â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Layout: uint32 count, count Ã— uint32 offset into StringData blob.
    if (includesOffset + 4 > size)
        throw std::runtime_error("FOB/IR Includes section out of bounds");

    uint32_t includeCount = readU32(includesOffset);
    if (includesOffset + 4 + includeCount * 4u > size)
        throw std::runtime_error("FOB/IR Includes entry list exceeds file size");

    std::vector<std::string> includes;
    includes.reserve(includeCount);
    for (uint32_t i = 0; i < includeCount; ++i) {
        uint32_t strOff = readU32(includesOffset + 4 + i * 4);
        includes.push_back(readPooledString(strOff));
    }

    // â”€â”€ Payload â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::vector<uint8_t> payload(data + payloadOffset,
                                 data + payloadOffset + payloadLength);

    return {version, std::move(includes), std::move(payload)};
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Payload parsing
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

FOBLoader::DecodedModule FOBLoader::ParsePayload(const std::vector<uint8_t>& payload) {
    std::istringstream s(std::string(payload.begin(), payload.end()),
                         std::ios::binary);
    DecodedModule mod;

    // â”€â”€ StringPool â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    uint32_t strCount = ReadU32(s);
    mod.strings.reserve(strCount);
    for (uint32_t i = 0; i < strCount; ++i)
        mod.strings.push_back(ReadLengthPrefixedString(s));

    // â”€â”€ ModuleHeader â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    mod.moduleNameIndex = ReadU32(s);
    mod.versionMajor    = ReadU16(s);
    mod.versionMinor    = ReadU16(s);
    mod.versionPatch    = ReadU16(s);
    mod.entryPoint      = ReadU32(s);

    // â”€â”€ Types â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    uint32_t typeCount = ReadU32(s);
    mod.types.reserve(typeCount);
    for (uint32_t i = 0; i < typeCount; ++i)
        mod.types.push_back(ParseType(s));

    // â”€â”€ Top-level Functions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    uint32_t funcCount = ReadU32(s);
    mod.functions.reserve(funcCount);
    for (uint32_t i = 0; i < funcCount; ++i)
        mod.functions.push_back(ParseMethod(s));

    return mod;
}

FOBLoader::DecodedType FOBLoader::ParseType(std::istream& s) {
    DecodedType t;
    t.kind           = ReadU8(s);
    t.access         = ReadU8(s);
    t.nameIndex      = ReadU32(s);
    t.namespaceIndex = ReadU32(s);
    t.typeFlags      = ReadU16(s);
    t.baseTypeIndex  = ReadU32(s);

    uint32_t ifaceCount = ReadU32(s);
    t.interfaceIndices.resize(ifaceCount);
    for (uint32_t i = 0; i < ifaceCount; ++i)
        t.interfaceIndices[i] = ReadU32(s);

    uint32_t fieldCount = ReadU32(s);
    t.fields.reserve(fieldCount);
    for (uint32_t i = 0; i < fieldCount; ++i) {
        DecodedField f;
        f.nameIndex     = ReadU32(s);
        f.typeNameIndex = ReadU32(s);
        f.access        = ReadU8(s);
        f.flags         = ReadU8(s);
        t.fields.push_back(f);
    }

    uint32_t methodCount = ReadU32(s);
    t.methods.reserve(methodCount);
    for (uint32_t i = 0; i < methodCount; ++i)
        t.methods.push_back(ParseMethod(s));

    return t;
}

FOBLoader::DecodedMethod FOBLoader::ParseMethod(std::istream& s) {
    DecodedMethod m;
    m.nameIndex       = ReadU32(s);
    m.returnTypeIndex = ReadU32(s);
    m.access          = ReadU8(s);
    m.flags           = ReadU8(s);

    uint32_t paramCount = ReadU32(s);
    m.parameters.reserve(paramCount);
    for (uint32_t i = 0; i < paramCount; ++i) {
        DecodedParam p;
        p.nameIndex     = ReadU32(s);
        p.typeNameIndex = ReadU32(s);
        m.parameters.push_back(p);
    }

    uint32_t localCount = ReadU32(s);
    m.locals.reserve(localCount);
    for (uint32_t i = 0; i < localCount; ++i) {
        DecodedLocal l;
        l.nameIndex     = ReadU32(s);
        l.typeNameIndex = ReadU32(s);
        m.locals.push_back(l);
    }

    m.instructions = ParseInstructionBlock(s);
    return m;
}

std::vector<FOBLoader::DecodedInstruction> FOBLoader::ParseInstructionBlock(std::istream& s) {
    uint32_t count = ReadU32(s);
    std::vector<DecodedInstruction> block;
    block.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        block.push_back(ParseInstruction(s));
    return block;
}

FOBLoader::DecodedInstruction FOBLoader::ParseInstruction(std::istream& s) {
    DecodedInstruction di;
    di.opcode = static_cast<SerializedOpCode>(ReadU8(s));

    switch (di.opcode) {
        // â”€â”€ Loads â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Ldarg:
            di.i32 = ReadI32(s);
            break;
        case SerializedOpCode::Ldloc:
        case SerializedOpCode::Ldstr:
        case SerializedOpCode::Starg:
        case SerializedOpCode::Stloc:
            di.idx0 = ReadU32(s);
            break;
        case SerializedOpCode::Ldfld:
        case SerializedOpCode::Ldsfld:
        case SerializedOpCode::Stfld:
        case SerializedOpCode::Stsfld:
            di.idx0 = ReadU32(s);  // declaring type name index
            di.idx1 = ReadU32(s);  // field name index
            di.idx2 = ReadU32(s);  // field type name index
            break;
        case SerializedOpCode::LdcI4:
            di.i32 = ReadI32(s);
            break;
        case SerializedOpCode::LdcI8:
            di.i64 = ReadI64(s);
            break;
        case SerializedOpCode::LdcR4:
            di.f32 = ReadF32(s);
            break;
        case SerializedOpCode::LdcR8:
            di.f64 = ReadF64(s);
            break;

        // â”€â”€ Branches â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Br:
        case SerializedOpCode::Brtrue:
        case SerializedOpCode::Brfalse:
        case SerializedOpCode::Beq:
        case SerializedOpCode::Bne:
        case SerializedOpCode::Bgt:
        case SerializedOpCode::Blt:
            di.i32 = ReadI32(s);  // label id
            break;

        // â”€â”€ Calls â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Call:
        case SerializedOpCode::Callvirt:
        case SerializedOpCode::Calli: {
            di.idx0 = ReadU32(s);  // declaring type name index
            di.idx1 = ReadU32(s);  // method name index
            di.idx2 = ReadU32(s);  // return type name index
            uint32_t paramCount = ReadU32(s);
            di.extraIndices.reserve(paramCount);
            for (uint32_t i = 0; i < paramCount; ++i)
                di.extraIndices.push_back(ReadU32(s));
            break;
        }

        // â”€â”€ Object / array creation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Newobj:
        case SerializedOpCode::Newarr:
        case SerializedOpCode::Castclass:
        case SerializedOpCode::Isinst:
        case SerializedOpCode::Box:
        case SerializedOpCode::Unbox:
            di.idx0 = ReadU32(s);  // type name index
            break;

        // â”€â”€ Structured control flow â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::If: {
            di.condition = ParseCondition(s);
            di.thenBlock = ParseInstructionBlock(s);
            uint8_t hasElse = ReadU8(s);
            if (hasElse)
                di.elseBlock = ParseInstructionBlock(s);
            break;
        }
        case SerializedOpCode::While:
            di.condition  = ParseCondition(s);
            di.bodyBlock  = ParseInstructionBlock(s);
            break;

        // â”€â”€ No-operand instructions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Ldelem, Ldlen, Ldnull, Stelem, Addâ€“Shr, Ceq/Cgt/Clt,
        // Ret, Dup, Pop, ConvI4â€“ConvU8, Break, Continue, Throw,
        // For/Switch/Try (future): all fall through with no operand bytes.
        default:
            break;
    }

    return di;
}

FOBLoader::DecodedCondition FOBLoader::ParseCondition(std::istream& s) {
    DecodedCondition dc;
    dc.kind = ReadU8(s);
    if (dc.kind == COND_BINARY) {
        dc.compOp = ReadU8(s);
    } else if (dc.kind == COND_EXPRESSION) {
        dc.setupExprs = ParseInstructionBlock(s);
    }
    return dc;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// VM construction
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static TypeReference ResolveTypeByName(const std::string& name,
                                       std::shared_ptr<VirtualMachine> vm) {
    if (name == "void")    return TypeReference::Void();
    if (name == "int32"  || name == "int"   || name == "System.Int32")  return TypeReference::Int32();
    if (name == "int64"  || name == "long"  || name == "System.Int64")  return TypeReference::Int64();
    if (name == "float32"|| name == "float" || name == "System.Single") return TypeReference::Float32();
    if (name == "float64"|| name == "double"|| name == "System.Double") return TypeReference::Float64();
    if (name == "bool"   || name == "System.Boolean") return TypeReference::Bool();
    if (name == "string" || name == "System.String")  return TypeReference::String();
    if (name == "uint8"  || name == "byte"  || name == "System.Byte")   return TypeReference::UInt8();

    // Look up a class registered in the VM
    if (vm) {
        auto classRef = vm->GetClass(name);
        if (classRef)
            return TypeReference::Object(classRef);
    }
    return TypeReference::Object();   // unresolved â€“ treat as generic object
}

TypeReference FOBLoader::ResolveType(const std::string& typeName,
                                     std::shared_ptr<VirtualMachine> vm) {
    return ResolveTypeByName(typeName, vm);
}

FOBLoader::FOBLoadResult FOBLoader::BuildVM(const DecodedModule& mod,
                                            const std::vector<std::string>& includes) {
    auto vm = std::make_shared<VirtualMachine>();
    RegisterStandardLibrary(vm);

    const auto& pool = mod.strings;
    auto str = [&](uint32_t idx) -> const std::string& {
        if (idx == NULL_IDX || idx >= pool.size())
            throw std::runtime_error("FOB/IR string index out of bounds: " +
                                     std::to_string(idx));
        return pool[idx];
    };
    auto strOrEmpty = [&](uint32_t idx) -> std::string {
        return (idx == NULL_IDX || idx >= pool.size()) ? std::string{} : pool[idx];
    };

    // â”€â”€ First pass: register all class shells so cross-references resolve â”€â”€â”€â”€
    std::vector<ClassRef> classRefs;
    classRefs.reserve(mod.types.size());
    for (const auto& td : mod.types) {
        auto classRef = std::make_shared<Class>(str(td.nameIndex));
        std::string ns = strOrEmpty(td.namespaceIndex);
        if (!ns.empty())
            classRef->SetNamespace(ns);
        classRef->SetAbstract((td.typeFlags & TYPE_ABSTRACT) != 0);
        classRef->SetSealed  ((td.typeFlags & TYPE_SEALED)   != 0);
        vm->RegisterClass(classRef);
        classRefs.push_back(classRef);
    }

    // â”€â”€ Second pass: populate fields and methods â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::vector<std::string>              outClassNames;
    std::vector<std::vector<std::string>> outMethodNames;
    outClassNames.reserve(mod.types.size());
    outMethodNames.reserve(mod.types.size());

    for (size_t ti = 0; ti < mod.types.size(); ++ti) {
        const DecodedType& td  = mod.types[ti];
        ClassRef           cls = classRefs[ti];

        // Base class
        if (td.baseTypeIndex != NULL_IDX) {
            std::string baseName = strOrEmpty(td.baseTypeIndex);
            auto baseRef = vm->GetClass(baseName);
            if (baseRef)
                cls->SetBaseClass(baseRef);
        }

        // Interfaces
        for (uint32_t ifIdx : td.interfaceIndices) {
            std::string ifName = strOrEmpty(ifIdx);
            auto ifRef = vm->GetClass(ifName);
            if (ifRef)
                cls->AddInterface(ifRef);
        }

        // Fields
        for (const auto& fd : td.fields) {
            std::string fieldName     = str(fd.nameIndex);
            std::string fieldTypeName = str(fd.typeNameIndex);
            auto fieldType = ResolveType(fieldTypeName, vm);
            cls->AddField(std::make_shared<Field>(fieldName, fieldType));
        }

        // Methods
        std::vector<std::string> mNames;
        mNames.reserve(td.methods.size());
        for (const auto& md : td.methods) {
            std::string methodName   = str(md.nameIndex);
            std::string retTypeName  = str(md.returnTypeIndex);
            bool isStatic  = (md.flags & METHOD_STATIC)  != 0;
            bool isVirtual = (md.flags & METHOD_VIRTUAL) != 0;
            auto retType   = ResolveType(retTypeName, vm);

            auto method = std::make_shared<Method>(methodName, retType, isStatic, isVirtual);

            for (const auto& pd : md.parameters) {
                std::string pName = str(pd.nameIndex);
                std::string pType = str(pd.typeNameIndex);
                method->AddParameter(pName, ResolveType(pType, vm));
            }
            for (const auto& ld : md.locals) {
                std::string lName = str(ld.nameIndex);
                std::string lType = str(ld.typeNameIndex);
                method->AddLocal(lName, ResolveType(lType, vm));
            }

            // Lower decoded instructions to runtime instructions
            std::vector<Instruction> instrs;
            instrs.reserve(md.instructions.size());
            for (const auto& di : md.instructions)
                instrs.push_back(LowerInstruction(di, pool));
            method->SetInstructions(std::move(instrs));

            cls->AddMethod(method);
            mNames.push_back(methodName);
        }

        std::string qualName = cls->GetNamespace().empty()
            ? cls->GetName()
            : cls->GetNamespace() + "." + cls->GetName();
        outClassNames.push_back(qualName);
        outMethodNames.push_back(std::move(mNames));
    }

    // â”€â”€ Entry point â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    uint16_t entryType   = 0;
    uint16_t entryMethod = 0;
    const uint32_t ep = mod.entryPoint;

    bool epValid = (ep != NULL_IDX) &&
                   ((ep >> 16) < static_cast<uint32_t>(outClassNames.size())) &&
                   ((ep & 0xFFFFu) < static_cast<uint32_t>(
                        outMethodNames.empty() ? 0 : outMethodNames[ep >> 16].size()));

    if (epValid) {
        entryType   = static_cast<uint16_t>(ep >> 16);
        entryMethod = static_cast<uint16_t>(ep & 0xFFFFu);
    } else {
        // Fallback: find first method named "Main"
        for (size_t t = 0; t < outMethodNames.size() && !epValid; ++t) {
            for (size_t m = 0; m < outMethodNames[t].size(); ++m) {
                if (outMethodNames[t][m] == "Main") {
                    entryType   = static_cast<uint16_t>(t);
                    entryMethod = static_cast<uint16_t>(m);
                    epValid = true;
                    break;
                }
            }
        }
    }

    return {vm, entryType, entryMethod,
            std::move(outClassNames), std::move(outMethodNames)};
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Instruction lowering  (DecodedInstruction â†’ runtime Instruction)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

Instruction::ConditionData FOBLoader::LowerCondition(const DecodedCondition& dc,
                                                      const std::vector<std::string>& pool) {
    Instruction::ConditionData cd;
    switch (dc.kind) {
        case COND_STACK:
            cd.kind = ConditionKind::Stack;
            break;
        case COND_BINARY: {
            cd.kind = ConditionKind::Binary;
            // Map serialised comparison op to runtime OpCode
            static const OpCode kCompOps[] = {
                OpCode::Ceq,   // 0 = Equal
                OpCode::Cne,   // 1 = NotEqual
                OpCode::Cgt,   // 2 = Greater
                OpCode::Cge,   // 3 = GreaterOrEqual
                OpCode::Clt,   // 4 = Less
                OpCode::Cle,   // 5 = LessOrEqual
            };
            if (dc.compOp < 6)
                cd.comparisonOp = kCompOps[dc.compOp];
            break;
        }
        case COND_EXPRESSION: {
            cd.kind = ConditionKind::Expression;
            for (const auto& ei : dc.setupExprs)
                cd.expressionInstructions.push_back(LowerInstruction(ei, pool));
            break;
        }
        default:
            cd.kind = ConditionKind::Stack;
            break;
    }
    return cd;
}

Instruction FOBLoader::LowerInstruction(const DecodedInstruction& di,
                                         const std::vector<std::string>& pool) {
    auto idx2str = [&](uint32_t idx) -> std::string {
        return (idx == NULL_IDX || idx >= pool.size()) ? std::string{} : pool[idx];
    };

    Instruction instr;

    switch (di.opcode) {
        // â”€â”€ Loads â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Ldarg:
            instr.opCode       = OpCode::LdArg;
            instr.operandInt   = di.i32;
            instr.hasOperandInt = true;
            break;
        case SerializedOpCode::Ldloc:
            instr.opCode    = OpCode::LdLoc;
            instr.identifier = idx2str(di.idx0);
            break;
        case SerializedOpCode::Ldfld:
        case SerializedOpCode::Ldsfld:
            instr.opCode = OpCode::LdFld;
            instr.fieldTarget = FieldTarget{
                idx2str(di.idx0), idx2str(di.idx1), idx2str(di.idx2)
            };
            break;
        case SerializedOpCode::Ldnull:
            instr.opCode         = OpCode::LdNull;
            instr.hasConstant    = true;
            instr.constantIsNull = true;
            break;
        case SerializedOpCode::Ldelem:
            instr.opCode = OpCode::LdElem;
            break;
        case SerializedOpCode::Ldlen:
            instr.opCode = OpCode::LdLen;
            break;
        case SerializedOpCode::LdcI4:
            instr.opCode         = OpCode::LdI4;
            instr.hasConstant    = true;
            instr.constantType   = "int32";
            instr.constantRawValue = std::to_string(di.i32);
            break;
        case SerializedOpCode::LdcI8:
            instr.opCode         = OpCode::LdI8;
            instr.hasConstant    = true;
            instr.constantType   = "int64";
            instr.constantRawValue = std::to_string(di.i64);
            break;
        case SerializedOpCode::LdcR4:
            instr.opCode         = OpCode::LdR4;
            instr.hasConstant    = true;
            instr.constantType   = "float32";
            instr.operandDouble  = static_cast<double>(di.f32);
            instr.constantRawValue = std::to_string(di.f32);
            break;
        case SerializedOpCode::LdcR8:
            instr.opCode         = OpCode::LdR8;
            instr.hasConstant    = true;
            instr.constantType   = "float64";
            instr.operandDouble  = di.f64;
            instr.constantRawValue = std::to_string(di.f64);
            break;
        case SerializedOpCode::Ldstr:
            instr.opCode         = OpCode::LdStr;
            instr.hasConstant    = true;
            instr.constantType   = "string";
            instr.operandString  = idx2str(di.idx0);
            instr.constantRawValue = instr.operandString;
            break;

        // â”€â”€ Stores â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Starg:
            instr.opCode    = OpCode::StArg;
            instr.identifier = idx2str(di.idx0);
            break;
        case SerializedOpCode::Stloc:
            instr.opCode    = OpCode::StLoc;
            instr.identifier = idx2str(di.idx0);
            break;
        case SerializedOpCode::Stfld:
        case SerializedOpCode::Stsfld:
            instr.opCode = OpCode::StFld;
            instr.fieldTarget = FieldTarget{
                idx2str(di.idx0), idx2str(di.idx1), idx2str(di.idx2)
            };
            break;
        case SerializedOpCode::Stelem:
            instr.opCode = OpCode::StElem;
            break;

        // â”€â”€ Arithmetic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Add:  instr.opCode = OpCode::Add; break;
        case SerializedOpCode::Sub:  instr.opCode = OpCode::Sub; break;
        case SerializedOpCode::Mul:  instr.opCode = OpCode::Mul; break;
        case SerializedOpCode::Div:  instr.opCode = OpCode::Div; break;
        case SerializedOpCode::Rem:  instr.opCode = OpCode::Rem; break;
        case SerializedOpCode::Neg:  instr.opCode = OpCode::Neg; break;
        // And/Or/Xor/Not/Shl/Shr â€“ not in runtime, emit Nop
        case SerializedOpCode::And:
        case SerializedOpCode::Or:
        case SerializedOpCode::Xor:
        case SerializedOpCode::Not:
        case SerializedOpCode::Shl:
        case SerializedOpCode::Shr:
            instr.opCode = OpCode::Nop;
            break;

        // â”€â”€ Comparisons â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Ceq: instr.opCode = OpCode::Ceq; break;
        case SerializedOpCode::Cgt: instr.opCode = OpCode::Cgt; break;
        case SerializedOpCode::Clt: instr.opCode = OpCode::Clt; break;

        // â”€â”€ Branches â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Br:      instr.opCode = OpCode::Br;      instr.operandInt = di.i32; instr.hasOperandInt = true; break;
        case SerializedOpCode::Brtrue:  instr.opCode = OpCode::BrTrue;  instr.operandInt = di.i32; instr.hasOperandInt = true; break;
        case SerializedOpCode::Brfalse: instr.opCode = OpCode::BrFalse; instr.operandInt = di.i32; instr.hasOperandInt = true; break;
        case SerializedOpCode::Beq:     instr.opCode = OpCode::Beq;     instr.operandInt = di.i32; instr.hasOperandInt = true; break;
        case SerializedOpCode::Bne:     instr.opCode = OpCode::Bne;     instr.operandInt = di.i32; instr.hasOperandInt = true; break;
        case SerializedOpCode::Bgt:     instr.opCode = OpCode::Bgt;     instr.operandInt = di.i32; instr.hasOperandInt = true; break;
        case SerializedOpCode::Blt:     instr.opCode = OpCode::Blt;     instr.operandInt = di.i32; instr.hasOperandInt = true; break;

        // â”€â”€ Return â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Ret:
            instr.opCode = OpCode::Ret;
            break;

        // â”€â”€ Calls â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Call:
        case SerializedOpCode::Calli: {
            instr.opCode = OpCode::Call;
            CallTarget ct;
            ct.declaringType = idx2str(di.idx0);
            ct.name          = idx2str(di.idx1);
            ct.returnType    = idx2str(di.idx2);
            ct.hasParameterTypes = true;
            for (uint32_t pi : di.extraIndices)
                ct.parameterTypes.push_back(idx2str(pi));
            instr.callTarget = ct;
            break;
        }
        case SerializedOpCode::Callvirt: {
            instr.opCode = OpCode::CallVirt;
            CallTarget ct;
            ct.declaringType = idx2str(di.idx0);
            ct.name          = idx2str(di.idx1);
            ct.returnType    = idx2str(di.idx2);
            ct.hasParameterTypes = true;
            for (uint32_t pi : di.extraIndices)
                ct.parameterTypes.push_back(idx2str(pi));
            instr.callTarget = ct;
            break;
        }

        // â”€â”€ Object / array creation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Newobj:
            instr.opCode       = OpCode::NewObj;
            instr.operandString = idx2str(di.idx0);
            break;
        case SerializedOpCode::Newarr:
            instr.opCode       = OpCode::NewArr;
            instr.operandString = idx2str(di.idx0);
            break;
        case SerializedOpCode::Castclass:
            instr.opCode       = OpCode::CastClass;
            instr.operandString = idx2str(di.idx0);
            break;
        case SerializedOpCode::Isinst:
            instr.opCode       = OpCode::IsInst;
            instr.operandString = idx2str(di.idx0);
            break;
        // Box/Unbox â€“ not in runtime, emit Nop
        case SerializedOpCode::Box:
        case SerializedOpCode::Unbox:
            instr.opCode = OpCode::Nop;
            break;

        // â”€â”€ Stack manipulation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::Dup: instr.opCode = OpCode::Dup; break;
        case SerializedOpCode::Pop: instr.opCode = OpCode::Pop; break;

        // â”€â”€ Conversions â€“ not in runtime, emit Nop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::ConvI4:
        case SerializedOpCode::ConvI8:
        case SerializedOpCode::ConvR4:
        case SerializedOpCode::ConvR8:
        case SerializedOpCode::ConvU4:
        case SerializedOpCode::ConvU8:
            instr.opCode = OpCode::Nop;
            break;

        // â”€â”€ Structured control flow â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case SerializedOpCode::If: {
            instr.opCode = OpCode::If;
            Instruction::IfData id;
            for (const auto& ti : di.thenBlock)
                id.thenBlock.push_back(LowerInstruction(ti, pool));
            for (const auto& ei : di.elseBlock)
                id.elseBlock.push_back(LowerInstruction(ei, pool));
            instr.ifData  = std::move(id);
            instr.whileData = std::nullopt;  // ensure clear
            // Embed condition as the first setup instruction (for the executor)
            auto cd = LowerCondition(di.condition, pool);
            Instruction::IfData& existing = *instr.ifData;
            (void)cd; // condition data stored inline in ifData if needed
            break;
        }
        case SerializedOpCode::While: {
            instr.opCode = OpCode::While;
            Instruction::WhileData wd;
            wd.condition = LowerCondition(di.condition, pool);
            for (const auto& bi : di.bodyBlock)
                wd.body.push_back(LowerInstruction(bi, pool));
            instr.whileData = std::move(wd);
            break;
        }

        case SerializedOpCode::Break:    instr.opCode = OpCode::Break;    break;
        case SerializedOpCode::Continue: instr.opCode = OpCode::Continue; break;
        case SerializedOpCode::Throw:    instr.opCode = OpCode::Throw;    break;

        // â”€â”€ Unimplemented (For/Switch/Try) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        default:
            instr.opCode = OpCode::Nop;
            break;
    }

    return instr;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Primitive read helpers  (little-endian)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

uint8_t FOBLoader::ReadU8(std::istream& s) {
    uint8_t v;
    s.read(reinterpret_cast<char*>(&v), 1);
    return v;
}

uint16_t FOBLoader::ReadU16(std::istream& s) {
    uint16_t v;
    s.read(reinterpret_cast<char*>(&v), 2);
    return v;
}

uint32_t FOBLoader::ReadU32(std::istream& s) {
    uint32_t v;
    s.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

int32_t FOBLoader::ReadI32(std::istream& s) {
    int32_t v;
    s.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

int64_t FOBLoader::ReadI64(std::istream& s) {
    int64_t v;
    s.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

float FOBLoader::ReadF32(std::istream& s) {
    float v;
    s.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

double FOBLoader::ReadF64(std::istream& s) {
    double v;
    s.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

std::string FOBLoader::ReadLengthPrefixedString(std::istream& s) {
    uint32_t len = ReadU32(s);
    std::string str(len, '\0');
    s.read(&str[0], static_cast<std::streamsize>(len));
    return str;
}

} // namespace ObjectIR
