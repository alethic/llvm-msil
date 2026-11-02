//===- Variadics.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The C runtime tables of native variadic functions and the link-time texts
// of variadic calls (see Variadics.h).
//
//===----------------------------------------------------------------------===//

#include "Variadics.h"
#include "Config.h"
#include "Exports.h"
#include "NativeLibraries.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

using namespace llvm;

namespace lld::msil {
namespace {
// A native variadic function and its va_list variant.
struct VariantPair {
  const char *Name;
  const char *Variant;
};

// A native variadic function without a va_list variant, called with fixed
// arity, and the variadic slots that its address adapter reads (5.15.3,
// M28): empty when it has no adapter.
struct FixedArityFunction {
  const char *Name;
  ArrayRef<VarSlot> Slots;
};
} // namespace

//===----------------------------------------------------------------------===//
// The tables (plan 5.15.2; recon p3 variadics 2.4)
//===----------------------------------------------------------------------===//

// Every table is sorted by name in byte order, for binary search
// (checkTables asserts it).

// glibc 2.43: the 44 exported variadic functions of libc.so.6 with a
// va_list variant (variadics-files/glibc-variadics.txt). Musl uses the pairs
// without the __isoc* and _chk names (isGlibcOnlyName; unverified, musl is
// no execution triple).
static constexpr VariantPair glibcVariants[] = {
    {"__asprintf_chk", "__vasprintf_chk"},
    {"__dprintf_chk", "__vdprintf_chk"},
    {"__fprintf_chk", "__vfprintf_chk"},
    {"__fwprintf_chk", "__vfwprintf_chk"},
    {"__isoc23_fscanf", "__isoc23_vfscanf"},
    {"__isoc23_fwscanf", "__isoc23_vfwscanf"},
    {"__isoc23_scanf", "__isoc23_vscanf"},
    {"__isoc23_sscanf", "__isoc23_vsscanf"},
    {"__isoc23_swscanf", "__isoc23_vswscanf"},
    {"__isoc23_wscanf", "__isoc23_vwscanf"},
    {"__isoc99_fscanf", "__isoc99_vfscanf"},
    {"__isoc99_fwscanf", "__isoc99_vfwscanf"},
    {"__isoc99_scanf", "__isoc99_vscanf"},
    {"__isoc99_sscanf", "__isoc99_vsscanf"},
    {"__isoc99_swscanf", "__isoc99_vswscanf"},
    {"__isoc99_wscanf", "__isoc99_vwscanf"},
    {"__obstack_printf_chk", "__obstack_vprintf_chk"},
    {"__printf_chk", "__vprintf_chk"},
    {"__snprintf_chk", "__vsnprintf_chk"},
    {"__sprintf_chk", "__vsprintf_chk"},
    {"__swprintf_chk", "__vswprintf_chk"},
    {"__syslog_chk", "__vsyslog_chk"},
    {"__wprintf_chk", "__vwprintf_chk"},
    {"asprintf", "vasprintf"},
    {"dprintf", "vdprintf"},
    {"err", "verr"},
    {"errx", "verrx"},
    {"fprintf", "vfprintf"},
    {"fscanf", "vfscanf"},
    {"fwprintf", "vfwprintf"},
    {"fwscanf", "vfwscanf"},
    {"obstack_printf", "obstack_vprintf"},
    {"printf", "vprintf"},
    {"scanf", "vscanf"},
    {"snprintf", "vsnprintf"},
    {"sprintf", "vsprintf"},
    {"sscanf", "vsscanf"},
    {"swprintf", "vswprintf"},
    {"swscanf", "vswscanf"},
    {"syslog", "vsyslog"},
    {"warn", "vwarn"},
    {"warnx", "vwarnx"},
    {"wprintf", "vwprintf"},
    {"wscanf", "vwscanf"},
};

// msvcrt.dll (windows-gnu): the 64 exported variadic functions with a
// va_list variant (variadics-files/msvcrt-variadics.txt). UCRT has none: its
// printf and scanf families are inline functions of its headers over
// __stdio_common_v*, which the helper archive defines for windows-msvc.
static constexpr VariantPair msvcrtVariants[] = {
    {"_cprintf", "_vcprintf"},
    {"_cprintf_l", "_vcprintf_l"},
    {"_cprintf_p", "_vcprintf_p"},
    {"_cprintf_p_l", "_vcprintf_p_l"},
    {"_cprintf_s", "_vcprintf_s"},
    {"_cprintf_s_l", "_vcprintf_s_l"},
    {"_cwprintf", "_vcwprintf"},
    {"_cwprintf_l", "_vcwprintf_l"},
    {"_cwprintf_p", "_vcwprintf_p"},
    {"_cwprintf_p_l", "_vcwprintf_p_l"},
    {"_cwprintf_s", "_vcwprintf_s"},
    {"_cwprintf_s_l", "_vcwprintf_s_l"},
    {"_fprintf_l", "_vfprintf_l"},
    {"_fprintf_p", "_vfprintf_p"},
    {"_fprintf_p_l", "_vfprintf_p_l"},
    {"_fprintf_s_l", "_vfprintf_s_l"},
    {"_fwprintf_l", "_vfwprintf_l"},
    {"_fwprintf_p", "_vfwprintf_p"},
    {"_fwprintf_p_l", "_vfwprintf_p_l"},
    {"_fwprintf_s_l", "_vfwprintf_s_l"},
    {"_printf_l", "_vprintf_l"},
    {"_printf_p", "_vprintf_p"},
    {"_printf_p_l", "_vprintf_p_l"},
    {"_printf_s_l", "_vprintf_s_l"},
    {"_scprintf", "_vscprintf"},
    {"_scprintf_l", "_vscprintf_l"},
    {"_scprintf_p_l", "_vscprintf_p_l"},
    {"_scwprintf", "_vscwprintf"},
    {"_scwprintf_l", "_vscwprintf_l"},
    {"_scwprintf_p_l", "_vscwprintf_p_l"},
    {"_snprintf", "_vsnprintf"},
    {"_snprintf_c", "_vsnprintf_c"},
    {"_snprintf_c_l", "_vsnprintf_c_l"},
    {"_snprintf_l", "_vsnprintf_l"},
    {"_snprintf_s", "_vsnprintf_s"},
    {"_snprintf_s_l", "_vsnprintf_s_l"},
    {"_snwprintf", "_vsnwprintf"},
    {"_snwprintf_l", "_vsnwprintf_l"},
    {"_snwprintf_s", "_vsnwprintf_s"},
    {"_snwprintf_s_l", "_vsnwprintf_s_l"},
    {"_sprintf_l", "_vsprintf_l"},
    {"_sprintf_p_l", "_vsprintf_p_l"},
    {"_sprintf_s_l", "_vsprintf_s_l"},
    {"_swprintf", "_vswprintf"},
    {"_swprintf_c", "_vswprintf_c"},
    {"_swprintf_c_l", "_vswprintf_c_l"},
    {"_swprintf_p_l", "_vswprintf_p_l"},
    {"_swprintf_s_l", "_vswprintf_s_l"},
    {"_wprintf_l", "_vwprintf_l"},
    {"_wprintf_p", "_vwprintf_p"},
    {"_wprintf_p_l", "_vwprintf_p_l"},
    {"_wprintf_s_l", "_vwprintf_s_l"},
    {"fprintf", "vfprintf"},
    {"fprintf_s", "vfprintf_s"},
    {"fwprintf", "vfwprintf"},
    {"fwprintf_s", "vfwprintf_s"},
    {"printf", "vprintf"},
    {"printf_s", "vprintf_s"},
    {"sprintf", "vsprintf"},
    {"sprintf_s", "vsprintf_s"},
    {"swprintf", "vswprintf"},
    {"swprintf_s", "vswprintf_s"},
    {"wprintf", "vwprintf"},
    {"wprintf_s", "vwprintf_s"},
};

// The adapters' variadic slots (5.15.2's fixed-arity list).
static constexpr VarSlot int32Slot[] = {VarSlot::Int32};
static constexpr VarSlot nativeIntSlot[] = {VarSlot::NativeInt};
static constexpr VarSlot semOpenSlots[] = {VarSlot::Int32, VarSlot::Int32};
static constexpr VarSlot mqOpenSlots[] = {VarSlot::Int32, VarSlot::NativeInt};
static constexpr VarSlot prctlSlots[] = {VarSlot::NativeInt, VarSlot::NativeInt,
                                         VarSlot::NativeInt,
                                         VarSlot::NativeInt};
static constexpr VarSlot ptraceSlots[] = {
    VarSlot::NativeInt, VarSlot::NativeInt, VarSlot::NativeInt};
static constexpr VarSlot syscallSlots[] = {
    VarSlot::NativeInt, VarSlot::NativeInt, VarSlot::NativeInt,
    VarSlot::NativeInt, VarSlot::NativeInt, VarSlot::NativeInt};

// glibc's exported variadic functions without a variant (25): open, open64,
// openat and openat64 (adapter Int32); fcntl, fcntl64, ioctl, mremap, semctl
// and ulimit (NativeInt); sem_open (Int32, Int32); mq_open (Int32,
// NativeInt); prctl (4 x NativeInt); ptrace (3 x NativeInt); syscall (6 x
// NativeInt); and without an adapter execl, execle, execlp, error,
// error_at_line, argp_error, argp_failure, strfmon, strfmon_l and
// __asprintf. clone and makecontext, which take callbacks, are on no list.
static constexpr FixedArityFunction glibcFixedArity[] = {
    {"__asprintf", {}},
    {"argp_error", {}},
    {"argp_failure", {}},
    {"error", {}},
    {"error_at_line", {}},
    {"execl", {}},
    {"execle", {}},
    {"execlp", {}},
    {"fcntl", nativeIntSlot},
    {"fcntl64", nativeIntSlot},
    {"ioctl", nativeIntSlot},
    {"mq_open", mqOpenSlots},
    {"mremap", nativeIntSlot},
    {"open", int32Slot},
    {"open64", int32Slot},
    {"openat", int32Slot},
    {"openat64", int32Slot},
    {"prctl", prctlSlots},
    {"ptrace", ptraceSlots},
    {"sem_open", semOpenSlots},
    {"semctl", nativeIntSlot},
    {"strfmon", {}},
    {"strfmon_l", {}},
    {"syscall", syscallSlots},
    {"ulimit", nativeIntSlot},
};

// musl: the names of glibc's list that musl's libc.so exports (unverified,
// musl is no execution triple): not glibc's error, error_at_line,
// argp_error, argp_failure and __asprintf; fcntl64, open64 and openat64 are
// musl's large-file aliases. The backend's list in MSILVariadics.cpp has the
// same names.
static constexpr FixedArityFunction muslFixedArity[] = {
    {"execl", {}},
    {"execle", {}},
    {"execlp", {}},
    {"fcntl", nativeIntSlot},
    {"fcntl64", nativeIntSlot},
    {"ioctl", nativeIntSlot},
    {"mq_open", mqOpenSlots},
    {"mremap", nativeIntSlot},
    {"open", int32Slot},
    {"open64", int32Slot},
    {"openat", int32Slot},
    {"openat64", int32Slot},
    {"prctl", prctlSlots},
    {"ptrace", ptraceSlots},
    {"sem_open", semOpenSlots},
    {"semctl", nativeIntSlot},
    {"strfmon", {}},
    {"strfmon_l", {}},
    {"syscall", syscallSlots},
    {"ulimit", nativeIntSlot},
};

// ucrtbase.dll: its 20 exported variadic functions, none with a variant
// (variadics-files/ucrt-variadics.txt), and the POSIX names of 10 of them,
// which the UCRT headers declare as variadic (corecrt_io.h:
// `int open(char const*, int, ...)`) and oldnames.lib maps to the
// underscore exports (plan section 12, '5.15.2's fixed-arity list amended
// (UCRT)'); _open, _sopen, _wopen and _wsopen, and open and sopen, have an
// adapter (Int32).
static constexpr FixedArityFunction ucrtFixedArity[] = {
    {"_execl", {}},         {"_execle", {}},      {"_execlp", {}},
    {"_execlpe", {}},       {"_open", int32Slot}, {"_sopen", int32Slot},
    {"_spawnl", {}},        {"_spawnle", {}},     {"_spawnlp", {}},
    {"_spawnlpe", {}},      {"_wexecl", {}},      {"_wexecle", {}},
    {"_wexeclp", {}},       {"_wexeclpe", {}},    {"_wopen", int32Slot},
    {"_wsopen", int32Slot}, {"_wspawnl", {}},     {"_wspawnle", {}},
    {"_wspawnlp", {}},      {"_wspawnlpe", {}},   {"execl", {}},
    {"execle", {}},         {"execlp", {}},       {"execlpe", {}},
    {"open", int32Slot},    {"sopen", int32Slot}, {"spawnl", {}},
    {"spawnle", {}},        {"spawnlp", {}},      {"spawnlpe", {}},
};

// msvcrt.dll: its 62 exported variadic functions without a variant, the
// whole scanf family among them (no adapters).
static constexpr FixedArityFunction msvcrtFixedArity[] = {
    {"_CrtDbgReport", {}}, {"_CrtDbgReportW", {}}, {"_cscanf", {}},
    {"_cscanf_l", {}},     {"_cscanf_s", {}},      {"_cscanf_s_l", {}},
    {"_cwscanf", {}},      {"_cwscanf_l", {}},     {"_cwscanf_s", {}},
    {"_cwscanf_s_l", {}},  {"_execl", {}},         {"_execle", {}},
    {"_execlp", {}},       {"_execlpe", {}},       {"_fscanf_l", {}},
    {"_fscanf_s_l", {}},   {"_fwscanf_l", {}},     {"_fwscanf_s_l", {}},
    {"_open", {}},         {"_scanf_l", {}},       {"_scanf_s_l", {}},
    {"_snscanf", {}},      {"_snscanf_l", {}},     {"_snscanf_s", {}},
    {"_snscanf_s_l", {}},  {"_snwscanf", {}},      {"_snwscanf_l", {}},
    {"_snwscanf_s", {}},   {"_snwscanf_s_l", {}},  {"_sopen", {}},
    {"_spawnl", {}},       {"_spawnle", {}},       {"_spawnlp", {}},
    {"_spawnlpe", {}},     {"_sscanf_l", {}},      {"_sscanf_s_l", {}},
    {"_swscanf_l", {}},    {"_swscanf_s_l", {}},   {"_wexecl", {}},
    {"_wexecle", {}},      {"_wexeclp", {}},       {"_wexeclpe", {}},
    {"_wopen", {}},        {"_wscanf_l", {}},      {"_wscanf_s_l", {}},
    {"_wsopen", {}},       {"_wspawnl", {}},       {"_wspawnle", {}},
    {"_wspawnlp", {}},     {"_wspawnlpe", {}},     {"fscanf", {}},
    {"fscanf_s", {}},      {"fwscanf", {}},        {"fwscanf_s", {}},
    {"scanf", {}},         {"scanf_s", {}},        {"sscanf", {}},
    {"sscanf_s", {}},      {"swscanf", {}},        {"swscanf_s", {}},
    {"wscanf", {}},        {"wscanf_s", {}},
};

//===----------------------------------------------------------------------===//
// Lookups and texts
//===----------------------------------------------------------------------===//

// The names of glibc's variant table that musl lacks: the __isoc99_* and
// __isoc23_* scanf entries of glibc's headers and the _FORTIFY_SOURCE _chk
// entries.
static bool isGlibcOnlyName(StringRef name) {
  return name.starts_with("__isoc") || name.ends_with("_chk");
}

static ArrayRef<VariantPair> getVariantTable(CRuntime rt) {
  switch (rt) {
  case CRuntime::Glibc:
  case CRuntime::Musl:
    return glibcVariants;
  case CRuntime::Msvcrt:
    return msvcrtVariants;
  case CRuntime::UCRT:
  case CRuntime::None:
    break;
  }
  return {};
}

static ArrayRef<FixedArityFunction> getFixedArityTable(CRuntime rt) {
  switch (rt) {
  case CRuntime::Glibc:
    return glibcFixedArity;
  case CRuntime::Musl:
    return muslFixedArity;
  case CRuntime::UCRT:
    return ucrtFixedArity;
  case CRuntime::Msvcrt:
    return msvcrtFixedArity;
  case CRuntime::None:
    break;
  }
  return {};
}

static const VariantPair *findVariant(StringRef name, CRuntime rt) {
  if (rt == CRuntime::Musl && isGlibcOnlyName(name))
    return nullptr;
  ArrayRef<VariantPair> table = getVariantTable(rt);
  const VariantPair *it =
      llvm::lower_bound(table, name, [](const VariantPair &p, StringRef n) {
        return StringRef(p.Name) < n;
      });
  if (it == table.end() || StringRef(it->Name) != name)
    return nullptr;
  return it;
}

static const FixedArityFunction *findFixedArityEntry(StringRef name,
                                                     CRuntime rt) {
  ArrayRef<FixedArityFunction> table = getFixedArityTable(rt);
  const FixedArityFunction *it = llvm::lower_bound(
      table, name, [](const FixedArityFunction &f, StringRef n) {
        return StringRef(f.Name) < n;
      });
  if (it == table.end() || StringRef(it->Name) != name)
    return nullptr;
  return it;
}

// Asserts, at the first lookup, that every table is sorted by name in byte
// order and that no name is in both tables of a C runtime (plan 5.15.2, as
// the callback list's is_sorted check).
static void checkTables() {
#ifndef NDEBUG
  static const bool checked = [] {
    for (CRuntime rt :
         {CRuntime::Glibc, CRuntime::Musl, CRuntime::UCRT, CRuntime::Msvcrt}) {
      ArrayRef<VariantPair> variants = getVariantTable(rt);
      ArrayRef<FixedArityFunction> fixed = getFixedArityTable(rt);
      assert(llvm::is_sorted(variants,
                             [](const VariantPair &a, const VariantPair &b) {
                               return StringRef(a.Name) < StringRef(b.Name);
                             }) &&
             "a va_list variant table must be sorted");
      assert(llvm::is_sorted(
                 fixed,
                 [](const FixedArityFunction &a, const FixedArityFunction &b) {
                   return StringRef(a.Name) < StringRef(b.Name);
                 }) &&
             "a fixed-arity list must be sorted");
      assert(llvm::none_of(fixed,
                           [&](const FixedArityFunction &f) {
                             return findVariant(f.Name, rt) != nullptr;
                           }) &&
             "a name is on a fixed-arity list and in the variant table");
      (void)variants;
      (void)fixed;
    }
    return true;
  }();
  (void)checked;
#endif
}

CRuntime getCRuntime(const Triple &tt, StringRef cRuntimeIdentity) {
  // As the backend's fixed-arity list (MSILVariadics.cpp): a Linux triple
  // other than musl and Android is glibc, with or without an environment.
  if (tt.isOSLinux()) {
    if (tt.isMusl())
      return CRuntime::Musl;
    if (tt.isAndroid())
      return CRuntime::None;
    return CRuntime::Glibc;
  }
  if (tt.isWindowsMSVCEnvironment())
    return CRuntime::UCRT;
  if (tt.isWindowsGNUEnvironment())
    return cRuntimeIdentity == "ucrt" || cRuntimeIdentity == "ucrtbase"
               ? CRuntime::UCRT
               : CRuntime::Msvcrt;
  return CRuntime::None;
}

std::optional<StringRef> getVaListVariant(StringRef name, CRuntime rt) {
  checkTables();
  if (const VariantPair *p = findVariant(name, rt))
    return StringRef(p->Variant);
  return std::nullopt;
}

std::optional<ArrayRef<VarSlot>> getFixedArityAdapter(StringRef name,
                                                      CRuntime rt) {
  checkTables();
  const FixedArityFunction *f = findFixedArityEntry(name, rt);
  if (!f || f->Slots.empty())
    return std::nullopt;
  return f->Slots;
}

bool isFixedArityFunction(StringRef name, CRuntime rt) {
  checkTables();
  return findFixedArityEntry(name, rt) != nullptr;
}

std::string getNoVaListVariantText(StringRef name, StringRef module) {
  return ("native function '" + name + "' (" + module +
          ") is variadic and has neither a va_list variant nor an entry in "
          "the fixed-arity list")
      .str();
}

std::string getUndefinedVaListVariantText(StringRef name, StringRef module,
                                          StringRef variant) {
  return ("native function '" + name + "' (" + module +
          ") is variadic, and its va_list variant '" + variant +
          "' is not defined by the native libraries")
      .str();
}

std::string getVariadicAddressText(StringRef name, StringRef module) {
  return ("the address of native variadic function '" + name + "' (" + module +
          ") is not supported for MSIL yet")
      .str();
}

std::string getFixedArityToVariadicText(StringRef name, StringRef file,
                                        StringRef defFile) {
  return ("fixed-arity call of '" + name + "' in " + file +
          " binds to the variadic definition in " + defFile +
          ", which MSIL cannot call with fixed arity")
      .str();
}

VaListVariantBinding bindVaListVariant(StringRef name, const NativeExport &e,
                                       const Ctx &ctx) {
  VaListVariantBinding result;
  CRuntime rt = getCRuntime(ctx.arg.target, getCRuntimeIdentity());
  // The native function that the reference binds to (an alias's target,
  // _snprintf for oldnames' snprintf), else the symbol's own name.
  std::optional<StringRef> variant = getVaListVariant(e.Entry, rt);
  if (!variant)
    variant = getVaListVariant(name, rt);
  if (!variant) {
    result.Error = getNoVaListVariantText(name, e.Module);
    return result;
  }
  SmallVector<const NativeExport *, 2> rejected;
  const NativeExport *v =
      mayBindNatively(*variant, ctx)
          ? ctx.nativeIndex.lookup(*variant, rejected,
                                   /*isFunctionReference=*/true)
          : nullptr;
  if (!v || (v->Kind != NativeKind::Function && v->Kind != NativeKind::NoType))
    result.Error = getUndefinedVaListVariantText(name, e.Module, *variant);
  else
    result.Variant = v;
  return result;
}
} // namespace lld::msil
