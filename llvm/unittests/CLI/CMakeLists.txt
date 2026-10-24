set(LLVM_LINK_COMPONENTS
  CLI
  Support
  TargetParser
  )

add_llvm_unittest(CLITests
  BlobTest.cpp
  CLIFormatTest.cpp
  CustomAttributeTest.cpp
  HeapsTest.cpp
  ILBuilderTest.cpp
  ILDecoderTest.cpp
  MetadataBuilderTest.cpp
  MethodBodyTest.cpp
  MSILLinkTest.cpp
  OpcodesTest.cpp
  SignatureTest.cpp
  TableSchemaTest.cpp
  )

target_link_libraries(CLITests PRIVATE LLVMTestingSupport)
