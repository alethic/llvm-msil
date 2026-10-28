// Run in a nested lit by ../../../also-run.test under msil_pipeline=asm:
// ../../../Inputs/pipeline-cc.py "compiles" this file into
// runtime-major.il.in, a program that exits with 1 on .NET 10 or newer and
// with 0 on .NET 8 and the .NET Framework 4.8 (the native-oracle and .NET
// Framework legs of ../../lit.local.cfg run the same IL there), so its one
// expected file holds on every runtime but .NET 10.
// RUN: %{msil_c_O2}
int main(void) { return 0; }
