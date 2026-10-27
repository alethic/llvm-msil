set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(MSILOptionsTableGen)

add_lld_library(lldMSIL
  AssemblyFiles.cpp
  BCLBinder.cpp
  Driver.cpp
  Exports.cpp
  InputChunks.cpp
  InputFiles.cpp
  LTO.cpp
  MapFile.cpp
  MarkLive.cpp
  NativeLibraries.cpp
  OutputSegment.cpp
  ReferenceSet.cpp
  Relocations.cpp
  Signatures.cpp
  SymbolTable.cpp
  Symbols.cpp
  Synthetic.cpp
  TokenMap.cpp
  Writer.cpp

  LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  BinaryFormat
  BitWriter
  BitReader
  CLI
  Core
  LTO
  MC
  Object
  Option
  Passes
  Support
  TargetParser

  LINK_LIBS
  lldCommon

  DEPENDS
  MSILOptionsTableGen
  intrinsics_gen
  )
