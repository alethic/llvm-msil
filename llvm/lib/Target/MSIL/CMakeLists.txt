add_llvm_component_group(MSIL)

set(LLVM_TARGET_DEFINITIONS MSIL.td)

tablegen(LLVM MSILGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MSILGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MSILGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MSILGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MSILGenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM MSILGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(MSILCommonTableGen)

add_llvm_target(MSILCodeGen
  MSILAddMissingPrototypes.cpp
  MSILArgumentMove.cpp
  MSILAsmPrinter.cpp
  MSILCleanCodeAfterTrap.cpp
  MSILDataSegment.cpp
  MSILDebugValueManager.cpp
  MSILExplicitLocals.cpp
  MSILFixFunctionBitcasts.cpp
  MSILFrameLowering.cpp
  MSILISelDAGToDAG.cpp
  MSILISelLowering.cpp
  MSILISelLoweringCall.cpp
  MSILInstrInfo.cpp
  MSILMCInstLower.cpp
  MSILMachineFunctionInfo.cpp
  MSILNullifyDebugValueLists.cpp
  MSILOptimizeLiveIntervals.cpp
  MSILOptimizeReturned.cpp
  MSILPrepareIR.cpp
  MSILRegColoring.cpp
  MSILRegNumbering.cpp
  MSILRegStackify.cpp
  MSILRegisterInfo.cpp
  MSILReplacePhysRegs.cpp
  MSILSelectionDAGInfo.cpp
  MSILSetMemFlags.cpp
  MSILStackCheck.cpp
  MSILSubtarget.cpp
  MSILTargetMachine.cpp
  MSILTargetObjectFile.cpp
  MSILTargetTransformInfo.cpp
  MSILUtilities.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CLI
  CodeGen
  CodeGenTypes
  Core
  MC
  MSILDesc
  MSILInfo
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  MSIL
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
