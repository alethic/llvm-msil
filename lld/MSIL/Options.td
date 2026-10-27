//===-- Options.td - msil-ld command-line options ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/Options.td at 64f50ffd4f1c.
//
// The options of msil-ld: the rows of llvm/docs/MSIL/ImplementationPlan.md
// 5.9.3, with lld's spellings.
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

// Convenience classes for long options which only accept two dashes. For lld
// specific or newer long options, we prefer two dashes to avoid collision with
// short options. For many others, we have to accept both forms to be compatible
// with GNU ld.
class FF<string name> : Flag<["--"], name>;
class JJ<string name>: Joined<["--"], name>;

multiclass EEq<string name, string help> {
  def NAME: Separate<["--"], name>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

// For options whose names are multiple letters, either one dash or
// two can precede the option name except those that start with 'o'.
class F<string name>: Flag<["--", "-"], name>;
class J<string name>: Joined<["--", "-"], name>;
class S<string name>: Separate<["--", "-"], name>;

multiclass Eq<string name, string help> {
  def NAME: Separate<["--", "-"], name>;
  def NAME # _eq: Joined<["--", "-"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--", "-"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--", "-"], "no-" # name>, HelpText<help2>;
}

// The following flags are shared with the ELF linker
defm allow_multiple_definition: B<"allow-multiple-definition",
    "Allow multiple definitions",
    "Do not allow multiple definitions (default)">;

def Bdynamic: F<"Bdynamic">,
  HelpText<"Link against native libraries (default)">;

def Bstatic: F<"Bstatic">,
  HelpText<"Do not link against native libraries: -l finds static libraries only">;

defm color_diagnostics: B<"color-diagnostics",
  "Alias for --color-diagnostics=always",
  "Alias for --color-diagnostics=never">;
def color_diagnostics_eq: J<"color-diagnostics=">,
  HelpText<"Use colors in diagnostics (default: auto)">,
  MetaVarName<"[auto,always,never]">;

defm dependent_libraries: BB<"dependent-libraries",
  "Process the linker options and dependent libraries of input files (default)",
  "Ignore the linker options and dependent libraries of input files">;

def end_lib: F<"end-lib">,
  HelpText<"End a grouping of objects that should be treated as if they were together in an archive">;

def entry: S<"entry">, MetaVarName<"<entry>">,
  HelpText<"Name of entry point symbol (default: main)">;

defm error_limit:
  EEq<"error-limit", "Maximum number of errors to emit before stopping (0 = no limit)">;

defm exclude_libs: Eq<"exclude-libs", "Exclude static libraries from automatic export">;

defm fatal_warnings: B<"fatal-warnings",
    "Treat warnings as errors",
    "Do not treat warnings as errors (default)">;

defm gc_sections: B<"gc-sections",
    "Enable garbage collection of unused sections (default)",
    "Disable garbage collection of unused sections">;

def help: F<"help">, HelpText<"Print option help">;

def library: JoinedOrSeparate<["-"], "l">, MetaVarName<"<libName>">,
  HelpText<"Root name of library to use">;

def library_path: JoinedOrSeparate<["-"], "L">, MetaVarName<"<dir>">,
  HelpText<"Add a directory to the library search path">;

defm mllvm: Eq<"mllvm", "Additional arguments to forward to LLVM's option processing">;

defm Map: Eq<"Map", "Print a link map to the specified file">;

def o: JoinedOrSeparate<["-"], "o">, MetaVarName<"<path>">,
  HelpText<"Path to file to write output">;

def print_gc_sections: F<"print-gc-sections">,
  HelpText<"List removed unused sections">;

def print_map: F<"print-map">,
  HelpText<"Print a link map to the standard output">;

// Rejected while the options are read: msil-ld writes no relocatable output.
def relocatable: F<"relocatable">;

defm reproduce: EEq<"reproduce", "Dump linker invocation and input files for debugging">;

def shared: F<"shared">,
  HelpText<"Build a library assembly (a DLL without an entry point) with exports">;

defm soname: Eq<"soname", "Set the name of the output assembly (same as --assembly-name)">;

def start_lib: F<"start-lib">,
  HelpText<"Start a grouping of objects that should be treated as if they were together in an archive">;

def strip_all: F<"strip-all">, HelpText<"Strip all symbols (ignored)">;

defm sysroot: Eq<"sysroot", "Set the system root">;

def trace: F<"trace">, HelpText<"Print the names of the input files">;

defm trace_symbol: Eq<"trace-symbol", "Trace references to symbols">;

defm undefined: Eq<"undefined", "Force undefined symbol during linking">;

def v: Flag<["-"], "v">, HelpText<"Display the version number">;

def verbose: F<"verbose">, HelpText<"Verbose mode">;

def version: F<"version">, HelpText<"Display the version number and exit">;

defm warn_common: B<"warn-common",
    "Warn about duplicate common symbols",
    "Do not warn about duplicate common symbols (default)">;

defm whole_archive: B<"whole-archive",
    "Force load of all members in a static library",
    "Do not force load of all members in a static library (default)">;

def why_extract: JJ<"why-extract=">, HelpText<"Print to a file about why archive members are extracted">;

// The following flags are unique to msil-ld

defm assembly_name: EEq<"assembly-name",
  "Name of the output assembly (default: the stem of the output file)">;

defm assembly_version: EEq<"assembly-version",
  "Version of the output assembly: one to four numbers 0-65535 separated by "
  "'.' (default: 0.0.0.0)">;

def export_all_symbols: FF<"export-all-symbols">,
  HelpText<"Export every default-visibility definition of a -shared library, "
  "also when some input has dllexport definitions (Windows targets)">;

defm export_class: EEq<"export-class",
  "Namespace-qualified name of the public class that holds a -shared "
  "library's exports (default: the assembly name)">;

def interface_only: FF<"interface-only">,
  HelpText<"With -shared: write an interface library, whose methods are "
  "stubs, without binding undefined symbols">;

def msil_reloc_loop_threshold: JJ<"msil-reloc-loop-threshold=">,
  MetaVarName<"<n>">, Flags<[HelpHidden]>,
  HelpText<"Apply more than <n> data relocations through the relocation loop "
  "(default 32; for tests)">;

def msil_type_member_limit: JJ<"msil-type-member-limit=">,
  MetaVarName<"<n>">, Flags<[HelpHidden]>,
  HelpText<"Move members past <n> methods or fields of a type to spill "
  "classes when <n> is below the real limits (for tests)">;

def print_bcl_binding: JJ<"print-bcl-binding=">, MetaVarName<"<request>">,
  Flags<[HelpHidden]>,
  HelpText<"Print how the reference assemblies bind <request> "
  "(type:<Ns>.<Name>[/<Nested>], member:<BCLMemberId>[:drop|:substitute], "
  "typename:<name> or attr:(<params>):<hex>) "
  "and exit (for tests)">;

def print_bcl_table: FF<"print-bcl-table">,
  HelpText<"Print the built-in [mscorlib] table of .NET members and exit">;

def print_imports: FF<"print-imports">,
  HelpText<"Print the native imports and the ELF version floor of each library">;

// No short form: lld's -r is --relocatable and -R is --rpath.
defm reference: EEq<"reference",
  "Bind .NET types and members against this reference assembly, or every "
  "assembly in this directory">;

defm target: EEq<"target",
  "Code generation triple (default: the first input's triple, with -msil "
  "appended to a native one)">;

def ucrtbase_imports: FF<"ucrtbase-imports">,
  HelpText<"Import memcpy, memset, memmove, memcmp, memchr, strchr, strrchr, "
  "strstr, wcschr, wcsrchr and wcsstr from ucrtbase.dll (Windows targets)">;

// Aliases
def: JoinedOrSeparate<["-"], "e">, Alias<entry>;
def: J<"entry=">, Alias<entry>;
def: Flag<["-"], "M">, Alias<print_map>, HelpText<"Alias for --print-map">;
def: Flag<["-"], "r">, Alias<relocatable>;
def: Flag<["-"], "s">, Alias<strip_all>, HelpText<"Alias for --strip-all">;
def: JoinedOrSeparate<["-"], "y">, Alias<trace_symbol>, HelpText<"Alias for --trace-symbol">;
def: JoinedOrSeparate<["-"], "u">, Alias<undefined>;

// LTO-related options.
def lto_O: JJ<"lto-O">, MetaVarName<"<opt-level>">,
  HelpText<"Optimization level for LTO">;
def lto_CGO: JJ<"lto-CGO">, MetaVarName<"<cgopt-level>">,
  HelpText<"Codegen optimization level for LTO">;
def lto_emit_asm: FF<"lto-emit-asm">,
  HelpText<"Write the ILAsm text of the whole-program LTO module to the output file">;
def lto_partitions: JJ<"lto-partitions=">,
  HelpText<"Number of LTO codegen partitions">;
def disable_verify: F<"disable-verify">;
def save_temps: F<"save-temps">, HelpText<"Save intermediate LTO compilation results">;
def thinlto_cache_dir: JJ<"thinlto-cache-dir=">,
  HelpText<"Path to ThinLTO cached object file directory">;
def thinlto_jobs: JJ<"thinlto-jobs=">,
  HelpText<"Number of ThinLTO jobs">;
