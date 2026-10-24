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
  LinkInfoTest.cpp
  MetadataBuilderTest.cpp
  MetadataReaderTest.cpp
  MethodBodyTest.cpp
  MSILLinkTest.cpp
  OpcodesTest.cpp
  PEImageTest.cpp
  PEWriterTest.cpp
  RoundTripTest.cpp
  SignatureTest.cpp
  SymbolTableTest.cpp
  TableSchemaTest.cpp
  )

target_link_libraries(CLITests PRIVATE LLVMTestingSupport)
