include_directories(
  ${PROJECT_SOURCE_DIR}/lib/Target/MSIL
  ${PROJECT_BINARY_DIR}/lib/Target/MSIL
  )

set(LLVM_LINK_COMPONENTS
  CodeGen
  CodeGenTypes
  Core
  MC
  MIRParser
  MSILCodeGen
  MSILDesc
  MSILInfo
  Support
  Target
  TargetParser
  )

add_llvm_target_unittest(MSILTests
  MSILMCObjectHooksTest.cpp
  MSILNameQuotingTest.cpp
  MSILStackEffectTest.cpp
  MSILTargetMachineTest.cpp
  MSILTargetStreamerTest.cpp
  MSILTypeUtilitiesTest.cpp
  )
