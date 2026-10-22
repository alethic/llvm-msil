add_llvm_component_library(LLVMCLI
  TableSchema.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/CLI

  LINK_COMPONENTS
  BinaryFormat
  MC
  Support
  TargetParser
  )
