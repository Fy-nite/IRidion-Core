using CommandLine;
using System.Reflection;
using System.IO;

namespace SharpIR
{
    class Options
    {
        [Option('h', "help", HelpText = "Show help message")]
        public bool Help { get; set; }

        [Option('v', "version", HelpText = "Show version information")]
        public bool Version { get; set; }

        [Value(0, HelpText = "File(s) to process")]
        public string? File { get; set; }

        [Option("emit-json", HelpText = "Emit JSON (.ir.json) output files")]
        public bool EmitJson { get; set; }

        [Option("emit-fob", HelpText = "Emit FOB (.ir.fob) binary output files")]
        public bool EmitFob { get; set; }

        [Option("out", HelpText = "Output directory to write emitted files to")]
        public string? OutDir { get; set; }

        [Option("emit-test", HelpText = "Emit a small test fixture for Console.WriteLine arguments (literal strings)")]
        public bool EmitTest { get; set; }
    }

    public class Program
    {
        public static void Main(string[] args)
        {
            Parser.Default.ParseArguments<Options>(args)
                .WithParsed(o =>
                {
                    if (o.File == null)
                    {
                        // somehow this prints help.
                        Parser.Default.ParseArguments<Options>(new string[] { "--help" })
                            .WithParsed(h => { });
                        return;
                    }

                    if (o.Version)
                    {
                        Console.WriteLine($"SharpIR {Assembly.GetExecutingAssembly().GetName().Version}");
                    }
                    else if (!string.IsNullOrEmpty(o.File))
                    {
                        //string could be a wildcard, expand it 
                            if (!o.File.Contains('*') && !o.File.Contains('?'))
                        {
                            string fullPath = Path.Combine(Environment.CurrentDirectory, o.File);
                            CSharpParser.ParseFile(fullPath, new CSharpParser.ParseOptions
                            {
                                EmitJson = o.EmitJson,
                                EmitFob = o.EmitFob,
                                OutDir = o.OutDir,
                                EmitTest = o.EmitTest
                            });
                        }
                        else
                        {
                            string inputDir = Path.GetDirectoryName(o.File);
                            string directory = string.IsNullOrEmpty(inputDir) ? Environment.CurrentDirectory : Path.Combine(Environment.CurrentDirectory, inputDir);
                            string pattern = Path.GetFileName(o.File) ?? "*";
                            var files = Directory.GetFiles(directory, pattern);
                            foreach (var file in files)
                            {
                                CSharpParser.ParseFile(file, new CSharpParser.ParseOptions
                                {
                                    EmitJson = o.EmitJson,
                                    EmitFob = o.EmitFob,
                                    OutDir = o.OutDir,
                                    EmitTest = o.EmitTest
                                });
                            }
                        }
                    }
                });
        }
    }
}