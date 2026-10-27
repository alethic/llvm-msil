//===- Synthetic.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The output model of msil-ld's writer (llvm/docs/MSIL/ImplementationPlan.md
// 5.9.12): the methods and fields of the output assembly before they become
// metadata rows, the entities that the shared trailer builder names
// (cli::EntityRef, plan 5.9.10), the objects' BCL references as the BCL
// binder binds them (BCLBinder.h, plan 5.9.7 and 5.11: against the link's
// reference assemblies, or, without references, against the built-in
// [mscorlib] table), and the cli::TrailerSink that turns the trailer's
// methods and RVA data into that model (lld/wasm's SyntheticSections have the
// same role for wasm).
//
// The writer collects every method and field first, in row order (5.9.12:
// the kept methods by object in link order and row order, then the
// linker-made members in first-use order, then the trailer's), names them,
// distributes them over '<Module>', the spill classes and, in a -shared
// output, the classes of plan 5.9.15 (the export class, '<msil>Data' and
// '<msil>SymbolTable'), and only then adds the rows: a cli::MetadataBuilder
// groups member rows by owner, and every body that names a member is encoded
// after MetadataBuilder::finalize().
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_SYNTHETIC_H
#define LLD_MSIL_SYNTHETIC_H

#include "BCLBinder.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lld::msil {
class InputMethod;
class ObjFile;
class Symbol;

// The class that a member of the output belongs to before the writer
// distributes the members (plan 5.9.12, 5.9.15).
enum class OutputClass : uint8_t {
  // '<Module>', or past its per-type limits a '<msil>Spill<N>' class.
  Module,
  // '<msil>Data' of a -shared output: the trailer's phase 1 and the fields
  // that it stores; past the field limit, fields go to the spill classes.
  Data,
  // The export class of a -shared output: the exported functions, the alias
  // forwarders and the '<msil>addr_' accessors (never spilled).
  Export,
  // '<msil>SymbolTable', the attribute class of a -shared output.
  SymbolTable,
};

// A method of the output assembly.
struct OutputMethod {
  enum Kind : uint8_t {
    // A kept method of a CIL object or an LTO object: its body is copied
    // with the token operands renumbered (plan 5.9.7).
    Object,
    // '<msil>adapt_<name>_<n>' (plan 5.9.8): a local adapter calls a method
    // of the output, a library adapter a library's MemberRef (see target).
    Adapter,
    // '<msil>undef_<name>', the call of an undefined weak function (plan
    // 5.9.6 step 3).
    TrapStub,
    // A P/Invoke stub of a natively bound function (plan 5.10.11).
    PInvoke,
    // A method of the trailer: '<msil>dataimport', '<msil>init_dataimports',
    // '.cctor', '<msil>Main' (plan 5.9.10).
    Trailer,
    // A method that a -shared output adds (plan 5.9.15): the forwarding
    // method of an exported function alias, an '<msil>addr_' accessor, the
    // constructor of '<msil>SymbolTable', and under --interface-only the
    // stub of an exported function.
    Export,
  };

  explicit OutputMethod(Kind k) : kind(k) {}

  Kind kind;
  // The name; Writer.cpp renames the renamable ones that collide (plan
  // 5.9.12) and names the local adapters. A library adapter arrives named
  // from CallBinder::bindManaged.
  std::string name;
  // True for a LOCAL method, and for the method of a definition that lost
  // resolution but stays because a winning alias lies in it.
  bool renamable = false;
  llvm::cli::MethodAttributes flags{};
  llvm::cli::MethodImplAttributes implFlags{};
  // The signature, with output tokens.
  llvm::cli::MethodSig sig;
  // Object: the input method.
  InputMethod *input = nullptr;
  // Adapter. A local adapter (CallBinder::bindCall): the method of the output
  // that it calls and its number (n from 0 per definition), from which
  // Writer.cpp names it. A library adapter (CallBinder::bindManaged) calls a
  // library's MemberRef: target is OutputField::NoMethod, which alone tells
  // the two kinds apart, adapterNumber is unused, and its name
  // '<msil>adapt_<export>_<n>' (n from 0 per export) is final when made.
  uint32_t target = 0;
  uint32_t adapterNumber = 0;
  // PInvoke: the module (the ModuleRef, verbatim) and the entry (the
  // ImplMap import name).
  std::string module, entry;
  // Adapter, TrapStub, Trailer and Export: the body, laid out, whose token
  // operands are TokenRef::getExternal(<entity>).
  std::unique_ptr<llvm::cli::ILBuilder> body;
  // The StandAloneSig token of the body's locals, or 0.
  uint32_t localSigToken = 0;
  // The class of the method before distribution.
  OutputClass cls = OutputClass::Module;
  // True if the method gets a Param row named 'A_<i>' (Flags 0) for each
  // parameter i from 0 (plan 5.9.15: the methods of the export class and
  // the constructor of '<msil>SymbolTable').
  bool namedParams = false;
  // The writer's index of the class that holds the method: 0 for
  // '<Module>', n for '<msil>Spill<n>', then the classes of plan 5.9.15.
  uint32_t owner = 0;
  llvm::cli::EntityHandle handle;
};

// A field of the output assembly.
struct OutputField {
  enum Kind : uint8_t {
    // The address field of a DATA symbol: a definition, a common symbol, an
    // alias that code names, __start_X/__stop_X, or an undefined weak
    // symbol (never stored).
    Address,
    // '<msil>fn_<name>', the address of a method (plan 5.9.7, R2).
    FnAddr,
    // The address field of a native data import (plan 5.10.12), stored by
    // '<msil>init_dataimports'.
    DataImport,
    // '<msil>data', '<msil>image', '<msil>relocs' (plan 5.9.10), and
    // '<msil>argc' and '<msil>argv' (cli::TrailerInput::CtorArgs).
    Trailer,
  };

  static constexpr uint32_t NoMethod = UINT32_MAX;

  explicit OutputField(Kind k) : kind(k) {}

  Kind kind;
  std::string name;
  // True for the address field of a LOCAL symbol.
  bool renamable = false;
  // Address: GLOBAL and WEAK symbols are public, LOCAL ones assembly.
  bool isPublic = false;
  // The symbol of an Address or DataImport field, or of the FnAddr field of
  // a native or undefined function.
  const Symbol *sym = nullptr;
  // The input of the symbol, for the map file.
  ObjFile *file = nullptr;
  // Address: the offset of the symbol in the data image (set once the image
  // is laid out); none for a symbol outside it.
  std::optional<uint64_t> imageOffset;
  // FnAddr: the method whose address '.cctor' stores, or NoMethod (an
  // undefined weak function: the field stays 0).
  uint32_t fnMethod = NoMethod;
  // Trailer: the flags, and the index into OutputModule::rvaData of an RVA
  // field, or -1.
  llvm::cli::FieldAttributes flags{};
  int32_t rva = -1;
  // The class of the field before distribution, and the writer's index of
  // the class that holds it (OutputMethod::owner).
  OutputClass cls = OutputClass::Module;
  uint32_t owner = 0;
  llvm::cli::EntityHandle handle;
};

// What a token of a CIL object becomes in the output.
struct OutRef {
  enum Kind : uint8_t { None, Method, Field, Token };
  Kind kind = None;
  // Method, Field: the index in OutputModule::methods or fields. Token: a
  // token that is final when it is made (a row of a stable table, a user
  // string, '<Module>').
  uint32_t value = 0;

  static OutRef method(uint32_t index) { return {Method, index}; }
  static OutRef field(uint32_t index) { return {Field, index}; }
  static OutRef token(uint32_t token) { return {Token, token}; }
};

// The output assembly before its rows exist.
class OutputModule {
public:
  explicit OutputModule(llvm::cli::SectionRules rules) : rules(rules) {}

  llvm::cli::MetadataBuilder mb;
  llvm::cli::SectionRules rules;
  // The binder of every BCL type and member of the output (plan 5.11): one
  // per output over mb, set by the writer before the first token is bound.
  BCLBinder *binder = nullptr;
  SmallVector<OutputMethod, 0> methods;
  SmallVector<OutputField, 0> fields;

  // The RVA data of the trailer ('<msil>image', '<msil>relocs'), in the
  // order the trailer adds it: the value type's name, the bytes and the
  // index of the field.
  struct RVAData {
    std::string typeName;
    std::vector<uint8_t> bytes;
    uint32_t field;
    llvm::cli::EntityHandle type;
  };
  SmallVector<RVAData, 2> rvaData;

  // What each token of each object that a kept body uses becomes (plan
  // 5.9.7), filled by the writer.
  llvm::DenseMap<std::pair<const ObjFile *, uint32_t>, OutRef> tokenRefs;

  uint32_t addMethod(OutputMethod m);
  uint32_t addField(OutputField f);

  // The output method of the kept method \p m.
  void setMethodOf(const InputMethod *m, uint32_t index) {
    methodOf[m] = index;
  }
  std::optional<uint32_t> getMethodOf(const InputMethod *m) const;

  // The entities of the trailer (cli::EntityRef, 0 is none): a method, a
  // field or a final token.
  llvm::cli::EntityRef getMethodEntity(uint32_t index);
  llvm::cli::EntityRef getFieldEntity(uint32_t index);
  llvm::cli::EntityRef getTokenEntity(uint32_t token);
  // The token of an entity or of a token operand; after the rows are added
  // and the metadata is finalized.
  llvm::Expected<uint32_t> resolveEntity(llvm::cli::EntityRef ref) const;
  llvm::Expected<uint32_t> resolveToken(llvm::cli::TokenRef ref) const;
  uint32_t getToken(OutRef ref) const;

  // The output TypeRef of a BCLMembers.def type, and the MemberRef of a
  // method (a type gives its TypeRef), as the binder binds them
  // (BCLBinder::bindMember). Returns 0 after an error, which is reported.
  uint32_t getBCLType(llvm::cli::BCLMemberId id);
  uint32_t getBCLMember(llvm::cli::BCLMemberId id);

  // The output token of a type token (TypeDefOrRefOrSpec) of \p f: a TypeRef
  // scoped to an AssemblyRef other than the placeholders msil.extern and
  // msil.link (msil.bcl, or any other name) is a type request that the
  // binder binds by name, a nested TypeRef as 'Outer/Inner' under its
  // outermost type's namespace (plan 5.11.4, 5.11.5); '<Module>' is
  // '<Module>', and a TypeSpec is re-encoded with output tokens. Returns 0
  // after an error, which is reported.
  uint32_t mapTypeToken(ObjFile &f, uint32_t token);
  // The output token of a BCL token of \p f (plan 5.9.7's 'MemberRef on
  // another TypeRef, TypeRef, TypeSpec, MethodSpec'): a MemberRef is a method
  // or field request on its parent type, with the object's signature mapped
  // to output tokens, which the binder binds by full signature. Returns 0
  // after an error, which is reported.
  uint32_t mapBCLToken(ObjFile &f, uint32_t token);
  // A StandAloneSig of \p f (a local or calli signature), re-encoded and
  // deduplicated.
  uint32_t mapStandAloneSig(ObjFile &f, ArrayRef<uint8_t> blob);
  // An ldstr operand of \p f, re-added to the output's #US heap.
  uint32_t mapUserString(ObjFile &f, uint32_t token);
  // \p sig of \p f with output tokens.
  llvm::cli::MethodSig mapSig(ObjFile &f, const llvm::cli::MethodSig &sig);
  // The StandAloneSig token of a local signature.
  uint32_t addLocalSig(ArrayRef<llvm::cli::TypeSig> locals);

  // The encoded bytes of \p sig, a key for signature equality.
  static std::string getSignatureKey(const llvm::cli::MethodSig &sig);

private:
  struct Entity {
    enum Kind : uint8_t { Method, Field, Token };
    Kind kind;
    uint32_t value;
  };
  llvm::cli::EntityRef addEntity(Entity e);
  // The namespace and the name ('Outer/Inner' for a nested type) of the
  // TypeRef \p token of \p f, which must be scoped, through its enclosing
  // TypeRefs, to an AssemblyRef other than the placeholders.
  llvm::Expected<std::pair<std::string, std::string>>
  getTypeRefName(ObjFile &f, uint32_t token);
  // The output token of the binder's result for \p r, or 0 after an error,
  // which is reported.
  uint32_t bind(const BCLRequest &r);
  void reportUnsupported(ObjFile &f, const llvm::Twine &what);
  // Reports \p msg once.
  void reportOnce(const llvm::Twine &msg);

  llvm::DenseMap<const InputMethod *, uint32_t> methodOf;
  std::vector<Entity> entities;
  llvm::DenseMap<uint32_t, llvm::cli::EntityRef> methodEntities, fieldEntities,
      tokenEntities;
  llvm::DenseMap<std::pair<const ObjFile *, uint32_t>, uint32_t> typeTokens;
  llvm::StringMap<bool> reported;
};

// Binds the class or value type \p ns.\p name of a BCLMembers.def signature
// to an output token (parseBCLMemberSig).
using BCLTypeBinder = llvm::function_ref<llvm::Expected<uint32_t>(
    StringRef ns, StringRef name, bool isValueType)>;

// A method signature in BCLMembers.def's text (canonical listing v1, plan
// 5.7.2): the return type \p ret and the comma-separated parameter types
// \p params, each a primitive, a pointer or an SZARRAY of one, or
// 'class <Ns.Name>' or 'valuetype <Ns.Name>', which \p bindType binds (an
// Error without it); an instance method when \p hasThis. A type that does
// not parse is an Error that names the signature as \p what (for example
// "BCL member 'NativeLibrary_Load'").
llvm::Expected<llvm::cli::MethodSig>
parseBCLMethodSig(StringRef ret, StringRef params, bool hasThis, StringRef what,
                  BCLTypeBinder bindType = nullptr);

// The signature of the BCLMembers.def method \p info, from its canonical
// listing v1 text (parseBCLMethodSig). An entry whose text does not parse is
// an Error that names it.
llvm::Expected<llvm::cli::MethodSig>
parseBCLMemberSig(const llvm::cli::BCLMemberInfo &info,
                  BCLTypeBinder bindType = nullptr);

// The request for the BCLMembers.def entry \p id (plan 5.11.5): a Type
// request for a type, a Method request for a method with the entry's
// signature (parseBCLMemberSig with \p bindType), Required, from msil-ld's
// own code (RequestedBy null).
llvm::Expected<BCLRequest>
getBCLMemberRequest(llvm::cli::BCLMemberId id,
                    BCLTypeBinder bindType = nullptr);

// The trailer sink of msil-ld (plan 5.9.10): the trailer's methods, fields,
// RVA data, BCL members and strings become entries of an OutputModule; the
// writer adds their rows with the others. Each method goes to the class of
// its cli::TrailerMethod::Owner. For a two-phase output (\p twoPhase, a
// -shared library, plan 5.9.15) '<msil>data' belongs to '<msil>Data', and
// it and the RVA fields '<msil>image' and '<msil>relocs', which stay in
// '<Module>', are 'assembly', because '<msil>Data'::.cctor and
// '<Module>'::.cctor both use them.
class RowTrailerSink : public llvm::cli::TrailerSink {
public:
  explicit RowTrailerSink(OutputModule &out, bool twoPhase = false)
      : out(out), twoPhase(twoPhase) {}

  llvm::cli::EntityRef getDataField() override;
  llvm::cli::EntityRef addRVAData(StringRef typeName, StringRef fieldName,
                                  ArrayRef<uint8_t> bytes) override;
  llvm::cli::EntityRef getBCLMember(llvm::cli::BCLMemberId id) override;
  llvm::cli::EntityRef declareHelper(StringRef name,
                                     const llvm::cli::MethodSig &sig) override;
  llvm::cli::EntityRef addUserString(StringRef utf8) override;
  llvm::cli::EntityRef addTrailerField(StringRef name) override;
  void emitMethod(const llvm::cli::TrailerMethod &m) override;

private:
  OutputModule &out;
  bool twoPhase;
  std::optional<uint32_t> dataField;
  // The helpers that declareHelper() made and emitMethod() has not defined
  // yet, by name.
  llvm::StringMap<uint32_t> declared;
};
} // namespace lld::msil

#endif
