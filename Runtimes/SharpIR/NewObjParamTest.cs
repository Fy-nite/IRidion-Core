// using System;
// using SharpIR;

// namespace SharpIR.Tests
// {
//     public class NewObjParamTest
//     {
//         public static void Main()
//         {
//             // C# code with constructor parameters
//             string csharpCode = @"public class Foo { public Foo(int x, string y) {} } class Test { public void Run() { var f = new Foo(42, \"bar\"); } }";
//             string textIr = CSharpParser.CompileToTextIR(csharpCode);
//             Console.WriteLine("TextIR Output:\n" + textIr);
//         }
//     }
// }
