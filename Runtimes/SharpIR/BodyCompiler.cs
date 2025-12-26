using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System;
using System.Collections.Generic;
using System.Linq;
using ObjectIR.Core.IR;

namespace SharpIR
{
    internal class BodyCompiler
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
                                typeRef = Helpers.MapType(initSymbol);
                            }
                            else
                            {
                                typeRef = TypeReference.FromName("object");
                            }
                        }
                        else
                        {
                            var typeSymbol = _semanticModel.GetTypeInfo(varType).Type;
                            typeRef = Helpers.MapType(typeSymbol);
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
                    if (whileStmt.Condition is BinaryExpressionSyntax be && Helpers.IsComparisonOperator(be.OperatorToken.Kind()))
                    {
                        // compile left and right as setup instructions
                        CompileExpression(be.Left);
                        CompileExpression(be.Right);
                        var compOp = Helpers.MapComparisonOperator(be.OperatorToken.Kind());
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
                                vtype = Helpers.MapType(_semanticModel.GetTypeInfo(init.Initializer.Value).Type);
                            }
                            else vtype = TypeReference.FromName("object");
                        }
                        else
                        {
                            vtype = Helpers.MapType(_semanticModel.GetTypeInfo(declaredType).Type);
                        }
                        _method.DefineLocal(init.Identifier.Text, vtype);
                        if (init.Initializer != null)
                        {
                            CompileExpression(init.Initializer.Value);
                            _target.Add(new StoreLocalInstruction(init.Identifier.Text));
                        }
                    }
                    // Condition: simple binary case only
                    if (forStmt.Condition is BinaryExpressionSyntax fbe && Helpers.IsComparisonOperator(fbe.OperatorToken.Kind()))
                    {
                        // Emit left and right as setup
                        CompileExpression(fbe.Left);
                        CompileExpression(fbe.Right);
                        var comp = Helpers.MapComparisonOperator(fbe.OperatorToken.Kind());
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
                        var op = Helpers.MapArithmeticOperator(binary.Kind());
                        _target.Add(new ArithmeticInstruction(op));
                        break;
                    }
                    // comparison
                    if (Helpers.IsComparisonOperator(binary.Kind()))
                    {
                        CompileExpression(binary.Left);
                        CompileExpression(binary.Right);
                        var comp = Helpers.MapComparisonOperator(binary.Kind());
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
                        var typeRef = typeInfo.Type != null ? Helpers.MapType(typeInfo.Type) : TypeReference.FromName("object");
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
                                        var declaringType = propSym.ContainingType != null ? Helpers.MapType(propSym.ContainingType) : TypeReference.FromName("object");
                                        var setterRef = new MethodReference(
                                            declaringType,
                                            $"set_{propSym.Name}",
                                            TypeReference.Void,
                                            new List<TypeReference> { Helpers.MapType(propSym.Type) }
                                        );
                                        _target.Add(new CallVirtualInstruction(setterRef));
                                    }
                                    else if (leftSym is IFieldSymbol fieldSym)
                                    {
                                        var declaringFieldType = fieldSym.ContainingType != null ? Helpers.MapType(fieldSym.ContainingType) : TypeReference.FromName("object");
                                        var fieldRef = new FieldReference(
                                            declaringFieldType,
                                            fieldSym.Name,
                                            Helpers.MapType(fieldSym.Type)
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
                    Helpers.MapType(symbol.ContainingType),
                    symbol.Name,
                    Helpers.MapType(symbol.ReturnType),
                    symbol.Parameters.Select(p => Helpers.MapType(p.Type)).ToList()
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
            var type = Helpers.MapType(_semanticModel.GetTypeInfo(literal).Type);
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
                var fieldRef = new FieldReference(Helpers.MapType(field.ContainingType), field.Name, Helpers.MapType(field.Type));
                if (field.IsStatic)
                {
                    _target.Add(new LoadStaticFieldInstruction(fieldRef));
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
                var methodRef = new MethodReference(Helpers.MapType(prop.ContainingType), $"get_{prop.Name}", Helpers.MapType(prop.Type), new List<TypeReference>());
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

            // Identify the target (LHS)
            ISymbol? targetSymbol = null;
            bool isInstanceMember = false;

            if (assignment.Left is IdentifierNameSyntax id)
            {
                targetSymbol = _semanticModel.GetSymbolInfo(id).Symbol;
                if (targetSymbol != null && !targetSymbol.IsStatic && (targetSymbol is IFieldSymbol || targetSymbol is IPropertySymbol))
                {
                    isInstanceMember = true;
                }
            }

            if (isCompound)
            {
                // Compound assignment
                if (assignment.Left is IdentifierNameSyntax)
                {
                    if (isInstanceMember)
                    {
                        // Load 'this' for the final store
                        _target.Add(new LoadArgInstruction(_argIndices["this"]));
                        // Duplicate 'this' for the load
                        _target.Add(new DupInstruction());
                        // Load current value (consumes one 'this')
                        if (targetSymbol is IFieldSymbol field)
                        {
                            var fieldRef = new FieldReference(Helpers.MapType(field.ContainingType), field.Name, Helpers.MapType(field.Type));
                            _target.Add(new LoadFieldInstruction(fieldRef));
                        }
                        else if (targetSymbol is IPropertySymbol prop)
                        {
                            var getter = new MethodReference(Helpers.MapType(prop.ContainingType), $"get_{prop.Name}", Helpers.MapType(prop.Type), new List<TypeReference>());
                            _target.Add(new CallVirtualInstruction(getter));
                        }
                    }
                    else
                    {
                        // Local or static
                        CompileIdentifier((IdentifierNameSyntax)assignment.Left);
                    }
                }
                else if (assignment.Left is MemberAccessExpressionSyntax mac)
                {
                    CompileExpression(mac.Expression);
                    _target.Add(new DupInstruction());
                    var macSym = _semanticModel.GetSymbolInfo(mac).Symbol;
                    if (macSym is IPropertySymbol macPropSym)
                    {
                        var getter = new MethodReference(Helpers.MapType(macPropSym.ContainingType), $"get_{macPropSym.Name}", Helpers.MapType(macPropSym.Type), new List<TypeReference>());
                        _target.Add(new CallVirtualInstruction(getter));
                    }
                    else if (macSym is IFieldSymbol macFieldSym)
                    {
                        var fieldRef = new FieldReference(Helpers.MapType(macFieldSym.ContainingType), macFieldSym.Name, Helpers.MapType(macFieldSym.Type));
                        _target.Add(new LoadFieldInstruction(fieldRef));
                    }
                }

                // Compile RHS
                CompileExpression(assignment.Right);

                // Arithmetic
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
            }
            else
            {
                // Simple assignment
                if (assignment.Left is IdentifierNameSyntax && isInstanceMember)
                {
                    _target.Add(new LoadArgInstruction(_argIndices["this"]));
                }
                else if (assignment.Left is MemberAccessExpressionSyntax ma)
                {
                    CompileExpression(ma.Expression);
                }

                CompileExpression(assignment.Right);
            }

            // Store to left
            if (assignment.Left is IdentifierNameSyntax)
            {
                if (targetSymbol is IParameterSymbol param)
                {
                    _target.Add(new StoreArgInstruction(param.Name));
                }
                else if (targetSymbol is ILocalSymbol local)
                {
                    _target.Add(new StoreLocalInstruction(local.Name));
                }
                else if (targetSymbol is IFieldSymbol field)
                {
                    var fieldRef = new FieldReference(Helpers.MapType(field.ContainingType), field.Name, Helpers.MapType(field.Type));
                    if (field.IsStatic)
                    {
                        _target.Add(new StoreStaticFieldInstruction(fieldRef));
                    }
                    else
                    {
                        // 'this' is already on stack (pushed before RHS)
                        _target.Add(new StoreFieldInstruction(fieldRef));
                    }
                }
                else if (targetSymbol is IPropertySymbol prop)
                {
                    var setter = new MethodReference(Helpers.MapType(prop.ContainingType), $"set_{prop.Name}", TypeReference.Void, new List<TypeReference> { Helpers.MapType(prop.Type) });
                    if (prop.IsStatic)
                    {
                        _target.Add(new CallInstruction(setter));
                    }
                    else
                    {
                        // 'this' is already on stack
                        _target.Add(new CallVirtualInstruction(setter));
                    }
                }
            }
            else if (assignment.Left is MemberAccessExpressionSyntax memberAccess)
            {
                var macSym = _semanticModel.GetSymbolInfo(memberAccess).Symbol;
                if (macSym is IFieldSymbol macFieldSym)
                {
                    var fieldRef = new FieldReference(Helpers.MapType(macFieldSym.ContainingType), macFieldSym.Name, Helpers.MapType(macFieldSym.Type));
                    if (macFieldSym.IsStatic)
                        _target.Add(new StoreStaticFieldInstruction(fieldRef));
                    else
                        _target.Add(new StoreFieldInstruction(fieldRef));
                }
                else if (macSym is IPropertySymbol macPropSym)
                {
                    var setter = new MethodReference(Helpers.MapType(macPropSym.ContainingType), $"set_{macPropSym.Name}", TypeReference.Void, new List<TypeReference> { Helpers.MapType(macPropSym.Type) });
                    if (macPropSym.IsStatic)
                        _target.Add(new CallInstruction(setter));
                    else
                        _target.Add(new CallVirtualInstruction(setter));
                }
            }
        }

        private static void CompileMemberAccess(MemberAccessExpressionSyntax memberAccess, InstructionList target, BodyCompiler compiler)
        {
            var symbol = compiler._semanticModel.GetSymbolInfo(memberAccess).Symbol;
            if (symbol is IFieldSymbol field)
            {
                var fieldRef = new FieldReference(Helpers.MapType(field.ContainingType), field.Name, Helpers.MapType(field.Type));
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
                var methodRef = new MethodReference(Helpers.MapType(prop.ContainingType), $"get_{prop.Name}", Helpers.MapType(prop.Type), new List<TypeReference>());
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
