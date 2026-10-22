//===- Driver.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from lld/wasm/Driver.cpp at 64f50ffd4f1c.
//
// The msil-ld driver: the "msil" flavor of lld, which links C programs into
// .NET assemblies. This is the stub that registers the flavor. It handles
// --help, --version and -v as wasm-ld does and reports any input as not
// linkable yet; the linker itself replaces it from M14 on.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Driver.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace llvm::opt;
using namespace llvm::sys;

namespace lld::msil {
namespace {
// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

class LinkerDriver {
public:
  void linkerMain(ArrayRef<const char *> argsArr);
};
} // anonymous namespace

bool link(ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  // This driver-specific context will be freed later by unsafeLldMain().
  auto *context = new CommonLinkerContext;

  context->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  // Diagnostics name msil-ld whatever the program name: "lld -flavor msil" and
  // the triple-prefixed copies are msil-ld too.
  context->e.logName = "msil-ld";

  LinkerDriver().linkerMain(args);

  return errorCount() == 0;
}

#define OPTTABLE_CODE
#include "Options.inc"

namespace {
class MSILOptTable : public opt::OptTable {
public:
  MSILOptTable() : opt::OptTable(optionTables()) {}
  opt::InputArgList parse(ArrayRef<const char *> argv);
};
} // namespace

static cl::TokenizerCallback getQuotingStyle() {
  if (Triple(sys::getProcessTriple()).isOSWindows())
    return cl::TokenizeWindowsCommandLine;
  return cl::TokenizeGNUCommandLine;
}

opt::InputArgList MSILOptTable::parse(ArrayRef<const char *> argv) {
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());

  unsigned missingIndex;
  unsigned missingCount;

  // Expand response files (arguments in the form of @<filename>)
  // and then parse the arguments.
  cl::ExpandResponseFiles(saver(), getQuotingStyle(), vec);
  opt::InputArgList args = this->ParseArgs(vec, missingIndex, missingCount);

  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");

  for (auto *arg : args.filtered(OPT_UNKNOWN))
    error("unknown argument: " + arg->getAsString(args));
  return args;
}

void LinkerDriver::linkerMain(ArrayRef<const char *> argsArr) {
  MSILOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));

  auto &errHandler = errorHandler();

  // Handle --help
  if (args.hasArg(OPT_help)) {
    parser.printHelp(errHandler.outs(),
                     (std::string(argsArr[0]) + " [options] file...").c_str(),
                     "LLVM Linker", false);
    return;
  }

  // Handle -v or -version.
  if (args.hasArg(OPT_v) || args.hasArg(OPT_version))
    errHandler.outs() << getLLDVersion() << "\n";

  // The behavior of -v or --version is a bit strange, but this is
  // needed for compatibility with GNU linkers.
  if (args.hasArg(OPT_v) && !args.hasArg(OPT_INPUT))
    return;
  if (args.hasArg(OPT_version))
    return;

  if (!args.hasArg(OPT_INPUT)) {
    if (errorCount() == 0)
      error("no input files");
    return;
  }
  if (errorCount())
    return;

  // The stub ends here. M14 reads bitcode inputs and links them.
  error("linking is not implemented yet");
}
} // namespace lld::msil
