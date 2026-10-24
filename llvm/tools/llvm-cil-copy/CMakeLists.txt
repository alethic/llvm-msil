set(LLVM_LINK_COMPONENTS
  CLI
  Support
  TargetParser
  )

add_llvm_tool(llvm-cil-copy
  llvm-cil-copy.cpp
  )
