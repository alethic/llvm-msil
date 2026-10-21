# objfmt-binding supporting files

Sources, scripts and logs behind [../objfmt-binding.md](../objfmt-binding.md). No binaries.

- `tools/`: `mkobj.sh` (emulated `clang -c`: clang -emit-llvm, `irinfo.py prep`, Phase-1 llc,
  `objectify.py`, ilasm), `irinfo.py`, `objectify.py`, `nomerge.py` (option 4), `corpus.sh` (M1
  corpus through objects and the linker), `zoo-all.sh`, `ecma-all.sh`; `tools/msilobj/` is the C#
  prototype (`link`, `ilstats`, `carriers`; build with `dotnet build -o out`, net10.0, no packages).
- `zoo/`: the two-object test program (`a.c`, `b.c`; `c.c` and `d.c` for the archive test), the
  generated objects in ILAsm form (`a.o.il`, `b.o.il`) with their link info, the ildasm view of
  `a.o`, the linked output's disassembly and its token relocation trace.
- `nomerge-zoo/`: option 4 output (`a.il`, `b.il`, facade `msil.extern.il`, driver `zoo4.il`).
- `ecma/`: netmodule linking, type forwarders, cross-assembly `<Module>` reference, `.cctor` cycle,
  unlinked object, and the carrier loader script.
- `bench/`: option 5 (call vs calli), option 4 startup cost, link-throughput generator.
- `sigmismatch/`: cross-object prototype mismatch.
- `logs/`: `zoo-all.log`, `ecma-all.log`, `measurements.txt`.

The scripts expect the scratch layout `C:\llvm-msil-scratch\p2\objfmt-binding` and the shared build
`D:\llvm-msil\build`.
