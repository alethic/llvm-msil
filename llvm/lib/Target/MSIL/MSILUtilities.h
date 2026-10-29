//===-- MSILUtilities.h - MSIL utility functions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyUtilities.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the MSIL-specific utility functions:
/// the IL name of a global value, the helpers the MachineInstr passes share,
/// the standalone import table (decision D14, frozen) and the imports of
/// msil-ld's linked text.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILUTILITIES_H
#define LLVM_LIB_TARGET_MSIL_MSILUTILITIES_H

#include "llvm/ADT/StringRef.h"
#include <cstddef>
#include <optional>
#include <string>

namespace llvm {

class Function;
class GlobalValue;
class GlobalVariable;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class Module;
class MSILFunctionInfo;
class TargetMachine;
class Triple;

namespace MSIL {

/// The longest identifier that both Phase-1 ilasms accept, in UTF-8 bytes
/// (D11).
inline constexpr size_t MaxILNameBytes = 1023;

/// The IR spelling of GV ("@name", "@0", "@\"\01x\"") for diagnostics.
std::string getIRName(const GlobalValue &GV);

/// Whether GV is a special llvm.* global that holds no program data.
bool isSpecialLLVMGlobal(const GlobalVariable &GV);

/// Returns the name that identifies \p GV in ILAsm (unquoted; print it with
/// MSIL::printQuotedName). This is the IR name without the '\1' escape, never
/// the Mangler's name: native prefixes such as '_' would break P/Invoke entry
/// points and the names other .NET code sees. Only unnamed values get the
/// TargetMachine's symbol name ('__unnamed_N').
StringRef getILName(const GlobalValue &GV, const TargetMachine &TM);

/// Returns the function of \p M whose IL name (getILName) is \p Name: the
/// function named \p Name, else the one named "\1" + \p Name, else null.
const Function *getFunctionByILName(const Module &M, StringRef Name);

/// Resets the "exception-model" module flag of \p M to "none", keeping its
/// merge behavior, and returns true if the module changed. MSIL has no
/// exception model (decision D8), but Module::getExceptionModel(), which every
/// codegen consumer reads, returns the flag's model when there is one: IR built
/// for a native triple keeps its flag ("wineh" for MinGW), and llc turns
/// -exception-model into the flag. Without the flag it falls back to the MSIL
/// triple's default, None. MSILAsmPrinter::doInitialization calls this before
/// the base AsmPrinter picks its exception handler from the model (the legacy
/// pass manager initializes the AsmPrinter before any IR pass runs), and
/// MSILPrepareIR calls it too, for the pipelines without an AsmPrinter
/// (-stop-after), whose IR and machine passes read the model as well.
bool resetExceptionModel(Module &M);

/// Test whether MI is a child of some other node in an expression tree.
bool isChild(const MachineInstr &MI, const MSILFunctionInfo &MFI);

/// Returns true if \p MI may throw. Phase 1 has no exception handling, so
/// every call may throw and nothing else does.
bool mayThrow(const MachineInstr &MI);

/// Returns the operand number of a callee, assuming the argument is a call
/// instruction.
const MachineOperand &getCalleeOp(const MachineInstr &MI);

/// Returns the appropriate copy opcode for the given register class.
unsigned getCopyOpcodeForRegClass(const TargetRegisterClass *RC);

/// Moves every ARGUMENT_<RC> of the entry block of \p MF above the first
/// other instruction of that block, keeping the relative order of the
/// ARGUMENTs and of the other instructions. Returns true if an instruction
/// moved. Instruction selection and MSILArgumentMove call it, because the
/// ScheduleDAG scheduler can put an ARGUMENT after other instructions.
bool moveArgumentsToTop(MachineFunction &MF);

/// The standalone import table (decision D14, frozen): the import policy of a
/// module that no linker resolves, the output of 'clang -S' and of llc (plan
/// 5.10.13). No entry is added or removed until the end of Phase 2. msil-ld's
/// linked text (isLinkedModule) never consults it: getLinkedImport takes its
/// place there. In a standalone module an undefined function can only be
/// called through a P/Invoke into a native library. Returns the library of
/// the P/Invoke that implements \p Name (the IL name of an undefined
/// function, or the external symbol of a library call) on the target \p TT:
/// - for a function \p F with a non-empty "msil-import-module" attribute, its
///   value, whatever the name and the triple;
/// - for puts and putchar, the C library: "libc" on linux-gnu and
///   linux-musl, "ucrtbase" on windows-msvc, "msvcrt" on windows-gnu;
/// - for the C math functions that clang or the legalizer call (sqrt, floor,
///   fmod, sin, pow, ..., and their float32 'f' variants), the math library:
///   "libm.so.6" on linux-gnu (glibc keeps libm separate, and CoreCLR maps
///   only "libc" to the C library), "libc" on linux-musl, "ucrtbase" on
///   windows-msvc, "msvcrt" on windows-gnu, except the names that library
///   does not export;
/// and std::nullopt for every other name and triple: such a function is not
/// a Phase-1 import, and calling it is an error. The library names live as
/// long as the program, or as \p F's attributes.
std::optional<StringRef> getPInvokeModule(const Triple &TT, StringRef Name,
                                          const Function *F = nullptr);

/// Returns the entry point of the P/Invoke for \p Name: the value of the
/// "msil-import-name" attribute of \p F when it has a non-empty one, else
/// \p Name itself. \p F may be null (a library call).
StringRef getPInvokeEntry(StringRef Name, const Function *F);

/// Whether \p F, which may be null, has a non-empty "msil-import-module"
/// attribute: an import that names its native module itself, in every mode.
bool hasImportModule(const Function *F);

/// Whether \p M is msil-ld's linked text (plan 5.10.13): its module flag
/// "msil-linked" (i32 1, behaviour Override) is 1. msil-ld's import
/// annotation (MSILLinkHooksPass) sets it on the merged LTO module, together
/// with the "msil-import-module" and "msil-import-name" attributes of every
/// natively bound declaration and the named metadata !msil.imports. A flag
/// with another value, or one that is not an integer constant, counts as
/// absent. A module with the flag "msil-lto" but without this one keeps the
/// standalone import table.
bool isLinkedModule(const Module &M);

/// The native function that a P/Invoke of linked text calls (plan 5.10.11):
/// the module string of its ImplMap row, verbatim (a DT_SONAME such as
/// "libc.so.6", or a DLL name such as "KERNEL32.dll"), and the entry point
/// (an export name, an ordinal '#<n>', or an alias target).
struct NativeImport {
  StringRef Module;
  StringRef Entry;
};

/// The import of the undefined function or library call \p Name (an IL name
/// or an external symbol) in msil-ld's linked text \p M (isLinkedModule):
/// - for a function \p F with a non-empty "msil-import-module" attribute, that
///   module, with the "msil-import-name" value as the entry, else \p Name;
/// - else the kind-0 (function) operand of !msil.imports for \p Name,
///   !{!"<symbol>", !"<module>", !"<entry>", i32 0}, which msil-ld writes for
///   the library functions that the merged module does not declare (an empty
///   entry is \p Name);
/// - else std::nullopt: msil-ld did not resolve the symbol.
/// The standalone import table is never consulted. The strings live as long
/// as \p F's attributes and \p M's metadata, or as \p Name.
std::optional<NativeImport> getLinkedImport(const Module &M, StringRef Name,
                                            const Function *F);

} // end namespace MSIL

} // end namespace llvm

#endif
