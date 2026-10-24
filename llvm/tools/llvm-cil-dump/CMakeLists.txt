set(LLVM_LINK_COMPONENTS
  CLI
  Support
  )

add_llvm_tool(llvm-cil-dump
  llvm-cil-dump.cpp
  )
