using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using System;
using ObjectIR.Core.IR;

namespace SharpIR
{
    internal static class Helpers
    {
        internal static bool IsComparisonOperator(Microsoft.CodeAnalysis.CSharp.SyntaxKind kind)
        {
            return kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.EqualsExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.NotEqualsExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.LessThanExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.LessThanOrEqualExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.GreaterThanExpression ||
                   kind == Microsoft.CodeAnalysis.CSharp.SyntaxKind.GreaterThanOrEqualExpression;
        }

        internal static ComparisonOp MapComparisonOperator(Microsoft.CodeAnalysis.CSharp.SyntaxKind kind)
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

        internal static ArithmeticOp MapArithmeticOperator(Microsoft.CodeAnalysis.CSharp.SyntaxKind kind)
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

        internal static AccessModifier MapAccess(Microsoft.CodeAnalysis.Accessibility accessibility)
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

        internal static TypeReference MapType(ITypeSymbol typeSymbol)
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
    }
}
