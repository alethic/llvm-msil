//===- Writer.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The writer of msil-ld (llvm/docs/MSIL/ImplementationPlan.md 5.9.8-5.9.12;
// the role of lld/wasm/Writer.cpp): it links the live methods and segments of
// the CIL objects and LTO objects into one IL-only PE, an EXE whose entry
// point is '<msil>Main' (decision D27). In order:
//
// 1. the kept methods and the objects' address fields (5.9.12's row order);
// 2. the data image without address slots, and __start_X/__stop_X (5.9.9);
// 3. the references of the live units: the unresolved ones are reported
//    (5.9.6 steps 3-4), and so are the native callees that a P/Invoke cannot
//    call (5.10.14);
// 4. every token of every kept body is bound (5.9.7): a definition, an
//    adapter or a trap stub (5.9.8), a P/Invoke stub (5.10.11), an address
//    or '<msil>fn_' field, or the BCL stand-in's [mscorlib] row;
// 5. the data relocations, the constructor list and the entry point, then
//    the trailer (cli::buildTrailer, shared with -S, 5.9.10);
// 6. names (colliding LOCAL members become '<name><msil><n>'), access, the
//    '<msil>Spill<N>' classes past the per-type limits, the rows, the bodies
//    (cli::rewriteILTokens for copied ones), and cli::writePEImage.
//
// Only input order decides anything: no hash or pointer order reaches the
// output.
//
//===----------------------------------------------------------------------===//

#include "Writer.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "MapFile.h"
#include "OutputSegment.h"
#include "Relocations.h"
#include "Signatures.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Synthetic.h"
#include "TokenMap.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/ILDecoder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::cli;
using namespace lld;
using namespace lld::msil;

namespace MSILLink = llvm::MSILLink;

namespace {
class Writer {
public:
  Writer()
      : out(ctx.arg.target.isOSWindows() ? SectionRules::COFF
                                         : SectionRules::ELF),
        image(out), symbols(out, image), calls(out, symbols) {}

  void run();

private:
  void addObjectMethods();
  void bindTokens();
  OutRef bindToken(ObjFile &f, uint32_t token, const TokenTarget &t);
  std::optional<EntryPoint> getEntryPoint();
  void addAddressFields(TrailerInput &in);
  void assignNames();
  void assignOwners();
  void addRows();
  bool encodeBodies(MethodBodyStream &bodies);
  void writeImage(const MethodBodyStream &bodies);
  void fillMap();

  OutputModule out;
  OutputImage image;
  SymbolBinder symbols;
  CallBinder calls;
  // '<Module>', then the spill classes.
  SmallVector<EntityHandle, 1> owners;
  // The FieldRVA data, at the offsets given to addFieldRVA.
  std::vector<uint8_t> mappedData;
};
} // namespace

// The name of the output assembly: --assembly-name or -soname, else the stem
// of -o (only the last extension is dropped, so -o My.App.exe names the
// assembly My.App; plan 5.9.12).
static std::string getAssemblyName() {
  if (!ctx.arg.assemblyName.empty())
    return ctx.arg.assemblyName.str();
  StringRef stem = sys::path::stem(ctx.arg.outputFile);
  if (stem.empty())
    stem = sys::path::filename(ctx.arg.outputFile);
  return stem.str();
}

static std::string getOwnerName(uint32_t owner) {
  return owner == 0 ? "<Module>" : "<msil>Spill" + std::to_string(owner);
}

// The kept methods, by object in link order and row order (plan 5.9.12).
// GLOBAL and WEAK definitions are public, LOCAL ones assembly; the method of
// a definition that lost resolution but stays because a winning alias lies
// in it is named apart from the winner's, as a LOCAL one.
void Writer::addObjectMethods() {
  for (ObjFile *f : ctx.objectFiles) {
    const MSILLink::Record &record = f->getRecord();
    for (InputMethod *m : f->methods()) {
      if (!m->live || m->discarded)
        continue;
      bool isLocal = m->symbolIndex < record.Symbols.size() &&
                     (record.Symbols[m->symbolIndex].Flags &
                      MSILLink::BindingMask) == MSILLink::BindingLocal;
      auto *d = dyn_cast_or_null<DefinedFunction>(f->getSymbol(m->symbolIndex));
      OutputMethod om(OutputMethod::Object);
      om.name = m->name.str();
      om.renamable = isLocal || !d || d->method != m;
      om.flags = (m->flags & ~MethodAttributes::MemberAccessMask) |
                 (om.renamable ? MethodAttributes::Assembly
                               : MethodAttributes::Public);
      om.implFlags = m->implFlags;
      if (m->signature)
        om.sig = out.mapSig(*f, *m->signature);
      om.input = m;
      out.setMethodOf(m, out.addMethod(std::move(om)));
    }
  }
}

OutRef Writer::bindToken(ObjFile &f, uint32_t token, const TokenTarget &t) {
  switch (t.kind) {
  case TokenTarget::Method:
    if (std::optional<uint32_t> m = calls.bindCall(f, t))
      return OutRef::method(*m);
    break;
  case TokenTarget::Field:
    if (std::optional<uint32_t> field = symbols.getDataField(t.sym))
      return OutRef::field(*field);
    break;
  case TokenTarget::FnAddr:
    if (std::optional<uint32_t> field = symbols.getFnAddrField(t.sym))
      return OutRef::field(*field);
    break;
  case TokenTarget::BCL:
    if (uint32_t r = out.mapBCLToken(f, token))
      return OutRef::token(r);
    break;
  case TokenTarget::TypeDef:
    if (uint32_t r = out.mapTypeToken(f, token))
      return OutRef::token(r);
    break;
  case TokenTarget::StandAloneSig:
    if (uint32_t r = out.mapStandAloneSig(f, t.blob))
      return OutRef::token(r);
    break;
  case TokenTarget::UserString:
    if (uint32_t r = out.mapUserString(f, token))
      return OutRef::token(r);
    break;
  case TokenTarget::None:
    error(toString(&f) + ": token 0x" + utohexstr(token, false, 8) +
          " names no row that msil-ld links");
    break;
  }
  return OutRef();
}

// Plan 5.9.7: what every token of every kept body becomes (the token
// operands, the local signature and the EH clauses' class tokens), in
// first-use order, which is also the order in which the linker-made members
// are made.
void Writer::bindTokens() {
  size_t numObjectMethods = out.methods.size();
  for (size_t i = 0; i != numObjectMethods; ++i) {
    if (out.methods[i].kind != OutputMethod::Object)
      continue;
    InputMethod *m = out.methods[i].input;
    ObjFile &f = *m->file;
    const TokenMap &tokens = f.getTokenMap();
    auto bind = [&](uint32_t token) {
      auto key = std::make_pair(static_cast<const ObjFile *>(&f), token);
      if (out.tokenRefs.count(key))
        return;
      OutRef ref = bindToken(f, token, tokens.lookup(token));
      out.tokenRefs[key] = ref;
    };
    for (const TokenSite &site : m->sites)
      bind(site.token);
    if (m->body.LocalVarSigToken)
      bind(m->body.LocalVarSigToken);
    for (const ExceptionClause &c : m->body.Clauses)
      if (c.Kind == ExceptionClauseKind::Catch)
        bind(c.ClassTokenOrFilterOffset);
  }
}

// The entry point (decision D27): '<msil>Main' calls 'int32 <entry>()', or
// builds argv for 'int32 <entry>(int32, native int)'.
std::optional<EntryPoint> Writer::getEntryPoint() {
  if (ctx.arg.shared || ctx.arg.entry.empty())
    return std::nullopt;
  Symbol *sym = symtab->find(ctx.arg.entry);
  auto *d = dyn_cast_or_null<DefinedFunction>(sym);
  std::optional<uint32_t> index =
      d && d->method ? out.getMethodOf(d->method) : std::nullopt;
  if (!index) {
    // SymbolTable::bindLateSymbols reported an undefined entry point.
    if (sym && (sym->isDefined() || sym->isCommon()))
      error("unsupported signature for " + ctx.arg.entry);
    return std::nullopt;
  }
  unsigned pointerSize = d->method->file->getRecord().Hdr.PointerSize;
  TypeSig pointer =
      TypeSig::get(pointerSize == 8 ? ELEMENT_TYPE_I : ELEMENT_TYPE_I4);
  TypeSig int32 = TypeSig::get(ELEMENT_TYPE_I4);
  const MethodSig &sig = out.methods[*index].sig;
  bool hasArgs = sig.Params.size() == 2 && sig.Params[0] == int32 &&
                 sig.Params[1] == pointer;
  const MSILLink::SignatureInfo *info = d->method->signatureInfo;
  bool isVarArg = (info && (info->Flags & MSILLink::VarArg)) ||
                  sig.RequiredParamCount < sig.Params.size();
  if (sig.Header != SIG_DEFAULT || !(sig.Return == int32) || isVarArg ||
      (!sig.Params.empty() && !hasArgs)) {
    error("unsupported signature for " + ctx.arg.entry);
    return std::nullopt;
  }
  EntryPoint e;
  e.Main = out.getMethodEntity(*index);
  e.HasArgs = hasArgs;
  e.PointerSize = pointerSize;
  return e;
}

// The address fields of the data in the image, in image-offset order (plan
// 5.9.10); fields of one offset stay in row order.
void Writer::addAddressFields(TrailerInput &in) {
  for (auto [index, f] : llvm::enumerate(out.fields))
    if (f.kind == OutputField::Address && f.imageOffset)
      in.AddressFields.push_back({out.getFieldEntity(index), *f.imageOffset});
  llvm::stable_sort(in.AddressFields, [](const auto &a, const auto &b) {
    return a.second < b.second;
  });
}

// Plan 5.9.12: functions and address fields keep their symbol names; a LOCAL
// member (or the method of a lost definition) whose name equals a non-LOCAL
// member's name, or an earlier LOCAL one's in input order, is renamed
// '<name><msil><n>' (n = 1, 2, ... per name), never privatescope.
void Writer::assignNames() {
  auto renameAll = [](auto &members, auto isFixed, auto isRenamable) {
    StringSet<> taken;
    StringMap<unsigned> counters;
    for (auto &m : members)
      if (isFixed(m))
        taken.insert(m.name);
    for (auto &m : members) {
      if (!isRenamable(m))
        continue;
      if (taken.insert(m.name).second)
        continue;
      unsigned &n = counters[m.name];
      std::string name;
      do
        name = m.name + "<msil>" + std::to_string(++n);
      while (taken.contains(name));
      m.name = name;
      taken.insert(m.name);
    }
  };

  renameAll(
      out.methods,
      [](const OutputMethod &m) {
        return !m.renamable && m.kind != OutputMethod::Adapter;
      },
      [](const OutputMethod &m) { return m.renamable; });
  // '<msil>adapt_<name>_<n>', after the definition's name is final.
  for (OutputMethod &m : out.methods)
    if (m.kind == OutputMethod::Adapter)
      m.name = "<msil>adapt_" + out.methods[m.target].name + "_" +
               std::to_string(m.adapterNumber);

  // '<msil>fn_<name>' after the name of the method it holds.
  for (OutputField &f : out.fields)
    if (f.kind == OutputField::FnAddr)
      f.name = "<msil>fn_" + (f.fnMethod != OutputField::NoMethod
                                  ? out.methods[f.fnMethod].name
                                  : f.sym->getName().str());
  renameAll(
      out.fields, [](const OutputField &f) { return !f.renamable; },
      [](const OutputField &f) { return f.renamable; });
}

// Plan 5.9.12's per-type limits: '<Module>' holds at most 65,524 methods and
// 65,534 fields (--msil-type-member-limit lowers both), counting the members
// that it keeps whatever the count (the trailer's, all private). Past that,
// members move to '<msil>Spill<N>' classes in row order.
void Writer::assignOwners() {
  uint32_t limit = std::max<uint32_t>(ctx.arg.msilTypeMemberLimit, 1);
  auto distribute = [](auto &members, auto isFixed, uint32_t limit) {
    uint32_t fixed = llvm::count_if(members, isFixed);
    uint32_t capacity = limit > fixed ? limit - fixed : 0;
    uint32_t n = 0, classes = 0;
    for (auto &m : members) {
      m.owner = 0;
      if (isFixed(m))
        continue;
      if (n >= capacity) {
        m.owner = 1 + (n - capacity) / limit;
        classes = std::max(classes, m.owner);
      }
      ++n;
    }
    return classes;
  };
  uint32_t methodClasses = distribute(
      out.methods,
      [](const OutputMethod &m) { return m.kind == OutputMethod::Trailer; },
      std::min(MaxMethodsPerType, limit));
  uint32_t fieldClasses = distribute(
      out.fields,
      [](const OutputField &f) { return f.kind == OutputField::Trailer; },
      std::min(MaxFieldsPerType, limit));
  uint32_t numClasses = std::max(methodClasses, fieldClasses);

  owners.push_back(out.mb.getModuleType());
  // 'private abstract sealed auto ansi', extending the core library's
  // System.Object (BCLMembers.def's Object, bound by the stand-in).
  EntityHandle object{out.getBCLType(BCLMemberId::Object)};
  for (uint32_t n = 1; n <= numClasses; ++n)
    owners.push_back(out.mb.addTypeDef(
        TypeAttributes::NotPublic | TypeAttributes::AutoLayout |
            TypeAttributes::AnsiClass | TypeAttributes::Abstract |
            TypeAttributes::Sealed,
        "", getOwnerName(n), object));
}

void Writer::addRows() {
  MetadataBuilder &mb = out.mb;

  // The RVA data's value types after the spill classes (plan 5.9.12): Phase
  // 1's '.class private explicit ansi sealed ... extends
  // [mscorlib]System.ValueType { .pack 1 .size <n> }'.
  EntityHandle valueType{out.getBCLType(BCLMemberId::ValueType)};
  for (OutputModule::RVAData &d : out.rvaData) {
    d.type = mb.addTypeDef(
        TypeAttributes::NotPublic | TypeAttributes::ExplicitLayout |
            TypeAttributes::AnsiClass | TypeAttributes::Sealed,
        "", d.typeName, valueType);
    mb.addClassLayout(d.type, 1, d.bytes.size());
  }

  // The methods, and the ImplMap rows of the P/Invoke stubs: NoMangle and
  // cdecl (0x0201), the module verbatim (plan 5.10.11).
  for (OutputMethod &m : out.methods) {
    SmallVector<uint8_t, 16> sig;
    encodeMethodSig(m.sig, sig);
    m.handle = mb.addMethod(owners[m.owner], m.flags, m.implFlags, m.name,
                            mb.addBlob(sig));
    if (m.kind == OutputMethod::PInvoke)
      mb.addImplMap(m.handle,
                    PInvokeAttributes::NoMangle |
                        PInvokeAttributes::CallConvCdecl,
                    m.entry, mb.addModuleRef(m.module));
  }

  // The fields. Address fields are 'initonly' in '<Module>', whose '.cctor'
  // stores them, and not in a spill class (ECMA's initonly rule); the
  // '<msil>fn_' fields are private without spill classes, as -S writes them
  // (O3), and assembly with them.
  bool hasSpill = owners.size() > 1;
  static constexpr uint8_t nativeIntSig[] = {SIG_FIELD, ELEMENT_TYPE_I};
  uint32_t nativeIntBlob = mb.addBlob(nativeIntSig);
  for (OutputField &f : out.fields) {
    FieldAttributes flags = FieldAttributes::Static;
    uint32_t blob = nativeIntBlob;
    switch (f.kind) {
    case OutputField::Address:
      flags |= f.isPublic ? FieldAttributes::Public : FieldAttributes::Assembly;
      if (f.owner == 0)
        flags |= FieldAttributes::InitOnly;
      break;
    case OutputField::FnAddr:
      flags |= hasSpill ? FieldAttributes::Assembly : FieldAttributes::Private;
      if (f.owner == 0)
        flags |= FieldAttributes::InitOnly;
      break;
    case OutputField::DataImport:
      // Stored by '<msil>init_dataimports', so never initonly.
      flags |= FieldAttributes::Public;
      break;
    case OutputField::Trailer:
      flags = f.flags;
      if (f.rva >= 0) {
        SmallVector<uint8_t, 8> sig;
        encodeFieldSig(TypeSig::getValueType(out.rvaData[f.rva].type.Value),
                       sig);
        blob = mb.addBlob(sig);
      }
      break;
    }
    f.handle = mb.addField(owners[f.owner], flags, f.name, blob);
    if (f.rva >= 0) {
      // Each RVA blob 8-aligned in the mapped field data (plan 5.7.7).
      mappedData.resize(alignTo(mappedData.size(), 8));
      mb.addFieldRVA(f.handle, mappedData.size());
      const std::vector<uint8_t> &bytes = out.rvaData[f.rva].bytes;
      mappedData.insert(mappedData.end(), bytes.begin(), bytes.end());
    }
  }

  // The Assembly row: the assembly name, version 0.0.0.0, HashAlgId SHA1,
  // no public key; the Module row names the output file (plan 5.9.12).
  AssemblyIdentity id;
  id.Name = getAssemblyName();
  mb.setAssembly(id);
  mb.setModuleName(sys::path::filename(ctx.arg.outputFile));
}

bool Writer::encodeBodies(MethodBodyStream &bodies) {
  for (OutputMethod &m : out.methods) {
    if (m.kind == OutputMethod::PInvoke)
      continue;
    SmallVector<uint8_t, 0> code;
    SmallVector<ExceptionClause, 0> clauses;
    MethodBodyInfo info;
    if (m.kind == OutputMethod::Object) {
      // Copied byte for byte; only token operands change, so code sizes,
      // branch offsets, .maxstack and EH offsets never change (plan 5.9.7).
      InputMethod *im = m.input;
      ObjFile *f = im->file;
      auto lookup = [&](uint32_t token) -> Expected<uint32_t> {
        auto it = out.tokenRefs.find({f, token});
        if (it == out.tokenRefs.end() || it->second.kind == OutRef::None)
          return createStringError(inconvertibleErrorCode(),
                                   "token 0x" + utohexstr(token, false, 8) +
                                       " is not linked");
        return out.getToken(it->second);
      };
      auto fail = [&](Error e) {
        error(toString(f) + ": '" + im->name + "': " + toString(std::move(e)));
      };
      code.assign(im->body.Code.begin(), im->body.Code.end());
      if (Error e = rewriteILTokens(code, [&](const ILInstruction &inst) {
            return lookup(inst.Token);
          })) {
        fail(std::move(e));
        continue;
      }
      if (uint32_t sig = im->body.LocalVarSigToken) {
        Expected<uint32_t> token = lookup(sig);
        if (!token) {
          fail(token.takeError());
          continue;
        }
        info.LocalVarSigToken = *token;
      }
      bool failed = false;
      for (ExceptionClause c : im->body.Clauses) {
        if (c.Kind == ExceptionClauseKind::Catch) {
          Expected<uint32_t> token = lookup(c.ClassTokenOrFilterOffset);
          if (!token) {
            fail(token.takeError());
            failed = true;
            break;
          }
          c.ClassTokenOrFilterOffset = *token;
        }
        clauses.push_back(c);
      }
      if (failed)
        continue;
      info.MaxStack = im->body.MaxStack;
      info.InitLocals = im->body.InitLocals;
    } else {
      // Adapters, trap stubs and the trailer's methods: ILBuilder bodies.
      if (Error e = m.body->encode(
              code, clauses, [&](TokenRef t) { return out.resolveToken(t); })) {
        error("cannot encode method '" + m.name +
              "': " + toString(std::move(e)));
        continue;
      }
      info.MaxStack = m.body->getMaxStack();
      info.LocalVarSigToken = m.localSigToken;
    }
    info.Code = code;
    info.Clauses = clauses;
    out.mb.setMethodBodyOffset(m.handle, bodies.addBody(info));
    if (m.kind == OutputMethod::Object)
      m.input->outputToken = out.mb.getToken(m.handle);
  }
  return errorCount() == 0;
}

void Writer::writeImage(const MethodBodyStream &bodies) {
  // An EXE image for the triple's machine whatever the name; a name that
  // ends in neither .exe nor .dll gets a warning (plan 5.10.5).
  StringRef ext = sys::path::extension(ctx.arg.outputFile);
  if (!ext.equals_insensitive(".exe") && !ext.equals_insensitive(".dll"))
    warn(ctx.arg.outputFile + ": the output name ends in neither .exe nor "
                              ".dll; msil-ld writes an EXE image");

  Expected<PEOptions> opts =
      PEOptions::get(ctx.arg.target, PEImageKind::Executable);
  if (!opts) {
    error(toString(opts.takeError()));
    return;
  }
  uint32_t entryToken = 0;
  for (const OutputMethod &m : out.methods)
    if (m.kind == OutputMethod::Trailer && m.name == EntryPointName)
      entryToken = out.mb.getToken(m.handle);

  PEContent content;
  content.Metadata = &out.mb;
  content.ILStream = bodies.getBytes();
  content.MappedFieldData = mappedData;
  content.EntryPointToken = entryToken;
  SmallVector<uint8_t, 0> bytes;
  if (Error e = writePEImage(*opts, content, bytes)) {
    error(toString(std::move(e)));
    return;
  }

  Expected<std::unique_ptr<FileOutputBuffer>> bufferOrErr =
      FileOutputBuffer::create(ctx.arg.outputFile, bytes.size(),
                               FileOutputBuffer::F_executable);
  if (!bufferOrErr) {
    error("failed to open " + ctx.arg.outputFile + ": " +
          toString(bufferOrErr.takeError()));
    return;
  }
  std::unique_ptr<FileOutputBuffer> &buffer = *bufferOrErr;
  std::copy(bytes.begin(), bytes.end(), buffer->getBufferStart());
  if (Error e = buffer->commit())
    error("failed to write output '" + buffer->getPath() +
          "': " + toString(std::move(e)));
}

// The PE map's sections (plan 5.9.14), in token order.
void Writer::fillMap() {
  OutputMap &map = outputMap;
  image.fillMap(map);
  for (const OutputMethod &m : out.methods) {
    uint32_t token = out.mb.getToken(m.handle);
    if (m.kind == OutputMethod::PInvoke) {
      map.imports.push_back({token, m.module + "!" + m.entry, m.name});
      continue;
    }
    std::string input = m.kind == OutputMethod::Object
                            ? toString(m.input->file)
                            : std::string("<internal>");
    map.methods.push_back({token, getOwnerName(m.owner), input, m.name});
  }
  for (const OutputField &f : out.fields) {
    uint32_t token = out.mb.getToken(f.handle);
    map.fields.push_back({token, getOwnerName(f.owner), f.imageOffset, f.name});
    if (f.kind == OutputField::DataImport)
      if (auto *n = dyn_cast_or_null<NativeData>(f.sym))
        map.imports.push_back(
            {token,
             (n->nativeExport->Module + "!" + n->nativeExport->Entry).str(),
             f.name});
  }
  auto byToken = [](const auto &a, const auto &b) { return a.token < b.token; };
  llvm::stable_sort(map.methods, byToken);
  llvm::stable_sort(map.fields, byToken);
  llvm::stable_sort(map.imports, byToken);
  map.valid = true;
}

void Writer::run() {
  // 1-3: the kept methods and fields, the image, the references.
  addObjectMethods();
  symbols.addObjectFields();
  image.layout();
  symbols.collectReferences();
  symbols.reportUnresolved();
  calls.checkNativeReferences();

  // 4-5: the tokens, the relocations, the constructors and the entry point.
  bindTokens();
  std::vector<RuntimeReloc> relocs;
  image.addRelocations(symbols, ctx.arg.msilRelocLoopThreshold, relocs);
  std::vector<std::pair<uint32_t, bool>> ctors = image.getConstructors(symbols);
  std::optional<EntryPoint> entry = getEntryPoint();
  if (errorCount())
    return;

  TrailerInput in;
  in.RelocLoopThreshold = ctx.arg.msilRelocLoopThreshold;
  if (!symbols.addDataImports(in))
    return;
  symbols.setImageOffsets();
  if (!image.empty())
    in.Image = &image.getLayout();
  addAddressFields(in);
  symbols.addFnAddrFields(in);
  in.Relocs = std::move(relocs);
  for (const auto &[method, returnsValue] : ctors)
    in.Ctors.push_back({out.getMethodEntity(method), returnsValue});
  in.Entry = entry;
  if (ctx.arg.printImports)
    symbols.printImports();
  RowTrailerSink sink(out);
  buildTrailer(in, sink);

  // 6: names, owners, rows, bodies and the PE.
  assignNames();
  assignOwners();
  addRows();
  MethodBodyStream bodies;
  if (Error e = out.mb.finalize(bodies)) {
    error(toString(std::move(e)));
    return;
  }
  if (!encodeBodies(bodies))
    return;
  writeImage(bodies);
  if (errorCount() == 0 && !ctx.arg.mapFile.empty())
    fillMap();
}

void lld::msil::writeResult() {
  outputMap = OutputMap();
  Writer().run();
}
