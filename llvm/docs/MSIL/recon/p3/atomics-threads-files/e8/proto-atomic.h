/* Prototype only (recon E8): declares the sized __atomic_* helpers that AtomicExpand calls with
   MaxAtomicSizeInBitsSupported = 0, as imports of the pseudo module "msil.atomic", so that clang -S
   keeps them; post.py replaces each such P/Invoke with a managed body on Interlocked/Volatile. */
#define MSIL_ATOMIC_IMP __attribute__((import_module("msil.atomic")))
MSIL_ATOMIC_IMP int __atomic_fetch_add_4(volatile void *, int, int);
MSIL_ATOMIC_IMP long long __atomic_fetch_add_8(volatile void *, long long, int);
MSIL_ATOMIC_IMP int __atomic_load_4(const volatile void *, int);
MSIL_ATOMIC_IMP long long __atomic_load_8(const volatile void *, int);
MSIL_ATOMIC_IMP void __atomic_store_4(volatile void *, int, int);
MSIL_ATOMIC_IMP void __atomic_store_8(volatile void *, long long, int);
MSIL_ATOMIC_IMP _Bool __atomic_compare_exchange_4(volatile void *, void *, int, int, int);
MSIL_ATOMIC_IMP _Bool __atomic_compare_exchange_8(volatile void *, void *, long long, int, int);
MSIL_ATOMIC_IMP int __atomic_exchange_4(volatile void *, int, int);
__attribute__((used)) static long long msil_keep_atomic_decls(void *p) {
  return __atomic_fetch_add_4(p, 0, 0) + __atomic_fetch_add_8(p, 0, 0) + __atomic_load_4(p, 0) +
         __atomic_load_8(p, 0) + __atomic_compare_exchange_4(p, p, 0, 0, 0) +
         __atomic_compare_exchange_8(p, p, 0, 0, 0) + __atomic_exchange_4(p, 0, 0) +
         (__atomic_store_4(p, 0, 0), 0) + (__atomic_store_8(p, 0, 0), 0);
}
