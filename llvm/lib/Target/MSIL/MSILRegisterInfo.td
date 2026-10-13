//===- MSILRegisterInfo.td - Describe the MSIL registers ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyRegisterInfo.td at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the MSIL register classes and some nominal physical
/// registers. Values live in virtual registers until MSILExplicitLocals turns
/// them into CIL locals, arguments and evaluation-stack slots.
///
//===----------------------------------------------------------------------===//

class MSILReg<string n> : Register<n> {
  let Namespace = "MSIL";
}

class MSILRegClass<list<ValueType> regTypes, int alignment, dag regList>
     : RegisterClass<"MSIL", regTypes, alignment, regList>;

//===----------------------------------------------------------------------===//
// Registers
//===----------------------------------------------------------------------===//

// Frame base: defined by the localloc in the prologue and replaced by a
// virtual register after prologue/epilogue insertion (MSILReplacePhysRegs).
// There is no stack pointer: arguments travel on the evaluation stack.
def FP : MSILReg<"%FP">;

// The register allocation framework requires register classes have at least
// one register, so we define one for each class even though MSIL never
// allocates physical registers.
def I32_0 : MSILReg<"%i32.0">;
def I64_0 : MSILReg<"%i64.0">;
def F32_0 : MSILReg<"%f32.0">;
def F64_0 : MSILReg<"%f64.0">;

// The value stack "register". This is an opaque entity which serves to order
// uses and defs that must remain in LIFO order.
def VALUE_STACK : MSILReg<"STACK">;

// The incoming arguments "register". This is an opaque entity which serves to
// order the ARGUMENT instructions that are emulating live-in registers and
// must not be scheduled below other instructions.
def ARGUMENTS : MSILReg<"ARGUMENTS">;

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//

// I64 holds pointers and 64-bit integers; both are CIL native int (D2).
def I32 : MSILRegClass<[i32], 32, (add I32_0)>;
def I64 : MSILRegClass<[i64], 64, (add FP, I64_0)>;
def F32 : MSILRegClass<[f32], 32, (add F32_0)>;
def F64 : MSILRegClass<[f64], 64, (add F64_0)>;
