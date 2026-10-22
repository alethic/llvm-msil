set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(MSILOptionsTableGen)

add_lld_library(lldMSIL
  Driver.cpp

  LINK_COMPONENTS
  Option
  Support
  TargetParser

  LINK_LIBS
  lldCommon

  DEPENDS
  MSILOptionsTableGen
  )
