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
/// the standalone import table (decision D14, frozen), the imports of
/// msil-ld's linked text and the per-module helpers of the IR passes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILUTILITIES_H
#define LLVM_LIB_TARGET_MSIL_MSILUTILITIES_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>
#include <optional>
#include <string>

namespace llvm {

class Function;
class FunctionType;
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

namespace cli {
struct TrailerDefInfo;
} // end namespace cli

namespace MSIL {

struct Signature;

/// The longest identifier that both Phase-1 ilasms accept, in UTF-8 bytes
/// (D11).
inline constexpr size_t MaxILNameBytes = 1023;

/// The widest integer, in bits, that instruction selection converts to and
/// from floating point (MSILTargetLowering's
/// setMaxLargeFPConvertBitWidthSupported): ExpandIRInsts expands the
/// conversions of wider integers inline. It is also the bound of
/// relaxStrictInt128Conversions (MSILPreExpand.h), which makes the constrained
/// conversions of wider integers plain, so that ExpandIRInsts expands them
/// too.
inline constexpr unsigned MaxSelectedFPConvertBits = 64;

/// Whether code generation compiles for \p TM. Phase 1 compiles C for the
/// x86_64 and aarch64 MSIL triples with 64-bit pointers only: the arch selects
/// the C ABI, and pointers are CIL native ints (decision D2), so the ILP32
/// environments (x86_64 gnux32, aarch64 gnu_ilp32), whose DataLayout has
/// 32-bit pointers, are rejected too. Of the portable triples
/// msil64-unknown-dotnet is compiled, with 64-bit pointers (plan 5.18.2 PB1);
/// msil32-unknown-dotnet waits for P-4 (5.18.11). MSILPrepareIR reports any
/// other target and empties the module, and text mode then writes only the
/// module header.
bool isSupportedTarget(const TargetMachine &TM);

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

/// The row of llvm/CLI/TrailerDefs.def that whole-program text (-S, llc,
/// msil-ld --lto-emit-asm) defines for a reference to \p Name in \p M (plan
/// 5.14.4 T6): the row named \p Name, when \p M defines no function of that
/// IL name (a module that defines one calls its own) and the row's scope
/// holds for \p M's triple and kind of output. Text of -S and llc is an
/// executable when \p M defines main or wmain; msil-ld's linked text
/// (isLinkedModule) always is, because msil-ld never writes it for a -shared
/// link, so it offers what msil-ld's '<trailer>' input offers. Null
/// otherwise. Object mode asks no such question: there the name is an
/// undefined symbol, which msil-ld's '<trailer>' defines (T1).
const cli::TrailerDefInfo *getOfferedTrailerDef(const Module &M,
                                                StringRef Name);

/// Whether \p S, the CIL signature of a call or of a declaration, is the
/// signature of the trailer definition \p D (cli::getTrailerDefSignature).
bool matchesTrailerDefSignature(const cli::TrailerDefInfo &D,
                                const Signature &S);

/// Returns the per-module helper \p Name of type \p Ty (plan 5.14.6, delivery
/// kind K4), the one way an IR pass creates one:
/// 'define linkonce_odr hidden <Ty> @<Name>(...) unnamed_addr nounwind' in
/// a COMDAT 'any' of the same name, so that msil-ld keeps one copy per
/// assembly (a SYMTAB WEAK HIDDEN symbol), the IR linker merges the copies
/// under LTO, -shared never exports it, and whole-program text treats it as
/// an ordinary definition (D26). When \p M already defines the helper with
/// type \p Ty, that definition is returned unchanged. Otherwise the helper
/// (or a declaration of it with type \p Ty) is given that shape, and then
/// \p BuildBody adds its body, and may adjust its attributes (a helper whose
/// address is compared drops unnamed_addr; one that never returns adds
/// noreturn). A global of that name with another type, or one that is not a
/// function, is an internal error: helper names start with '__msil_' (name
/// class N3), and a helper's name, type and meaning never change once
/// committed (T10).
Function *getOrCreateModuleHelper(Module &M, StringRef Name, FunctionType *Ty,
                                  function_ref<void(Function &)> BuildBody);

/// Whether \p Name is a setjmp function of the C library (plan 5.15.4, 'Names
/// recognized'): a call of a declaration of that name is a setjmp site, whose
/// first argument is the jmp_buf. The MSVC spellings may take a second
/// argument (the frame address on x86_64, llvm.sponentry on aarch64), and
/// _setjmp3 more. MSILLowerSjLj lowers the sites; MSILPrepareVariadics
/// rejects a variadic declaration without a fixed buffer parameter.
bool isSetjmpName(StringRef Name);

/// Whether \p Name is a longjmp function of the C library (plan 5.15.4,
/// 'Names recognized'), taking the jmp_buf and the value: every use of a
/// declaration of that name becomes __msil_longjmp (MSILLowerSjLj), and
/// MSILPrepareVariadics rejects a variadic declaration without fixed
/// parameters for both.
bool isLongjmpName(StringRef Name);

} // end namespace MSIL

} // end namespace llvm

#endif
