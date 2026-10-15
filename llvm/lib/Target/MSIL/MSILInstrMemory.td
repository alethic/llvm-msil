//===- MSILInstrMemory.td - MSIL memory codegen support ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL memory access code-gen constructs: ldind/stind with a memflags_op
/// first operand, cpblk, initblk, the memmove call and the fences.
///
/// An address is one native int (I64) operand: CIL has no offset or index
/// operands, so address arithmetic is explicit (isLegalAddressingMode accepts
/// [reg] only). The memflags_op operand holds the 'unaligned.' and 'volatile.'
/// prefixes (MSIL::MemFlags); instruction selection sets it to 0 and
/// MSILSetMemFlags fills it from the instruction's memory operands.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MSIL-specific DAG Nodes.
//===----------------------------------------------------------------------===//

// The memory intrinsics (decision D15), built by MSILSelectionDAGInfo with
// SelectionDAG::getMemIntrinsicNode: a store memory operand for the
// destination and, for the copies, a load memory operand for the source.
// Addresses are i64 rather than SDTCisPtrTy (pointers are native ints, I64,
// on every Phase-1 triple): the node verifier reads the address space of a
// node with a pointer constraint through MemSDNode::getMemOperand, which
// asserts on a node with two memory operands.

// (dst, src, len): cpblk copies len bytes, an unsigned int32, from src to dst.
def SDT_MSILCpBlk : SDTypeProfile<0, 3, [SDTCisVT<0, i64>, SDTCisVT<1, i64>,
                                         SDTCisVT<2, i32>]>;
// (dst, value, len): initblk sets len bytes, an unsigned int32, at dst to the
// low 8 bits of value.
def SDT_MSILInitBlk : SDTypeProfile<0, 3, [SDTCisVT<0, i64>, SDTCisVT<1, i32>,
                                           SDTCisVT<2, i32>]>;
// (src, dst, len): the argument order of System.Buffer::MemoryCopy, with a
// native int length.
def SDT_MSILMemMove : SDTypeProfile<0, 3, [SDTCisVT<0, i64>, SDTCisVT<1, i64>,
                                           SDTCisVT<2, i64>]>;

def MSILcpblk : SDNode<"MSILISD::CPBLK", SDT_MSILCpBlk,
                       [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                        SDNPMemOperand]>;
def MSILinitblk : SDNode<"MSILISD::INITBLK", SDT_MSILInitBlk,
                         [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def MSILmemmove : SDNode<"MSILISD::MEMMOVE", SDT_MSILMemMove,
                         [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                          SDNPMemOperand]>;

//===----------------------------------------------------------------------===//
// Loads and stores.
//===----------------------------------------------------------------------===//

// ldind.* pops the address and pushes the value. The integer forms push an
// int32 (ldind.i1/u1/i2/u2 sign- or zero-extend), ldind.i a native int.
multiclass Load<MSILRegClass rc, string name, bits<16> cilop> {
  let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
  defm "" : I<(outs rc:$dst), (ins memflags_op:$flags, I64:$addr), [],
              "${flags}" # name, cilop>;
}

defm LDIND_I1 : Load<I32, "ldind.i1", 0x46>;
defm LDIND_U1 : Load<I32, "ldind.u1", 0x47>;
defm LDIND_I2 : Load<I32, "ldind.i2", 0x48>;
defm LDIND_U2 : Load<I32, "ldind.u2", 0x49>;
defm LDIND_I4 : Load<I32, "ldind.i4", 0x4A>;
defm LDIND_I : Load<I64, "ldind.i", 0x4D>;
defm LDIND_R4 : Load<F32, "ldind.r4", 0x4E>;
defm LDIND_R8 : Load<F64, "ldind.r8", 0x4F>;

// stind.* pops the address and then the value, pushed in that order.
// stind.i1 and stind.i2 store the low 8 or 16 bits of an int32.
multiclass Store<MSILRegClass rc, string name, bits<16> cilop> {
  let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
  defm "" : I<(outs), (ins memflags_op:$flags, I64:$addr, rc:$val), [],
              "${flags}" # name, cilop>;
}

defm STIND_I1 : Store<I32, "stind.i1", 0x52>;
defm STIND_I2 : Store<I32, "stind.i2", 0x53>;
defm STIND_I4 : Store<I32, "stind.i4", 0x54>;
defm STIND_I : Store<I64, "stind.i", 0xDF>;
defm STIND_R4 : Store<F32, "stind.r4", 0x56>;
defm STIND_R8 : Store<F64, "stind.r8", 0x57>;

// The memflags operand is 0 at selection.
class LoadPat<ValueType vt, PatFrag kind, Instruction inst>
    : Pat<(vt (kind I64:$addr)), (inst 0, I64:$addr)>;
class StorePat<ValueType vt, MSILRegClass rc, PatFrag kind, Instruction inst>
    : Pat<(kind (vt rc:$val), I64:$addr), (inst 0, I64:$addr, rc:$val)>;

def : LoadPat<i32, load, LDIND_I4>;
def : LoadPat<i64, load, LDIND_I>;
def : LoadPat<f32, load, LDIND_R4>;
def : LoadPat<f64, load, LDIND_R8>;
// Extending loads into an int32. An any-extending load zero-extends, as in
// WebAssembly. Extending loads into a native int and the f32 -> f64 extending
// load are Expand (MSILTargetLowering), so they become one of these plus a
// conversion.
def : LoadPat<i32, sextloadi8, LDIND_I1>;
def : LoadPat<i32, zextloadi8, LDIND_U1>;
def : LoadPat<i32, extloadi8, LDIND_U1>;
def : LoadPat<i32, sextloadi16, LDIND_I2>;
def : LoadPat<i32, zextloadi16, LDIND_U2>;
def : LoadPat<i32, extloadi16, LDIND_U2>;

def : StorePat<i32, I32, store, STIND_I4>;
def : StorePat<i64, I64, store, STIND_I>;
def : StorePat<f32, F32, store, STIND_R4>;
def : StorePat<f64, F64, store, STIND_R8>;
// Truncating stores from an int32. Truncating stores from a native int and
// the f64 -> f32 truncating store are Expand (MSILTargetLowering).
def : StorePat<i32, I32, truncstorei8, STIND_I1>;
def : StorePat<i32, I32, truncstorei16, STIND_I2>;

//===----------------------------------------------------------------------===//
// Memory intrinsics (decision D15).
//===----------------------------------------------------------------------===//

// cpblk and initblk assume addresses aligned to the natural size of the
// machine unless an 'unaligned.' prefix says otherwise (ECMA-335 III.3.30,
// III.3.36); MSILSetMemFlags sets it from the memory operands.
let hasSideEffects = 0, mayLoad = 1, mayStore = 1 in
defm CPBLK : I<(outs), (ins memflags_op:$flags, I64:$dst, I64:$src, I32:$len),
               [], "${flags}cpblk", 0xFE17>;
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
defm INITBLK : I<(outs),
                 (ins memflags_op:$flags, I64:$dst, I32:$val, I32:$len),
                 [], "${flags}initblk", 0xFE18>;

def : Pat<(MSILcpblk I64:$dst, I64:$src, I32:$len),
          (CPBLK 0, I64:$dst, I64:$src, I32:$len)>;
def : Pat<(MSILinitblk I64:$dst, I32:$val, I32:$len),
          (INITBLK 0, I64:$dst, I32:$val, I32:$len)>;

// memmove, and memcpy without a constant int32 length: overlapping cpblk
// gives different results on CoreCLR and .NET Framework (ECMA leaves it
// unspecified), and System.Buffer::MemoryCopy has memmove semantics on every
// runtime. 'conv.i8; dup' turns the native int length into both int64
// arguments (the destination size and the byte count); the addresses stay
// native int (PEVerify's allowed 'Unexpected type on the stack', decision
// D21). It takes no prefixes: a volatile memmove is an ordinary call.
let hasSideEffects = 0, mayLoad = 1, mayStore = 1, StackPeakExtra = 1 in
defm MEMMOVE : I<(outs), (ins I64:$src, I64:$dst, I64:$len),
                 [(MSILmemmove I64:$src, I64:$dst, I64:$len)],
                 "conv.i8\n  dup\n  call void [mscorlib]System.Buffer::"
                 "MemoryCopy(void*, void*, int64, int64)">;

//===----------------------------------------------------------------------===//
// Fences (decision D36).
//===----------------------------------------------------------------------===//

// A full memory barrier for every fence that is not single-threaded. It is not
// a call MachineInstr (it has no callee operand). As for the generic MEMBARRIER
// and other targets' fences, hasSideEffects without memory operands is what
// keeps every memory access on its side: MachineInstr::hasOrderedMemoryRef is
// true, the schedulers treat it as a barrier, and MSILRegStackify's query()
// counts it as a write with side effects. (mayStore = 1 would not match the
// atomic_fence pattern, which TableGen checks.)
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
defm MEMORY_BARRIER : I<(outs), (ins), [],
                        "call void [mscorlib]System.Threading.Interlocked::"
                        "MemoryBarrier()", 0x28>;

// atomic_fence (ordering, scope). A single-thread fence (scope 0, clang's
// translation of 'asm volatile("" ::: "memory")' and of
// __atomic_signal_fence) only orders code generation: the generic MEMBARRIER
// pseudo has side effects and no memory operands, so MSILRegStackify never
// moves a load or store across it, and AsmPrinter prints it as a comment
// (AArch64 precedent). Every other fence calls Interlocked::MemoryBarrier.
let AddedComplexity = 15 in
def : Pat<(atomic_fence (timm), 0), (MEMBARRIER)>;
def : Pat<(atomic_fence (timm), (timm)), (MEMORY_BARRIER)>;
