using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using System.Text;
using ObjectIR.Core.IR;
using ObjectIR.Core.Serialization;

namespace SharpIR
{
    public class CSharpParser
    {
        private static bool IsComparisonOperator(Microsoft.CodeAnalysis.CSharp.SyntaxKind kind)
        {
            return kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.EqualsExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.NotEqualsExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.LessThanExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.LessThanOrEqualExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.GreaterThanExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.GreaterThanOrEqualExpression;
        }

        private static ComparisonOp MapComparisonOperator(Microsoft.CodeAnalysis.CSharp.SyntaxKind kind)
        {
            return kind switch
            {
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.EqualsExpression => ComparisonOp.Equal,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.NotEqualsExpression => ComparisonOp.NotEqual,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.LessThanExpression => ComparisonOp.Less,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.LessThanOrEqualExpression => ComparisonOp.LessOrEqual,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.GreaterThanExpression => ComparisonOp.Greater,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.GreaterThanOrEqualExpression => ComparisonOp.GreaterOrEqual,
                _ => throw new NotSupportedException("Unsupported comparison operator: " + kind)
            };
        }

        private static ArithmeticOp MapArithmeticOperator(Microsoft.CodeAnalysis.CSharp.SyntaxKind kind)
        {
            return kind switch
            {
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.AddExpression => ArithmeticOp.Add,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.SubtractExpression => ArithmeticOp.Sub,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.MultiplyExpression => ArithmeticOp.Mul,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.DivideExpression => ArithmeticOp.Div,
                Microsoft.CodeAnalysis.CSharp.SyntaxKind.ModuloExpression => ArithmeticOp.Rem,
                _ => throw new NotSupportedException("Unsupported arithmetic operator: " + kind)
            };
        }
        public class ParseOptions
        {
            public bool EmitJson { get; set; } = true;
            public bool EmitFob { get; set; } = false;
            public string? OutDir { get; set; } = null;
            public bool EmitTest { get; set; } = false;
        }
        public static void ParseFile(string filePath, ParseOptions? options = null)
        {
            options ??= new ParseOptions();
            Console.WriteLine($"Parsing file: {filePath}");
            // Read the file content
            string code = File.ReadAllText(filePath);
            // Parse the code into a SyntaxTree (the AST)
            SyntaxTree tree = CSharpSyntaxTree.ParseText(code);
            // Create compilation for semantic analysis
            var compilation = CSharpCompilation.Create("temp")
                .AddReferences(MetadataReference.CreateFromFile(typeof(object).Assembly.Location))
                .AddReferences(MetadataReference.CreateFromFile(typeof(Console).Assembly.Location))
                .AddReferences(MetadataReference.CreateFromFile(typeof(System.Linq.Enumerable).Assembly.Location))
                .AddReferences(MetadataReference.CreateFromFile(typeof(System.Collections.Generic.List<>).Assembly.Location))
                .AddSyntaxTrees(tree);
            var semanticModel = compilation.GetSemanticModel(tree);
            // Get the root of the AST
            CompilationUnitSyntax root = (CompilationUnitSyntax)tree.GetRoot();
            // Create ObjectIR module
            var module = new Module(Path.GetFileNameWithoutExtension(filePath));
            // Traverse the AST to extract information and build IR
            var walker = new CodeWalker(semanticModel, module, options.EmitTest);
            walker.Visit(root);
            // Output ObjectIR
            var serializer = new ModuleSerializer(module);
            var json = serializer.DumpToJson();
            Console.WriteLine("ObjectIR Output:");
            Console.WriteLine(json);

            // Determine output directory
            string outDir = options.OutDir ?? Path.GetDirectoryName(filePath) ?? ".";
            Directory.CreateDirectory(outDir);

            // write JSON if requested (or default behavior)
            if (options.EmitJson)
            {
                string outputPath = Path.Combine(outDir, Path.GetFileName(filePath) + ".ir.json");
                File.WriteAllText(outputPath, json);
            }

            // write FOB if requested
            if (options.EmitFob)
            {
                var fobData = serializer.DumpToFOB();
                string fobPath = Path.Combine(outDir, Path.GetFileName(filePath) + ".ir.fob");
                File.WriteAllBytes(fobPath, fobData);
            }
        }

        private static AccessModifier MapAccess(Microsoft.CodeAnalysis.Accessibility accessibility)
        {
            return accessibility switch
            {
                Microsoft.CodeAnalysis.Accessibility.Public => AccessModifier.Public,
                Microsoft.CodeAnalysis.Accessibility.Private => AccessModifier.Private,
                Microsoft.CodeAnalysis.Accessibility.Protected => AccessModifier.Protected,
                Microsoft.CodeAnalysis.Accessibility.Internal => AccessModifier.Internal,
                _ => AccessModifier.Public
            };
        }

        private static TypeReference MapType(ITypeSymbol typeSymbol)
        {
            if (typeSymbol is IArrayTypeSymbol arrayType)
            {
                return MapType(arrayType.ElementType).MakeArrayType();
            }
            if (typeSymbol is INamedTypeSymbol namedType)
            {
                if (namedType.IsGenericType)
                {
                    // TODO: Handle generics
                    return TypeReference.FromName(namedType.Name);
                }
                // Map built-in types
                switch (namedType.ToDisplayString())
                {
                    case "void": return TypeReference.Void;
                    case "bool": return TypeReference.Bool;
                    case "int": return TypeReference.Int32;
                    case "long": return TypeReference.Int64;
                    case "short": return TypeReference.Int16;
                    case "byte": return TypeReference.UInt8;
                    case "sbyte": return TypeReference.Int8;
                    case "uint": return TypeReference.UInt32;
                    case "ulong": return TypeReference.UInt64;
                    case "ushort": return TypeReference.UInt16;
                    case "float": return TypeReference.Float32;
                    case "double": return TypeReference.Float64;
                    case "char": return TypeReference.Char;
                    case "string": return TypeReference.String;
                    default: return TypeReference.FromName(namedType.ToDisplayString());
                }
            }
            return TypeReference.FromName(typeSymbol.ToDisplayString());
        }

        private class CodeWalker : CSharpSyntaxWalker
        {
            private readonly SemanticModel _semanticModel;
            private readonly Module _module;
            private readonly Dictionary<string, TypeDefinition> _typeMap = new();
            private readonly List<string> _expectedOutputs = new();

            public CodeWalker(SemanticModel semanticModel, Module module, bool collectConsoleWrites)
            {
                _semanticModel = semanticModel;
                _module = module;
                if (!collectConsoleWrites)
                {
                    // keep expectedOutputs empty
                }
            }

            public List<string> ExpectedOutputs => _expectedOutputs;

            public override void VisitClassDeclaration(ClassDeclarationSyntax node)
            {
                var symbol = _semanticModel.GetDeclaredSymbol(node);
                if (symbol != null)
                {
                    var classDef = _module.DefineClass(symbol.Name);
                    classDef.Namespace = symbol.ContainingNamespace?.ToDisplayString();
                    classDef.Access = MapAccess(symbol.DeclaredAccessibility);
                    _typeMap[symbol.ToDisplayString()] = classDef;
                    // TODO: Handle base types and interfaces
                    // For now, set BodySource for methods
                }
                base.VisitClassDeclaration(node);
            }

            public override void VisitInterfaceDeclaration(InterfaceDeclarationSyntax node)
            {
                var symbol = _semanticModel.GetDeclaredSymbol(node);
                if (symbol != null)
                {
                    var interfaceDef = _module.DefineInterface(symbol.Name);
                    interfaceDef.Namespace = symbol.ContainingNamespace?.ToDisplayString();
                    interfaceDef.Access = MapAccess(symbol.DeclaredAccessibility);
                    _typeMap[symbol.ToDisplayString()] = interfaceDef;
                }
                base.VisitInterfaceDeclaration(node);
            }

            public override void VisitStructDeclaration(StructDeclarationSyntax node)
            {
                var symbol = _semanticModel.GetDeclaredSymbol(node);
                if (symbol != null)
                {
                    var structDef = _module.DefineStruct(symbol.Name);
                    structDef.Namespace = symbol.ContainingNamespace?.ToDisplayString();
                    structDef.Access = MapAccess(symbol.DeclaredAccessibility);
                    _typeMap[symbol.ToDisplayString()] = structDef;
                }
                base.VisitStructDeclaration(node);
            }

            public override void VisitEnumDeclaration(EnumDeclarationSyntax node)
            {
                // TODO: Implement enum handling
                base.VisitEnumDeclaration(node);
            }

            public override void VisitMethodDeclaration(MethodDeclarationSyntax node)
            {
                var symbol = _semanticModel.GetDeclaredSymbol(node);
                if (symbol != null && symbol.ContainingType != null && _typeMap.TryGetValue(symbol.ContainingType.ToDisplayString(), out var typeDef))
                {
                    if (typeDef is ClassDefinition classDef)
                    {
                        MethodDefinition methodDef;
                        if (symbol.MethodKind == MethodKind.Constructor)
                        {
                            methodDef = classDef.DefineConstructor();
                            methodDef.IsConstructor = true;
                        }
                        else
                        {
                            methodDef = classDef.DefineMethod(symbol.Name, CSharpParser.MapType(symbol.ReturnType));
                        }
                        methodDef.Access = MapAccess(symbol.DeclaredAccessibility);
                        methodDef.IsStatic = symbol.IsStatic;
                        methodDef.IsVirtual = symbol.IsVirtual;
                        methodDef.IsOverride = symbol.IsOverride;
                        methodDef.IsAbstract = symbol.IsAbstract;
                        foreach (var param in symbol.Parameters)
                        {
                            methodDef.DefineParameter(param.Name, CSharpParser.MapType(param.Type));
                        }
                        // Compile body if present
                        if (node.Body != null)
                        {
                            CompileBody(methodDef, node.Body, symbol);
                        }
                        else if (node.ExpressionBody != null)
                        {
                            var compiler = new BodyCompiler(_semanticModel, symbol, methodDef, _expectedOutputs);
                            compiler.CompileExpression(node.ExpressionBody.Expression);
                            methodDef.Instructions.Add(new ReturnInstruction(null));
                        }
                    }
                }
                base.VisitMethodDeclaration(node);
            }

            public override void VisitPropertyDeclaration(PropertyDeclarationSyntax node)
            {
                var symbol = _semanticModel.GetDeclaredSymbol(node);
                if (symbol != null && symbol.ContainingType != null && _typeMap.TryGetValue(symbol.ContainingType.ToDisplayString(), out var typeDef))
                {
                    if (typeDef is ClassDefinition classDef)
                    {
                        var propDef = new PropertyDefinition(symbol.Name, MapType(symbol.Type));
                        propDef.Access = MapAccess(symbol.DeclaredAccessibility);
                        classDef.Properties.Add(propDef);
                        // Handle getter/setter if accessor bodies are provided
                        if (node.AccessorList != null)
                        {
                            foreach (var acc in node.AccessorList.Accessors)
                            {
                                if (acc.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.GetAccessorDeclaration)
                                {
                                    // define getter method get_<PropertyName>
                                    var getter = classDef.DefineMethod($"get_{symbol.Name}", MapType(symbol.Type));
                                    getter.IsStatic = symbol.IsStatic;
                                    propDef.Getter = getter;
                                    if (acc.Body != null)
                                    {
                                        var getterCompiler = new BodyCompiler(_semanticModel, symbol.GetMethod!, getter, _expectedOutputs, getter.Instructions);
                                        getterCompiler.Compile(acc.Body);
                                    }
                                    else if (acc.ExpressionBody != null)
                                    {
                                        var getterCompiler = new BodyCompiler(_semanticModel, symbol.GetMethod!, getter, _expectedOutputs, getter.Instructions);
                                        getterCompiler.CompileExpression(acc.ExpressionBody.Expression);
                                        getter.Instructions.Add(new ReturnInstruction(null));
                                    }
                                    else
                                    {
                                        // Auto-property: create backing field
                                        var backing = classDef.DefineField("_" + char.ToLower(symbol.Name[0]) + symbol.Name.Substring(1), MapType(symbol.Type));
                                        backing.Access = AccessModifier.Private;
                                        getter.Instructions.Add(new LoadFieldInstruction(new FieldReference(TypeReference.FromName(classDef.GetQualifiedName()), backing.Name, backing.Type)));
                                        getter.Instructions.Add(new ReturnInstruction(null));
                                    }
                                }
                                else if (acc.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.SetAccessorDeclaration)
                                {
                                    var setter = classDef.DefineMethod($"set_{symbol.Name}", TypeReference.Void);
                                    setter.IsStatic = symbol.IsStatic;
                                    setter.DefineParameter("value", MapType(symbol.Type));
                                    propDef.Setter = setter;
                                    if (acc.Body != null)
                                    {
                                        var setterCompiler = new BodyCompiler(_semanticModel, symbol.SetMethod!, setter, _expectedOutputs, setter.Instructions);
                                        setterCompiler.Compile(acc.Body);
                                    }
                                    else if (acc.ExpressionBody != null)
                                    {
                                        var setterCompiler = new BodyCompiler(_semanticModel, symbol.SetMethod!, setter, _expectedOutputs, setter.Instructions);
                                        setterCompiler.CompileExpression(acc.ExpressionBody.Expression);
                                    }
                                    else
                                    {
                                        // Auto-property: create backing field if not present, and set it
                                        var backing = classDef.Fields.FirstOrDefault(f => f.Name == "_" + char.ToLower(symbol.Name[0]) + symbol.Name.Substring(1));
                                        if (backing == null)
                                        {
                                            backing = classDef.DefineField("_" + char.ToLower(symbol.Name[0]) + symbol.Name.Substring(1), MapType(symbol.Type));
                                            backing.Access = AccessModifier.Private;
                                        }
                                        setter.Instructions.Add(new LoadArgInstruction(setter.IsStatic ? 0 : 1));
                                        // Ensure 'this' is on the stack as instance for the store operation
                                        setter.Instructions.Add(new LoadArgInstruction(0));
                                        setter.Instructions.Add(new StoreFieldInstruction(new FieldReference(TypeReference.FromName(classDef.GetQualifiedName()), backing.Name, backing.Type)));
                                    }
                                }
                            }
                        }
                    }
                }
                base.VisitPropertyDeclaration(node);
            }

            public override void VisitFieldDeclaration(FieldDeclarationSyntax node)
            {
                var symbol = _semanticModel.GetDeclaredSymbol(node);
                if (symbol is IFieldSymbol fieldSymbol && fieldSymbol.ContainingType != null && _typeMap.TryGetValue(fieldSymbol.ContainingType.ToDisplayString(), out var typeDef))
                {
                    if (typeDef is ClassDefinition classDef)
                    {
                        foreach (var variable in node.Declaration.Variables)
                        {
                            var fieldDef = classDef.DefineField(variable.Identifier.Text, MapType(fieldSymbol.Type));
                            fieldDef.Access = MapAccess(fieldSymbol.DeclaredAccessibility);
                            fieldDef.IsStatic = fieldSymbol.IsStatic;
                            fieldDef.IsReadOnly = fieldSymbol.IsReadOnly;
                        }
                    }
                }
                base.VisitFieldDeclaration(node);
            }

            private void CompileBody(MethodDefinition method, BlockSyntax body, IMethodSymbol methodSymbol)
            {
                var compiler = new BodyCompiler(_semanticModel, methodSymbol, method, _expectedOutputs);
                compiler.Compile(body);
            }

            private class BodyCompiler
            {
                internal readonly SemanticModel _semanticModel;
                private readonly MethodDefinition _method;
                private readonly IMethodSymbol _methodSymbol;
                private readonly InstructionList _target;
                private readonly List<string>? _expectedOutputs;
                private readonly Dictionary<string, int> _argIndices = new();

                public BodyCompiler(SemanticModel semanticModel, IMethodSymbol methodSymbol, MethodDefinition method, List<string>? expectedOutputs = null, InstructionList? target = null)
                {
                    _semanticModel = semanticModel;
                    _methodSymbol = methodSymbol;
                    _method = method;
                    _expectedOutputs = expectedOutputs;
                    _target = target ?? method.Instructions;

                    // Build argument indices
                    int idx = 0;
                    if (!_methodSymbol.IsStatic)
                    {
                        _argIndices["this"] = idx++;
                    }
                    foreach (var p in _methodSymbol.Parameters)
                    {
                        _argIndices[p.Name] = idx++;
                    }
                }

                public void Compile(BlockSyntax body)
                {
                    foreach (var statement in body.Statements)
                    {
                        CompileStatement(statement);
                    }
                }

                public void CompileStatement(StatementSyntax statement)
                {
                    switch (statement)
                    {
                        case ExpressionStatementSyntax exprStmt:
                            CompileExpression(exprStmt.Expression);
                            // Pop the result if not void and if the expression is not an invocation or assignment
                            // Invocation results may be void or print output; assignment results are stored
                            if (!(exprStmt.Expression is InvocationExpressionSyntax) && !(exprStmt.Expression is AssignmentExpressionSyntax))
                            {
                                _target.Add(new PopInstruction());
                            }
                            break;
                        case ReturnStatementSyntax returnStmt:
                            if (returnStmt.Expression != null)
                            {
                                CompileExpression(returnStmt.Expression);
                            }
                            _target.Add(new ReturnInstruction(null));
                            break;
                        case LocalDeclarationStatementSyntax localDecl:
                            foreach (var variable in localDecl.Declaration.Variables)
                            {
                                var varType = localDecl.Declaration.Type;
                                TypeReference typeRef;
                                if (varType is IdentifierNameSyntax id && id.Identifier.Text == "var")
                                {
                                    // try to resolve type from initializer
                                    if (variable.Initializer != null)
                                    {
                                        var initSymbol = _semanticModel.GetTypeInfo(variable.Initializer.Value).Type;
                                        typeRef = CSharpParser.MapType(initSymbol);
                                    }
                                    else
                                    {
                                        typeRef = TypeReference.FromName("object");
                                    }
                                }
                                else
                                {
                                    var typeSymbol = _semanticModel.GetTypeInfo(varType).Type;
                                    typeRef = CSharpParser.MapType(typeSymbol);
                                }
                                _method.DefineLocal(variable.Identifier.Text, typeRef);
                                if (variable.Initializer != null)
                                {
                                    CompileExpression(variable.Initializer.Value);
                                    _target.Add(new StoreLocalInstruction(variable.Identifier.Text));
                                }
                            }
                            break;
                        case IfStatementSyntax ifStmt:
                            // compile condition into stack
                            CompileExpression(ifStmt.Condition);
                            var cond = Condition.Stack();
                            var ifInst = new IfInstruction(cond);
                            // then block
                            if (ifStmt.Statement is BlockSyntax thenBlock)
                            {
                                var thenCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, ifInst.ThenBlock);
                                thenCompiler.Compile(thenBlock);
                            }
                            else if (ifStmt.Statement != null)
                            {
                                var thenCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, ifInst.ThenBlock);
                                thenCompiler.CompileStatement(ifStmt.Statement);
                            }
                            // else block
                            if (ifStmt.Else != null)
                            {
                                if (ifStmt.Else.Statement is BlockSyntax elseBlock)
                                {
                                    ifInst.ElseBlock = new InstructionList();
                                    var elseCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, ifInst.ElseBlock);
                                    elseCompiler.Compile(elseBlock);
                                }
                                else if (ifStmt.Else.Statement != null)
                                {
                                    ifInst.ElseBlock = new InstructionList();
                                    var elseCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, ifInst.ElseBlock);
                                    elseCompiler.CompileStatement(ifStmt.Else.Statement);
                                }
                            }
                            _target.Add(ifInst);
                            break;
                        case WhileStatementSyntax whileStmt:
                            // handle simple binary comparison case specially
                            if (whileStmt.Condition is BinaryExpressionSyntax be && IsComparisonOperator(be.OperatorToken.Kind()))
                            {
                                // compile left and right as setup instructions
                                CompileExpression(be.Left);
                                CompileExpression(be.Right);
                                var compOp = MapComparisonOperator(be.OperatorToken.Kind());
                                var whileInst = new WhileInstruction(Condition.Binary(compOp));
                                var bodyCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, whileInst.Body);
                                if (whileStmt.Statement is BlockSyntax bodyBlock)
                                {
                                    bodyCompiler.Compile(bodyBlock);
                                }
                                else if (whileStmt.Statement != null)
                                {
                                    // single statement
                                    bodyCompiler.CompileStatement(whileStmt.Statement);
                                }
                                _target.Add(whileInst);
                            }
                            else
                            {
                                // fallback: compile condition, use stack condition
                                CompileExpression(whileStmt.Condition);
                                var wi = new WhileInstruction(Condition.Stack());
                                var bodyCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, wi.Body);
                                if (whileStmt.Statement is BlockSyntax bodyBlock)
                                {
                                    bodyCompiler.Compile(bodyBlock);
                                }
                                else if (whileStmt.Statement != null)
                                {
                                    bodyCompiler.CompileStatement(whileStmt.Statement);
                                }
                                _target.Add(wi);
                            }
                            break;
                        case ForStatementSyntax forStmt:
                            // emit initializers
                            foreach (var init in forStmt.Declaration?.Variables ?? Enumerable.Empty<VariableDeclaratorSyntax>())
                            {
                                var declaredType = forStmt.Declaration.Type;
                                TypeReference vtype;
                                if (declaredType is IdentifierNameSyntax id && id.Identifier.Text == "var")
                                {
                                    if (init.Initializer != null)
                                    {
                                        vtype = CSharpParser.MapType(_semanticModel.GetTypeInfo(init.Initializer.Value).Type);
                                    }
                                    else vtype = TypeReference.FromName("object");
                                }
                                else
                                {
                                    vtype = CSharpParser.MapType(_semanticModel.GetTypeInfo(declaredType).Type);
                                }
                                _method.DefineLocal(init.Identifier.Text, vtype);
                                if (init.Initializer != null)
                                {
                                    CompileExpression(init.Initializer.Value);
                                    _target.Add(new StoreLocalInstruction(init.Identifier.Text));
                                }
                            }
                            // Condition: simple binary case only
                            if (forStmt.Condition is BinaryExpressionSyntax fbe && IsComparisonOperator(fbe.OperatorToken.Kind()))
                            {
                                // Emit left and right as setup
                                CompileExpression(fbe.Left);
                                CompileExpression(fbe.Right);
                                var comp = MapComparisonOperator(fbe.OperatorToken.Kind());
                                var forWhile = new WhileInstruction(Condition.Binary(comp));
                                // Body: compile original body then increments
                                var bodyCompiler = new BodyCompiler(_semanticModel, _methodSymbol, _method, _expectedOutputs, forWhile.Body);
                                if (forStmt.Statement is BlockSyntax fbody)
                                {
                                    bodyCompiler.Compile(fbody);
                                }
                                else if (forStmt.Statement != null)
                                {
                                    bodyCompiler.CompileStatement(forStmt.Statement);
                                }
                                // append incrementors
                                foreach (var inc in forStmt.Incrementors)
                                {
                                    bodyCompiler.CompileExpression(inc);
                                }
                                _target.Add(forWhile);
                            }
                            break;
                        case BreakStatementSyntax brk:
                            _target.Add(new BreakInstruction());
                            break;
                        case ContinueStatementSyntax cont:
                            _target.Add(new ContinueInstruction());
                            break;
                        // TODO: Add more statement types
                        default:
                            // For now, skip
                            break;
                    }
                }

                public void CompileExpression(ExpressionSyntax expression)
                {
                    // Defensive check: ensure the expression is from the same syntax tree as the semantic model
                    if (expression.SyntaxTree != _semanticModel.SyntaxTree)
                    {
                        Console.WriteLine($"Warning: Syntax node {expression.Kind()} at {expression.GetLocation()} is not in the expected syntax tree. Skipping compilation for this expression.");
                        _target.Add(new LoadNullInstruction());
                        return;
                    }

                    switch (expression)
                    {
                                                case BinaryExpressionSyntax binary:
                                                    // arithmetic
                                                    if (binary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.PlusToken ||
                                                        binary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.MinusToken ||
                                                        binary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.AsteriskToken ||
                                                        binary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.SlashToken ||
                                                        binary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.PercentToken)
                                                    {
                                                        CompileExpression(binary.Left);
                                                        CompileExpression(binary.Right);
                                                        var op = MapArithmeticOperator(binary.Kind());
                                                        _target.Add(new ArithmeticInstruction(op));
                                                        break;
                                                    }
                                                    // comparison
                                                    if (IsComparisonOperator(binary.Kind()))
                                                    {
                                                        CompileExpression(binary.Left);
                                                        CompileExpression(binary.Right);
                                                        var comp = MapComparisonOperator(binary.Kind());
                                                        _target.Add(new ComparisonInstruction(comp));
                                                        break;
                                                    }
                                                    // fallback: compile left and right then leave null
                                                    CompileExpression(binary.Left);
                                                    CompileExpression(binary.Right);
                                                    _target.Add(new LoadNullInstruction());
                                                    break;
                                                case PrefixUnaryExpressionSyntax unary:
                                                    // unary minus or not
                                                    if (unary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.MinusToken)
                                                    {
                                                        CompileExpression(unary.Operand);
                                                        _target.Add(new UnaryNegateInstruction());
                                                    }
                                                    else if (unary.OperatorToken.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.ExclamationToken)
                                                    {
                                                        CompileExpression(unary.Operand);
                                                        _target.Add(new UnaryNotInstruction());
                                                    }
                                                    else
                                                    {
                                                        _target.Add(new LoadNullInstruction());
                                                    }
                                                    break;
                        case InvocationExpressionSyntax invocation:
                            CompileInvocation(invocation);
                            break;
                        case ObjectCreationExpressionSyntax objCreation:
                            {
                                // Create new object and leave it on the stack
                                TypeInfo typeInfo;
                                try
                                {
                                    typeInfo = _semanticModel.GetTypeInfo(objCreation);
                                }
                                catch (ArgumentException ex) when (ex.Message.Contains("not within syntax tree"))
                                {
                                    Console.WriteLine($"Warning: Failed to get type info for object creation at {objCreation.GetLocation()}: {ex.Message}. Using default type.");
                                    typeInfo = default;
                                }
                                var typeRef = typeInfo.Type != null ? CSharpParser.MapType(typeInfo.Type) : TypeReference.FromName("object");
                                _target.Add(new NewObjectInstruction(typeRef));

                                // If there's an initializer, apply property setters / field stores
                                if (objCreation.Initializer != null)
                                {
                                    foreach (var expr in objCreation.Initializer.Expressions)
                                    {
                                        if (expr is AssignmentExpressionSyntax assign)
                                        {
                                            // Verify the assignment is from the correct syntax tree
                                            if (assign.SyntaxTree != _semanticModel.SyntaxTree)
                                            {
                                                Console.WriteLine($"Warning: Assignment in object initializer at {assign.GetLocation()} is not in the expected syntax tree. Skipping.");
                                                continue;
                                            }

                                            // Instance duplication to keep object for subsequent initializers
                                            _target.Add(new DupInstruction());
                                            // Compile the RHS value
                                            CompileExpression(assign.Right);
                                            // Determine property/field symbol for the left-hand side
                                            var leftSym = _semanticModel.GetSymbolInfo(assign.Left).Symbol;
                                            if (leftSym is IPropertySymbol propSym)
                                            {
                                                var declaringType = propSym.ContainingType != null ? CSharpParser.MapType(propSym.ContainingType) : TypeReference.FromName("object");
                                                var setterRef = new MethodReference(
                                                    declaringType,
                                                    $"set_{propSym.Name}",
                                                    TypeReference.Void,
                                                    new List<TypeReference> { CSharpParser.MapType(propSym.Type) }
                                                );
                                                _target.Add(new CallVirtualInstruction(setterRef));
                                            }
                                            else if (leftSym is IFieldSymbol fieldSym)
                                            {
                                                var declaringFieldType = fieldSym.ContainingType != null ? CSharpParser.MapType(fieldSym.ContainingType) : TypeReference.FromName("object");
                                                var fieldRef = new FieldReference(
                                                    declaringFieldType,
                                                    fieldSym.Name,
                                                    CSharpParser.MapType(fieldSym.Type)
                                                );
                                                // Use StoreFieldInstruction; note that the runtime currently prefers property setters
                                                _target.Add(new StoreFieldInstruction(fieldRef));
                                            }
                                            else
                                            {
                                                // Unsupported initializer type - consume value and ignore
                                                _target.Add(new PopInstruction());
                                                _target.Add(new PopInstruction());
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                        case LiteralExpressionSyntax literal:
                            CompileLiteral(literal);
                            break;
                        case IdentifierNameSyntax identifier:
                            CompileIdentifier(identifier);
                            break;
                        case InterpolatedStringExpressionSyntax interpolated:
                            CompileInterpolatedString(interpolated);
                            break;
                        case AssignmentExpressionSyntax assignment:
                            CompileAssignment(assignment);
                            break;
                        case MemberAccessExpressionSyntax memberAccess:
                            CompileMemberAccess(memberAccess, _target, this);
                            break;
                        // TODO: Add more expression types
                        default:
                            // For now, load null
                                _target.Add(new LoadNullInstruction());
                            break;
                    }
                }

                private void CompileInvocation(InvocationExpressionSyntax invocation)
                {
                    var symbol = _semanticModel.GetSymbolInfo(invocation).Symbol as IMethodSymbol;
                    if (symbol != null)
                    {
                        // If collecting console writes for test output, capture simple string literal arguments
                        try
                        {
                            if (_expectedOutputs != null && symbol.ContainingType != null && symbol.ContainingType.ToDisplayString() == "System.Console" && symbol.Name == "WriteLine")
                            {
                                if (invocation.ArgumentList.Arguments.Count == 1)
                                {
                                    var argExpr = invocation.ArgumentList.Arguments[0].Expression;
                                    if (argExpr is LiteralExpressionSyntax lit)
                                    {
                                        var cv = _semanticModel.GetConstantValue(lit);
                                        if (cv.HasValue && cv.Value is string s)
                                        {
                                            _expectedOutputs.Add(s);
                                        }
                                    }
                                }
                            }
                        }
                        catch
                        {
                            // be forgiving and don't crash on semantic issues
                        }
                        // If the callable is on an instance (member access), compile the instance first
                        if (invocation.Expression is MemberAccessExpressionSyntax memberAccess && !(symbol.IsStatic))
                        {
                            CompileExpression(memberAccess.Expression);
                        }
                        // Compile arguments (pushed after the instance)
                        foreach (var arg in invocation.ArgumentList.Arguments)
                        {
                            CompileExpression(arg.Expression);
                        }
                        // Emit call
                        var methodRef = new MethodReference(
                            CSharpParser.MapType(symbol.ContainingType),
                            symbol.Name,
                            CSharpParser.MapType(symbol.ReturnType),
                            symbol.Parameters.Select(p => CSharpParser.MapType(p.Type)).ToList()
                        );
                        if (symbol.IsStatic)
                        {
                            _target.Add(new CallInstruction(methodRef));
                        }
                        else
                        {
                            _target.Add(new CallVirtualInstruction(methodRef));
                        }
                    }
                }

                private void CompileLiteral(LiteralExpressionSyntax literal)
                {
                    var value = _semanticModel.GetConstantValue(literal).Value;
                    var type = CSharpParser.MapType(_semanticModel.GetTypeInfo(literal).Type);
                    _target.Add(new LoadConstantInstruction(value, type));
                }

                private void CompileIdentifier(IdentifierNameSyntax identifier)
                {
                    var symbol = _semanticModel.GetSymbolInfo(identifier).Symbol;
                        if (symbol is IParameterSymbol param)
                        {
                            _target.Add(new LoadArgInstruction(_argIndices[param.Name]));
                        }
                    else if (symbol is ILocalSymbol local)
                    {
                            _target.Add(new LoadLocalInstruction(local.Name));
                    }
                        else if (symbol is IFieldSymbol field)
                    {
                        var fieldRef = new FieldReference(CSharpParser.MapType(field.ContainingType), field.Name, CSharpParser.MapType(field.Type));
                        if (field.IsStatic)
                        {
                                _target.Add(new LoadStaticFieldInstruction(fieldRef));
                        }
                        else if (symbol is IPropertySymbol propSym)
                        {
                            // property assignment to property on this
                            // call set_PropertyName(this, value)
                            // value already on the stack
                            var setterRef = new MethodReference(CSharpParser.MapType(propSym.ContainingType), $"set_{propSym.Name}", TypeReference.Void, new List<TypeReference> { CSharpParser.MapType(propSym.Type) });
                            _target.Add(new LoadArgInstruction(_argIndices["this"]));
                            _target.Add(new CallVirtualInstruction(setterRef));
                        }
                        else
                        {
                            // Need 'this' on stack
                                _target.Add(new LoadArgInstruction(_argIndices["this"]));
                                _target.Add(new LoadFieldInstruction(fieldRef));
                        }
                    }
                    else if (symbol is IPropertySymbol prop)
                    {
                        var methodRef = new MethodReference(CSharpParser.MapType(prop.ContainingType), $"get_{prop.Name}", CSharpParser.MapType(prop.Type), new List<TypeReference>());
                        if (prop.IsStatic)
                        {
                            _target.Add(new CallInstruction(methodRef));
                        }
                        else
                        {
                            _target.Add(new LoadArgInstruction(_argIndices["this"]));
                            _target.Add(new CallVirtualInstruction(methodRef));
                        }
                    }
                }

                private void CompileInterpolatedString(InterpolatedStringExpressionSyntax interpolated)
                {
                    var expressions = new List<ExpressionSyntax>();
                    foreach (var content in interpolated.Contents)
                    {
                        if (content is InterpolatedStringTextSyntax text)
                        {
                            // Emit load constant directly
                            _target.Add(new LoadConstantInstruction(text.TextToken.ValueText, TypeReference.String));
                        }
                        else if (content is InterpolationSyntax interp)
                        {
                            CompileExpression(interp.Expression);
                        }
                    }
                    // If more than one on stack, call Concat
                    int count = interpolated.Contents.Count(c => c is InterpolatedStringTextSyntax) + interpolated.Contents.Count(c => c is InterpolationSyntax);
                    if (count > 1)
                    {
                        var concatMethod = new MethodReference(TypeReference.String, "Concat", TypeReference.String, 
                            Enumerable.Repeat(TypeReference.String, count).ToList());
                        _target.Add(new CallInstruction(concatMethod));
                    }
                }

                private void CompileAssignment(AssignmentExpressionSyntax assignment)
                {
                    var kind = assignment.Kind();
                    bool isCompound = kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.AddAssignmentExpression ||
                                      kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.SubtractAssignmentExpression ||
                                      kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.MultiplyAssignmentExpression ||
                                      kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.DivideAssignmentExpression ||
                                      kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.ModuloAssignmentExpression;
                    // handle compound assignments e.g., +=
                    if (isCompound)
                    {
                        // Compound assignment: LHS is either local/param or member access
                        if (assignment.Left is IdentifierNameSyntax id)
                        {
                            CompileIdentifier(id);
                        }
                        else if (assignment.Left is MemberAccessExpressionSyntax mac)
                        {
                            // For compound assignment on member access: push instance, duplicate it, then load current value
                            CompileExpression(mac.Expression);
                            _target.Add(new DupInstruction());
                            var macSym = _semanticModel.GetSymbolInfo(mac).Symbol;
                            var macFieldSym = macSym as IFieldSymbol;
                            var macPropSym = macSym as IPropertySymbol;
                            if (macPropSym != null)
                            {
                                var getter = new MethodReference(CSharpParser.MapType(macPropSym.ContainingType), $"get_{macPropSym.Name}", CSharpParser.MapType(macPropSym.Type), new List<TypeReference>());
                                _target.Add(new CallVirtualInstruction(getter));
                            }
                            else if (macFieldSym != null)
                            {
                                var fieldRef = new FieldReference(CSharpParser.MapType(macFieldSym.ContainingType), macFieldSym.Name, CSharpParser.MapType(macFieldSym.Type));
                                _target.Add(new LoadFieldInstruction(fieldRef));
                            }
                        }
                        // compile RHS
                        CompileExpression(assignment.Right);
                        // emit arithmetic op
                        ArithmeticOp aop = kind switch
                        {
                            Microsoft.CodeAnalysis.CSharp.SyntaxKind.AddAssignmentExpression => ArithmeticOp.Add,
                            Microsoft.CodeAnalysis.CSharp.SyntaxKind.SubtractAssignmentExpression => ArithmeticOp.Sub,
                            Microsoft.CodeAnalysis.CSharp.SyntaxKind.MultiplyAssignmentExpression => ArithmeticOp.Mul,
                            Microsoft.CodeAnalysis.CSharp.SyntaxKind.DivideAssignmentExpression => ArithmeticOp.Div,
                            Microsoft.CodeAnalysis.CSharp.SyntaxKind.ModuloAssignmentExpression => ArithmeticOp.Rem,
                            _ => throw new NotSupportedException("Unsupported compound assignment")
                        };
                        _target.Add(new ArithmeticInstruction(aop));
                        // fall through to store into LHS
                    }
                    else
                    {
                        // Simple assignment: if LHS is a member access, compile instance first, then RHS, so stack is [instance, value]
                        if (assignment.Left is MemberAccessExpressionSyntax ma)
                        {
                            CompileExpression(ma.Expression);
                            CompileExpression(assignment.Right);
                        }
                        else
                        {
                            // Compile right side
                            CompileExpression(assignment.Right);
                        }
                    }
                    // Store to left
                    if (assignment.Left is IdentifierNameSyntax identifier)
                    {
                        var symbol = _semanticModel.GetSymbolInfo(identifier).Symbol;
                        if (symbol is IParameterSymbol param)
                        {
                            _target.Add(new StoreArgInstruction(param.Name));
                        }
                        else if (symbol is ILocalSymbol local)
                        {
                            _target.Add(new StoreLocalInstruction(local.Name));
                        }
                        else if (symbol is IFieldSymbol field)
                        {
                            var fieldRef = new FieldReference(CSharpParser.MapType(field.ContainingType), field.Name, CSharpParser.MapType(field.Type));
                            if (field.IsStatic)
                            {
                                _target.Add(new StoreStaticFieldInstruction(fieldRef));
                            }
                            else
                            {
                                _target.Add(new LoadArgInstruction(_argIndices["this"]));
                                _target.Add(new StoreFieldInstruction(fieldRef));
                            }
                        }
                    }
                    else if (assignment.Left is MemberAccessExpressionSyntax memberAccess)
                    {
                        var macSym = _semanticModel.GetSymbolInfo(memberAccess).Symbol;
                        var macFieldSym = macSym as IFieldSymbol;
                        var macPropSym = macSym as IPropertySymbol;
                        // If target is a 'this' member, use 'this' for store
                        if (memberAccess.Expression is ThisExpressionSyntax)
                        {
                            if (macFieldSym != null)
                            {
                                var fieldRef = new FieldReference(CSharpParser.MapType(macFieldSym.ContainingType), macFieldSym.Name, CSharpParser.MapType(macFieldSym.Type));
                                _target.Add(new LoadArgInstruction(_argIndices["this"]));
                                _target.Add(new StoreFieldInstruction(fieldRef));
                            }
                            else if (macPropSym != null)
                            {
                                var setter = new MethodReference(CSharpParser.MapType(macPropSym.ContainingType), $"set_{macPropSym.Name}", TypeReference.Void, new List<TypeReference> { CSharpParser.MapType(macPropSym.Type) });
                                _target.Add(new LoadArgInstruction(_argIndices["this"]));
                                _target.Add(new CallVirtualInstruction(setter));
                            }
                        }
                        else if (macFieldSym != null || macPropSym != null)
                        {
                            // For both compound and simple assignment, after above logic, stack is [instance, value]
                            if (macPropSym != null)
                            {
                                var setter = new MethodReference(CSharpParser.MapType(macPropSym.ContainingType), $"set_{macPropSym.Name}", TypeReference.Void, new List<TypeReference> { CSharpParser.MapType(macPropSym.Type) });
                                _target.Add(new CallVirtualInstruction(setter));
                            }
                            else if (macFieldSym != null)
                            {
                                var fieldRef = new FieldReference(CSharpParser.MapType(macFieldSym.ContainingType), macFieldSym.Name, CSharpParser.MapType(macFieldSym.Type));
                                _target.Add(new StoreFieldInstruction(fieldRef));
                            }
                        }
                    }
                    }
                }

                private static void CompileMemberAccess(MemberAccessExpressionSyntax memberAccess, InstructionList target, BodyCompiler compiler)
                {
                    var symbol = compiler._semanticModel.GetSymbolInfo(memberAccess).Symbol;
                    if (symbol is IFieldSymbol field)
                    {
                        var fieldRef = new FieldReference(CSharpParser.MapType(field.ContainingType), field.Name, CSharpParser.MapType(field.Type));
                        if (field.IsStatic)
                        {
                            target.Add(new LoadStaticFieldInstruction(fieldRef));
                        }
                        else
                        {
                            compiler.CompileExpression(memberAccess.Expression);
                            target.Add(new LoadFieldInstruction(fieldRef));
                        }
                    }
                    else if (symbol is IPropertySymbol prop)
                    {
                        var methodRef = new MethodReference(CSharpParser.MapType(prop.ContainingType), $"get_{prop.Name}", CSharpParser.MapType(prop.Type), new List<TypeReference>());
                        if (prop.IsStatic)
                        {
                            target.Add(new CallInstruction(methodRef));
                        }
                        else
                        {
                            compiler.CompileExpression(memberAccess.Expression);
                            target.Add(new CallVirtualInstruction(methodRef));
                        }
                    }
                }
            }
        }
    }