//===- Signatures.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Call-site signature checks, adapters, trap stubs and the native-callee
// checks of msil-ld (see Signatures.h).
//
//===----------------------------------------------------------------------===//

#include "Signatures.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "NativeLibraries.h"
#include "Relocations.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Synthetic.h"
#include "TokenMap.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/CallAdapter.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::cli;
using namespace lld;
using namespace lld::msil;

namespace MSILLink = llvm::MSILLink;

// The SIGNATURES descriptor of a call side, as a key.
static std::string getDescriptorKey(const MSILLink::SignatureInfo *info) {
  std::string key;
  if (!info)
    return key;
  raw_string_ostream os(key);
  os << info->Flags << ',' << info->FixedParams << ',' << info->SretSize << ','
     << unsigned(info->SretAlignLog2);
  for (const MSILLink::ByvalParam &b : info->Byval)
    os << ';' << b.Param << ',' << b.Size << ',' << unsigned(b.AlignLog2);
  return key;
}

static std::string printShape(const CallShape &shape) {
  std::string str;
  raw_string_ostream os(str);
  printCallShape(os, shape);
  return str;
}

// True if a P/Invoke cannot pass what the call side \p info describes: an
// aggregate result through a hidden pointer, an aggregate by value, or a
// type that is no native scalar (5.10.13, 5.10.14).
static bool isNotPassable(const MSILLink::SignatureInfo *info) {
  return info && ((info->Flags & MSILLink::Sret) || !info->Byval.empty() ||
                  (info->Flags & MSILLink::NativeIncompatible));
}

void CallBinder::checkNativeReferences() {
  // The references of each natively bound function, in first-use order. A
  // data reference of a native function is SymbolBinder's type mismatch.
  SmallVector<Symbol *, 0> order;
  DenseMap<const Symbol *, SmallVector<const SymbolReference *, 2>> refsOf;
  for (const SymbolReference &r : symbols.getReferences()) {
    if (!isa_and_nonnull<NativeFunction>(r.sym) ||
        r.kind == SymbolReference::Data)
      continue;
    SmallVector<const SymbolReference *, 2> &refs = refsOf[r.sym];
    if (refs.empty())
      order.push_back(r.sym);
    refs.push_back(&r);
  }

  bool wideLongDouble = hasWideNativeLongDouble(ctx.arg.target);
  for (Symbol *sym : order) {
    const NativeExport &e = *cast<NativeFunction>(sym)->nativeExport;
    StringRef name = sym->getName();
    StringRef lookupName = symtab->getImportName(sym);
    if (lookupName.empty())
      lookupName = name;
    // An explicit import module is used as it is, unchecked (plan 5.10.7).
    bool isExplicit = symtab->isExplicitImport(sym);
    // Whether the symbol, lookup or entry name is in a list.
    auto isListed = [&](function_ref<bool(StringRef)> isInList) {
      return isInList(name) || isInList(lookupName) || isInList(e.Entry);
    };

    bool hasCall = false, variadic = false, notPassable = false;
    for (const SymbolReference *r : refsOf[sym]) {
      if (r->kind != SymbolReference::Call || !r->signature)
        continue;
      hasCall = true;
      variadic |= CallShape::get(*r->signature, r->signatureInfo).isVarArg();
      notPassable |= isNotPassable(r->signatureInfo);
    }
    std::optional<NativeSignature> declared = symbols.getNativeSignature(sym);
    if (declared) {
      variadic |= CallShape::get(*declared->signature, declared->signatureInfo)
                      .isVarArg();
      notPassable |= isNotPassable(declared->signatureInfo);
    }
    // The IR signature of a native `long double` function says double
    // (decision D3), but the native function takes or returns an x87 or
    // binary128 value (plan 5.10.10).
    if (!isExplicit && wideLongDouble && isListed(isNativeLongDoubleFunction))
      notPassable = true;

    std::string prefix =
        ("native function '" + name + "' (" + e.Module + ") ").str();
    std::string msg;
    // Native code cannot call a managed function before phase 3's thunks
    // (plan 5.10.10): the functions of the callback list are errors, whether
    // or not the call passes a function.
    if (!isExplicit && isListed(isNativeCallbackFunction))
      msg = prefix + "takes a C function pointer; passing functions to "
                     "native code needs phase 3 thunks";
    else if (variadic)
      msg = prefix + "is variadic; calls of native variadic functions are not "
                     "supported for MSIL yet (phase 3)";
    else if (notPassable)
      msg = prefix + "takes or returns a value that a P/Invoke cannot pass "
                     "natively yet (phase 3)";
    else if (!hasCall && !declared)
      // Phase 1's text, which M19.b (2a) moved to msil-ld: no object gives
      // the signature of its P/Invoke stub.
      msg = ("the signature of prototype-less function '" + name +
             "' cannot be derived: its address is used, but it is never "
             "called directly; declare it with a prototype")
                .str();
    if (!msg.empty())
      symtab->reportUnresolved(std::move(msg), sym, /*liveOnly=*/true);
  }
}

std::optional<uint32_t> CallBinder::bindCall(ObjFile &f, const TokenTarget &t) {
  Symbol *sym = t.sym;
  if (!sym)
    return std::nullopt;

  // A definition of an object or an LTO object.
  if (auto *d = dyn_cast<DefinedFunction>(sym)) {
    if (!d->method)
      return std::nullopt;
    std::optional<uint32_t> def = out.getMethodOf(d->method);
    if (!def)
      return std::nullopt;
    // A MethodDef that stays this object's own definition is never checked.
    if ((t.form == TokenTarget::Definition && !t.isRedirected()) ||
        !t.signature)
      return def;

    MethodSig callSig = out.mapSig(f, *t.signature);
    CallShape call = CallShape::get(callSig, t.signatureInfo);
    CallShape defShape =
        CallShape::get(out.methods[*def].sig, d->method->signatureInfo);
    CallMatch match = classifyCall(call, defShape);
    if (match.Kind == CallMatchKind::Direct)
      return def;

    // One adapter per (definition, call signature, descriptor); one
    // diagnostic per adapter and calling object.
    std::string key = std::to_string(*def);
    key.push_back('\0');
    key += OutputModule::getSignatureKey(callSig);
    key.push_back('\0');
    key += getDescriptorKey(t.signatureInfo);
    bool isAdapter = match.Kind == CallMatchKind::Adapter;
    std::string diagKey = key + '\0' + toString(&f);
    if (reported.insert(diagKey).second) {
      std::string where = "\n>>> defined as " + printShape(defShape) + " in " +
                          toString(d->method->file) + "\n>>> called as " +
                          printShape(call) + " in " + toString(&f);
      // A variadic side is 5.9.14's Phase 3 text (P3-B), the others
      // wasm-ld's reportFunctionSignatureMismatch shape.
      std::string msg =
          call.isVarArg() || defShape.isVarArg()
              ? "variadic functions are not supported yet: " + toString(*sym) +
                    where
              : "function signature mismatch: " + toString(*sym) + where +
                    "\n>>> " + (isAdapter ? "adapter: " : "") + match.Detail;
      if (isAdapter)
        warn(msg);
      else
        error(msg);
    }
    if (!isAdapter)
      return std::nullopt;

    auto [it, inserted] = adapters.try_emplace(key, 0);
    if (inserted) {
      // 'assembly static '<msil>adapt_<name>_<n>'' with the call's
      // signature (plan 5.9.8); Writer.cpp names it once the definition's
      // name is final.
      OutputMethod a(OutputMethod::Adapter);
      a.flags = MethodAttributes::Assembly | MethodAttributes::Static;
      a.implFlags = MethodImplAttributes::IL | MethodImplAttributes::Managed;
      a.sig = callSig;
      a.target = *def;
      a.adapterNumber = adapterCounts[*def]++;
      a.body = std::make_unique<ILBuilder>(callSig.Return.getElementType() !=
                                           ELEMENT_TYPE_VOID);
      buildAdapter(match, callSig,
                   TokenRef::getExternal(out.getMethodEntity(*def)), *a.body);
      if (Error e = a.body->layout())
        reportFatalInternalError(Twine("msil-ld: adapter of '") +
                                 toString(*sym) +
                                 "': " + toString(std::move(e)));
      it->second = out.addMethod(std::move(a));
    }
    return it->second;
  }

  // A natively bound function: its P/Invoke stub for the call's signature.
  if (auto *n = dyn_cast<NativeFunction>(sym)) {
    if (!t.signature)
      return std::nullopt;
    const NativeExport &e = *n->nativeExport;
    return symbols.getPInvokeStub(sym->getName(), e.Module, e.Entry,
                                  out.mapSig(f, *t.signature));
  }

  // An undefined weak function: a trap stub per call signature (plan 5.9.6
  // step 3, wasm-ld's createUndefinedStub).
  auto *u = dyn_cast<Undefined>(sym);
  if (!(u && u->discarded) && sym->isUndefWeak() && t.signature) {
    MethodSig callSig = out.mapSig(f, *t.signature);
    std::string key = sym->getName().str();
    key.push_back('\0');
    key += OutputModule::getSignatureKey(callSig);
    auto [it, inserted] = trapStubs.try_emplace(key, 0);
    if (inserted) {
      OutputMethod s(OutputMethod::TrapStub);
      s.name = ("<msil>undef_" + sym->getName()).str();
      s.flags = MethodAttributes::Assembly | MethodAttributes::Static;
      s.implFlags = MethodImplAttributes::IL | MethodImplAttributes::Managed;
      s.sig = callSig;
      s.body = std::make_unique<ILBuilder>(callSig.Return.getElementType() !=
                                           ELEMENT_TYPE_VOID);
      s.body->emit(Opcode::Ldnull);
      s.body->emit(Opcode::Throw);
      if (Error e = s.body->layout())
        reportFatalInternalError(Twine("msil-ld: trap stub of '") +
                                 toString(*sym) +
                                 "': " + toString(std::move(e)));
      it->second = out.addMethod(std::move(s));
    }
    return it->second;
  }

  // Undefined, discarded, data called as a function and kind mismatches are
  // reported elsewhere.
  return std::nullopt;
}
