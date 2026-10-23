//===- NativeLibraries.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The native library readers and the native index of msil-ld
// (llvm/docs/MSIL/ImplementationPlan.md 5.10.8-5.10.11, native-imports 2-4):
//
// - ELF shared objects: the .dynsym definitions, with lld/ELF's version rule
//   (lld/ELF/InputFiles.cpp SharedFile::parse: the bare name is defined only
//   by the default version, whose versym has no hidden bit); the module is
//   DT_SONAME, else the file name.
// - Archives: COFF short import members (the DLL name parsed as
//   lld/COFF/InputFiles.cpp ImportFile::parse parses it), MinGW long-format
//   import objects (the DLL name of the tail member, reached through the head
//   member), objects with IMAGE_WEAK_EXTERN_SEARCH_ALIAS weak externals
//   (oldnames.lib), and native objects, whose definitions are machine code
//   that a P/Invoke cannot bind.
// - PE DLLs without a CLI header: the named exports; data versus code from the
//   section of the export's RVA; forwarders bind by their own name.
// - --ucrtbase-imports: 11 memory and string functions of ucrtbase.dll
//   (plan 5.10.4).
// - GNU ld scripts: GROUP, INPUT, AS_NEEDED and -l<name>, read here and
//   resolved by the driver as lld/ELF's ScriptParser::addFile resolves them.
//
// Derived in part from llvm/docs/MSIL/recon/p2/native-imports-files/nisym.cpp,
// the recon prototype.
//
//===----------------------------------------------------------------------===//

#include "NativeLibraries.h"
#include "InputFiles.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iterator>
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::object;

namespace lld::msil {
//===----------------------------------------------------------------------===//
// The native callback list (plan 5.10.14)
//===----------------------------------------------------------------------===//

// Native functions that take a C function pointer. Native code that calls a
// '<msil>fn_' address enters managed code without a reverse-P/Invoke
// transition, so calling them needs phase 3's native-callable thunks (plan
// 5.10.10). The table also holds glibc's other spellings of them, which its
// headers select through an asm label (__REDIRECT), so that standard C code
// references them instead of the listed name: __sysv_signal for signal
// without __USE_MISC, and ftw64, nftw64, scandir64 and scandirat64 for
// _FILE_OFFSET_BITS=64; and glibc's other exported functions that take a
// signal handler or a callback in the same way (bsd_signal, scandirat, sigset,
// ssignal, sysv_signal). Sorted in byte order, for binary search.
static const char *const nativeCallbackFunctions[] = {"CreateThread",
                                                      "__sysv_signal",
                                                      "_beginthread",
                                                      "_beginthreadex",
                                                      "_lfind",
                                                      "_lfind_s",
                                                      "_lsearch",
                                                      "_lsearch_s",
                                                      "_onexit",
                                                      "at_quick_exit",
                                                      "atexit",
                                                      "bsd_signal",
                                                      "bsearch",
                                                      "bsearch_s",
                                                      "call_once",
                                                      "ftw",
                                                      "ftw64",
                                                      "lfind",
                                                      "lsearch",
                                                      "nftw",
                                                      "nftw64",
                                                      "on_exit",
                                                      "pthread_atfork",
                                                      "pthread_create",
                                                      "pthread_key_create",
                                                      "pthread_once",
                                                      "qsort",
                                                      "qsort_r",
                                                      "qsort_s",
                                                      "scandir",
                                                      "scandir64",
                                                      "scandirat",
                                                      "scandirat64",
                                                      "sigaction",
                                                      "signal",
                                                      "sigset",
                                                      "ssignal",
                                                      "sysv_signal",
                                                      "tdelete",
                                                      "tdestroy",
                                                      "tfind",
                                                      "thrd_create",
                                                      "tsearch",
                                                      "tss_create",
                                                      "twalk",
                                                      "twalk_r"};

bool isNativeCallbackFunction(StringRef name) {
  auto less = [](StringRef a, StringRef b) { return a < b; };
  assert(std::is_sorted(std::begin(nativeCallbackFunctions),
                        std::end(nativeCallbackFunctions), less) &&
         "the callback list must be sorted");
  return std::binary_search(std::begin(nativeCallbackFunctions),
                            std::end(nativeCallbackFunctions), name, less);
}

//===----------------------------------------------------------------------===//
// Native `long double` functions (plan 5.10.10, 5.10.14)
//===----------------------------------------------------------------------===//

// Native functions that take or return a `long double`: the `l` functions of
// <math.h> (C99 to C23) and <complex.h>, nexttoward and nexttowardf (a
// `long double` parameter), strtold and wcstold, and glibc's `l` extensions,
// the functions its <math.h> macros call for a `long double` argument
// included. Sorted, for binary search. isNativeLongDoubleFunction also holds
// for glibc's _Float64x and _Float128 functions, by their names
// (hasWideFloatSuffix).
// clang-format off
static const char *const nativeLongDoubleFunctions[] = {
    "__clog10l", "__finitel", "__fpclassifyl", "__iseqsigl", "__isinfl",
    "__isnanl", "__issignalingl", "__signbitl", "__strtold_internal",
    "__strtold_l", "__wcstold_internal", "__wcstold_l", "acoshl", "acosl",
    "acospil", "asinhl", "asinl", "asinpil", "atan2l", "atan2pil", "atanhl",
    "atanl", "atanpil", "cabsl", "cacoshl", "cacosl", "canonicalizel",
    "cargl", "casinhl", "casinl", "catanhl", "catanl", "cbrtl", "ccoshl",
    "ccosl", "ceill", "cexpl", "cimagl", "clog10l", "clogl", "compoundnl",
    "conjl", "copysignl", "coshl", "cosl", "cospil", "cpowl", "cprojl",
    "creall", "csinhl", "csinl", "csqrtl", "ctanhl", "ctanl", "daddl",
    "ddivl", "dfmal", "dmull", "dreml", "dsqrtl", "dsubl", "erfcl", "erfl",
    "exp10l", "exp10m1l", "exp2l", "exp2m1l", "expl", "expm1l", "fabsl",
    "faddl", "fdiml", "fdivl", "ffmal", "finitel", "floorl", "fmal",
    "fmaximum_mag_numl", "fmaximum_magl", "fmaximum_numl", "fmaximuml",
    "fmaxl", "fmaxmagl", "fminimum_mag_numl", "fminimum_magl",
    "fminimum_numl", "fminimuml", "fminl", "fminmagl", "fmodl", "fmull",
    "frexpl", "fromfpl", "fromfpxl", "fsqrtl", "fsubl", "gammal",
    "getpayloadl", "hypotl", "ilogbl", "isinfl", "isnanl", "j0l", "j1l",
    "jnl", "ldexpl", "lgammal", "lgammal_r", "llogbl", "llrintl",
    "llroundl", "log10l", "log10p1l", "log1pl", "log2l", "log2p1l", "logbl",
    "logl", "logp1l", "lrintl", "lroundl", "modfl", "nanl", "nearbyintl",
    "nextafterl", "nextdownl", "nexttoward", "nexttowardf", "nexttowardl",
    "nextupl", "pow10l", "powl", "pownl", "powrl", "qecvt", "qecvt_r",
    "qfcvt", "qfcvt_r", "qgcvt", "remainderl", "remquol", "rintl", "rootnl",
    "roundevenl", "roundl", "rsqrtl", "scalbl", "scalblnl", "scalbnl",
    "setpayloadl", "setpayloadsigl", "significandl", "sincosl", "sinhl",
    "sinl", "sinpil", "sqrtl", "strfroml", "strtold", "strtold_l", "tanhl",
    "tanl", "tanpil", "tgammal", "totalorderl", "totalordermagl", "truncl",
    "ufromfpl", "ufromfpxl", "wcstold", "wcstold_l", "y0l", "y1l", "ynl"};
// clang-format on

// True if \p name has "f64x" or "f128" followed by its end or by '_': glibc's
// TS 18661-3 _Float64x and _Float128 functions (sqrtf64x, lgammaf64x_r,
// strtof64x_l, the narrowing f32addf64x and f64xaddf128, __isinff128,
// __strtof128_internal, ...). With clang, glibc's <bits/floatn.h> makes
// _Float64x the source's `long double` while the native function passes x87
// or binary128 values, and MSIL has no form of the native _Float128.
static bool hasWideFloatSuffix(StringRef name) {
  for (StringRef suffix : {"f64x", "f128"}) {
    for (size_t pos = name.find(suffix); pos != StringRef::npos;
         pos = name.find(suffix, pos + 1)) {
      size_t end = pos + suffix.size();
      if (end == name.size() || name[end] == '_')
        return true;
    }
  }
  return false;
}

bool isNativeLongDoubleFunction(StringRef name) {
  auto less = [](StringRef a, StringRef b) { return a < b; };
  assert(std::is_sorted(std::begin(nativeLongDoubleFunctions),
                        std::end(nativeLongDoubleFunctions), less) &&
         "the long double list must be sorted");
  return std::binary_search(std::begin(nativeLongDoubleFunctions),
                            std::end(nativeLongDoubleFunctions), name, less) ||
         hasWideFloatSuffix(name);
}

bool hasWideNativeLongDouble(const Triple &tt) {
  if (!tt.isOSWindows())
    return true;
  return tt.getArch() == Triple::x86_64 && !tt.isWindowsMSVCEnvironment();
}

//===----------------------------------------------------------------------===//
// NativeIndex
//===----------------------------------------------------------------------===//

void NativeIndex::add(StringRef name, const NativeExport &e) {
  defs[CachedHashStringRef(name)].push_back(new (alloc.Allocate())
                                                NativeExport(e));
}

// The first alias of a name wins, as the first definition does.
void NativeIndex::addAlias(StringRef name, StringRef target) {
  aliases.try_emplace(CachedHashStringRef(name), target);
}

// True for a definition that a function reference never binds: data that a
// COFF import library or a DLL's export table defines. lld-link defines
// <name> only for an IMPORT_CODE member (a DATA member defines only
// __imp_<name>), so it goes on with the later libraries (plan 5.10.10).
static bool isCOFFData(const NativeExport &e) {
  if (e.Kind != NativeKind::Data)
    return false;
  const auto *f = cast<NativeLibraryFile>(e.File);
  return f->format == NativeLibraryFile::Archive ||
         f->format == NativeLibraryFile::PEDll;
}

const NativeExport *
NativeIndex::lookup(StringRef name,
                    SmallVectorImpl<const NativeExport *> &rejected,
                    bool isFunctionReference) const {
  // An alias applies only when its name has no bindable definition of its
  // own (a weak external), and the target resolves like any symbol. Chains of
  // aliases are followed a few steps, so that a cycle cannot hang the link.
  for (unsigned depth = 0; depth != 8; ++depth) {
    auto it = defs.find(CachedHashStringRef(name));
    if (it != defs.end()) {
      for (const NativeExport *e : it->second) {
        if (e->Why == NotBindable::None &&
            !(isFunctionReference && isCOFFData(*e)))
          return e;
        rejected.push_back(e);
      }
    }
    auto alias = aliases.find(CachedHashStringRef(name));
    if (alias == aliases.end())
      return nullptr;
    name = alias->second;
  }
  return nullptr;
}

void NativeIndex::clear() {
  defs.clear();
  aliases.clear();
  alloc.DestroyAll();
}

//===----------------------------------------------------------------------===//
// Object format and machine checks (plan 5.10.8, 5.10.14)
//===----------------------------------------------------------------------===//

std::string getOtherFormatMessage(const Twine &path, const Triple &tt) {
  return (path + ": " + (tt.isOSWindows() ? "ELF" : "PE/COFF") +
          " file cannot be imported for target " + tt.str())
      .str();
}

// A native library is read only in the object format of the codegen triple's
// OS (plan 5.10.8): ELF on non-Windows triples, PE/COFF on Windows triples.
// lld-link rejects an ELF input, and ld.lld rejects a DLL and ignores COFF
// import members. An empty triple (no --target and no bitcode input) accepts
// both formats.
static bool isOtherFormat(bool isCOFF, const Triple &tt) {
  return !tt.str().empty() && isCOFF != tt.isOSWindows();
}

// Reports the native library \p name if it is of the other format (\p isCOFF)
// and returns false for it. The machine checks apply to the files of the
// triple's format only.
static bool checkObjectFormat(const Twine &name, bool isCOFF,
                              const Triple &tt) {
  if (!isOtherFormat(isCOFF, tt))
    return true;
  error(getOtherFormatMessage(name, tt));
  return false;
}

static StringRef getELFMachineName(uint16_t machine) {
  switch (machine) {
  case ELF::EM_386:
    return "EM_386";
  case ELF::EM_X86_64:
    return "EM_X86_64";
  case ELF::EM_AARCH64:
    return "EM_AARCH64";
  case ELF::EM_ARM:
    return "EM_ARM";
  case ELF::EM_RISCV:
    return "EM_RISCV";
  case ELF::EM_PPC:
    return "EM_PPC";
  case ELF::EM_PPC64:
    return "EM_PPC64";
  case ELF::EM_MIPS:
    return "EM_MIPS";
  case ELF::EM_S390:
    return "EM_S390";
  case ELF::EM_LOONGARCH:
    return "EM_LOONGARCH";
  case ELF::EM_SPARCV9:
    return "EM_SPARCV9";
  default:
    return saver().save("EM_" + Twine(machine));
  }
}

static StringRef getCOFFMachineName(uint16_t machine) {
  switch (machine) {
  case COFF::IMAGE_FILE_MACHINE_UNKNOWN:
    return "IMAGE_FILE_MACHINE_UNKNOWN";
  case COFF::IMAGE_FILE_MACHINE_I386:
    return "IMAGE_FILE_MACHINE_I386";
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return "IMAGE_FILE_MACHINE_AMD64";
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return "IMAGE_FILE_MACHINE_ARMNT";
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return "IMAGE_FILE_MACHINE_ARM64";
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
    return "IMAGE_FILE_MACHINE_ARM64EC";
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
    return "IMAGE_FILE_MACHINE_ARM64X";
  default:
    return saver().save("IMAGE_FILE_MACHINE_" + utohexstr(machine));
  }
}

// The codegen triple is one of D6's x86_64 and aarch64 MSIL triples (the
// driver rejects any other before it loads a native library). An empty
// triple (no --target and no bitcode input) accepts any machine.
static bool isCompatibleELF(uint16_t machine, bool is64, bool isLE,
                            const Triple &tt) {
  if (tt.str().empty())
    return true;
  if (!is64 || !isLE)
    return false;
  if (tt.getArch() == Triple::x86_64)
    return machine == ELF::EM_X86_64;
  if (tt.getArch() == Triple::aarch64)
    return machine == ELF::EM_AARCH64;
  return true;
}

static bool isCompatibleCOFF(uint16_t machine, const Triple &tt) {
  if (tt.str().empty() || machine == COFF::IMAGE_FILE_MACHINE_UNKNOWN)
    return true;
  if (tt.getArch() == Triple::x86_64)
    return machine == COFF::IMAGE_FILE_MACHINE_AMD64;
  if (tt.getArch() == Triple::aarch64)
    return machine == COFF::IMAGE_FILE_MACHINE_ARM64 ||
           machine == COFF::IMAGE_FILE_MACHINE_ARM64EC ||
           machine == COFF::IMAGE_FILE_MACHINE_ARM64X;
  return true;
}

static void reportMalformed(const Twine &name, Error e) {
  error(name + ": " + toString(std::move(e)));
}

//===----------------------------------------------------------------------===//
// ELF shared objects
//===----------------------------------------------------------------------===//

// The rules of lld/ELF/InputFiles.cpp SharedFile::parse (~1564-1745) and of
// plan 5.10.8: every defined GLOBAL, WEAK or GNU_UNIQUE dynamic symbol of
// default or protected visibility, except the absolute version markers
// (GLIBC_2.2.5@@GLIBC_2.2.5); a versym without the hidden bit is the default
// version (dlsym returns it), and VER_NDX_GLOBAL is unversioned.
template <class ELFT>
static void readELFShared(NativeIndex &index, NativeLibraryFile &f,
                          const Triple &tt) {
  using Elf_Dyn = typename ELFT::Dyn;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Sym = typename ELFT::Sym;
  using Elf_Versym = typename ELFT::Versym;
  std::string name = toString(&f);
  if (!checkObjectFormat(name, /*isCOFF=*/false, tt))
    return;

  Expected<ELFFile<ELFT>> objOrErr =
      ELFFile<ELFT>::create(f.getMemoryBufferRef().getBuffer());
  if (!objOrErr)
    return reportMalformed(name, objOrErr.takeError());
  const ELFFile<ELFT> &obj = *objOrErr;

  // A library of the right machine for another class or byte order (x32)
  // names them after the machine.
  uint16_t machine = obj.getHeader().e_machine;
  bool isLE = ELFT::Endianness == llvm::endianness::little;
  if (!isCompatibleELF(machine, ELFT::Is64Bits, isLE, tt)) {
    std::string machineName = getELFMachineName(machine).str();
    if (isCompatibleELF(machine, /*is64=*/true, /*isLE=*/true, tt))
      machineName += ELFT::Is64Bits ? " (ELFDATA2MSB)" : " (ELFCLASS32)";
    error(name + ": ELF machine " + machineName + " does not match target " +
          tt.str());
    return;
  }

  Expected<typename ELFT::ShdrRange> sectionsOrErr = obj.sections();
  if (!sectionsOrErr)
    return reportMalformed(name, sectionsOrErr.takeError());
  const Elf_Shdr *dynsymSec = nullptr, *dynamicSec = nullptr,
                 *versymSec = nullptr, *verdefSec = nullptr;
  for (const Elf_Shdr &sec : *sectionsOrErr) {
    switch (sec.sh_type) {
    case ELF::SHT_DYNSYM:
      dynsymSec = &sec;
      break;
    case ELF::SHT_DYNAMIC:
      dynamicSec = &sec;
      break;
    case ELF::SHT_GNU_versym:
      versymSec = &sec;
      break;
    case ELF::SHT_GNU_verdef:
      verdefSec = &sec;
      break;
    default:
      break;
    }
  }

  // The module is DT_SONAME, else the file name as found (plan 5.10.11),
  // which is what a native link records in DT_NEEDED.
  StringRef module = sys::path::filename(f.getName());
  if (dynamicSec) {
    Expected<ArrayRef<Elf_Dyn>> dynOrErr =
        obj.template getSectionContentsAsArray<Elf_Dyn>(*dynamicSec);
    if (!dynOrErr)
      return reportMalformed(name, dynOrErr.takeError());
    Expected<StringRef> strOrErr = obj.getLinkAsStrtab(*dynamicSec);
    if (!strOrErr)
      return reportMalformed(name, strOrErr.takeError());
    for (const Elf_Dyn &dyn : *dynOrErr) {
      if (dyn.d_tag != ELF::DT_SONAME)
        continue;
      uint64_t val = dyn.getVal();
      if (val >= strOrErr->size()) {
        error(name + ": invalid DT_SONAME entry");
        return;
      }
      module = StringRef(strOrErr->data() + val);
    }
  }
  if (!dynsymSec)
    return;

  Expected<ArrayRef<Elf_Sym>> symsOrErr = obj.symbols(dynsymSec);
  if (!symsOrErr)
    return reportMalformed(name, symsOrErr.takeError());
  Expected<StringRef> strtabOrErr =
      obj.getStringTableForSymtab(*dynsymSec, *sectionsOrErr);
  if (!strtabOrErr)
    return reportMalformed(name, strtabOrErr.takeError());

  // Version names by version index (vd_ndx); the names are copied, because
  // ELFFile returns them as std::string.
  DenseMap<unsigned, StringRef> versionNames;
  if (verdefSec) {
    Expected<std::vector<VerDef>> defsOrErr =
        obj.getVersionDefinitions(*verdefSec);
    if (!defsOrErr)
      return reportMalformed(name, defsOrErr.takeError());
    for (const VerDef &def : *defsOrErr)
      versionNames[def.Ndx] = saver().save(def.Name);
  }
  ArrayRef<Elf_Versym> versyms;
  if (versymSec) {
    Expected<ArrayRef<Elf_Versym>> versymsOrErr =
        obj.template getSectionContentsAsArray<Elf_Versym>(*versymSec);
    if (!versymsOrErr)
      return reportMalformed(name, versymsOrErr.takeError());
    versyms = *versymsOrErr;
  }

  ArrayRef<Elf_Sym> syms = *symsOrErr;
  for (size_t i = 1, e = syms.size(); i < e; ++i) {
    const Elf_Sym &sym = syms[i];
    if (sym.isUndefined() || sym.st_shndx == ELF::SHN_ABS)
      continue;
    uint8_t binding = sym.getBinding();
    if (binding != ELF::STB_GLOBAL && binding != ELF::STB_WEAK &&
        binding != ELF::STB_GNU_UNIQUE)
      continue;
    uint8_t visibility = sym.getVisibility();
    if (visibility != ELF::STV_DEFAULT && visibility != ELF::STV_PROTECTED)
      continue;

    NativeKind kind;
    switch (sym.getType()) {
    case ELF::STT_FUNC:
    case ELF::STT_GNU_IFUNC:
      kind = NativeKind::Function;
      break;
    case ELF::STT_OBJECT:
    case ELF::STT_COMMON:
      kind = NativeKind::Data;
      break;
    case ELF::STT_TLS:
      kind = NativeKind::TLS;
      break;
    case ELF::STT_NOTYPE:
      kind = NativeKind::NoType;
      break;
    default:
      continue;
    }

    Expected<StringRef> symNameOrErr = sym.getName(*strtabOrErr);
    if (!symNameOrErr)
      return reportMalformed(name, symNameOrErr.takeError());

    // lld/ELF: VER_NDX_LOCAL is not exported, VER_NDX_GLOBAL is unversioned,
    // and an index without a definition is a corrupt file, which lld reports
    // and msil-ld skips.
    uint16_t ver = i < versyms.size() ? uint16_t(versyms[i].vs_index)
                                      : uint16_t(ELF::VER_NDX_GLOBAL);
    uint16_t idx = ver & ELF::VERSYM_VERSION;
    if (idx == ELF::VER_NDX_LOCAL)
      continue;
    StringRef version;
    if (idx != ELF::VER_NDX_GLOBAL) {
      auto it = versionNames.find(idx);
      if (it == versionNames.end())
        continue;
      version = it->second;
    }

    // The order of the checks gives errno@@GLIBC_PRIVATE, which is TLS, the
    // thread-local text (plan 5.10.14).
    NotBindable why = NotBindable::None;
    if (!version.empty() && (ver & ELF::VERSYM_HIDDEN))
      why = NotBindable::HiddenVersion;
    else if (kind == NativeKind::TLS)
      why = NotBindable::ThreadLocal;
    else if (version == "GLIBC_PRIVATE")
      why = NotBindable::PrivateVersion;
    index.add(*symNameOrErr, {module, *symNameOrErr, kind, why, version, &f});
  }
}

static void readELFShared(NativeIndex &index, NativeLibraryFile &f,
                          const Triple &tt) {
  std::pair<unsigned char, unsigned char> type =
      getElfArchType(f.getMemoryBufferRef().getBuffer());
  bool is64 = type.first == ELF::ELFCLASS64;
  bool isLE = type.second == ELF::ELFDATA2LSB;
  if (is64 && isLE)
    readELFShared<ELF64LE>(index, f, tt);
  else if (is64)
    readELFShared<ELF64BE>(index, f, tt);
  else if (isLE)
    readELFShared<ELF32LE>(index, f, tt);
  else
    readELFShared<ELF32BE>(index, f, tt);
}

//===----------------------------------------------------------------------===//
// PE DLLs
//===----------------------------------------------------------------------===//

// The named exports of a DLL without a CLI header (plan 5.10.8), read from the
// name pointer table, so that every name of an export is found. An export
// whose RVA lies in a section without IMAGE_SCN_CNT_CODE is data; a
// forwarder (an RVA inside the export directory) binds by its own name, as
// GetProcAddress follows it. The module is the DLL's file name.
static void readPEDll(NativeIndex &index, NativeLibraryFile &f,
                      const Triple &tt) {
  std::string name = toString(&f);
  if (!checkObjectFormat(name, /*isCOFF=*/true, tt))
    return;
  Expected<std::unique_ptr<COFFObjectFile>> objOrErr =
      COFFObjectFile::create(f.getMemoryBufferRef());
  if (!objOrErr)
    return reportMalformed(name, objOrErr.takeError());
  const COFFObjectFile &obj = **objOrErr;

  uint16_t machine = obj.getMachine();
  if (!isCompatibleCOFF(machine, tt)) {
    error(name + ": PE machine " + getCOFFMachineName(machine) +
          " does not match target " + tt.str());
    return;
  }

  const data_directory *dir = obj.getDataDirectory(COFF::EXPORT_TABLE);
  if (!dir || dir->RelativeVirtualAddress == 0)
    return;

  // The export directory and its three tables, each wholly inside a section
  // and inside the file. The sizes come from 32-bit counts and are computed
  // in 64 bits, so that a count cannot wrap the check (plan 5.10.8): a table
  // that runs past the image is a malformed export table.
  StringRef data = obj.getData();
  auto getTable = [&](uint32_t rva, uint64_t size,
                      ArrayRef<uint8_t> &bytes) -> bool {
    if (size > UINT32_MAX)
      return false;
    if (Error e = obj.getRvaAndSizeAsBytes(rva, size, bytes)) {
      consumeError(std::move(e));
      return false;
    }
    const char *begin = reinterpret_cast<const char *>(bytes.data());
    return begin >= data.begin() && begin <= data.end() &&
           size <= uint64_t(data.end() - begin);
  };
  auto reportMalformedTable = [&] { error(name + ": malformed export table"); };

  ArrayRef<uint8_t> tableBytes;
  if (!getTable(dir->RelativeVirtualAddress,
                sizeof(export_directory_table_entry), tableBytes))
    return reportMalformedTable();
  const auto *table =
      reinterpret_cast<const export_directory_table_entry *>(tableBytes.data());

  // Only named exports bind (a NONAME export has no name to bind by).
  uint64_t numNames = table->NumberOfNamePointers;
  uint64_t numAddresses = table->AddressTableEntries;
  if (numNames == 0)
    return;
  ArrayRef<uint8_t> nameBytes, ordinalBytes, addressBytes;
  if (!getTable(table->NamePointerRVA, numNames * 4, nameBytes) ||
      !getTable(table->OrdinalTableRVA, numNames * 2, ordinalBytes) ||
      !getTable(table->ExportAddressTableRVA, numAddresses * 4, addressBytes))
    return reportMalformedTable();
  const auto *namePointers =
      reinterpret_cast<const support::ulittle32_t *>(nameBytes.data());
  const auto *ordinals =
      reinterpret_cast<const support::ulittle16_t *>(ordinalBytes.data());
  const auto *addresses =
      reinterpret_cast<const support::ulittle32_t *>(addressBytes.data());

  StringRef module = sys::path::filename(f.getName());
  uint64_t dirBegin = dir->RelativeVirtualAddress;
  uint64_t dirEnd = dirBegin + dir->Size;
  for (uint64_t i = 0; i != numNames; ++i) {
    uintptr_t namePtr;
    if (Error e = obj.getRvaPtr(namePointers[i], namePtr)) {
      consumeError(std::move(e));
      return reportMalformedTable();
    }
    const char *begin = reinterpret_cast<const char *>(namePtr);
    if (begin < data.begin() || begin >= data.end())
      continue;
    StringRef exportName(begin, strnlen(begin, data.end() - begin));
    uint16_t ordinal = ordinals[i];
    if (exportName.empty() || ordinal >= numAddresses)
      continue;
    uint32_t rva = addresses[ordinal];
    bool isForwarder = rva >= dirBegin && rva < dirEnd;
    NativeKind kind = NativeKind::Function;
    if (!isForwarder) {
      for (const SectionRef &sec : obj.sections()) {
        const coff_section *coffSec = obj.getCOFFSection(sec);
        if (rva >= coffSec->VirtualAddress &&
            rva < uint64_t(coffSec->VirtualAddress) +
                      std::max<uint32_t>(coffSec->VirtualSize,
                                         coffSec->SizeOfRawData)) {
          if (!(coffSec->Characteristics & COFF::IMAGE_SCN_CNT_CODE))
            kind = NativeKind::Data;
          break;
        }
      }
    }
    index.add(exportName,
              {module, exportName, kind, NotBindable::None, "", &f});
  }
}

//===----------------------------------------------------------------------===//
// Archives
//===----------------------------------------------------------------------===//

namespace {
// The reader of one archive: its COFF short import members, its MinGW
// long-format import objects, its weak-alias objects and its native objects.
class ArchiveReader {
public:
  ArchiveReader(NativeIndex &index, NativeLibraryFile &f, const Triple &tt)
      : index(index), file(f), tt(tt) {}
  void read();

private:
  void readMember(MemoryBufferRef mb);
  void readCOFFShortImport(MemoryBufferRef mb);
  void readCOFFObject(MemoryBufferRef mb);
  void readELFRelocatable(MemoryBufferRef mb);
  void readGNULongImport(const COFFObjectFile &obj, MemoryBufferRef mb,
                         StringRef head);
  bool checkFormat();
  bool checkMachine(uint16_t machine, MemoryBufferRef mb);
  void addNativeCode(StringRef sym, MemoryBufferRef mb);
  std::string memberName(MemoryBufferRef mb) const {
    return (toString(&file) + "(" + mb.getBufferIdentifier() + ")").str();
  }

  NativeIndex &index;
  NativeLibraryFile &file;
  const Triple &tt;
  std::optional<bool> isRightFormat;
  bool machineReported = false;

  // MinGW long-format import libraries (native-imports 2.5): the member of
  // each function refers to the head member's symbol from its .idata$7; the
  // head member's .idata$2 names, at offset 12 (the Name field of the import
  // directory entry), the symbol of the tail member whose .idata$7 holds the
  // DLL name.
  struct LongImport {
    std::unique_ptr<COFFObjectFile> obj;
    MemoryBufferRef mb;
    StringRef head;
  };
  SmallVector<LongImport, 0> longImports;
  DenseMap<CachedHashStringRef, StringRef> headToName;
  DenseMap<CachedHashStringRef, StringRef> nameToDll;

  // The file of the machine code of the current member, created on demand.
  NativeLibraryFile *memberFile = nullptr;
};
} // namespace

// The import members, the parts of long-format import libraries and the
// weak-alias objects of an archive make it a PE/COFF import library, which a
// non-Windows triple does not read (plan 5.10.8). Reported once per archive,
// and those members add nothing.
bool ArchiveReader::checkFormat() {
  if (!isRightFormat)
    isRightFormat = checkObjectFormat(toString(&file), /*isCOFF=*/true, tt);
  return *isRightFormat;
}

bool ArchiveReader::checkMachine(uint16_t machine, MemoryBufferRef mb) {
  if (isCompatibleCOFF(machine, tt))
    return true;
  // Reported once per archive: an import library for another machine has
  // thousands of members.
  if (!machineReported)
    error(memberName(mb) + ": COFF machine " + getCOFFMachineName(machine) +
          " does not match target " + tt.str());
  machineReported = true;
  return false;
}

// lld/COFF/InputFiles.cpp:
static StringRef ltrim1(StringRef s, const char *chars) {
  if (!s.empty() && strchr(chars, s[0]))
    return s.substr(1);
  return s;
}

// A COFF short import member (lld/COFF/InputFiles.cpp ImportFile::parse,
// ~1259-1300): a 20-byte header, the symbol name, the DLL name and, for
// IMPORT_NAME_EXPORTAS, the export name, each NUL-terminated. The entry
// follows the name type; an ordinal-only import is "#<ordinal>".
void ArchiveReader::readCOFFShortImport(MemoryBufferRef mb) {
  if (!checkFormat())
    return;
  const auto *hdr =
      reinterpret_cast<const coff_import_header *>(mb.getBufferStart());
  if (mb.getBufferSize() < sizeof(*hdr) ||
      mb.getBufferSize() != sizeof(*hdr) + hdr->SizeOfData) {
    error(memberName(mb) + ": broken import library");
    return;
  }
  if (!checkMachine(hdr->Machine, mb))
    return;

  StringRef buf = mb.getBuffer().substr(sizeof(*hdr));
  auto split = buf.split('\0');
  StringRef name = split.first;
  buf = split.second;
  StringRef dllName = buf.split('\0').first;
  StringRef extName;
  switch (hdr->getNameType()) {
  case COFF::IMPORT_ORDINAL:
    extName = saver().save("#" + Twine(uint32_t(hdr->OrdinalHint)));
    break;
  case COFF::IMPORT_NAME:
    extName = name;
    break;
  case COFF::IMPORT_NAME_NOPREFIX:
    extName = ltrim1(name, "?@_");
    break;
  case COFF::IMPORT_NAME_UNDECORATE:
    extName = ltrim1(name, "?@_");
    extName = extName.substr(0, extName.find('@'));
    break;
  case COFF::IMPORT_NAME_EXPORTAS:
    extName = buf.substr(dllName.size() + 1).split('\0').first;
    break;
  default:
    error(memberName(mb) + ": broken import library");
    return;
  }
  if (name.empty() || dllName.empty())
    return;

  NativeKind kind = hdr->getType() == COFF::IMPORT_CODE ? NativeKind::Function
                                                        : NativeKind::Data;
  index.add(name, {dllName, extName, kind, NotBindable::None, "", &file});
}

void ArchiveReader::addNativeCode(StringRef sym, MemoryBufferRef mb) {
  if (!memberFile) {
    memberFile = make<NativeLibraryFile>(mb, NativeLibraryFile::ArchiveMember);
    memberFile->archiveName = std::string(file.getName());
  }
  index.add(sym, {"", sym, NativeKind::Function, NotBindable::NativeCodeOnly,
                  "", memberFile});
}

static Expected<StringRef> getSymbolNameByIndex(const COFFObjectFile &obj,
                                                uint32_t symIndex) {
  Expected<COFFSymbolRef> symOrErr = obj.getSymbol(symIndex);
  if (!symOrErr)
    return symOrErr.takeError();
  return obj.getSymbolName(*symOrErr);
}

// The symbol of \p obj that the relocation at \p offset of \p sec names, or
// an empty name.
static StringRef getRelocationTarget(const COFFObjectFile &obj,
                                     const coff_section *sec, uint32_t offset) {
  for (const coff_relocation &rel : obj.getRelocations(sec)) {
    if (rel.VirtualAddress != offset)
      continue;
    Expected<StringRef> nameOrErr =
        getSymbolNameByIndex(obj, rel.SymbolTableIndex);
    if (!nameOrErr) {
      consumeError(nameOrErr.takeError());
      return "";
    }
    return *nameOrErr;
  }
  return "";
}

// A COFF object member: an oldnames.lib-style object (weak externals with
// IMAGE_WEAK_EXTERN_SEARCH_ALIAS: the alias resolves to its target), a part
// of a MinGW long-format import library (a head, a tail or a function's
// member, recognized by their .idata$ sections), or native code.
void ArchiveReader::readCOFFObject(MemoryBufferRef mb) {
  Expected<std::unique_ptr<COFFObjectFile>> objOrErr =
      COFFObjectFile::create(mb);
  if (!objOrErr) {
    reportMalformed(memberName(mb), objOrErr.takeError());
    return;
  }
  std::unique_ptr<COFFObjectFile> obj = std::move(*objOrErr);

  const coff_section *idata2 = nullptr, *idata5 = nullptr, *idata7 = nullptr;
  bool hasIdata = false;
  for (const SectionRef &sec : obj->sections()) {
    const coff_section *coffSec = obj->getCOFFSection(sec);
    Expected<StringRef> secNameOrErr = obj->getSectionName(coffSec);
    if (!secNameOrErr) {
      consumeError(secNameOrErr.takeError());
      continue;
    }
    if (!secNameOrErr->starts_with(".idata$"))
      continue;
    hasIdata = true;
    if (*secNameOrErr == ".idata$2")
      idata2 = coffSec;
    else if (*secNameOrErr == ".idata$5")
      idata5 = coffSec;
    else if (*secNameOrErr == ".idata$7")
      idata7 = coffSec;
  }
  if (hasIdata && !checkFormat())
    return;

  // A function's member: its .idata$7 refers to the head symbol.
  if (idata7 && idata5 && !obj->getRelocations(idata7).empty()) {
    if (!checkMachine(obj->getMachine(), mb))
      return;
    StringRef head = getRelocationTarget(*obj, idata7, 0);
    if (!head.empty())
      longImports.push_back({std::move(obj), mb, head});
    return;
  }

  for (uint32_t i = 0, e = obj->getNumberOfSymbols(); i < e; ++i) {
    Expected<COFFSymbolRef> symOrErr = obj->getSymbol(i);
    if (!symOrErr) {
      reportMalformed(memberName(mb), symOrErr.takeError());
      return;
    }
    COFFSymbolRef sym = *symOrErr;
    i += sym.getNumberOfAuxSymbols();
    Expected<StringRef> nameOrErr = obj->getSymbolName(sym);
    if (!nameOrErr) {
      reportMalformed(memberName(mb), nameOrErr.takeError());
      return;
    }

    // oldnames.lib: 'strdup' is a weak external that aliases '_strdup'.
    if (sym.isWeakExternal()) {
      const coff_aux_weak_external *aux = sym.getWeakExternal();
      if (aux && aux->Characteristics == COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS &&
          checkFormat()) {
        Expected<StringRef> targetOrErr =
            getSymbolNameByIndex(*obj, aux->TagIndex);
        if (!targetOrErr) {
          reportMalformed(memberName(mb), targetOrErr.takeError());
          return;
        }
        index.addAlias(*nameOrErr, *targetOrErr);
      }
      continue;
    }

    if (!sym.isExternal() || sym.getSectionNumber() <= 0)
      continue;
    Expected<const coff_section *> secOrErr =
        obj->getSection(sym.getSectionNumber());
    if (!secOrErr) {
      reportMalformed(memberName(mb), secOrErr.takeError());
      return;
    }

    // The head of a long-format import library: _head_<lib> in .idata$2,
    // whose Name field (offset 12) names the tail's symbol.
    if (idata2 && *secOrErr == idata2) {
      StringRef nameSym = getRelocationTarget(*obj, idata2, 12);
      if (!nameSym.empty())
        headToName.try_emplace(CachedHashStringRef(*nameOrErr), nameSym);
      continue;
    }

    // The tail: a symbol in .idata$7 that holds the DLL name.
    if (idata7 && *secOrErr == idata7) {
      ArrayRef<uint8_t> contents;
      if (Error err = obj->getSectionContents(idata7, contents)) {
        reportMalformed(memberName(mb), std::move(err));
        return;
      }
      StringRef str = toStringRef(contents);
      if (sym.getValue() < str.size()) {
        StringRef dll = str.substr(sym.getValue());
        dll = dll.take_until([](char c) { return c == '\0'; });
        if (!dll.empty())
          nameToDll.try_emplace(CachedHashStringRef(*nameOrErr), dll);
      }
      continue;
    }

    // Other parts of import libraries (MSVC's import descriptors) define
    // nothing that code calls; anything else is machine code.
    if (!hasIdata)
      addNativeCode(*nameOrErr, mb);
  }
}

// A function's (or data's) member of a MinGW long-format import library
// (native-imports 2.5): '<name>' in .text and '__imp_<name>' in .idata$5 for a
// function, '__imp_<name>' alone for data; the entry is the hint-name entry
// of .idata$6 (after the 2-byte hint), or, without one, the ordinal of an
// .idata$5 entry with IMAGE_ORDINAL_FLAG64.
void ArchiveReader::readGNULongImport(const COFFObjectFile &obj,
                                      MemoryBufferRef mb, StringRef head) {
  StringRef dll;
  if (auto it = headToName.find(CachedHashStringRef(head));
      it != headToName.end())
    if (auto it2 = nameToDll.find(CachedHashStringRef(it->second));
        it2 != nameToDll.end())
      dll = it2->second;
  if (dll.empty()) {
    log(memberName(mb) + ": no DLL name for import head " + head + "; ignored");
    return;
  }

  StringRef entry;
  const coff_section *idata5 = nullptr;
  for (const SectionRef &sec : obj.sections()) {
    const coff_section *coffSec = obj.getCOFFSection(sec);
    Expected<StringRef> secNameOrErr = obj.getSectionName(coffSec);
    if (!secNameOrErr) {
      consumeError(secNameOrErr.takeError());
      continue;
    }
    if (*secNameOrErr == ".idata$5")
      idata5 = coffSec;
    if (*secNameOrErr != ".idata$6")
      continue;
    ArrayRef<uint8_t> contents;
    if (Error err = obj.getSectionContents(coffSec, contents)) {
      reportMalformed(memberName(mb), std::move(err));
      return;
    }
    StringRef str = toStringRef(contents);
    if (str.size() > 2)
      entry = str.substr(2).take_until([](char c) { return c == '\0'; });
  }
  if (entry.empty() && idata5) {
    ArrayRef<uint8_t> contents;
    if (Error err = obj.getSectionContents(idata5, contents)) {
      reportMalformed(memberName(mb), std::move(err));
      return;
    }
    if (contents.size() >= 8) {
      uint64_t value = support::endian::read64le(contents.data());
      if (value & (uint64_t(1) << 63))
        entry = saver().save("#" + Twine(value & 0xffff));
    } else if (contents.size() >= 4) {
      uint32_t value = support::endian::read32le(contents.data());
      if (value & (uint32_t(1) << 31))
        entry = saver().save("#" + Twine(value & 0xffff));
    }
  }

  StringRef function, data;
  for (uint32_t i = 0, e = obj.getNumberOfSymbols(); i < e; ++i) {
    Expected<COFFSymbolRef> symOrErr = obj.getSymbol(i);
    if (!symOrErr) {
      reportMalformed(memberName(mb), symOrErr.takeError());
      return;
    }
    COFFSymbolRef sym = *symOrErr;
    i += sym.getNumberOfAuxSymbols();
    if (!sym.isExternal() || sym.getSectionNumber() <= 0)
      continue;
    Expected<StringRef> nameOrErr = obj.getSymbolName(sym);
    if (!nameOrErr) {
      reportMalformed(memberName(mb), nameOrErr.takeError());
      return;
    }
    if (nameOrErr->starts_with("__imp_"))
      data = nameOrErr->drop_front(6);
    else
      function = *nameOrErr;
  }
  StringRef sym = function.empty() ? data : function;
  if (sym.empty())
    return;
  if (entry.empty())
    entry = sym;
  index.add(sym, {dll, entry,
                  function.empty() ? NativeKind::Data : NativeKind::Function,
                  NotBindable::None, "", &file});
}

// An ELF relocatable object (libc_nonshared.a): its global definitions are
// machine code.
void ArchiveReader::readELFRelocatable(MemoryBufferRef mb) {
  Expected<std::unique_ptr<ObjectFile>> objOrErr =
      ObjectFile::createObjectFile(mb);
  if (!objOrErr) {
    reportMalformed(memberName(mb), objOrErr.takeError());
    return;
  }
  for (const SymbolRef &sym : (*objOrErr)->symbols()) {
    Expected<uint32_t> flagsOrErr = sym.getFlags();
    if (!flagsOrErr) {
      reportMalformed(memberName(mb), flagsOrErr.takeError());
      return;
    }
    if (!(*flagsOrErr & SymbolRef::SF_Global) ||
        (*flagsOrErr & (SymbolRef::SF_Undefined | SymbolRef::SF_Absolute |
                        SymbolRef::SF_FormatSpecific)))
      continue;
    Expected<StringRef> nameOrErr = sym.getName();
    if (!nameOrErr) {
      reportMalformed(memberName(mb), nameOrErr.takeError());
      return;
    }
    addNativeCode(*nameOrErr, mb);
  }
}

void ArchiveReader::readMember(MemoryBufferRef mb) {
  memberFile = nullptr;
  switch (identify_magic(mb.getBuffer())) {
  case file_magic::coff_import_library:
    readCOFFShortImport(mb);
    return;
  case file_magic::coff_object:
    readCOFFObject(mb);
    return;
  case file_magic::elf_relocatable:
    readELFRelocatable(mb);
    return;
  default:
    // LLVM bitcode and CIL objects are the symbol table's (the driver makes
    // them lazy), and anything else defines nothing a P/Invoke can bind.
    return;
  }
}

void ArchiveReader::read() {
  MemoryBufferRef mb = file.getMemoryBufferRef();
  Expected<std::unique_ptr<Archive>> archiveOrErr = Archive::create(mb);
  if (!archiveOrErr) {
    reportMalformed(toString(&file) + ": failed to parse archive",
                    archiveOrErr.takeError());
    return;
  }
  Archive &archive = **archiveOrErr;
  Error err = Error::success();
  for (const Archive::Child &c : archive.children(err)) {
    Expected<MemoryBufferRef> memberOrErr = c.getMemoryBufferRef();
    if (!memberOrErr) {
      reportMalformed(toString(&file), memberOrErr.takeError());
      continue;
    }
    readMember(*memberOrErr);
  }
  if (err)
    reportMalformed(toString(&file), std::move(err));

  // Take ownership of memory buffers created for members of thin archives.
  for (std::unique_ptr<MemoryBuffer> &thin : archive.takeThinBuffers())
    make<std::unique_ptr<MemoryBuffer>>(std::move(thin));

  // The heads and tails of long-format import libraries come anywhere in the
  // archive, so the functions' members are read last.
  for (LongImport &imp : longImports)
    readGNULongImport(*imp.obj, imp.mb, imp.head);
  longImports.clear();
}

//===----------------------------------------------------------------------===//
// --ucrtbase-imports (plan 5.10.4)
//===----------------------------------------------------------------------===//

// The C memory and string functions that only vcruntime.lib imports (from
// VCRUNTIME140.dll); ucrtbase.dll exports all of them (native-imports 2.4).
static void readUCRTBaseImports(NativeIndex &index, NativeLibraryFile &f) {
  static const char *const functions[] = {
      "memcpy",  "memset", "memmove", "memcmp",  "memchr", "strchr",
      "strrchr", "strstr", "wcschr",  "wcsrchr", "wcsstr"};
  for (const char *fn : functions)
    index.add(fn, {"ucrtbase.dll", fn, NativeKind::Function, NotBindable::None,
                   "", &f});
}

//===----------------------------------------------------------------------===//
// NativeIndex::addFile
//===----------------------------------------------------------------------===//

void NativeIndex::addFile(NativeLibraryFile &f, const Triple &tt) {
  switch (f.format) {
  case NativeLibraryFile::ELFShared:
    readELFShared(*this, f, tt);
    return;
  case NativeLibraryFile::Archive:
    ArchiveReader(*this, f, tt).read();
    return;
  case NativeLibraryFile::PEDll:
    readPEDll(*this, f, tt);
    return;
  case NativeLibraryFile::Builtin:
    readUCRTBaseImports(*this, f);
    return;
  case NativeLibraryFile::ArchiveMember:
    return;
  }
  llvm_unreachable("unknown native library format");
}

//===----------------------------------------------------------------------===//
// Classification (plan 5.10.9)
//===----------------------------------------------------------------------===//

static bool hasCLIHeader(MemoryBufferRef mb) {
  Expected<std::unique_ptr<COFFObjectFile>> objOrErr =
      COFFObjectFile::create(mb);
  if (!objOrErr) {
    consumeError(objOrErr.takeError());
    return false;
  }
  const data_directory *dir =
      (*objOrErr)->getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  return dir && dir->RelativeVirtualAddress != 0;
}

// True for a COFF object that is part of an import library (a .idata$
// section) or an oldnames.lib-style alias object.
static bool isCOFFImportObject(MemoryBufferRef mb) {
  Expected<std::unique_ptr<COFFObjectFile>> objOrErr =
      COFFObjectFile::create(mb);
  if (!objOrErr) {
    consumeError(objOrErr.takeError());
    return false;
  }
  const COFFObjectFile &obj = **objOrErr;
  for (const SectionRef &sec : obj.sections()) {
    Expected<StringRef> nameOrErr = sec.getName();
    if (!nameOrErr) {
      consumeError(nameOrErr.takeError());
      continue;
    }
    if (nameOrErr->starts_with(".idata$"))
      return true;
  }
  for (uint32_t i = 0, e = obj.getNumberOfSymbols(); i < e; ++i) {
    Expected<COFFSymbolRef> symOrErr = obj.getSymbol(i);
    if (!symOrErr) {
      consumeError(symOrErr.takeError());
      return false;
    }
    i += symOrErr->getNumberOfAuxSymbols();
    if (const coff_aux_weak_external *aux = symOrErr->getWeakExternal())
      if (aux->Characteristics == COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS)
        return true;
  }
  return false;
}

bool isNativeArchiveMember(MemoryBufferRef mb) {
  switch (identify_magic(mb.getBuffer())) {
  case file_magic::coff_import_library:
  case file_magic::coff_object:
  case file_magic::elf_relocatable:
    return true;
  default:
    return false;
  }
}

LibraryKind classifyLibrary(MemoryBufferRef mb, const Triple &tt) {
  switch (identify_magic(mb.getBuffer())) {
  case file_magic::bitcode:
    return LibraryKind::Static;
  case file_magic::pecoff_executable:
    if (hasCLIHeader(mb))
      return LibraryKind::Managed;
    return isOtherFormat(/*isCOFF=*/true, tt) ? LibraryKind::OtherFormat
                                              : LibraryKind::NativeShared;
  case file_magic::elf_shared_object:
    return isOtherFormat(/*isCOFF=*/false, tt) ? LibraryKind::OtherFormat
                                               : LibraryKind::NativeShared;
  case file_magic::archive: {
    Expected<std::unique_ptr<Archive>> archiveOrErr = Archive::create(mb);
    if (!archiveOrErr) {
      // Reading the file reports it.
      consumeError(archiveOrErr.takeError());
      return LibraryKind::NativeShared;
    }
    bool hasMembers = false, hasImports = false;
    Error err = Error::success();
    for (const Archive::Child &c : (*archiveOrErr)->children(err)) {
      Expected<MemoryBufferRef> memberOrErr = c.getMemoryBufferRef();
      if (!memberOrErr) {
        consumeError(memberOrErr.takeError());
        continue;
      }
      hasMembers = true;
      switch (identify_magic(memberOrErr->getBuffer())) {
      case file_magic::bitcode:
        consumeError(std::move(err));
        return LibraryKind::Static;
      case file_magic::pecoff_executable:
        if (hasCLIHeader(*memberOrErr)) {
          consumeError(std::move(err));
          return LibraryKind::Static;
        }
        break;
      case file_magic::coff_import_library:
        hasImports = true;
        break;
      case file_magic::coff_object:
        if (!hasImports && isCOFFImportObject(*memberOrErr))
          hasImports = true;
        break;
      default:
        break;
      }
    }
    consumeError(std::move(err));
    // An archive without members (glibc's libpthread.a and libdl.a since
    // 2.34) is a static library that defines nothing, as a native link takes
    // it.
    if (!hasMembers)
      return LibraryKind::Static;
    if (!hasImports)
      return LibraryKind::NativeOnly;
    return isOtherFormat(/*isCOFF=*/true, tt) ? LibraryKind::OtherFormat
                                              : LibraryKind::Import;
  }
  default:
    return LibraryKind::NativeShared;
  }
}

//===----------------------------------------------------------------------===//
// GNU ld scripts (plan 5.10.8)
//===----------------------------------------------------------------------===//

namespace {
struct ScriptToken {
  StringRef text;
  size_t offset;
};
} // namespace

// The lexer of lld/ELF's ScriptLexer, for the commands msil-ld reads:
// /* */ and # comments, quoted strings, the punctuation ( ) , ; and words.
static void tokenizeScript(StringRef s, SmallVectorImpl<ScriptToken> &tokens,
                           size_t &unclosedComment) {
  unclosedComment = StringRef::npos;
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (isSpace(c)) {
      ++i;
      continue;
    }
    if (s.substr(i).starts_with("/*")) {
      size_t end = s.find("*/", i + 2);
      if (end == StringRef::npos) {
        unclosedComment = i;
        return;
      }
      i = end + 2;
      continue;
    }
    if (c == '#') {
      size_t end = s.find('\n', i);
      i = end == StringRef::npos ? s.size() : end + 1;
      continue;
    }
    if (c == '"') {
      size_t end = s.find('"', i + 1);
      if (end == StringRef::npos)
        end = s.size() - 1;
      tokens.push_back({s.slice(i, end + 1), i});
      i = end + 1;
      continue;
    }
    if (c == '(' || c == ')' || c == ',' || c == ';') {
      tokens.push_back({s.substr(i, 1), i});
      ++i;
      continue;
    }
    size_t end = s.find_first_of(" \t\n\v\f\r(),;\"", i);
    if (end == StringRef::npos)
      end = s.size();
    tokens.push_back({s.slice(i, end), i});
    i = end;
  }
}

static StringRef unquote(StringRef s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    return s.substr(1, s.size() - 2);
  return s;
}

bool isLinkerScript(MemoryBufferRef mb) {
  StringRef s = mb.getBuffer();
  if (s.take_front(4096).contains('\0'))
    return false;
  SmallVector<ScriptToken, 8> tokens;
  size_t unclosed;
  tokenizeScript(s, tokens, unclosed);
  if (tokens.empty())
    return false;
  return StringSwitch<bool>(tokens.front().text)
      .Cases({"GROUP", "INPUT", "OUTPUT_FORMAT", "SEARCH_DIR"}, true)
      .Default(false);
}

std::string getScriptDiagnostic(MemoryBufferRef mb, size_t offset,
                                const Twine &msg) {
  StringRef s = mb.getBuffer();
  offset = std::min(offset, s.size());
  size_t lineNo = 1 + s.take_front(offset).count('\n');
  size_t lineBegin = s.take_front(offset).rfind('\n');
  lineBegin = lineBegin == StringRef::npos ? 0 : lineBegin + 1;
  StringRef line = s.substr(lineBegin).take_until(
      [](char c) { return c == '\n' || c == '\r'; });
  return (mb.getBufferIdentifier() + ":" + Twine(lineNo) + ": " + msg +
          "\n>>> " + line + "\n>>> " + std::string(offset - lineBegin, ' ') +
          "^")
      .str();
}

Error readLinkerScript(MemoryBufferRef mb,
                       SmallVectorImpl<LinkerScriptInput> &inputs) {
  StringRef s = mb.getBuffer();
  SmallVector<ScriptToken, 0> tokens;
  size_t unclosed;
  tokenizeScript(s, tokens, unclosed);
  auto fail = [&](size_t offset, const Twine &msg) {
    return createStringError(getScriptDiagnostic(mb, offset, msg));
  };
  if (unclosed != StringRef::npos)
    return fail(unclosed, "unclosed comment in a linker script");

  size_t pos = 0;
  auto atEnd = [&] { return pos == tokens.size(); };
  auto eofOffset = [&] {
    return tokens.empty() ? s.size()
                          : tokens.back().offset + tokens.back().text.size();
  };
  auto expect = [&](StringRef expected) -> Error {
    if (atEnd())
      return fail(eofOffset(), "unexpected EOF");
    if (tokens[pos].text != expected)
      return fail(tokens[pos].offset,
                  expected + " expected, but got " + tokens[pos].text);
    ++pos;
    return Error::success();
  };
  auto addInput = [&](const ScriptToken &tok) {
    StringRef name = unquote(tok.text);
    if (name.starts_with("-l"))
      inputs.push_back({name.drop_front(2), /*isLibrary=*/true, tok.offset});
    else
      inputs.push_back({name, /*isLibrary=*/false, tok.offset});
  };
  // lld/ELF's readInput and readAsNeeded: file names until ')', commas
  // between them allowed.
  std::function<Error(bool)> readInputList = [&](bool allowAsNeeded) -> Error {
    if (Error e = expect("("))
      return e;
    while (true) {
      if (atEnd())
        return fail(eofOffset(), "unexpected EOF");
      const ScriptToken &tok = tokens[pos++];
      if (tok.text == ")")
        return Error::success();
      if (tok.text == ",")
        continue;
      if (allowAsNeeded && tok.text == "AS_NEEDED") {
        if (Error e = readInputList(/*allowAsNeeded=*/false))
          return e;
        continue;
      }
      if (tok.text == "(" || tok.text == ";")
        return fail(tok.offset, "unexpected " + tok.text);
      addInput(tok);
    }
  };

  while (!atEnd()) {
    const ScriptToken &tok = tokens[pos++];
    if (tok.text == ";")
      continue;
    if (tok.text == "GROUP" || tok.text == "INPUT") {
      if (Error e = readInputList(/*allowAsNeeded=*/true))
        return e;
      continue;
    }
    if (tok.text == "OUTPUT_FORMAT" || tok.text == "SEARCH_DIR") {
      // Ignored (plan 5.10.8): msil-ld links one format, and a script's
      // search directories are not the -L directories of the link.
      if (Error e = expect("("))
        return e;
      while (!atEnd() && tokens[pos].text != ")")
        ++pos;
      if (Error e = expect(")"))
        return e;
      continue;
    }
    return fail(tok.offset, "unknown directive: " + tok.text);
  }
  return Error::success();
}
} // namespace lld::msil
