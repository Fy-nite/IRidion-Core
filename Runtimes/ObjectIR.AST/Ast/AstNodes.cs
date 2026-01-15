namespace ObjectIR.AST;

public abstract record AstNode;

public sealed record ModuleNode(
    string Name,
    string? Version,
    IReadOnlyList<InterfaceNode> Interfaces,
    IReadOnlyList<ClassNode> Classes
) : AstNode;

public sealed record InterfaceNode(
    string Name,
    IReadOnlyList<MethodSignature> Methods
) : AstNode;

public sealed record ClassNode(
    string Name,
    IReadOnlyList<string> BaseTypes,
    IReadOnlyList<FieldNode> Fields,
    IReadOnlyList<ConstructorNode> Constructors,
    IReadOnlyList<MethodNode> Methods
) : AstNode;

public sealed record FieldNode(
    string Name,
    TypeRef FieldType,
    AccessModifier Access
) : AstNode;

public sealed record MethodSignature(
    string Name,
    IReadOnlyList<ParameterNode> Parameters,
    TypeRef ReturnType,
    bool IsStatic,
    string? Implements
) : AstNode;

public sealed record ConstructorNode(
    IReadOnlyList<ParameterNode> Parameters,
    BlockStatement Body
) : AstNode;

public sealed record MethodNode(
    string Name,
    IReadOnlyList<ParameterNode> Parameters,
    TypeRef ReturnType,
    bool IsStatic,
    string? Implements,
    BlockStatement Body
) : AstNode;

public sealed record ParameterNode(string Name, TypeRef ParameterType) : AstNode;

public sealed record TypeRef(string Name) : AstNode;

public enum AccessModifier
{
    Private,
    Public,
    Protected,
    Internal
}

public abstract record Statement : AstNode;

public sealed record BlockStatement(IReadOnlyList<Statement> Statements) : Statement;

public sealed record LocalDeclarationStatement(
    string Name,
    TypeRef LocalType
) : Statement;

public sealed record InstructionStatement(Instruction Instruction) : Statement;

public sealed record IfStatement(
    string Condition,
    BlockStatement Then,
    BlockStatement? Else
) : Statement;

public sealed record WhileStatement(
    string Condition,
    BlockStatement Body
) : Statement;

public abstract record Instruction : AstNode;

public sealed record SimpleInstruction(
    string OpCode,
    string? Operand
) : Instruction;

public sealed record CallInstruction(
    MethodRef Target,
    IReadOnlyList<TypeRef> Arguments,
    TypeRef ReturnType,
    bool IsVirtual
) : Instruction;

public sealed record NewObjInstruction(
    TypeRef Type,
    MethodRef? Constructor,
    IReadOnlyList<TypeRef> Arguments
) : Instruction;

public sealed record MethodRef(
    TypeRef DeclaringType,
    string MethodName
) : AstNode;
