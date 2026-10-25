include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

add_llvm_component_library(LLVMMSILDesc
  MSILAsmBackend.cpp
  MSILInstPrinter.cpp
  MSILMCAsmInfo.cpp
  MSILMCCodeEmitter.cpp
  MSILMCExpr.cpp
  MSILMCTargetDesc.cpp
  MSILMCTypeUtilities.cpp
  MSILTargetStreamer.cpp

  LINK_COMPONENTS
  CLI
  CodeGenTypes
  MC
  Support
  TargetParser
  MSILInfo

  ADD_TO_COMPONENT
  MSIL
  )

add_dependencies(LLVMMSILDesc MSILCommonTableGen)
