| Query | Framework 4.8 | .NET 6 | .NET 8 | .NET 10 | [mscorlib]-safe |
|---|---|---|---|---|---|
| `System.Object` | yes | yes | yes | yes | **yes** |
| `System.ValueType` | yes | yes | yes | yes | **yes** |
| `native int System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)` | yes | yes | yes | yes | **yes** |
| `native int System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)` | yes | yes | yes | yes | **yes** |
| `string[] System.Environment::GetCommandLineArgs()` | yes | yes | yes | yes | **yes** |
| `void System.Buffer::MemoryCopy(void*,void*,int64,int64)` | yes | yes | yes | yes | **yes** |
| `void System.Threading.Interlocked::MemoryBarrier()` | yes | yes | yes | yes | **yes** |
| `void* System.Runtime.InteropServices.NativeMemory::Alloc(native uint)` | no (type) | no (type) | no (type) | no (type) | no |
| `void* System.Runtime.InteropServices.NativeMemory::AllocZeroed(native uint)` | no (type) | no (type) | no (type) | no (type) | no |
| `void* System.Runtime.InteropServices.NativeMemory::AlignedAlloc(native uint,native uint)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.InteropServices.NativeMemory::Free(void*)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.InteropServices.NativeMemory::AlignedFree(void*)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.InteropServices.NativeMemory::Copy(void*,void*,native uint)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.InteropServices.NativeMemory::Fill(void*,native uint,uint8)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.InteropServices.NativeMemory::Clear(void*,native uint)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.InteropServices.Marshal::FreeHGlobal(native int)` | yes | yes | yes | yes | **yes** |
| `System.ThreadStaticAttribute::.ctor()` | yes | yes | yes | yes | **yes** |
| `System.AppDomain System.AppDomain::get_CurrentDomain()` | yes | yes | yes | yes | **yes** |
| `void System.AppDomain::add_ProcessExit(System.EventHandler)` | yes | yes | yes | yes | **yes** |
| `void System.EventHandler::.ctor(object,native int)` | yes | yes | yes | yes | **yes** |
| `void System.Environment::Exit(int32)` | yes | yes | yes | yes | **yes** |
| `native int System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(System.Delegate)` | yes | yes | yes | yes | **yes** |
| `native int System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate``1(!!0)` | yes | yes | yes | yes | **yes** |
| `System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor()` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Type[] System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::CallConvs` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Runtime.CompilerServices.CallConvCdecl` | yes | yes | yes | yes | **yes** |
| `System.Runtime.InteropServices.UnmanagedFunctionPointerAttribute::.ctor(System.Runtime.InteropServices.CallingConvention)` | yes | yes | yes | yes | **yes** |
| `System.MulticastDelegate` | yes | yes | yes | yes | **yes** |
| `System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute::.ctor()` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Runtime.InteropServices.SuppressGCTransitionAttribute::.ctor()` | no (type) | no (type) | no (type) | no (type) | no |
| `native int System.Runtime.InteropServices.NativeLibrary::Load(string)` | no (type) | no (type) | no (type) | no (type) | no |
| `native int System.Runtime.InteropServices.NativeLibrary::GetExport(native int,string)` | no (type) | no (type) | no (type) | no (type) | no |
| `int32 System.Runtime.InteropServices.Marshal::GetLastPInvokeError()` | no (member) | yes | yes | yes | no |
| `int32 System.Runtime.InteropServices.Marshal::GetLastWin32Error()` | yes | yes | yes | yes | **yes** |
| `int32 System.Threading.Interlocked::CompareExchange(int32&,int32,int32)` | yes | yes | yes | yes | **yes** |
| `int64 System.Threading.Interlocked::CompareExchange(int64&,int64,int64)` | yes | yes | yes | yes | **yes** |
| `native int System.Threading.Interlocked::CompareExchange(native int&,native int,native int)` | yes | yes | yes | yes | **yes** |
| `uint32 System.Threading.Interlocked::CompareExchange(uint32&,uint32,uint32)` | no (member) | yes | yes | yes | no |
| `uint8 System.Threading.Interlocked::CompareExchange(uint8&,uint8,uint8)` | no (member) | no (member) | no (member) | yes | no |
| `int16 System.Threading.Interlocked::CompareExchange(int16&,int16,int16)` | no (member) | no (member) | no (member) | yes | no |
| `int32 System.Threading.Interlocked::Exchange(int32&,int32)` | yes | yes | yes | yes | **yes** |
| `int64 System.Threading.Interlocked::Exchange(int64&,int64)` | yes | yes | yes | yes | **yes** |
| `native int System.Threading.Interlocked::Exchange(native int&,native int)` | yes | yes | yes | yes | **yes** |
| `uint8 System.Threading.Interlocked::Exchange(uint8&,uint8)` | no (member) | no (member) | no (member) | yes | no |
| `int32 System.Threading.Interlocked::Add(int32&,int32)` | yes | yes | yes | yes | **yes** |
| `int64 System.Threading.Interlocked::Add(int64&,int64)` | yes | yes | yes | yes | **yes** |
| `int32 System.Threading.Interlocked::And(int32&,int32)` | no (member) | yes | yes | yes | no |
| `int64 System.Threading.Interlocked::Or(int64&,int64)` | no (member) | yes | yes | yes | no |
| `void System.Threading.Interlocked::MemoryBarrierProcessWide()` | no (member) | yes | yes | yes | no |
| `int32 System.Threading.Volatile::Read(int32&)` | yes | yes | yes | yes | **yes** |
| `int64 System.Threading.Volatile::Read(int64&)` | yes | yes | yes | yes | **yes** |
| `native int System.Threading.Volatile::Read(native int&)` | yes | yes | yes | yes | **yes** |
| `void System.Threading.Volatile::Write(int32&,int32)` | yes | yes | yes | yes | **yes** |
| `void System.Threading.Volatile::Write(native int&,native int)` | yes | yes | yes | yes | **yes** |
| `void System.Threading.Thread::SpinWait(int32)` | yes | yes | yes | yes | **yes** |
| `System.Half` | no (type) | no (type) | no (type) | no (type) | no |
| `float32 System.Half::op_Explicit(System.Half)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Half System.Half::op_Explicit(float32)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Half System.Half::op_Explicit(float64)` | no (type) | no (type) | no (type) | no (type) | no |
| `int16 System.BitConverter::HalfToInt16Bits(System.Half)` | no (member) | yes | yes | yes | no |
| `System.Half System.BitConverter::Int16BitsToHalf(int16)` | no (member) | yes | yes | yes | no |
| `System.Int128` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Int128::.ctor(uint64,uint64)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Int128 System.Int128::op_Division(System.Int128,System.Int128)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Int128 System.Int128::op_Modulus(System.Int128,System.Int128)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.UInt128 System.UInt128::op_Division(System.UInt128,System.UInt128)` | no (type) | no (type) | no (type) | no (type) | no |
| `uint64 System.Int128::op_Explicit(System.Int128)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Int128 System.Int128::op_RightShift(System.Int128,int32)` | no (type) | no (type) | no (type) | no (type) | no |
| `float64 System.Int128::op_Explicit(System.Int128)` | no (type) | no (type) | no (type) | no (type) | no |
| `float64 System.Math::FusedMultiplyAdd(float64,float64,float64)` | no (member) | yes | yes | yes | no |
| `float32 System.MathF::FusedMultiplyAdd(float32,float32,float32)` | no (type) | no (type) | yes | yes | no |
| `float64 System.Math::CopySign(float64,float64)` | no (member) | yes | yes | yes | no |
| `float64 System.Math::Sqrt(float64)` | yes | yes | yes | yes | **yes** |
| `float32 System.MathF::Sqrt(float32)` | no (type) | no (type) | yes | yes | no |
| `float64 System.Math::Floor(float64)` | yes | yes | yes | yes | **yes** |
| `float64 System.Math::Round(float64,System.MidpointRounding)` | yes | yes | yes | yes | **yes** |
| `float64 System.Math::Min(float64,float64)` | yes | yes | yes | yes | **yes** |
| `float64 System.Math::Abs(float64)` | yes | yes | yes | yes | **yes** |
| `float64 System.Math::ScaleB(float64,int32)` | no (member) | yes | yes | yes | no |
| `float64 System.Math::Cbrt(float64)` | no (member) | yes | yes | yes | no |
| `float64 System.Math::Log2(float64)` | no (member) | yes | yes | yes | no |
| `float64 System.Math::IEEERemainder(float64,float64)` | yes | yes | yes | yes | **yes** |
| `int32 System.BitConverter::SingleToInt32Bits(float32)` | no (member) | yes | yes | yes | no |
| `float32 System.BitConverter::Int32BitsToSingle(int32)` | no (member) | yes | yes | yes | no |
| `int64 System.BitConverter::DoubleToInt64Bits(float64)` | yes | yes | yes | yes | **yes** |
| `int32 System.Numerics.BitOperations::PopCount(uint32)` | no (type) | no (type) | no (type) | no (type) | no |
| `int32 System.Numerics.BitOperations::LeadingZeroCount(uint64)` | no (type) | no (type) | no (type) | no (type) | no |
| `int32 System.Numerics.BitOperations::TrailingZeroCount(uint32)` | no (type) | no (type) | no (type) | no (type) | no |
| `uint32 System.Numerics.BitOperations::RotateLeft(uint32,int32)` | no (type) | no (type) | no (type) | no (type) | no |
| `uint32 System.Buffers.Binary.BinaryPrimitives::ReverseEndianness(uint32)` | no (type) | no (type) | yes | yes | no |
| `System.Exception::.ctor()` | yes | yes | yes | yes | **yes** |
| `System.Collections.IDictionary System.Exception::get_Data()` | yes | yes | yes | yes | **yes** |
| `System.Collections.IDictionary::set_Item(object,object)` | yes | yes | yes | yes | **yes** |
| `void System.Diagnostics.Debugger::Break()` | yes | yes | yes | yes | **yes** |
| `bool System.Runtime.Intrinsics.X86.Sse42::get_IsSupported()` | no (type) | no (type) | no (type) | no (type) | no |
| `uint32 System.Runtime.Intrinsics.X86.Sse42::Crc32(uint32,uint32)` | no (type) | no (type) | no (type) | no (type) | no |
| `uint64 System.Runtime.Intrinsics.X86.Sse42/X64::Crc32(uint64,uint64)` | no (type) | no (type) | no (type) | no (type) | no |
| `void System.Runtime.Intrinsics.X86.X86Base::Pause()` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Runtime.Intrinsics.Vector128`1<uint8> System.Runtime.Intrinsics.X86.Ssse3::Shuffle(System.Runtime.Intrinsics.Vector128`1<uint8>,System.Runtime.Intrinsics.Vector128`1<uint8>)` | no (type) | no (type) | no (type) | no (type) | no |
| `uint32 System.Runtime.Intrinsics.Arm.Crc32::ComputeCrc32(uint32,uint32)` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Runtime.Intrinsics.Vector128`1` | no (type) | no (type) | no (type) | no (type) | no |
| `System.Runtime.Intrinsics.Vector128`1<!!0> System.Runtime.Intrinsics.Vector128::Create``1(!!0)` | no (type) | no (type) | no (type) | no (type) | no |
| `!!0[] System.GC::AllocateArray``1(int32,bool)` | no (member) | yes | yes | yes | no |
| `bool System.Runtime.InteropServices.RuntimeInformation::IsOSPlatform(System.Runtime.InteropServices.OSPlatform)` | yes | yes | yes | yes | **yes** |
| `System.Span`1` | no (type) | no (type) | yes | yes | no |
| `System.Runtime.CompilerServices.RuntimeCompatibilityAttribute::.ctor()` | yes | yes | yes | yes | **yes** |
| `System.Runtime.Versioning.TargetFrameworkAttribute::.ctor(string)` | yes | yes | yes | yes | **yes** |
