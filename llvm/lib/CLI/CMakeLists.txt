add_llvm_component_library(LLVMCLI
  Blob.cpp
  CustomAttribute.cpp
  Heaps.cpp
  ILBuilder.cpp
  ILDecoder.cpp
  MetadataBuilder.cpp
  MethodBody.cpp
  Opcodes.cpp
  Signature.cpp
  TableSchema.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/CLI

  LINK_COMPONENTS
  BinaryFormat
  MC
  Support
  TargetParser
  )
