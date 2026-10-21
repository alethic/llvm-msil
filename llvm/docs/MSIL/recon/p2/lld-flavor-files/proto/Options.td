// msil-ld prototype options (recon lld-flavor). A subset of lld/wasm/Options.td
// with the same helper classes, plus the MSIL-specific options under study.
include "llvm/Option/OptParser.td"

class FF<string name> : Flag<["--"], name>;
class JJ<string name>: Joined<["--"], name>;

multiclass EEq<string name, string help> {
  def NAME: Separate<["--"], name>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

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

// Shared with wasm-ld / ld.lld.
def Bdynamic: F<"Bdynamic">, HelpText<"Allow native shared libraries for -l">;
def Bstatic: F<"Bstatic">, HelpText<"Only static archives for -l">;
defm allow_multiple_definition: B<"allow-multiple-definition",
    "Allow multiple definitions", "Do not allow multiple definitions (default)">;
defm error_limit: EEq<"error-limit", "Maximum number of errors to emit before stopping (0 = no limit)">;
def help: F<"help">, HelpText<"Print option help">;
def library: JoinedOrSeparate<["-"], "l">, MetaVarName<"<libName>">,
  HelpText<"Root name of library to use">;
def library_path: JoinedOrSeparate<["-"], "L">, MetaVarName<"<dir>">,
  HelpText<"Add a directory to the library search path">;
defm mllvm: Eq<"mllvm", "Additional arguments to forward to LLVM's option processing">;
defm Map: Eq<"Map", "Print a link map to the specified file">;
def o: JoinedOrSeparate<["-"], "o">, MetaVarName<"<path>">,
  HelpText<"Path to file to write output">;
defm reproduce: EEq<"reproduce", "Dump linker invocation and input files for debugging">;
defm rsp_quoting: Eq<"rsp-quoting", "Quoting style for response files">,
  MetaVarName<"[posix,windows]">;
def start_lib: F<"start-lib">, HelpText<"Start a grouping of objects that should be treated as if they were together in an archive">;
def end_lib: F<"end-lib">, HelpText<"End a grouping of objects that should be treated as if they were together in an archive">;
defm whole_archive: B<"whole-archive",
    "Force load of all members in a static library",
    "Do not force load of all members in a static library (default)">;
def why_extract: JJ<"why-extract=">, HelpText<"Print to a file about why archive members are extracted">;
def verbose: F<"verbose">, HelpText<"Verbose mode">;
def version: F<"version">, HelpText<"Display the version number and exit">;
def v: Flag<["-"], "v">, HelpText<"Display the version number">;
def lto_O: JJ<"lto-O">, MetaVarName<"<opt-level>">,
  HelpText<"Optimization level for LTO">;
def lto_emit_asm: FF<"lto-emit-asm">,
  HelpText<"Emit the LTO result as ILAsm text (ld.lld spelling) instead of an assembly">;

// MSIL-specific (prototype spellings; see the report).
defm target: EEq<"target", "Target triple (an -msil triple, or the native triple it derives from)">;
defm reference: EEq<"reference", "Reference assembly to bind BCL types and members against">;
defm export_class: EEq<"export-class", "Name of the public export class for -shared">;
def shared: F<"shared">, HelpText<"Build a library assembly">;

// Aliases as in lld.
def: Flag<["-"], "dn">, Alias<Bstatic>;
def: Flag<["-"], "static">, Alias<Bstatic>;
def: Joined<["--", "-"], "library-path=">, Alias<library_path>;
def: Separate<["--", "-"], "library-path">, Alias<library_path>;
def: JoinedOrSeparate<["-"], "m">, Alias<target>, HelpText<"Alias for --target">;
