# FOB/IR v3 Binary Format

The FOB/IR (Flat Object Binary / Intermediate Representation) format is the compact binary distribution format used by the Iridion runtime and the complementary `ObjectIR.Core` C# library.

---

## File layout

A `.fob` file consists of a fixed 24-byte header followed by three variable-length sections.

```
Offset  Size   Field
──────  ─────  ──────────────────────────────────────────────────────────
0       6      Magic bytes — ASCII "FOB/IR"
6       2      Format version (uint16, little-endian) — currently 3
8       4      includesOffset   — absolute byte position of Includes section
12      4      stringDataOffset — absolute byte position of StringData section
16      4      payloadOffset    — absolute byte position of Payload section
20      4      payloadLength    — byte count of the binary payload
──────  ─────  ──────────────────────────────────────────────────────────

[Includes]    @ includesOffset
  4 bytes     uint32  count of include entries
  count × 4   uint32  offsets into StringData blob (per entry)

[StringData]  @ stringDataOffset
  4 bytes     uint32  total byte length of the packed string pool
  N bytes     null-terminated packed UTF-8 strings

[Payload]     @ payloadOffset
  payloadLength bytes  — compact binary module bytecode (see §Payload)
```

All multi-byte integers are **little-endian**.

---

## Format versions

| Version | Description |
|---------|-------------|
| 3 (current) | Module stored as compact binary bytecode (see §Payload). No JSON parsing or AST lowering required at load time. |

---

## Includes section

Encodes the external type names that the module depends on at runtime (e.g. types defined in other modules or the standard library).  Each entry is a `uint32` byte offset into the **StringData** blob, pointing at a null-terminated UTF-8 string.

---

## StringData section

A flat pool of null-terminated UTF-8 strings. Strings are referenced by byte offsets from the start of the data blob (i.e. after the 4-byte `dataLength` field). Used exclusively by the Includes section for external type names.

---

## Payload section

The payload is the self-contained binary bytecode for the module.  Its format is fully defined by the Iridion runtime (`FOBLoader`).

### Payload layout

```
[StringPool]
  uint32  count
  For each: uint32 byteLength, UTF-8 bytes (no null terminator)

[ModuleHeader]
  uint32  moduleNameIndex  (index into StringPool)
  uint16  versionMajor
  uint16  versionMinor
  uint16  versionPatch
  uint32  entryPoint       ((typeIdx << 16) | methodIdx)
                           0xFFFFFFFF = no explicit entry point

[Types]
  uint32  typeCount
  For each type: <type record>

[Functions]   (module-level, not in any type)
  uint32  count
  For each: <method record>
```

### Type record

```
  uint8   kind
            0 = Class
            1 = Interface
            2 = Struct
            3 = Enum
  uint8   access
            0 = Public
            1 = Private
            2 = Protected
            3 = Internal
  uint32  nameIndex         (StringPool)
  uint32  namespaceIndex    (StringPool, 0xFFFFFFFF = no namespace)
  uint16  typeFlags
            bit 0 = abstract
            bit 1 = sealed
  uint32  baseTypeIndex     (StringPool name, 0xFFFFFFFF = no base)
  uint32  interfaceCount
  count × uint32  interface type name indices (StringPool)
  uint32  fieldCount
  For each field: <field record>
  uint32  methodCount
  For each method: <method record>
```

### Field record

```
  uint32  nameIndex         (StringPool)
  uint32  typeNameIndex     (StringPool)
  uint8   access            (same encoding as type access above)
  uint8   flags
            bit 0 = static
            bit 1 = readonly
```

### Method record

```
  uint32  nameIndex             (StringPool)
  uint32  returnTypeNameIndex   (StringPool)
  uint8   access
  uint8   flags
            bit 0 = static
            bit 1 = virtual
            bit 2 = abstract
            bit 3 = override
            bit 4 = constructor
  uint32  paramCount
  For each param: uint32 nameIndex, uint32 typeNameIndex  (StringPool)
  uint32  localCount
  For each local: uint32 nameIndex, uint32 typeNameIndex  (StringPool)
  uint32  instrCount
  For each instruction: <instruction record>
```

---

## Instruction encoding

Each instruction starts with a 1-byte opcode matching the `ObjectIR.Core.IR.OpCode` C# enum ordinal, followed by opcode-specific operand bytes.

| Opcode | Value | Operands |
|--------|-------|----------|
| `Ldarg` | 0 | `int32` argument index |
| `Ldloc` | 1 | `uint32` local name index |
| `Ldfld` | 2 | `uint32` declType, `uint32` fieldName, `uint32` fieldType (all StringPool) |
| `Ldsfld` | 3 | same as `Ldfld` |
| `Ldelem` | 4 | *(none)* |
| `Ldlen` | 5 | *(none)* |
| `Ldnull` | 6 | *(none)* |
| `LdcI4` | 7 | `int32` constant value |
| `LdcI8` | 8 | `int64` constant value |
| `LdcR4` | 9 | `float32` constant value |
| `LdcR8` | 10 | `float64` constant value |
| `Ldstr` | 11 | `uint32` string index (StringPool) |
| `Starg` | 12 | `uint32` arg name index |
| `Stloc` | 13 | `uint32` local name index |
| `Stfld` | 14 | same as `Ldfld` |
| `Stsfld` | 15 | same as `Ldfld` |
| `Stelem` | 16 | *(none)* |
| `Add`–`Neg` | 17–22 | *(none)* |
| `And`/`Or`/`Xor`/`Not`/`Shl`/`Shr` | 23–28 | *(none)* — lowered to `Nop` by current runtime |
| `Ceq` | 29 | *(none)* |
| `Cgt` | 30 | *(none)* |
| `Clt` | 31 | *(none)* |
| `Br`/`Brtrue`/`Brfalse`/`Beq`/`Bne`/`Bgt`/`Blt` | 32–38 | `int32` label id |
| `Ret` | 39 | *(none)* |
| `Call` | 40 | `uint32` declType, `uint32` methodName, `uint32` returnType, `uint32` paramCount, *paramCount* × `uint32` paramType (all StringPool) |
| `Callvirt` | 41 | same as `Call` |
| `Calli` | 42 | same as `Call` |
| `Newobj` | 43 | `uint32` type name index |
| `Newarr` | 44 | `uint32` element type name index |
| `Castclass` | 45 | `uint32` target type name index |
| `Isinst` | 46 | `uint32` target type name index |
| `Box` | 47 | `uint32` type name index — lowered to `Nop` |
| `Unbox` | 48 | `uint32` type name index — lowered to `Nop` |
| `Dup` | 49 | *(none)* |
| `Pop` | 50 | *(none)* |
| `ConvI4`–`ConvU8` | 51–56 | *(none)* — lowered to `Nop` |
| `If` | 57 | `uint8` condKind, *[cond data]*, `uint32` thenCount, *[then instrs]*, `uint8` hasElse, *if hasElse:* `uint32` elseCount, *[else instrs]* |
| `While` | 58 | `uint8` condKind, *[cond data]*, `uint32` bodyCount, *[body instrs]* |
| `For` | 59 | *(future — currently Nop)* |
| `Switch` | 60 | *(future — currently Nop)* |
| `Try` | 61 | *(future — currently Nop)* |
| `Break` | 62 | *(none)* |
| `Continue` | 63 | *(none)* |
| `Throw` | 64 | *(none)* |

### Condition data (used by `If` / `While`)

```
condKind == 0  (Stack):      <no extra bytes>
condKind == 1  (Binary):     uint8 compOp
                               0 = Equal
                               1 = NotEqual
                               2 = Greater
                               3 = GreaterOrEqual
                               4 = Less
                               5 = LessOrEqual
condKind == 2  (Expression): uint32 exprCount, exprCount × <instruction record>
```

---

## Supported features

### Fully supported

| Feature | Notes |
|---------|-------|
| Classes | With base type, interfaces, abstract/sealed flags |
| Interfaces | Declared as `KIND_INTERFACE` |
| Structs | Declared as `KIND_STRUCT` |
| Enums | Declared as `KIND_ENUM` (runtime maps to class) |
| Fields | Instance and static, with access modifiers and readonly flag |
| Methods | Instance, static, virtual, abstract, override, constructors |
| Method parameters | Typed, named |
| Local variables | Typed, named |
| Module-level functions | Top-level methods not bound to any type |
| External dependencies | Via `Includes` section (resolved at load time) |
| Namespaces | Per-type namespace stored in StringPool |
| Load constants | `int32`, `int64`, `float32`, `float64`, `string`, `null` |
| Load/store locals & args | By name index |
| Load/store fields | Instance (`ldfld`/`stfld`) and static (`ldsfld`/`stsfld`) |
| Arithmetic | `add`, `sub`, `mul`, `div`, `rem`, `neg` |
| Comparisons | `ceq`, `cgt`, `clt` |
| Branches | `br`, `brtrue`, `brfalse`, `beq`, `bne`, `bgt`, `blt` |
| Method calls | `call`, `callvirt` |
| Object creation | `newobj`, `newarr` |
| Type checks | `castclass`, `isinst` |
| Array ops | `ldelem`, `stelem`, `ldlen` |
| Stack ops | `dup`, `pop` |
| Structured control flow | `if`/`else`, `while` — with `stack`, `binary` and `expression` conditions |
| `break` / `continue` | Inside loops |
| `ret` / `throw` | Method return and exception throw |
| Entry point | Encoded as `(typeIdx << 16) \| methodIdx`; auto-discovery falls back to first `Main` method |

### Partially supported (lowered to `Nop`)

| Feature | Reason |
|---------|--------|
| Bitwise ops (`and`, `or`, `xor`, `not`, `shl`, `shr`) | Not yet in the C++ runtime `OpCode` set |
| `box` / `unbox` | Value-type boxing not implemented in runtime |
| Numeric conversions (`conv.i4` … `conv.u8`) | Implicit conversions handled by executor; explicit opcodes not needed |

### Not yet supported (future)

| Feature |
|---------|
| `for` loops |
| `switch` statements |
| Structured exception handling (`try`/`catch`/`finally`) |
| Generic type parameters |

---

## C# compatibility

Files produced by `ObjectIR.Core.FobIrCompiler` (`CompileFromPayload`) can be read by the C++ `FOBLoader` whenever the payload was generated by this runtime's own writer.  The outer file wrapper (magic, header, Includes, StringData) is identical between the two implementations.
