set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(MSILOptionsTableGen)

add_lld_library(lldMSIL
  Driver.cpp
  InputFiles.cpp
  LTO.cpp
  MapFile.cpp
  NativeLibraries.cpp
  SymbolTable.cpp
  Symbols.cpp

  LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  BinaryFormat
  BitWriter
  BitReader
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
