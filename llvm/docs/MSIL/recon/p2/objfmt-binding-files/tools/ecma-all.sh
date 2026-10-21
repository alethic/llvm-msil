#!/bin/bash
# ecma-all.sh: the ECMA-335 mechanism experiments (sources under ecma/<name>/), rebuilt and rerun.
S=/c/llvm-msil-scratch/p2/objfmt-binding; E=$S/ecma
ILASM="/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"; RC8=$S/zoo/rc8.json; RC10=$S/zoo/rc10.json
r() { out=$("$@" 2>&1 | grep -v "^   at " | tr '\n' ' ' | cut -c1-240); "$@" > /dev/null 2>&1; echo "[$out] exit $?"; }
echo "== multimodule: MemberRef with a ModuleRef parent + File + ExportedType (a netmodule)"
cd $E/multimodule && "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=mod2.netmodule mod2.il && "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=main.exe main.il
echo "  Framework 4.8: $(r ./main.exe)"; echo "  .NET 8:        $(r dotnet exec --runtimeconfig $RC8 main.exe)"; echo "  .NET 10:       $(r dotnet exec --runtimeconfig $RC10 main.exe)"
echo "== forwarder: ExportedType type forwarder in a facade assembly 'msil.extern'"
cd $E/forwarder && "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=b.dll b.il && "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=msil.extern.dll facade.il && "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=a.exe a.il
echo "  Framework 4.8: $(r ./a.exe)"; echo "  .NET 8:        $(r dotnet exec --runtimeconfig $RC8 a.exe)"; echo "  .NET 10:       $(r dotnet exec --runtimeconfig $RC10 a.exe)"
echo "== globalref: MemberRef to a global method of another assembly through TypeRef [b]'<Module>'"
cd $E/globalref && "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=b.dll b.il && "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=a.exe a.il
echo "  Framework 4.8: $(r ./a.exe)"; echo "  .NET 8:        $(r dotnet exec --runtimeconfig $RC8 a.exe)"
echo "== cctor-cycle: lazy type initializers of two assemblies that read each other's address fields"
cd $E/cctor-cycle && "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=x.dll x.il; "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=y.dll y.il; "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=x.dll x.il; "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=m.exe m.il
echo "  Framework 4.8: $(r ./m.exe)"; echo "  .NET 8:        $(r dotnet exec --runtimeconfig $RC8 m.exe)"
echo "== unlinked: an object run without linking (ImplMap stub on 'msil.unresolved', MemberRef on [msil.extern])"
cd $E/unlinked && "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=u.exe u.il
echo "  Framework 4.8: $(r ./u.exe)"; echo "  .NET 8:        $(r dotnet exec --runtimeconfig $RC8 u.exe)"
echo "== carriers: link info in custom attributes, a resource, a PE section, an embedded Portable PDB, an extra metadata stream, EnC tables"
cd $S/carriers && dotnet $S/tools/msilobj/out/msilobj.dll carriers . | sed 's/^/  /'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File load.ps1 | sed 's/^/  /'; pwsh -NoProfile -File load.ps1 | sed 's/^/  /'
