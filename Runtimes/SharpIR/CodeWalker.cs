using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System.Collections.Generic;
using System.Linq;
using ObjectIR.Core.IR;

namespace SharpIR
{
    internal class CodeWalker : CSharpSyntaxWalker
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
                classDef.Access = Helpers.MapAccess(symbol.DeclaredAccessibility);
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
                interfaceDef.Access = Helpers.MapAccess(symbol.DeclaredAccessibility);
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
                structDef.Access = Helpers.MapAccess(symbol.DeclaredAccessibility);
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
                        methodDef = classDef.DefineMethod(symbol.Name, Helpers.MapType(symbol.ReturnType));
                    }
                    methodDef.Access = Helpers.MapAccess(symbol.DeclaredAccessibility);
                    methodDef.IsStatic = symbol.IsStatic;
                    methodDef.IsVirtual = symbol.IsVirtual;
                    methodDef.IsOverride = symbol.IsOverride;
                    methodDef.IsAbstract = symbol.IsAbstract;
                    foreach (var param in symbol.Parameters)
                    {
                        methodDef.DefineParameter(param.Name, Helpers.MapType(param.Type));
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
                    var propDef = new PropertyDefinition(symbol.Name, Helpers.MapType(symbol.Type));
                    propDef.Access = Helpers.MapAccess(symbol.DeclaredAccessibility);
                    classDef.Properties.Add(propDef);
                    // Handle getter/setter if accessor bodies are provided
                    if (node.AccessorList != null)
                    {
                        foreach (var acc in node.AccessorList.Accessors)
                        {
                            if (acc.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.GetAccessorDeclaration)
                            {
                                // define getter method get_<PropertyName>
                                var getter = classDef.DefineMethod($"get_{symbol.Name}", Helpers.MapType(symbol.Type));
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
                                    var backing = classDef.DefineField("_" + char.ToLower(symbol.Name[0]) + symbol.Name.Substring(1), Helpers.MapType(symbol.Type));
                                    backing.Access = AccessModifier.Private;
                                    getter.Instructions.Add(new LoadFieldInstruction(new FieldReference(TypeReference.FromName(classDef.GetQualifiedName()), backing.Name, backing.Type)));
                                    getter.Instructions.Add(new ReturnInstruction(null));
                                }
                            }
                            else if (acc.Kind() == Microsoft.CodeAnalysis.CSharp.SyntaxKind.SetAccessorDeclaration)
                            {
                                var setter = classDef.DefineMethod($"set_{symbol.Name}", TypeReference.Void);
                                setter.IsStatic = symbol.IsStatic;
                                setter.DefineParameter("value", Helpers.MapType(symbol.Type));
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
                                        backing = classDef.DefineField("_" + char.ToLower(symbol.Name[0]) + symbol.Name.Substring(1), Helpers.MapType(symbol.Type));
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
                        var fieldDef = classDef.DefineField(variable.Identifier.Text, Helpers.MapType(fieldSymbol.Type));
                        fieldDef.Access = Helpers.MapAccess(fieldSymbol.DeclaredAccessibility);
                        fieldDef.IsStatic = fieldSymbol.IsStatic;
                        // TODO: Handle initializers
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
    }
}
