//===- llvm-cil-dump.cpp - Dump an ECMA-335 module ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-cil-dump prints IL-only PE images (assemblies, CIL objects):
//
//   llvm-cil-dump [--canonical] [--scopes=keep|strip] [--order=sorted|decl]
//                 [--forms=exact|canon] [--assembly-hash=keep|ignore]
//                 [--params=keep|drop]
//                 [--initlocals=keep|ignore-without-locals]
//                 [--snan=keep|quiet] [--pinvoke-module=keep|strip]
//                 [--pinvoke-access=keep|ignore] [--stats|--stats-only]
//                 [--headers] [--tables] [--link-info] <file>...
//
// --canonical (canonical listing v1, the default when no other mode is
// given) and its options, --headers, --tables and --link-info are
// cli::dumpCanonical, cli::dumpHeaders, cli::dumpTables and cli::dumpLinkInfo;
// several modes print in that order. --stats appends the canonical mode's
// statistics lines to its listing and --stats-only prints them instead of
// it, so either one selects the canonical mode as --canonical does, also
// next to another mode; the other listing options select no mode. With
// several files, each file's output is preceded by 'File: <path>'. A file
// that cannot be read or dumped prints
// "llvm-cil-dump: error: '<file>': <message>" and the tool goes on with the
// next one; the exit status is 1 if any file failed.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CLI/Dump.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>

using namespace llvm;
using namespace llvm::cli;

static constexpr StringLiteral ToolName = "llvm-cil-dump";

static cl::OptionCategory DumpCategory("llvm-cil-dump Options");

static cl::opt<bool>
    Canonical("canonical",
              cl::desc("Print canonical listing v1 (the default mode)"),
              cl::cat(DumpCategory));
static cl::opt<bool> Headers("headers",
                             cl::desc("Print the PE, CLI and metadata headers"),
                             cl::cat(DumpCategory));
static cl::opt<bool> Tables("tables",
                            cl::desc("Print every row of every metadata table"),
                            cl::cat(DumpCategory));
static cl::opt<bool> LinkInfo(
    "link-info",
    cl::desc("Print the link-info record and the symbol tables of the image"),
    cl::cat(DumpCategory));

namespace {
/// The two values of a listing option: the listing as it is, or changed.
enum Choice { Keep, Change };
} // namespace

static cl::opt<Choice>
    Scopes("scopes", cl::desc("AssemblyRef scopes of the listing"),
           cl::values(clEnumValN(Keep, "keep", "Print [<assembly>] (default)"),
                      clEnumValN(Change, "strip",
                                 "Print [*] and omit the assemblyref lines")),
           cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice> Order(
    "order", cl::desc("Order of the listing's types, members and lines"),
    cl::values(clEnumValN(Keep, "sorted", "Sorted by their text (default)"),
               clEnumValN(Change, "decl", "Table order")),
    cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice> Forms(
    "forms", cl::desc("Encoding forms of the listing's instructions"),
    cl::values(clEnumValN(Keep, "exact", "As encoded (default)"),
               clEnumValN(Change, "canon",
                          "Short branches and short variable and ldc.i4.s "
                          "forms without '.s'")),
    cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice>
    AssemblyHash("assembly-hash", cl::desc("HashAlgId of the assembly line"),
                 cl::values(clEnumValN(Keep, "keep", "Print it (default)"),
                            clEnumValN(Change, "ignore", "Print hash=*")),
                 cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice> Params(
    "params", cl::desc("Param rows of the listing"),
    cl::values(clEnumValN(Keep, "keep", "Print every Param row (default)"),
               clEnumValN(Change, "drop",
                          "Omit Param rows with flags 0, no FieldMarshal row "
                          "with a non-zero NativeType index and no Constant "
                          "row")),
    cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice> InitLocals(
    "initlocals", cl::desc("InitLocals of method bodies"),
    cl::values(clEnumValN(Keep, "keep", "Print it (default)"),
               clEnumValN(Change, "ignore-without-locals",
                          "Omit it on bodies without a local signature")),
    cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice>
    SNaN("snan", cl::desc("float32 signaling NaN operands"),
         cl::values(clEnumValN(Keep, "keep", "Print their bits (default)"),
                    clEnumValN(Change, "quiet", "Print them with bit 22 set")),
         cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice> PInvokeModule(
    "pinvoke-module", cl::desc("Modules of P/Invoke methods"),
    cl::values(clEnumValN(Keep, "keep", "Print them (default)"),
               clEnumValN(Change, "strip",
                          "Print '*' and omit the moduleref lines")),
    cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<Choice> PInvokeAccess(
    "pinvoke-access", cl::desc("Member access of P/Invoke methods"),
    cl::values(clEnumValN(Keep, "keep", "Print it (default)"),
               clEnumValN(Change, "ignore",
                          "Print their flags with MemberAccess cleared")),
    cl::init(Keep), cl::cat(DumpCategory));
static cl::opt<bool> StatsOnly(
    "stats-only",
    cl::desc("Print the table, heap and image statistics instead of the "
             "canonical listing (selects the canonical mode)"),
    cl::cat(DumpCategory));

static cl::list<std::string> InputFiles(cl::Positional, cl::OneOrMore,
                                        cl::desc("<file>..."),
                                        cl::cat(DumpCategory));

/// LLVM's Support library registers -stats itself (Statistic.cpp), so the
/// listing's --stats is that option, documented here with its meaning in
/// this tool and read with AreStatisticsEnabled(). The tool has no
/// statistics of its own, so it never prints LLVM's statistics report.
static void adoptStatsOption() {
  auto &Options = cl::getRegisteredOptions();
  auto It = Options.find("stats");
  if (It == Options.end())
    return;
  It->second->setDescription("Append the table, heap and image statistics "
                             "to the canonical listing (selects the "
                             "canonical mode)");
  It->second->addCategory(DumpCategory);
  It->second->setHiddenFlag(cl::NotHidden);
}

static void reportError(StringRef File, const Twine &Message) {
  outs().flush();
  WithColor::error(errs(), ToolName) << "'" << File << "': " << Message << "\n";
}

/// Dumps one file to outs(); false if it failed.
static bool dumpFile(StringRef File, const CanonicalOptions &Opts,
                     bool DoCanonical) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer = MemoryBuffer::getFileOrSTDIN(
      File, /*IsText=*/false, /*RequiresNullTerminator=*/false);
  if (!Buffer) {
    reportError(File, Buffer.getError().message());
    return false;
  }
  Expected<std::unique_ptr<MetadataReader>> Reader =
      MetadataReader::create(arrayRefFromStringRef((*Buffer)->getBuffer()));
  if (!Reader) {
    reportError(File, toString(Reader.takeError()));
    return false;
  }
  // The whole output of a file is written only when every mode succeeded.
  std::string Text;
  raw_string_ostream OS(Text);
  auto Dump = [&](const MetadataReader &R) -> Error {
    if (DoCanonical)
      if (Error E = dumpCanonical(R, OS, Opts))
        return E;
    if (Headers)
      if (Error E = dumpHeaders(R, OS))
        return E;
    if (Tables)
      if (Error E = dumpTables(R, OS))
        return E;
    if (LinkInfo)
      if (Error E = dumpLinkInfo(R, OS))
        return E;
    return Error::success();
  };
  if (Error E = Dump(**Reader)) {
    reportError(File, toString(std::move(E)));
    return false;
  }
  outs() << Text;
  return true;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  adoptStatsOption();
  cl::HideUnrelatedOptions(DumpCategory);
  cl::ParseCommandLineOptions(argc, argv, "ECMA-335 module dumper\n");

  CanonicalOptions Opts;
  Opts.StripScopes = Scopes == Change;
  Opts.DeclarationOrder = Order == Change;
  Opts.CanonicalForms = Forms == Change;
  Opts.IgnoreAssemblyHash = AssemblyHash == Change;
  Opts.DropParams = Params == Change;
  Opts.IgnoreInitLocalsWithoutLocals = InitLocals == Change;
  Opts.QuietSignalingNaN = SNaN == Change;
  Opts.StripPInvokeModule = PInvokeModule == Change;
  Opts.IgnorePInvokeAccess = PInvokeAccess == Change;
  if (StatsOnly)
    Opts.StatsMode = CanonicalOptions::Stats::Only;
  else if (AreStatisticsEnabled())
    Opts.StatsMode = CanonicalOptions::Stats::Append;
  // --stats and --stats-only print the canonical mode's lines, so they select
  // it as --canonical does; with no mode option it is the default.
  bool DoCanonical = Canonical ||
                     Opts.StatsMode != CanonicalOptions::Stats::None ||
                     !(Headers || Tables || LinkInfo);

  bool Failed = false;
  for (const std::string &File : InputFiles) {
    if (InputFiles.size() > 1)
      outs() << "File: " << File << "\n";
    if (!dumpFile(File, Opts, DoCanonical))
      Failed = true;
  }
  outs().flush();
  return Failed ? 1 : 0;
}
