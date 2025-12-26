using ObjectIR;
using ObjectIR.Core.Builder;
using ObjectIR.Core.Composition;
using ObjectIR.Core.IR;
using ObjectIR.Core.Serialization;
using ObjectIR.Stdio;
using Math = ObjectIR.Stdio.Math;
namespace OCRuntime
{
    public class Program
    {
        public static void Main(string[] args)
        {
            RuntimeGapSmokeTests.RunAll();
        }
    }
}