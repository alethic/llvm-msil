//===- nisym.cpp - prototype of msil-ld's native import resolver ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Phase 2 recon prototype (native-imports). Reads native libraries with
// llvm::object and answers, for each queried C symbol, which P/Invoke a
// linker would emit: pinvokeimpl("<module>" as "<entry>"), the symbol kind
// (function, IFUNC, data, TLS), and why a symbol cannot be bound.
//
// Inputs (in link order): ELF shared objects (.dynsym, DT_SONAME, GNU symbol
// versions), GNU linker scripts (GROUP/INPUT/AS_NEEDED), archives (COFF short
// import members, COFF objects with weak-external aliases such as
// oldnames.lib, native ELF/COFF objects), PE DLLs (export table) and IFS text
// stubs (llvm-ifs).
//
// Usage: nisym [--os=linux|windows] [--sysroot=DIR] [-LDIR]... [-lNAME]...
//              [FILE]... [--query=a,b,c | --dump]
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/InterfaceStub/IFSHandler.h"
#include "llvm/InterfaceStub/IFSStub.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::object;

namespace {

enum class Kind { Func, IFunc, Data, TLS, NoType, Code, Const, Unknown };

const char *kindName(Kind K) {
  switch (K) {
  case Kind::Func: return "func";
  case Kind::IFunc: return "ifunc";
  case Kind::Data: return "data";
  case Kind::TLS: return "tls";
  case Kind::NoType: return "notype";
  case Kind::Code: return "code";
  case Kind::Const: return "const";
  case Kind::Unknown: return "unknown";
  }
  return "?";
}

/// One way a library can provide a symbol.
struct Def {
  std::string Module;  // DllImport module name, as a linker would emit it
  std::string Entry;   // native entry point ("#N" for an ordinal)
  std::string Origin;  // file or archive(member)
  Kind K = Kind::Unknown;
  std::string Version; // ELF version name, empty if unversioned
  bool DefaultVer = false;
  bool Weak = false;
  bool Bindable = true;  // can a P/Invoke reach it (dlsym/GetProcAddress)?
  std::string WhyNot;
};

struct Table {
  std::string OS = "linux";
  std::string Sysroot;
  std::vector<std::string> LibDirs;
  std::map<std::string, std::vector<Def>> Defs;          // by C name
  std::map<std::string, std::string> Aliases;           // COFF weak alias
  std::vector<std::string> Loaded;                       // files, in order
  std::vector<std::unique_ptr<MemoryBuffer>> Buffers;
  std::vector<std::unique_ptr<Binary>> Binaries;
  void add(StringRef Name, Def D) { Defs[Name.str()].push_back(std::move(D)); }
};

std::string stripDll(StringRef Dll) {
  if (Dll.ends_with_insensitive(".dll"))
    return Dll.drop_back(4).str();
  return Dll.str();
}

//===--------------------------------------------------------------------===//
// ELF shared objects
//===--------------------------------------------------------------------===//

template <class ELFT>
void loadELFShared(Table &T, const ELFObjectFile<ELFT> &Obj, StringRef Path) {
  const ELFFile<ELFT> &EF = Obj.getELFFile();
  std::string SoName;
  std::vector<std::string> Needed;
  for (const auto &Sec : cantFail(EF.sections())) {
    if (Sec.sh_type != ELF::SHT_DYNAMIC)
      continue;
    const auto *StrSec = cantFail(EF.getSection(Sec.sh_link));
    StringRef Str = cantFail(EF.getStringTable(*StrSec));
    for (const auto &D : cantFail(EF.dynamicEntries())) {
      if (D.d_tag == ELF::DT_SONAME)
        SoName = Str.data() + D.getVal();
      else if (D.d_tag == ELF::DT_NEEDED)
        Needed.push_back(Str.data() + D.getVal());
    }
  }
  std::string Module =
      SoName.empty() ? sys::path::filename(Path).str() : SoName;
  outs() << "# " << Path << ": ELF shared object, DT_SONAME '" << SoName
         << "', DT_NEEDED [" << join(Needed, ", ") << "]\n";

  std::vector<VersionEntry> Vers;
  if (Expected<std::vector<VersionEntry>> V = Obj.readDynsymVersions())
    Vers = std::move(*V);
  else
    consumeError(V.takeError());

  // Raw versym values, for the hidden bit (readDynsymVersions folds it).
  std::vector<uint16_t> VerSym;
  for (const auto &Sec : cantFail(EF.sections()))
    if (Sec.sh_type == ELF::SHT_GNU_versym)
      for (const auto &VS :
           cantFail(EF.template getSectionContentsAsArray<
                    typename ELFT::Versym>(Sec)))
        VerSym.push_back(VS.vs_index);

  size_t I = 0;
  unsigned Count = 0;
  for (const ELFSymbolRef &Sym : Obj.getDynamicSymbolIterators()) {
    size_t Idx = I++;
    const typename ELFT::Sym *ES = cantFail(Obj.getSymbol(Sym.getRawDataRefImpl()));
    if (ES->isUndefined())
      continue;
    uint8_t Bind = ES->getBinding();
    if (Bind != ELF::STB_GLOBAL && Bind != ELF::STB_WEAK &&
        Bind != ELF::STB_GNU_UNIQUE)
      continue;
    uint8_t Vis = ES->getVisibility();
    if (Vis != ELF::STV_DEFAULT && Vis != ELF::STV_PROTECTED)
      continue;
    // Version-name markers (GLIBC_2.2.5@@GLIBC_2.2.5) are absolute symbols.
    if (ES->st_shndx == ELF::SHN_ABS)
      continue;
    StringRef Name = cantFail(Sym.getName());
    Def D;
    D.Module = Module;
    D.Entry = Name.str();
    D.Origin = Path.str();
    D.Weak = Bind == ELF::STB_WEAK;
    switch (ES->getType()) {
    case ELF::STT_FUNC: D.K = Kind::Func; break;
    case ELF::STT_GNU_IFUNC: D.K = Kind::IFunc; break;
    case ELF::STT_OBJECT:
    case ELF::STT_COMMON: D.K = Kind::Data; break;
    case ELF::STT_TLS: D.K = Kind::TLS; break;
    case ELF::STT_NOTYPE: D.K = Kind::NoType; break;
    default: D.K = Kind::Unknown; break;
    }
    uint16_t Raw = Idx + 1 < VerSym.size() ? VerSym[Idx + 1] : 1;
    if (Idx < Vers.size()) {
      D.Version = Vers[Idx].Name;
      D.DefaultVer = Vers[Idx].IsVerDef;
    }
    bool Hidden = Raw & ELF::VERSYM_HIDDEN;
    if (!D.Version.empty() && Hidden) {
      // glibc's dlsym (DL_LOOKUP_RETURN_NEWEST) never returns a hidden
      // (non-default, 'name@VER') definition; lld binds the bare name only
      // when ver == idx (lld/ELF/InputFiles.cpp SharedFile::parse).
      D.Bindable = false;
      D.WhyNot = "only a non-default version (" + Name.str() + "@" +
                 D.Version + "); dlsym cannot return it";
    } else if (D.K == Kind::TLS) {
      D.Bindable = false;
      D.WhyNot = "thread-local data";
    } else if (StringRef(D.Version) == "GLIBC_PRIVATE") {
      D.Bindable = false;
      D.WhyNot = "GLIBC_PRIVATE";
    }
    T.add(Name, std::move(D));
    ++Count;
  }
  outs() << "#   " << Count << " defined dynamic symbols\n";
}

void loadELFSharedAny(Table &T, const ObjectFile &O, StringRef Path) {
  if (const auto *E = dyn_cast<ELF64LEObjectFile>(&O))
    return loadELFShared(T, *E, Path);
  if (const auto *E = dyn_cast<ELF32LEObjectFile>(&O))
    return loadELFShared(T, *E, Path);
  if (const auto *E = dyn_cast<ELF64BEObjectFile>(&O))
    return loadELFShared(T, *E, Path);
  if (const auto *E = dyn_cast<ELF32BEObjectFile>(&O))
    return loadELFShared(T, *E, Path);
}

//===--------------------------------------------------------------------===//
// COFF import members, COFF objects, PE DLLs
//===--------------------------------------------------------------------===//

void loadCOFFImport(Table &T, const COFFImportFile &Imp, StringRef Origin) {
  const coff_import_header *H = Imp.getCOFFImportHeader();
  StringRef Buf = Imp.getMemoryBufferRef().getBuffer();
  // Layout (PE/COFF spec 'Import Library Format'): 20-byte header, symbol
  // name, NUL, DLL name, NUL [, EXPORTAS name, NUL].
  StringRef SymName = Buf.substr(sizeof(*H)).split('\0').first;
  StringRef Dll = Buf.substr(sizeof(*H) + SymName.size() + 1).split('\0').first;
  Def D;
  D.Module = stripDll(Dll);
  D.Origin = Origin.str();
  switch (H->getType()) {
  case COFF::IMPORT_CODE: D.K = Kind::Code; break;
  case COFF::IMPORT_DATA: D.K = Kind::Data; break;
  case COFF::IMPORT_CONST: D.K = Kind::Const; break;
  }
  if (H->getNameType() == COFF::IMPORT_ORDINAL)
    D.Entry = "#" + utostr(H->OrdinalHint);
  else
    D.Entry = Imp.getExportName().str();
  if (D.K != Kind::Code) {
    D.Bindable = false;
    D.WhyNot = "data import (only __imp_" + SymName.str() + " exists)";
  }
  T.add(SymName, std::move(D));
}

void loadCOFFObject(Table &T, const COFFObjectFile &Obj, StringRef Origin) {
  for (const SymbolRef &S : Obj.symbols()) {
    COFFSymbolRef CS = Obj.getCOFFSymbol(S);
    Expected<StringRef> NameOrErr = Obj.getSymbolName(CS);
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    if (CS.isWeakExternal()) {
      const coff_aux_weak_external *Aux = CS.getWeakExternal();
      if (Aux->Characteristics == COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS) {
        Expected<COFFSymbolRef> Tgt = Obj.getSymbol(Aux->TagIndex);
        if (Tgt)
          if (Expected<StringRef> TN = Obj.getSymbolName(*Tgt))
            T.Aliases[NameOrErr->str()] = TN->str();
      }
      continue;
    }
    if (CS.isExternal() && !CS.isUndefined() && CS.getSectionNumber() > 0) {
      Def D;
      D.Origin = Origin.str();
      D.K = Kind::Code;
      D.Bindable = false;
      D.WhyNot = "defined only in native object code (" + Origin.str() + ")";
      T.add(*NameOrErr, std::move(D));
    }
  }
}

void loadPEDll(Table &T, const COFFObjectFile &Obj, StringRef Path) {
  StringRef DllName;
  unsigned Count = 0;
  for (const ExportDirectoryEntryRef &E : Obj.export_directories()) {
    StringRef Name;
    uint32_t Ord = 0, RVA = 0;
    bool Fwd = false;
    if (DllName.empty() && E.getDllName(DllName))
      DllName = sys::path::filename(Path);
    cantFail(E.getOrdinal(Ord));
    cantFail(E.getExportRVA(RVA));
    cantFail(E.isForwarder(Fwd));
    if (Error Err = E.getSymbolName(Name))
      consumeError(std::move(Err));
    Def D;
    D.Module = stripDll(sys::path::filename(Path));
    D.Origin = Path.str();
    D.Entry = Name.empty() ? "#" + utostr(Ord) : Name.str();
    D.K = Kind::Code;
    if (!Fwd) {
      // The export table has no kind: a data export lies in a section
      // without IMAGE_SCN_CNT_CODE.
      for (const SectionRef &S : Obj.sections()) {
        const coff_section *CS = Obj.getCOFFSection(S);
        if (RVA >= CS->VirtualAddress &&
            RVA < CS->VirtualAddress + CS->VirtualSize &&
            !(CS->Characteristics & COFF::IMAGE_SCN_CNT_CODE)) {
          D.K = Kind::Data;
          D.Bindable = false;
          D.WhyNot = "data export (RVA in a non-code section)";
        }
      }
    }
    if (!Name.empty()) {
      T.add(Name, std::move(D));
      ++Count;
    }
  }
  outs() << "# " << Path << ": PE DLL '" << DllName << "', " << Count
         << " named exports\n";
}

//===--------------------------------------------------------------------===//
// Text inputs: IFS stubs and GNU linker scripts
//===--------------------------------------------------------------------===//

bool loadFile(Table &T, StringRef Path, bool FromScript);

std::string findLib(Table &T, StringRef Name) {
  SmallVector<std::string, 4> Cands;
  if (T.OS == "windows") {
    if (sys::path::has_extension(Name))
      Cands.push_back(Name.str());
    else
      Cands.push_back((Name + ".lib").str());
  } else {
    Cands.push_back(("lib" + Name + ".so").str());
    Cands.push_back(("lib" + Name + ".ifs").str()); // text stub (proposal)
    Cands.push_back(("lib" + Name + ".a").str());
  }
  for (const std::string &Dir : T.LibDirs)
    for (const std::string &C : Cands) {
      SmallString<256> P(Dir);
      sys::path::append(P, C);
      if (sys::fs::exists(P))
        return std::string(P);
    }
  return "";
}

void loadIFS(Table &T, StringRef Text, StringRef Path) {
  Expected<std::unique_ptr<ifs::IFSStub>> StubOrErr =
      ifs::readIFSFromBuffer(Text);
  if (!StubOrErr) {
    errs() << Path << ": " << toString(StubOrErr.takeError()) << "\n";
    return;
  }
  ifs::IFSStub &Stub = **StubOrErr;
  std::string Module =
      Stub.SoName ? *Stub.SoName : sys::path::filename(Path).str();
  unsigned Count = 0;
  for (const ifs::IFSSymbol &S : Stub.Symbols) {
    if (S.Undefined)
      continue;
    Def D;
    D.Module = Module;
    D.Entry = S.Name;
    D.Origin = Path.str();
    D.Weak = S.Weak;
    switch (S.Type) {
    case ifs::IFSSymbolType::Func: D.K = Kind::Func; break;
    case ifs::IFSSymbolType::Object: D.K = Kind::Data; break;
    case ifs::IFSSymbolType::TLS: D.K = Kind::TLS; break;
    case ifs::IFSSymbolType::NoType: D.K = Kind::NoType; break;
    default: D.K = Kind::Unknown; break;
    }
    if (D.K == Kind::TLS) {
      D.Bindable = false;
      D.WhyNot = "thread-local data";
    }
    T.add(S.Name, std::move(D));
    ++Count;
  }
  outs() << "# " << Path << ": IFS stub, SoName '" << Module << "', "
         << Count << " symbols\n";
}

void loadLinkerScript(Table &T, StringRef Text, StringRef Path) {
  outs() << "# " << Path << ": GNU linker script\n";
  // Minimal tokenizer: comments, GROUP/INPUT/AS_NEEDED and file names.
  std::string S;
  for (size_t I = 0; I < Text.size(); ++I) {
    if (Text.substr(I).starts_with("/*")) {
      size_t E = Text.find("*/", I + 2);
      I = E == StringRef::npos ? Text.size() : E + 1;
      continue;
    }
    char C = Text[I];
    S += (C == '(' || C == ')' || C == ',') ? ' ' : C;
  }
  SmallVector<StringRef, 16> Toks;
  StringRef(S).split(Toks, ' ', -1, false);
  for (StringRef Tok : Toks) {
    Tok = Tok.trim();
    if (Tok.empty() || Tok == "GROUP" || Tok == "INPUT" || Tok == "AS_NEEDED" ||
        Tok.starts_with("OUTPUT_FORMAT"))
      continue;
    std::string P;
    if (Tok.starts_with("-l"))
      P = findLib(T, Tok.drop_front(2));
    else if (sys::path::is_absolute(Tok, sys::path::Style::posix)) {
      P = T.Sysroot + Tok.str();
      if (!sys::fs::exists(P)) {
        // Fall back to the -L directories by file name.
        for (const std::string &Dir : T.LibDirs) {
          SmallString<256> Q(Dir);
          sys::path::append(Q, sys::path::filename(Tok, sys::path::Style::posix));
          if (sys::fs::exists(Q)) {
            P = std::string(Q);
            break;
          }
        }
      }
    } else
      P = Tok.str();
    outs() << "#   script input '" << Tok << "' -> " << (P.empty() ? "?" : P)
           << "\n";
    if (!P.empty() && sys::fs::exists(P))
      loadFile(T, P, /*FromScript=*/true);
  }
}

//===--------------------------------------------------------------------===//
// Dispatch
//===--------------------------------------------------------------------===//

void loadBinary(Table &T, Binary &B, StringRef Origin) {
  if (auto *Imp = dyn_cast<COFFImportFile>(&B))
    return loadCOFFImport(T, *Imp, Origin);
  if (auto *C = dyn_cast<COFFObjectFile>(&B)) {
    if (C->getDOSHeader() || C->getPE32Header() || C->getPE32PlusHeader())
      return loadPEDll(T, *C, Origin);
    return loadCOFFObject(T, *C, Origin);
  }
  if (auto *O = dyn_cast<ObjectFile>(&B)) {
    if (O->isELF()) {
      auto *E = cast<ELFObjectFileBase>(O);
      if (E->getEType() == ELF::ET_DYN)
        return loadELFSharedAny(T, *O, Origin);
      // A relocatable ELF member (libc_nonshared.a): native code only.
      for (const SymbolRef &S : O->symbols()) {
        uint32_t F = cantFail(S.getFlags());
        if ((F & SymbolRef::SF_Global) && !(F & SymbolRef::SF_Undefined)) {
          Def D;
          D.Origin = Origin.str();
          D.K = Kind::Code;
          D.Bindable = false;
          D.WhyNot = "defined only in native object code (" + Origin.str() + ")";
          T.add(cantFail(S.getName()), std::move(D));
        }
      }
    }
  }
}

bool loadFile(Table &T, StringRef Path, bool FromScript) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(Path, /*IsText=*/false,
                            /*RequiresNullTerminator=*/false);
  if (!BufOrErr) {
    errs() << Path << ": " << BufOrErr.getError().message() << "\n";
    return false;
  }
  MemoryBufferRef Ref = (*BufOrErr)->getMemBufferRef();
  T.Buffers.push_back(std::move(*BufOrErr));
  T.Loaded.push_back(Path.str());
  file_magic M = identify_magic(Ref.getBuffer());
  if (M == file_magic::unknown) {
    StringRef Text = Ref.getBuffer();
    if (Text.ltrim().starts_with("--- !ifs-v1"))
      loadIFS(T, Text, Path);
    else
      loadLinkerScript(T, Text, Path);
    return true;
  }
  if (M == file_magic::archive) {
    Expected<std::unique_ptr<Archive>> AOrErr = Archive::create(Ref);
    if (!AOrErr) {
      errs() << Path << ": " << toString(AOrErr.takeError()) << "\n";
      return false;
    }
    Archive &A = **AOrErr;
    Error Err = Error::success();
    unsigned Members = 0;
    for (const Archive::Child &C : A.children(Err)) {
      ++Members;
      Expected<std::unique_ptr<Binary>> BOrErr = C.getAsBinary();
      if (!BOrErr) {
        consumeError(BOrErr.takeError());
        continue;
      }
      std::string Origin =
          (Path + "(" + cantFail(C.getName()) + ")").str();
      loadBinary(T, **BOrErr, Origin);
      T.Binaries.push_back(std::move(*BOrErr));
    }
    if (Err)
      errs() << Path << ": " << toString(std::move(Err)) << "\n";
    outs() << "# " << Path << ": archive, " << Members << " members\n";
    T.Binaries.push_back(std::move(*AOrErr));
    return true;
  }
  Expected<std::unique_ptr<Binary>> BOrErr = createBinary(Ref);
  if (!BOrErr) {
    errs() << Path << ": " << toString(BOrErr.takeError()) << "\n";
    return false;
  }
  loadBinary(T, **BOrErr, Path);
  T.Binaries.push_back(std::move(*BOrErr));
  return true;
}

void printDef(StringRef Name, const Def &D, bool Chosen) {
  outs() << (Chosen ? "  => " : "     ") << Name << ": ";
  if (D.Bindable) {
    outs() << "pinvokeimpl(\"" << D.Module << "\"";
    if (D.Entry != Name)
      outs() << " as \"" << D.Entry << "\"";
    outs() << ")";
  } else {
    outs() << "NOT BINDABLE: " << D.WhyNot;
  }
  outs() << " [" << kindName(D.K);
  if (!D.Version.empty())
    outs() << ", " << Name << (D.DefaultVer ? "@@" : "@") << D.Version;
  if (D.Weak)
    outs() << ", weak";
  outs() << "] from " << D.Origin << "\n";
}

void query(Table &T, StringRef Name) {
  std::string N = Name.str();
  std::string Via;
  auto It = T.Defs.find(N);
  if (It == T.Defs.end()) {
    auto A = T.Aliases.find(N);
    if (A != T.Aliases.end()) {
      Via = A->second;
      It = T.Defs.find(Via);
    }
  }
  if (It == T.Defs.end()) {
    outs() << Name << ": UNDEFINED (link error)\n";
    return;
  }
  outs() << Name;
  if (!Via.empty())
    outs() << " (weak alias -> " << Via << ")";
  outs() << ":\n";
  bool Chosen = false;
  for (const Def &D : It->second) {
    bool C = !Chosen && D.Bindable;
    Chosen |= C;
    Def Shown = D;
    if (!Via.empty() && Shown.Entry.empty())
      Shown.Entry = Via;
    printDef(Name, Shown, C);
  }
  if (!Chosen)
    outs() << "  => " << Name << ": link error (no bindable definition)\n";
}

} // namespace

int main(int argc, char **argv) {
  Table T;
  std::vector<std::string> Queries;
  bool Dump = false;
  for (int I = 1; I < argc; ++I) {
    StringRef A = argv[I];
    if (A.consume_front("--os="))
      T.OS = A.str();
    else if (A.consume_front("--sysroot="))
      T.Sysroot = A.str();
    else if (A.consume_front("-L"))
      T.LibDirs.push_back(A.str());
    else if (A.consume_front("-l")) {
      std::string P = findLib(T, A);
      if (P.empty())
        errs() << "-l" << A << ": not found\n";
      else
        loadFile(T, P, false);
    } else if (A.consume_front("--query=")) {
      SmallVector<StringRef, 16> Q;
      A.split(Q, ',', -1, false);
      for (StringRef S : Q)
        Queries.push_back(S.str());
    } else if (A == "--dump")
      Dump = true;
    else
      loadFile(T, A, false);
  }
  for (const std::string &Q : Queries)
    query(T, Q);
  if (Dump)
    for (auto &[Name, Ds] : T.Defs)
      for (const Def &D : Ds)
        printDef(Name, D, false);
  return 0;
}
