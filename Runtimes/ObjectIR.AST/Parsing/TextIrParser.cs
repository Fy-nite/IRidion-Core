using System.Text;

namespace ObjectIR.AST;

public static class TextIrParser
{
    public static ModuleNode ParseModule(string text)
    {
        var tokens = Tokenize(text);
        var reader = new TokenReader(tokens);

        if (!reader.TryReadLine(out var moduleLine))
        {
            throw new TextIrParseException("Input is empty.");
        }

        var (moduleName, moduleVersion) = ParseModuleHeader(moduleLine);

        var interfaces = new List<InterfaceNode>();
        var classes = new List<ClassNode>();

        while (!reader.IsAtEnd)
        {
            if (reader.PeekStartsWith("interface "))
            {
                interfaces.Add(ParseInterface(reader));
                continue;
            }

            if (reader.PeekStartsWith("class "))
            {
                classes.Add(ParseClass(reader));
                continue;
            }

            throw new TextIrParseException($"Unexpected token '{reader.Peek()}' at top-level.");
        }

        return new ModuleNode(moduleName, moduleVersion, interfaces, classes);
    }

    public static CallInstruction ParseCall(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            throw new TextIrParseException("Input is empty.");
        }

        var span = text.AsSpan().Trim();
        if (!span.StartsWith("call ", StringComparison.OrdinalIgnoreCase))
        {
            throw new TextIrParseException("Expected input to start with 'call'.");
        }

        span = span[5..].Trim();
        return ParseCallCore(span, isVirtual: false);
    }

    private static (string Name, string? Version) ParseModuleHeader(string line)
    {
        var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length < 2 || !parts[0].Equals("module", StringComparison.OrdinalIgnoreCase))
        {
            throw new TextIrParseException("Expected module header like 'module Name version X'.");
        }

        var name = parts[1];
        string? version = null;
        var versionIndex = Array.IndexOf(parts, "version");
        if (versionIndex >= 0 && versionIndex + 1 < parts.Length)
        {
            version = string.Join(' ', parts[(versionIndex + 1)..]);
        }

        return (name, version);
    }

    private static InterfaceNode ParseInterface(TokenReader reader)
    {
        var line = reader.ReadLine();
        var name = line["interface ".Length..].Trim();
        if (name.Length == 0)
        {
            throw new TextIrParseException("Interface name is missing.");
        }

        reader.Expect("{");
        var methods = new List<MethodSignature>();

        while (!reader.PeekIs("}"))
        {
            var methodLine = reader.ReadLine();
            methods.Add(ParseMethodSignature(methodLine));
        }

        reader.Expect("}");
        return new InterfaceNode(name, methods);
    }

    private static ClassNode ParseClass(TokenReader reader)
    {
        var line = reader.ReadLine();
        var declaration = line["class ".Length..].Trim();
        if (declaration.Length == 0)
        {
            throw new TextIrParseException("Class name is missing.");
        }

        var baseTypes = new List<string>();
        var parts = declaration.Split(':', 2, StringSplitOptions.TrimEntries);
        var name = parts[0].Trim();
        if (parts.Length > 1)
        {
            baseTypes.AddRange(parts[1].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
        }

        reader.Expect("{");

        var fields = new List<FieldNode>();
        var constructors = new List<ConstructorNode>();
        var methods = new List<MethodNode>();

        while (!reader.PeekIs("}"))
        {
            var memberLine = reader.ReadLine();
            if (IsField(memberLine))
            {
                fields.Add(ParseField(memberLine));
                continue;
            }

            if (IsConstructor(memberLine))
            {
                constructors.Add(ParseConstructor(reader, memberLine));
                continue;
            }

            if (IsMethod(memberLine))
            {
                methods.Add(ParseMethod(reader, memberLine));
                continue;
            }

            throw new TextIrParseException($"Unexpected class member '{memberLine}'.");
        }

        reader.Expect("}");
        return new ClassNode(name, baseTypes, fields, constructors, methods);
    }

    private static bool IsField(string line) => line.Contains(" field ", StringComparison.OrdinalIgnoreCase) || line.StartsWith("field ", StringComparison.OrdinalIgnoreCase);

    private static bool IsConstructor(string line) => line.StartsWith("constructor", StringComparison.OrdinalIgnoreCase);

    private static bool IsMethod(string line) => line.StartsWith("method ", StringComparison.OrdinalIgnoreCase) || line.StartsWith("static method ", StringComparison.OrdinalIgnoreCase);

    private static FieldNode ParseField(string line)
    {
        var access = AccessModifier.Private;
        var remaining = line;

        if (remaining.StartsWith("private ", StringComparison.OrdinalIgnoreCase))
        {
            access = AccessModifier.Private;
            remaining = remaining["private ".Length..];
        }
        else if (remaining.StartsWith("public ", StringComparison.OrdinalIgnoreCase))
        {
            access = AccessModifier.Public;
            remaining = remaining["public ".Length..];
        }
        else if (remaining.StartsWith("protected ", StringComparison.OrdinalIgnoreCase))
        {
            access = AccessModifier.Protected;
            remaining = remaining["protected ".Length..];
        }
        else if (remaining.StartsWith("internal ", StringComparison.OrdinalIgnoreCase))
        {
            access = AccessModifier.Internal;
            remaining = remaining["internal ".Length..];
        }

        remaining = remaining.Trim();
        if (remaining.StartsWith("field ", StringComparison.OrdinalIgnoreCase))
        {
            remaining = remaining["field ".Length..];
        }

        var parts = remaining.Split(':', 2, StringSplitOptions.TrimEntries);
        if (parts.Length != 2)
        {
            throw new TextIrParseException("Invalid field declaration.");
        }

        var name = parts[0].Trim();
        var type = parts[1].Trim();
        if (name.Length == 0 || type.Length == 0)
        {
            throw new TextIrParseException("Invalid field declaration.");
        }

        return new FieldNode(name, new TypeRef(type), access);
    }

    private static ConstructorNode ParseConstructor(TokenReader reader, string line)
    {
        var parameters = ParseParametersFromSignature("constructor", line);
        var body = ParseBlock(reader);
        return new ConstructorNode(parameters, body);
    }

    private static MethodSignature ParseMethodSignature(string line)
    {
        var (isStatic, signature) = ConsumeStaticPrefix(line);
        if (!signature.StartsWith("method ", StringComparison.OrdinalIgnoreCase))
        {
            throw new TextIrParseException("Expected method signature.");
        }

        var (name, parameters, returnType, implements) = ParseMethodParts(signature["method ".Length..]);
        return new MethodSignature(name, parameters, returnType, isStatic, implements);
    }

    private static MethodNode ParseMethod(TokenReader reader, string line)
    {
        var (isStatic, signature) = ConsumeStaticPrefix(line);
        if (!signature.StartsWith("method ", StringComparison.OrdinalIgnoreCase))
        {
            throw new TextIrParseException("Expected method declaration.");
        }

        var (name, parameters, returnType, implements) = ParseMethodParts(signature["method ".Length..]);
        var body = ParseBlock(reader);
        return new MethodNode(name, parameters, returnType, isStatic, implements, body);
    }

    private static (bool IsStatic, string Signature) ConsumeStaticPrefix(string line)
    {
        if (line.StartsWith("static method ", StringComparison.OrdinalIgnoreCase))
        {
            return (true, line["static ".Length..]);
        }

        return (false, line);
    }

    private static (string Name, IReadOnlyList<ParameterNode> Parameters, TypeRef ReturnType, string? Implements) ParseMethodParts(string line)
    {
        var implementsIndex = line.IndexOf(" implements ", StringComparison.OrdinalIgnoreCase);
        string? implements = null;
        if (implementsIndex >= 0)
        {
            implements = line[(implementsIndex + " implements ".Length)..].Trim();
            line = line[..implementsIndex].Trim();
        }

        var arrowIndex = line.IndexOf("->", StringComparison.Ordinal);
        if (arrowIndex < 0)
        {
            throw new TextIrParseException("Expected '->' in method signature.");
        }

        var left = line[..arrowIndex].Trim();
        var returnType = line[(arrowIndex + 2)..].Trim();
        if (returnType.Length == 0)
        {
            throw new TextIrParseException("Missing return type.");
        }

        var openParenIndex = left.IndexOf('(');
        var closeParenIndex = left.LastIndexOf(')');
        if (openParenIndex < 0 || closeParenIndex < openParenIndex)
        {
            throw new TextIrParseException("Expected parameters in parentheses.");
        }

        var name = left[..openParenIndex].Trim();
        var parametersSpan = left[(openParenIndex + 1)..closeParenIndex];
        var parameters = ParseParameters(parametersSpan);

        return (name, parameters, new TypeRef(returnType), implements);
    }

    private static IReadOnlyList<ParameterNode> ParseParametersFromSignature(string keyword, string line)
    {
        var start = keyword.Length;
        var signature = line[start..].Trim();
        var openParenIndex = signature.IndexOf('(');
        var closeParenIndex = signature.LastIndexOf(')');
        if (openParenIndex < 0 || closeParenIndex < openParenIndex)
        {
            throw new TextIrParseException("Expected parameters in parentheses.");
        }

        var parametersSpan = signature[(openParenIndex + 1)..closeParenIndex];
        return ParseParameters(parametersSpan);
    }

    private static BlockStatement ParseBlock(TokenReader reader)
    {
        reader.Expect("{");
        var statements = new List<Statement>();

        while (!reader.PeekIs("}"))
        {
            var line = reader.ReadLine();
            if (line.StartsWith("local ", StringComparison.OrdinalIgnoreCase))
            {
                statements.Add(ParseLocal(line));
                continue;
            }

            if (line.StartsWith("if ", StringComparison.OrdinalIgnoreCase))
            {
                statements.Add(ParseIf(reader, line));
                continue;
            }

            if (line.StartsWith("while ", StringComparison.OrdinalIgnoreCase))
            {
                statements.Add(ParseWhile(reader, line));
                continue;
            }

            statements.Add(new InstructionStatement(ParseInstruction(line)));
        }

        reader.Expect("}");
        return new BlockStatement(statements);
    }

    private static LocalDeclarationStatement ParseLocal(string line)
    {
        var parts = line["local ".Length..].Split(':', 2, StringSplitOptions.TrimEntries);
        if (parts.Length != 2)
        {
            throw new TextIrParseException("Invalid local declaration.");
        }

        return new LocalDeclarationStatement(parts[0], new TypeRef(parts[1]));
    }

    private static IfStatement ParseIf(TokenReader reader, string line)
    {
        var condition = ExtractCondition(line, "if");
        var thenBlock = ParseBlock(reader);
        BlockStatement? elseBlock = null;

        if (!reader.IsAtEnd && reader.PeekIs("else"))
        {
            reader.ReadLine();
            elseBlock = ParseBlock(reader);
        }

        return new IfStatement(condition, thenBlock, elseBlock);
    }

    private static WhileStatement ParseWhile(TokenReader reader, string line)
    {
        var condition = ExtractCondition(line, "while");
        var body = ParseBlock(reader);
        return new WhileStatement(condition, body);
    }

    private static string ExtractCondition(string line, string keyword)
    {
        var start = keyword.Length;
        var trimmed = line[start..].Trim();
        var openParenIndex = trimmed.IndexOf('(');
        var closeParenIndex = trimmed.LastIndexOf(')');
        if (openParenIndex < 0 || closeParenIndex < openParenIndex)
        {
            throw new TextIrParseException($"Expected condition in parentheses for '{keyword}'.");
        }

        return trimmed[(openParenIndex + 1)..closeParenIndex].Trim();
    }

    private static Instruction ParseInstruction(string line)
    {
        if (line.StartsWith("callvirt ", StringComparison.OrdinalIgnoreCase))
        {
            return ParseCallCore(line["callvirt ".Length..].AsSpan().Trim(), isVirtual: true);
        }

        if (line.StartsWith("call ", StringComparison.OrdinalIgnoreCase))
        {
            return ParseCallCore(line["call ".Length..].AsSpan().Trim(), isVirtual: false);
        }

        if (line.StartsWith("newobj ", StringComparison.OrdinalIgnoreCase))
        {
            return ParseNewObj(line["newobj ".Length..].Trim());
        }

        var parts = line.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
        var opcode = parts[0];
        var operand = parts.Length > 1 ? parts[1].Trim() : null;
        return new SimpleInstruction(opcode, operand);
    }

    private static CallInstruction ParseCallCore(ReadOnlySpan<char> span, bool isVirtual)
    {
        var arrowIndex = span.IndexOf("->", StringComparison.Ordinal);
        if (arrowIndex < 0)
        {
            throw new TextIrParseException("Expected '->' before return type.");
        }

        var left = span[..arrowIndex].Trim();
        var right = span[(arrowIndex + 2)..].Trim();
        if (right.IsEmpty)
        {
            throw new TextIrParseException("Return type is missing.");
        }

        var (methodRef, args) = ParseTargetAndArgs(left);
        var returnType = new TypeRef(right.ToString());

        return new CallInstruction(methodRef, args, returnType, isVirtual);
    }

    private static NewObjInstruction ParseNewObj(string text)
    {
        var span = text.AsSpan().Trim();
        var ctorIndex = span.IndexOf(".constructor", StringComparison.OrdinalIgnoreCase);
        if (ctorIndex < 0)
        {
            return new NewObjInstruction(new TypeRef(span.ToString()), null, Array.Empty<TypeRef>());
        }

        var typeName = span[..ctorIndex].Trim();
        var ctorSpan = span[(ctorIndex + 1)..].Trim();
        if (!ctorSpan.StartsWith("constructor", StringComparison.OrdinalIgnoreCase))
        {
            throw new TextIrParseException("Invalid constructor reference.");
        }

        var openParenIndex = ctorSpan.IndexOf('(');
        var closeParenIndex = ctorSpan.LastIndexOf(')');
        if (openParenIndex < 0 || closeParenIndex < openParenIndex)
        {
            throw new TextIrParseException("Expected constructor arguments.");
        }

        var argsSpan = ctorSpan[(openParenIndex + 1)..closeParenIndex];
        var args = ParseTypeList(argsSpan);
        var method = new MethodRef(new TypeRef(typeName.ToString()), "constructor");
        return new NewObjInstruction(new TypeRef(typeName.ToString()), method, args);
    }

    private static (MethodRef Method, IReadOnlyList<TypeRef> Args) ParseTargetAndArgs(ReadOnlySpan<char> text)
    {
        var openParenIndex = text.IndexOf('(');
        var closeParenIndex = text.LastIndexOf(')');
        if (openParenIndex < 0 || closeParenIndex < openParenIndex)
        {
            throw new TextIrParseException("Expected argument list in parentheses.");
        }

        var targetSpan = text[..openParenIndex].Trim();
        var argsSpan = text[(openParenIndex + 1)..closeParenIndex];

        if (targetSpan.IsEmpty)
        {
            throw new TextIrParseException("Missing call target.");
        }

        var lastDot = targetSpan.LastIndexOf('.');
        if (lastDot < 0)
        {
            throw new TextIrParseException("Target must be qualified as Type.Method.");
        }

        var typeName = targetSpan[..lastDot].Trim();
        var methodName = targetSpan[(lastDot + 1)..].Trim();

        if (typeName.IsEmpty || methodName.IsEmpty)
        {
            throw new TextIrParseException("Invalid target format.");
        }

        var method = new MethodRef(new TypeRef(typeName.ToString()), methodName.ToString());
        var args = ParseTypeList(argsSpan);
        return (method, args);
    }

    private static IReadOnlyList<TypeRef> ParseTypeList(ReadOnlySpan<char> text)
    {
        var list = new List<TypeRef>();
        var raw = text.ToString().Trim();
        if (raw.Length == 0)
        {
            return list;
        }

        foreach (var item in SplitTopLevel(raw))
        {
            var trimmed = item.Trim();
            if (trimmed.Length == 0)
            {
                throw new TextIrParseException("Empty argument type.");
            }

            list.Add(new TypeRef(trimmed));
        }

        return list;
    }

    private static IReadOnlyList<ParameterNode> ParseParameters(ReadOnlySpan<char> text)
    {
        var list = new List<ParameterNode>();
        var raw = text.ToString().Trim();
        if (raw.Length == 0)
        {
            return list;
        }

        foreach (var item in SplitTopLevel(raw))
        {
            var trimmed = item.Trim();
            if (trimmed.Length == 0)
            {
                throw new TextIrParseException("Empty parameter.");
            }

            var parts = trimmed.Split(':', 2, StringSplitOptions.TrimEntries);
            if (parts.Length != 2)
            {
                throw new TextIrParseException("Invalid parameter format.");
            }

            list.Add(new ParameterNode(parts[0], new TypeRef(parts[1])));
        }

        return list;
    }

    private static IEnumerable<string> SplitTopLevel(string input)
    {
        var parts = new List<string>();
        var sb = new StringBuilder();
        var angleDepth = 0;
        var bracketDepth = 0;

        foreach (var ch in input)
        {
            switch (ch)
            {
                case '<':
                    angleDepth++;
                    break;
                case '>':
                    angleDepth = Math.Max(0, angleDepth - 1);
                    break;
                case '[':
                    bracketDepth++;
                    break;
                case ']':
                    bracketDepth = Math.Max(0, bracketDepth - 1);
                    break;
                case ',' when angleDepth == 0 && bracketDepth == 0:
                    parts.Add(sb.ToString());
                    sb.Clear();
                    continue;
            }

            sb.Append(ch);
        }

        parts.Add(sb.ToString());
        return parts;
    }

    private static List<string> Tokenize(string text)
    {
        var tokens = new List<string>();
        var lines = text.Split(new[] { "\r\n", "\n" }, StringSplitOptions.None);

        foreach (var rawLine in lines)
        {
            var line = StripComment(rawLine).Trim();
            if (line.Length == 0)
            {
                continue;
            }

            var sb = new StringBuilder();
            foreach (var ch in line)
            {
                if (ch == '{' || ch == '}')
                {
                    var current = sb.ToString().Trim();
                    if (current.Length > 0)
                    {
                        tokens.Add(current);
                    }

                    tokens.Add(ch.ToString());
                    sb.Clear();
                    continue;
                }

                sb.Append(ch);
            }

            var final = sb.ToString().Trim();
            if (final.Length > 0)
            {
                tokens.Add(final);
            }
        }

        return tokens;
    }

    private static string StripComment(string line)
    {
        var commentIndex = line.IndexOf("//", StringComparison.Ordinal);
        return commentIndex >= 0 ? line[..commentIndex] : line;
    }

    private sealed class TokenReader
    {
        private readonly List<string> _tokens;
        private int _index;

        public TokenReader(List<string> tokens)
        {
            _tokens = tokens;
        }

        public bool IsAtEnd => _index >= _tokens.Count;

        public string Peek() => _tokens[_index];

        public bool PeekIs(string token) => !IsAtEnd && _tokens[_index].Equals(token, StringComparison.OrdinalIgnoreCase);

        public bool PeekStartsWith(string prefix) => !IsAtEnd && _tokens[_index].StartsWith(prefix, StringComparison.OrdinalIgnoreCase);

        public bool TryReadLine(out string line)
        {
            if (IsAtEnd)
            {
                line = string.Empty;
                return false;
            }

            line = _tokens[_index++];
            return true;
        }

        public string ReadLine()
        {
            if (!TryReadLine(out var line))
            {
                throw new TextIrParseException("Unexpected end of input.");
            }

            return line;
        }

        public void Expect(string token)
        {
            var value = ReadLine();
            if (!value.Equals(token, StringComparison.OrdinalIgnoreCase))
            {
                throw new TextIrParseException($"Expected '{token}' but found '{value}'.");
            }
        }
    }
}
