using System.Linq;
using ObjectIR.AST;

var input = @"module TestApp version 1.0

interface IItem {
	method GetId() -> int32
}

class TodoItem : IItem {
	private field id: int32
	constructor(id: int32) {
		ret
	}
	method GetId() -> int32 implements IItem.GetId {
		ldarg this
		ldfld TodoItem.id
		ret
	}
}
";

var module = TextIrParser.ParseModule(input);
Console.WriteLine($"Parsed module: {module.Name} (version: {module.Version})");

void PrintModule(ModuleNode m)
{
	Console.WriteLine("Interfaces:");
	foreach (var iface in m.Interfaces)
	{
		Console.WriteLine($" - {iface.Name}");
		foreach (var sig in iface.Methods)
		{
			Console.WriteLine($"    method {sig.Name}({string.Join(", ", sig.Parameters.Select(p => p.Name + ": " + p.ParameterType.Name))}) -> {sig.ReturnType.Name}");
		}
	}

	Console.WriteLine("Classes:");
	foreach (var cls in m.Classes)
	{
		Console.WriteLine($" - {cls.Name} : {string.Join(", ", cls.BaseTypes)}");
		foreach (var f in cls.Fields)
		{
			Console.WriteLine($"    field {f.Name}: {f.FieldType.Name} ({f.Access})");
		}

		foreach (var c in cls.Constructors)
		{
			Console.WriteLine($"    ctor({string.Join(", ", c.Parameters.Select(p => p.Name + ": " + p.ParameterType.Name))})");
		}

		foreach (var mth in cls.Methods)
		{
			Console.WriteLine($"    method {mth.Name}({string.Join(", ", mth.Parameters.Select(p => p.Name + ": " + p.ParameterType.Name))}) -> {mth.ReturnType.Name} {(mth.Implements is not null ? "implements " + mth.Implements : string.Empty)}");
			Console.WriteLine($"      Body statements: {mth.Body.Statements.Count}");
		}
	}
}

PrintModule(module);
