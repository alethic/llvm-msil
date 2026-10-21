# Curated 'upstream-hooks' lit list, option A (only tools the M10 gate already builds:
# clang llc opt FileCheck not count split-file). Keys: hooked upstream file.
CURATED = {
    "clang/lib/Basic/Targets.cpp": [
        "clang/test/Preprocessor/init-x86.c",
        "clang/test/Preprocessor/init-aarch64.c",
        "clang/test/Preprocessor/predefined-win-macros.c",
    ],
    "llvm/lib/TargetParser/TargetDataLayout.cpp": [
        "clang/test/CodeGen/target-data.c",
    ],
    "clang/lib/CodeGen/BackendUtil.cpp": [
        "clang/test/CodeGen/asan-globals-gc.cpp",
    ],
    "clang/lib/CodeGen/CGBuiltin.cpp": [
        "clang/test/CodeGen/bittest-intrin.c",
        "clang/test/CodeGen/ms-intrinsics.c",
        "clang/test/Sema/bittest-intrinsics.c",
    ],
    "clang/lib/CodeGen/CGCall.cpp": [
        "clang/test/CodeGen/X86/x86_64-arguments.c",
        "clang/test/CodeGen/2007-02-25-C-DotDotDot.c",
        "clang/test/CodeGen/variadic-null-win64.c",
        "clang/test/CodeGen/no-prototype.c",
    ],
    "clang/lib/CodeGen/CGObjCMac.cpp": [
        "clang/test/CodeGenObjC/section-name.m",
        "clang/test/CodeGenObjC/sections.m",
    ],
    "clang/lib/CodeGen/CGStmt.cpp": [
        "clang/test/CodeGen/asm.c",
        "clang/test/CodeGen/asm_64.c",
        "clang/test/CodeGen/asm-inout.c",
        "clang/test/CodeGen/asm-goto.c",
        "clang/test/CodeGen/asm-errors.c",
        "clang/test/CodeGen/X86/x86-64-inline-asm.c",
        "clang/test/CodeGen/X86/inline-asm-constraints.c",
    ],
    "clang/lib/CodeGen/CodeGenAction.cpp": [
        "clang/test/CodeGen/exception-model-flag.c",
        "clang/test/Driver/ir-exception-model.c",
        "clang/test/CodeGen/WebAssembly/wasm-exception-model-flag-ir-input-mismatch.ll",
        "clang/test/CodeGen/WebAssembly/wasm-exception-model-flag-parse-ir-input.ll",
    ],
    "clang/lib/CodeGen/CodeGenModule.cpp": [
        "clang/test/CodeGen/exception-model-flag.c",
        "clang/test/CodeGen/ptrauth-module-flags.c",
        "clang/test/CodeGen/kcfi.c",
        "clang/test/CodeGen/kcfi-arity.c",
        "clang/test/CodeGen/kcfi-generalize.c",
        "clang/test/CodeGen/kcfi-normalize.c",
        "clang/test/CodeGen/cfstring.c",
        "clang/test/CodeGen/cfstring-windows.c",
        "clang/test/CodeGen/cfstring-elf-sections-x86_64.c",
        "clang/test/CodeGen/2006-01-23-FileScopeAsm.c",
        "clang/test/CodeGen/no-prototype.c",
    ],
    "clang/lib/CodeGen/TargetBuiltins/X86.cpp": [
        "clang/test/CodeGen/ms-intrinsics-cpuid.c",
        "clang/test/CodeGen/ms-intrinsics.c",
        "clang/test/Headers/cpuid.c",
        "clang/test/Headers/__cpuidex_conflict.c",
    ],
    "clang/lib/Driver/ToolChain.cpp": [
        "clang/test/Driver/integrated-as.c",
        "clang/test/Driver/fsanitize-cfi.c",
    ],
    "clang/lib/Driver/ToolChains/Clang.cpp": [
        "clang/test/Driver/embed-bitcode.c",
        "clang/test/Driver/fembed-bitcode.c",
        "clang/test/Driver/as-options.s",
        "clang/test/Driver/integrated-as.s",
    ],
    "clang/lib/Parse/ParseStmtAsm.cpp": [
        "clang/test/Parser/ms-inline-asm.c",
        "clang/test/Parser/ms-inline-asm-nested-braces.c",
        "clang/test/Sema/ms-inline-asm.c",
        "clang/test/Sema/ms-inline-asm-invalid-arch.c",
        "clang/test/CodeGen/ms-inline-asm*.c",
        "clang/test/CodeGen/ms-inline-asm*.cpp",
    ],
    "clang/lib/Sema/SemaChecking.cpp": [
        "clang/test/Sema/builtins.c",
        "clang/test/CodeGen/cfstring.c",
    ],
    "clang/lib/Sema/SemaDeclAttr.cpp": [
        "clang/test/Sema/MicrosoftCompatibility.c",
        "clang/test/Sema/attr-aligned.c",
    ],
    "llvm/lib/MC/MCParser/AsmParser.cpp": [
        "clang/test/Misc/cc1as-asm.s",
        "clang/test/Misc/cc1as-output-asm-variant.c",
        "llvm/test/CodeGen/X86/inline-asm*.ll",
        "llvm/test/CodeGen/X86/2008-09-17-inline-asm-1.ll",
        "llvm/test/CodeGen/WebAssembly/inline-asm.ll",
        "llvm/test/CodeGen/WebAssembly/inline-asm-m.ll",
        "llvm/test/CodeGen/WebAssembly/inline-asm-tied.ll",
        "llvm/test/CodeGen/WebAssembly/inline-asm-failure.ll",
    ],
    "llvm/lib/MC/TargetRegistry.cpp": [
        "llvm/test/tools/llc/*.ll",
        "llvm/test/tools/llc/*.mir",
    ],
    "llvm/lib/MC/MCContext.cpp + llvm/lib/MC/MCObjectFileInfo.cpp": [
        "llvm/test/CodeGen/WebAssembly/custom-sections.ll",
        "llvm/test/CodeGen/WebAssembly/comdat*.ll",
        "llvm/test/CodeGen/X86/coff-comdat*.ll",
        "llvm/test/CodeGen/X86/elf-comdat*.ll",
        "llvm/test/CodeGen/X86/section-*.ll",
    ],
    "llvm/lib/Target/TargetMachine.cpp": [
        "llvm/test/CodeGen/X86/*pic*.ll",
        "llvm/test/CodeGen/X86/dso_local_equivalent*.ll",
        "llvm/test/CodeGen/X86/extern_weak.ll",
        "llvm/test/CodeGen/WebAssembly/function-addr-offset.ll",
    ],
    "llvm/lib/Bitcode/Writer/BitcodeWriter.cpp": [
        "llvm/test/Transforms/EmbedBitcode/*.ll",
        "clang/test/Frontend/x86-embed-bitcode.ll",
        "clang/test/Frontend/embed-bitcode.ll",
    ],
    "llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp": [
        "llvm/test/Instrumentation/AddressSanitizer/*.ll",
        "llvm/test/Instrumentation/AddressSanitizer/X86/*.ll",
    ],
}
# Option B: add llvm-profdata to the gate build list, then also:
OPTION_B = {
    "clang/lib/Driver/ToolChains/Clang.cpp": [
        "clang/test/Driver/cl-options.c",
        "clang/test/Driver/clang_f_opts.c",
    ],
}
