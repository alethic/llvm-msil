add_llvm_component_library(LLVMCLI
  Blob.cpp
  CustomAttribute.cpp
  Dump.cpp
  Heaps.cpp
  ILBuilder.cpp
  ILDecoder.cpp
  LinkInfo.cpp
  MetadataBuilder.cpp
  MetadataReader.cpp
  MethodBody.cpp
  Opcodes.cpp
  PEImage.cpp
  PEWriter.cpp
  Signature.cpp
  SymbolTable.cpp
  TableSchema.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/CLI

  LINK_COMPONENTS
  BinaryFormat
  MC
  Support
  TargetParser
  )
