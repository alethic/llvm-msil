set(LLVM_LINK_COMPONENTS
  CLI
  Support
  TargetParser
  )

add_llvm_unittest(CLITests
  CLIFormatTest.cpp
  MSILLinkTest.cpp
  TableSchemaTest.cpp
  )

target_link_libraries(CLITests PRIVATE LLVMTestingSupport)
