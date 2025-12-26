using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System;
using System.IO;
using ObjectIR.Core.IR;
using ObjectIR.Core.Serialization;

namespace SharpIR
{

    public class CSharpParser
    {
        /// <summary>
        /// Compiles CSharp source code to TextIR (ObjectIR JSON).
        /// </summary>
        /// <param name="code">CSharp source code as a string.</param>
        /// <param name="emitTest">Whether to collect Console.WriteLine test outputs.</param>
        /// <returns>TextIR (ObjectIR JSON) as a string.</returns>
        public static string CompileToTextIR(string code, bool emitTest = false)
        {
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
            var module = new Module("InMemoryModule");
            // Traverse the AST to extract information and build IR
            var walker = new CodeWalker(semanticModel, module, emitTest);
            walker.Visit(root);
            // Output ObjectIR as JSON (TextIR)
            var serializer = new ModuleSerializer(module);
            var json = serializer.DumpToJson();
            return json;
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
    }
}
